// Times decode() and extract() on the C++ core, without Ruby.
//
// Build (any C++17 compiler; flags match extconf.rb):
//   g++     -std=c++17 -O2 -Iext/palettegen -Iext/palettegen/vendor bench/bench_extract.cpp ext/palettegen/image_io.cpp ext/palettegen/kmeans.cpp -o bench_extract
//   cl /std:c++17 /O2 /EHsc /Iext/palettegen /Iext/palettegen/vendor bench/bench_extract.cpp ext/palettegen/image_io.cpp ext/palettegen/kmeans.cpp
//
// Usage:
//   bench_extract [image ...]
// With no arguments, runs on a synthetic 12 MP noisy gradient (lots of unique
// colours, a near worst case for clustering). Prints the palette too, so the
// output from two builds can be diffed to check they produce the same colours.
#include "palettegen.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

template <class F>
double median_ms(int runs, F&& f) {
    std::vector<double> t;
    for (int r = 0; r < runs; ++r) {
        const auto t0 = Clock::now();
        f();
        t.push_back(ms_since(t0));
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

palettegen::Image synthetic(int w, int h) {
    palettegen::Image img;
    img.width = w;
    img.height = h;
    img.pixels.reset(static_cast<std::uint8_t*>(std::malloc(img.byte_size())));
    std::mt19937 rng(1);
    std::uint8_t* p = img.pixels.get();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x, p += 4) {
            const int noise = static_cast<int>(rng() % 24);
            p[0] = static_cast<std::uint8_t>(std::min(255, x * 255 / w + noise));
            p[1] = static_cast<std::uint8_t>(std::min(255, y * 255 / h + noise));
            p[2] = static_cast<std::uint8_t>(std::min(255, (x + y) * 255 / (w + h) + noise));
            p[3] = 255;
        }
    }
    return img;
}

void report(const char* name, const palettegen::Image& img, double decode_ms) {
    const int kRuns = 5;
    for (auto space : {palettegen::ColorSpace::Lab, palettegen::ColorSpace::Rgb}) {
        for (int colors : {5, 10}) {
            palettegen::Options opts;
            opts.colors = colors;
            opts.space = space;
            std::vector<palettegen::Swatch> out;
            const double t = median_ms(kRuns, [&] { out = palettegen::extract(img, opts); });
            std::printf("%s %dx%d %s k=%-2d decode %8.2f ms  extract %8.2f ms  ", name, img.width, img.height,
                        space == palettegen::ColorSpace::Lab ? "lab" : "rgb", colors, decode_ms, t);
            for (const auto& s : out) std::printf(" #%02x%02x%02x:%.6f", s.color.r, s.color.g, s.color.b, s.share);
            std::printf("\n");
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            report("synthetic", synthetic(4000, 3000), 0.0);
            return 0;
        }
        for (int a = 1; a < argc; ++a) {
            std::ifstream f(argv[a], std::ios::binary);
            if (!f) {
                std::fprintf(stderr, "cannot open %s\n", argv[a]);
                return 1;
            }
            const std::vector<std::uint8_t> blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            palettegen::Image img;
            const double decode_ms = median_ms(3, [&] { img = palettegen::decode(blob.data(), blob.size()); });
            report(argv[a], img, decode_ms);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
