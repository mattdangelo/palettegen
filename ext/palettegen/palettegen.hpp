// Core palette extraction library. Pure C++17, no Ruby dependencies, so it can
// be unit-tested or reused outside the gem.
#ifndef PALETTEGEN_HPP
#define PALETTEGEN_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace palettegen {

constexpr int kMinColors = 2;
constexpr int kMaxColors = 10;

// Default cap on decoded image size (width * height). Checked from the image
// header before any pixels are decoded, so oversized images and decompression
// bombs are rejected cheaply. Decoding needs 4 bytes per pixel, so 50 MP peaks
// at roughly 200 MB. Callers can lower or raise it per call.
constexpr std::uint64_t kDefaultMaxPixels = 50ull * 1000 * 1000;

struct Rgb {
    std::uint8_t r, g, b;
};

struct Swatch {
    Rgb color;
    double share;  // Fraction of (opaque, sampled) pixels in this cluster, 0..1.
};

// Frees an Image's pixel buffer. Defined in image_io.cpp, so the allocator that
// matches the decoder is always used (never one redefined by Ruby's headers).
void free_pixels(void* p);

// 8-bit RGBA, row-major, no padding. Owns a malloc'd buffer (the decoder's own
// allocation, so decoded pixels are never copied).
struct Image {
    int width = 0;
    int height = 0;
    std::unique_ptr<std::uint8_t, void (*)(void*)> pixels{nullptr, free_pixels};

    const std::uint8_t* data() const { return pixels.get(); }
    std::size_t byte_size() const { return static_cast<std::size_t>(width) * height * 4; }
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
    // Polled during clustering; when it becomes true, extract() throws Cancelled.
    const std::atomic<bool>* cancel = nullptr;
};

class Error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
class DecodeError : public Error {
    using Error::Error;
};
// The image's dimensions exceed the caller's max_pixels limit.
class TooLargeError : public DecodeError {
    using DecodeError::DecodeError;
};
// Options::cancel was set while extract() was running.
class Cancelled : public Error {
    using Error::Error;
};

// Decode an encoded image (JPEG/PNG/GIF/BMP, plus WebP when built with
// libwebp). Throws TooLargeError if width * height > max_pixels (checked
// before decoding), DecodeError for anything unsupported or corrupt.
Image decode(const std::uint8_t* data, std::size_t size, std::uint64_t max_pixels = kDefaultMaxPixels);

// Cluster the image's colours. Returns at most `opts.colors` swatches sorted by
// share, descending (fewer if the image has fewer distinct colours).
std::vector<Swatch> extract(const Image& image, const Options& opts);

// Render swatches as a horizontal strip of equal `square` x `square` blocks.
std::vector<std::uint8_t> encode_jpeg(const std::vector<Rgb>& colors, int square, int quality);
std::vector<std::uint8_t> encode_png(const std::vector<Rgb>& colors, int square);

bool webp_supported();

}  // namespace palettegen

#endif
