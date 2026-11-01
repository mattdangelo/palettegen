// Image decoding/encoding via the vendored stb headers (and optional libwebp).
#include "palettegen.hpp"

#include <cstring>
#include <limits>

#define STBI_NO_STDIO          // We only ever decode from memory.
#define STBI_MAX_DIMENSIONS (1 << 16)
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

void check_dimensions(std::uint64_t w, std::uint64_t h) {
    if (w == 0 || h == 0) throw DecodeError("image has zero width or height");
    if (w * h > kMaxPixels) {
        throw DecodeError("image is too large (" + std::to_string(w) + "x" + std::to_string(h) +
                          ", limit is " + std::to_string(kMaxPixels) + " pixels)");
    }
}

bool is_webp(const std::uint8_t* d, std::size_t n) {
    return n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0;
}

#ifdef PALETTEGEN_HAVE_WEBP
Image decode_webp(const std::uint8_t* data, std::size_t size) {
    int w = 0, h = 0;
    if (!WebPGetInfo(data, size, &w, &h)) throw DecodeError("invalid WebP data");
    check_dimensions(static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h));

    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    if (!WebPDecodeRGBAInto(data, size, img.rgba.data(), img.rgba.size(), w * 4)) {
        throw DecodeError("failed to decode WebP image");
    }
    return img;
}
#endif

}  // namespace

bool webp_supported() {
#ifdef PALETTEGEN_HAVE_WEBP
    return true;
#else
    return false;
#endif
}

Image decode(const std::uint8_t* data, std::size_t size) {
    if (size == 0) throw DecodeError("image data is empty");
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw DecodeError("image data is too large");
    }

    if (is_webp(data, size)) {
#ifdef PALETTEGEN_HAVE_WEBP
        return decode_webp(data, size);
#else
        throw DecodeError("WebP support was not compiled in (install libwebp and reinstall the gem)");
#endif
    }

    const int len = static_cast<int>(size);
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(data, len, &w, &h, &channels)) {
        const char* why = stbi_failure_reason();
        throw DecodeError(std::string("unsupported or corrupt image") + (why ? ": " + std::string(why) : ""));
    }
    check_dimensions(static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h));

    // Always ask for 4 channels: greyscale is expanded, missing alpha becomes 255,
    // 16-bit PNGs and HDR are converted to 8-bit.
    unsigned char* pixels = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
    if (!pixels) {
        const char* why = stbi_failure_reason();
        throw DecodeError(std::string("failed to decode image") + (why ? ": " + std::string(why) : ""));
    }

    Image img;
    img.width = w;
    img.height = h;
    try {
        img.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    } catch (...) {
        stbi_image_free(pixels);
        throw;
    }
    stbi_image_free(pixels);
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
