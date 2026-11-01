// Image decoding/encoding via the vendored stb headers (and optional libwebp).
#include "palettegen.hpp"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#define STBI_NO_STDIO          // We only ever decode from memory.
#define STBI_MAX_DIMENSIONS (1 << 16)
// Only compile the decoders we need. The rest (PSD, HDR, TGA, PIC, PNM) are
// rarely uploaded but are a large attack surface for untrusted input.
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STB_IMAGE_WRITE_STATIC

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef PALETTEGEN_HAVE_WEBP
#include <webp/decode.h>
#endif

namespace palettegen {

namespace {

void check_dimensions(std::uint64_t w, std::uint64_t h, std::uint64_t max_pixels) {
    if (w == 0 || h == 0) throw DecodeError("image has zero width or height");
    if (w * h > max_pixels) {
        throw TooLargeError("image is too large (" + std::to_string(w) + "x" + std::to_string(h) + " = " +
                            std::to_string(w * h) + " pixels, limit is " + std::to_string(max_pixels) + ")");
    }
}

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint32_t le32(const std::uint8_t* p) {
    return (std::uint32_t(p[3]) << 24) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[1]) << 8) | p[0];
}

// PNG and BMP headers can declare dimensions far beyond what stb accepts, and
// stb then reports a generic "unknown image type". Read them here so oversized
// images get TooLargeError. (JPEG and GIF store 16-bit dimensions, which stb's
// own limit already covers.)
void check_header_dimensions(const std::uint8_t* d, std::size_t n, std::uint64_t max_pixels) {
    static const std::uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n >= 24 && std::memcmp(d, kPngSig, 8) == 0 && std::memcmp(d + 12, "IHDR", 4) == 0) {
        const std::uint32_t w = be32(d + 16), h = be32(d + 20);
        if (w && h) check_dimensions(w, h, max_pixels);
    } else if (n >= 26 && d[0] == 'B' && d[1] == 'M' && le32(d + 14) >= 40) {  // BITMAPINFOHEADER or later
        const std::int64_t w = static_cast<std::int32_t>(le32(d + 18));
        const std::int64_t h = static_cast<std::int32_t>(le32(d + 22));  // negative = top-down
        const auto aw = static_cast<std::uint64_t>(w < 0 ? -w : w);
        const auto ah = static_cast<std::uint64_t>(h < 0 ? -h : h);
        if (aw && ah) check_dimensions(aw, ah, max_pixels);
    }
}

bool is_webp(const std::uint8_t* d, std::size_t n) {
    return n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0;
}

#ifdef PALETTEGEN_HAVE_WEBP
Image decode_webp(const std::uint8_t* data, std::size_t size, std::uint64_t max_pixels) {
    int w = 0, h = 0;
    if (!WebPGetInfo(data, size, &w, &h)) throw DecodeError("invalid WebP data");
    check_dimensions(static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h), max_pixels);

    Image img;
    img.width = w;
    img.height = h;
    img.pixels.reset(static_cast<std::uint8_t*>(std::malloc(img.byte_size())));
    if (!img.pixels) throw std::bad_alloc();
    if (!WebPDecodeRGBAInto(data, size, img.pixels.get(), img.byte_size(), w * 4)) {
        throw DecodeError("failed to decode WebP image");
    }
    return img;
}
#endif

}  // namespace

void free_pixels(void* p) { std::free(p); }  // stb_image and decode_webp both allocate with malloc

bool webp_supported() {
#ifdef PALETTEGEN_HAVE_WEBP
    return true;
#else
    return false;
#endif
}

Image decode(const std::uint8_t* data, std::size_t size, std::uint64_t max_pixels) {
    if (size == 0) throw DecodeError("image data is empty");
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw DecodeError("image data is too large");
    }

    if (is_webp(data, size)) {
#ifdef PALETTEGEN_HAVE_WEBP
        return decode_webp(data, size, max_pixels);
#else
        throw DecodeError("WebP support was not compiled in (install libwebp and reinstall the gem)");
#endif
    }

    check_header_dimensions(data, size, max_pixels);

    const int len = static_cast<int>(size);
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(data, len, &w, &h, &channels)) {
        const char* why = stbi_failure_reason();
        throw DecodeError(std::string("unsupported or corrupt image") + (why ? ": " + std::string(why) : ""));
    }
    check_dimensions(static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h), max_pixels);

    // Always ask for 4 channels: greyscale is expanded, missing alpha becomes 255,
    // and 16-bit PNGs are converted to 8-bit. The buffer is adopted, not copied.
    Image img;
    img.pixels.reset(stbi_load_from_memory(data, len, &w, &h, &channels, 4));
    if (!img.pixels) {
        const char* why = stbi_failure_reason();
        throw DecodeError(std::string("failed to decode image") + (why ? ": " + std::string(why) : ""));
    }
    img.width = w;
    img.height = h;
    return img;
}

namespace {

void validate_strip(const std::vector<Rgb>& colors, int square) {
    if (colors.empty()) throw Error("palette has no colours");
    if (square < 1 || square > 4096) throw Error("square size must be between 1 and 4096");
}

std::vector<std::uint8_t> render_strip(const std::vector<Rgb>& colors, int square) {
    const std::size_t width = colors.size() * static_cast<std::size_t>(square);
    std::vector<std::uint8_t> px(width * square * 3);
    for (int y = 0; y < square; ++y) {
        std::uint8_t* row = px.data() + static_cast<std::size_t>(y) * width * 3;
        for (std::size_t x = 0; x < width; ++x) {
            const Rgb& c = colors[x / square];
            row[x * 3 + 0] = c.r;
            row[x * 3 + 1] = c.g;
            row[x * 3 + 2] = c.b;
        }
    }
    return px;
}

void append_bytes(void* ctx, void* data, int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(ctx);
    auto* bytes = static_cast<std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

}  // namespace

std::vector<std::uint8_t> encode_jpeg(const std::vector<Rgb>& colors, int square, int quality) {
    validate_strip(colors, square);
    if (quality < 1 || quality > 100) throw Error("JPEG quality must be between 1 and 100");

    const auto px = render_strip(colors, square);
    std::vector<std::uint8_t> out;
    const int width = static_cast<int>(colors.size()) * square;
    // Note: stb disables chroma subsampling above quality 90, which keeps the
    // edges between swatches clean.
    if (!stbi_write_jpg_to_func(append_bytes, &out, width, square, 3, px.data(), quality)) {
        throw Error("failed to encode JPEG");
    }
    return out;
}

std::vector<std::uint8_t> encode_png(const std::vector<Rgb>& colors, int square) {
    validate_strip(colors, square);

    const auto px = render_strip(colors, square);
    std::vector<std::uint8_t> out;
    const int width = static_cast<int>(colors.size()) * square;
    if (!stbi_write_png_to_func(append_bytes, &out, width, square, 3, px.data(), width * 3)) {
        throw Error("failed to encode PNG");
    }
    return out;
}

}  // namespace palettegen
