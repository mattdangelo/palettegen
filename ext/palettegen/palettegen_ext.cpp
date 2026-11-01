// Ruby bindings for the palettegen core. Exposes Palettegen::Native, a thin,
// private layer; the public API lives in lib/palettegen.rb.
//
// Two rules keep C++ and Ruby's longjmp-based exceptions from fighting:
//   * No C++ exception ever escapes into Ruby: every core call is wrapped.
//   * Anything with a destructor that is alive while Ruby might raise is owned by
//     a TypedData object, so the GC frees it even if we longjmp out.
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/thread.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include "palettegen.hpp"

namespace {

VALUE mPalettegen, mNative, eError, eDecodeError;

enum class Failure { None, Error, Decode, NoMemory };

struct ExtractCall {
    std::string blob;
    palettegen::Options opts;
    std::vector<palettegen::Swatch> result;
    Failure failure = Failure::None;
    std::string message;
};

void call_free(void* p) { delete static_cast<ExtractCall*>(p); }

size_t call_memsize(const void* p) {
    const auto* c = static_cast<const ExtractCall*>(p);
    return sizeof(*c) + c->blob.capacity();
}

const rb_data_type_t extract_call_type = {
    "Palettegen::Native::ExtractCall",
    {nullptr, call_free, call_memsize, nullptr},
    nullptr,
    nullptr,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

template <class Fn>
void capture_failures(Failure& failure, std::string& message, Fn&& fn) {
    try {
        fn();
    } catch (const palettegen::DecodeError& e) {
        failure = Failure::Decode;
        message = e.what();
    } catch (const palettegen::Error& e) {
        failure = Failure::Error;
        message = e.what();
    } catch (const std::bad_alloc&) {
        failure = Failure::NoMemory;
    } catch (const std::exception& e) {
        failure = Failure::Error;
        message = e.what();
    } catch (...) {
        failure = Failure::Error;
        message = "unknown native error";
    }
}

// Runs without the GVL: must not touch any Ruby objects.
void* extract_without_gvl(void* arg) {
    auto* call = static_cast<ExtractCall*>(arg);
    capture_failures(call->failure, call->message, [call] {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(call->blob.data());
        const palettegen::Image image = palettegen::decode(bytes, call->blob.size());
        call->blob.clear();
        call->blob.shrink_to_fit();
        call->result = palettegen::extract(image, call->opts);
    });
    return nullptr;
}

// Raise from a plain C buffer so no std::string is alive across the longjmp.
[[noreturn]] void raise_failure(Failure failure, const std::string& message) {
    if (failure == Failure::NoMemory) rb_memerror();
    char buf[512];
    std::snprintf(buf, sizeof buf, "%s", message.c_str());
    rb_raise(failure == Failure::Decode ? eDecodeError : eError, "%s", buf);
}

// Native.extract(blob, colors, space, seed, max_samples, alpha_threshold)
//   space: 0 = Lab, 1 = RGB
//   => [[r, g, b, share], ...] sorted by share descending
VALUE native_extract(VALUE, VALUE blob, VALUE colors, VALUE space, VALUE seed, VALUE max_samples,
                     VALUE alpha_threshold) {
    StringValue(blob);
    const int n_colors = NUM2INT(colors);
    const int space_id = NUM2INT(space);
    const unsigned long long seed_value = NUM2ULL(seed);
    const long long samples = NUM2LL(max_samples);
    const int alpha = NUM2INT(alpha_threshold);
    if (samples < 1) rb_raise(rb_eArgError, "max_samples must be positive");
    if (alpha < 0 || alpha > 255) rb_raise(rb_eArgError, "alpha_threshold must be between 0 and 255");

    VALUE holder = TypedData_Wrap_Struct(0, &extract_call_type, nullptr);
    auto* call = new (std::nothrow) ExtractCall;
    if (!call) rb_memerror();
    DATA_PTR(holder) = call;

    // Copy the bytes: the Ruby string could be mutated or moved by another
    // thread while we run without the GVL.
    bool copied = false;
    try {
        call->blob.assign(RSTRING_PTR(blob), static_cast<size_t>(RSTRING_LEN(blob)));
        copied = true;
    } catch (...) {
    }
    if (!copied) rb_memerror();

    call->opts.colors = n_colors;
    call->opts.space = space_id == 1 ? palettegen::ColorSpace::Rgb : palettegen::ColorSpace::Lab;
    call->opts.seed = seed_value;
    call->opts.max_samples = static_cast<std::size_t>(samples);
    call->opts.alpha_threshold = static_cast<std::uint8_t>(alpha);

    rb_thread_call_without_gvl(extract_without_gvl, call, nullptr, nullptr);

    if (call->failure != Failure::None) raise_failure(call->failure, call->message);

    VALUE out = rb_ary_new_capa(static_cast<long>(call->result.size()));
    for (const auto& s : call->result) {
        rb_ary_push(out, rb_ary_new_from_args(4, INT2FIX(s.color.r), INT2FIX(s.color.g), INT2FIX(s.color.b),
                                              DBL2NUM(s.share)));
    }

    DATA_PTR(holder) = nullptr;
    delete call;
    RB_GC_GUARD(holder);
    RB_GC_GUARD(blob);
    return out;
}

// Native.encode(colors, square, format, quality)
//   colors: [[r, g, b], ...]; format: 0 = JPEG, 1 = PNG
//   => binary String
VALUE native_encode(VALUE, VALUE colors, VALUE square, VALUE format, VALUE quality) {
    Check_Type(colors, T_ARRAY);
    const int sq = NUM2INT(square);
    const int fmt = NUM2INT(format);
    const int q = NUM2INT(quality);

    const long n = RARRAY_LEN(colors);
    if (n < 1 || n > 256) rb_raise(rb_eArgError, "expected between 1 and 256 colours, got %ld", n);

    // Validate and convert into a plain array first (NUM2INT may raise).
    palettegen::Rgb rgb[256];
    for (long i = 0; i < n; ++i) {
        VALUE c = rb_ary_entry(colors, i);
        Check_Type(c, T_ARRAY);
        if (RARRAY_LEN(c) != 3) rb_raise(rb_eArgError, "each colour must be [r, g, b]");
        int ch[3];
        for (int j = 0; j < 3; ++j) {
            ch[j] = NUM2INT(rb_ary_entry(c, j));
            if (ch[j] < 0 || ch[j] > 255) rb_raise(rb_eArgError, "colour channels must be 0..255");
        }
        rgb[i] = {static_cast<std::uint8_t>(ch[0]), static_cast<std::uint8_t>(ch[1]), static_cast<std::uint8_t>(ch[2])};
    }

    VALUE holder = TypedData_Wrap_Struct(0, &extract_call_type, nullptr);
    auto* call = new (std::nothrow) ExtractCall;  // reused as an owned scratch buffer
    if (!call) rb_memerror();
    DATA_PTR(holder) = call;

    capture_failures(call->failure, call->message, [&] {
        const std::vector<palettegen::Rgb> list(rgb, rgb + n);
        const auto bytes = fmt == 1 ? palettegen::encode_png(list, sq) : palettegen::encode_jpeg(list, sq, q);
        call->blob.assign(bytes.begin(), bytes.end());
    });
    if (call->failure != Failure::None) raise_failure(call->failure, call->message);

    VALUE out = rb_str_new(call->blob.data(), static_cast<long>(call->blob.size()));
    rb_enc_associate(out, rb_ascii8bit_encoding());

    DATA_PTR(holder) = nullptr;
    delete call;
    RB_GC_GUARD(holder);
    return out;
}

VALUE native_webp_supported(VALUE) { return palettegen::webp_supported() ? Qtrue : Qfalse; }

}  // namespace

extern "C" void Init_palettegen() {
#ifdef HAVE_RB_EXT_RACTOR_SAFE
    rb_ext_ractor_safe(true);
#endif
    mPalettegen = rb_define_module("Palettegen");
    mNative = rb_define_module_under(mPalettegen, "Native");
    // Defined in Ruby before this extension is loaded; fall back if not.
    eError = rb_const_defined(mPalettegen, rb_intern("Error"))
                 ? rb_const_get(mPalettegen, rb_intern("Error"))
                 : rb_define_class_under(mPalettegen, "Error", rb_eStandardError);
    eDecodeError = rb_const_defined(mPalettegen, rb_intern("DecodeError"))
                       ? rb_const_get(mPalettegen, rb_intern("DecodeError"))
                       : rb_define_class_under(mPalettegen, "DecodeError", eError);
    rb_global_variable(&eError);
    rb_global_variable(&eDecodeError);

    rb_define_singleton_method(mNative, "extract", RUBY_METHOD_FUNC(native_extract), 6);
    rb_define_singleton_method(mNative, "encode", RUBY_METHOD_FUNC(native_encode), 4);
    rb_define_singleton_method(mNative, "webp_supported?", RUBY_METHOD_FUNC(native_webp_supported), 0);
    rb_define_const(mNative, "MIN_COLORS", INT2FIX(palettegen::kMinColors));
    rb_define_const(mNative, "MAX_COLORS", INT2FIX(palettegen::kMaxColors));
}
