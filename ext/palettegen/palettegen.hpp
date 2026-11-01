// Core palette extraction library. Pure C++17, no Ruby dependencies, so it can
// be unit-tested or reused outside the gem.
#ifndef PALETTEGEN_HPP
#define PALETTEGEN_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace palettegen {

constexpr int kMinColors = 2;
constexpr int kMaxColors = 10;

// Refuse to decode anything larger than this (protects servers from
// decompression bombs). 100 megapixels is well beyond any normal photo.
constexpr std::uint64_t kMaxPixels = 100ull * 1000 * 1000;

struct Rgb {
    std::uint8_t r, g, b;
};

struct Swatch {
    Rgb color;
    double share;  // Fraction of (opaque, sampled) pixels in this cluster, 0..1.
};

// 8-bit RGBA, row-major, no padding.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

enum class ColorSpace { Lab, Rgb };

struct Options {
    int colors = 5;
    ColorSpace space = ColorSpace::Lab;
    std::uint64_t seed = 0;            // Fixed default => same image gives same palette.
    std::size_t max_samples = 250000;  // Pixels sampled from large images.
    std::uint8_t alpha_threshold = 128;  // Pixels with alpha below this are ignored.
    int attempts = 3;                  // k-means restarts; best (lowest error) wins.
    int max_iterations = 100;
};

class Error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
class DecodeError : public Error {
    using Error::Error;
};

// Decode an encoded image (JPEG/PNG/BMP/GIF/TGA/PSD/HDR/PNM, plus WebP when
// built with libwebp). Throws DecodeError.
Image decode(const std::uint8_t* data, std::size_t size);

// Cluster the image's colours. Returns at most `opts.colors` swatches sorted by
// share, descending (fewer if the image has fewer distinct colours).
std::vector<Swatch> extract(const Image& image, const Options& opts);

// Render swatches as a horizontal strip of equal `square` x `square` blocks.
std::vector<std::uint8_t> encode_jpeg(const std::vector<Rgb>& colors, int square, int quality);
std::vector<std::uint8_t> encode_png(const std::vector<Rgb>& colors, int square);

bool webp_supported();

}  // namespace palettegen

#endif
