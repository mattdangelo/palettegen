// Weighted k-means colour clustering.
//
// Pipeline:
//   1. Sample up to `max_samples` pixels (jittered stride), skipping transparent ones.
//   2. Collapse identical colours into weighted points (big speed-up, and makes
//      "image has fewer colours than requested" trivially correct).
//   3. Convert to the working colour space (CIELAB by default, so averages are
//      perceptual instead of muddy gamma-encoded RGB averages).
//   4. k-means++ seeding + Lloyd iterations, best of `attempts` restarts.
//
// Everything uses std::mt19937_64 with explicit arithmetic (no std::*_distribution)
// and sorted inputs, so a given seed yields identical palettes on every platform.
#include "palettegen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>

namespace palettegen {

namespace {

using Vec3 = std::array<double, 3>;

struct Point {
    Vec3 v;
    double weight;
    std::uint32_t key;  // 0xRRGGBB of the source colour.
};

// ---------------------------------------------------------------------------
// Colour space conversion (sRGB <-> CIELAB, D65)
// ---------------------------------------------------------------------------

constexpr double kXn = 0.95047, kYn = 1.0, kZn = 1.08883;
constexpr double kDelta = 6.0 / 29.0;

double srgb_to_linear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double c) {
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

double lab_f(double t) {
    return t > kDelta * kDelta * kDelta ? std::cbrt(t) : t / (3 * kDelta * kDelta) + 4.0 / 29.0;
}

double lab_f_inv(double t) {
    return t > kDelta ? t * t * t : 3 * kDelta * kDelta * (t - 4.0 / 29.0);
}

const std::array<double, 256>& linear_lut() {
    static const std::array<double, 256> lut = [] {
        std::array<double, 256> t{};
        for (int i = 0; i < 256; ++i) t[i] = srgb_to_linear(i / 255.0);
        return t;
    }();
    return lut;
}

Vec3 rgb_to_lab(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8) {
    const auto& lut = linear_lut();
    const double r = lut[r8], g = lut[g8], b = lut[b8];
    const double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    const double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
    const double fx = lab_f(x / kXn), fy = lab_f(y / kYn), fz = lab_f(z / kZn);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

std::uint8_t to_byte(double unit) {
    const double v = std::round(unit * 255.0);
    return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
}

Rgb lab_to_rgb(const Vec3& lab) {
    const double fy = (lab[0] + 16.0) / 116.0;
    const double fx = fy + lab[1] / 500.0;
    const double fz = fy - lab[2] / 200.0;
    const double x = kXn * lab_f_inv(fx), y = kYn * lab_f_inv(fy), z = kZn * lab_f_inv(fz);
    const double r = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
    const double g = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
    const double b = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;
    auto enc = [](double c) { return to_byte(linear_to_srgb(std::clamp(c, 0.0, 1.0))); };
    return {enc(r), enc(g), enc(b)};
}

Vec3 to_space(std::uint32_t key, ColorSpace space) {
    const auto r = static_cast<std::uint8_t>(key >> 16);
    const auto g = static_cast<std::uint8_t>(key >> 8);
    const auto b = static_cast<std::uint8_t>(key);
    if (space == ColorSpace::Lab) return rgb_to_lab(r, g, b);
    return {double(r), double(g), double(b)};
}

Rgb from_space(const Vec3& v, ColorSpace space) {
    if (space == ColorSpace::Lab) return lab_to_rgb(v);
    return {to_byte(v[0] / 255.0), to_byte(v[1] / 255.0), to_byte(v[2] / 255.0)};
}

Rgb key_to_rgb(std::uint32_t key) {
    return {static_cast<std::uint8_t>(key >> 16), static_cast<std::uint8_t>(key >> 8),
            static_cast<std::uint8_t>(key)};
}

std::uint32_t rgb_key(const Rgb& c) {
    return (std::uint32_t(c.r) << 16) | (std::uint32_t(c.g) << 8) | c.b;
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

double uniform01(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * 0x1.0p-53;
}

std::vector<Point> collect_points(const Image& img, const Options& opts, std::mt19937_64& rng) {
    const std::size_t total = static_cast<std::size_t>(img.width) * img.height;
    if (!img.data()) throw Error("image has no pixel data");

    std::unordered_map<std::uint32_t, std::uint32_t> hist;

    auto add = [&](std::size_t idx) {
        const std::uint8_t* p = img.data() + idx * 4;
        if (p[3] < opts.alpha_threshold) return;
        ++hist[(std::uint32_t(p[0]) << 16) | (std::uint32_t(p[1]) << 8) | p[2]];
    };

    const std::size_t stride = std::max<std::size_t>(1, (total + opts.max_samples - 1) / opts.max_samples);
    if (stride == 1) {
        for (std::size_t i = 0; i < total; ++i) add(i);
    } else {
        // One random pixel per block: avoids the aliasing a fixed stride gets on
        // regular patterns (stripes, checkerboards, columns).
        hist.reserve(opts.max_samples);
        for (std::size_t block = 0; block < total; block += stride) {
            const std::size_t span = std::min(stride, total - block);
            add(block + static_cast<std::size_t>(rng() % span));
        }
        // A tiny opaque region in a huge transparent image can be missed by
        // sampling; fall back to a full scan in that case.
        if (hist.empty()) {
            for (std::size_t i = 0; i < total; ++i) add(i);
        }
    }

    if (hist.empty()) throw Error("image has no opaque pixels");

    std::vector<Point> points;
    points.reserve(hist.size());
    for (const auto& [key, count] : hist) {
        points.push_back({to_space(key, opts.space), static_cast<double>(count), key});
    }
    // unordered_map iteration order is implementation-defined; sort for determinism.
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.key < b.key; });
    return points;
}

// ---------------------------------------------------------------------------
// k-means
// ---------------------------------------------------------------------------

double dist2(const Vec3& a, const Vec3& b) {
    const double d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2];
    return d0 * d0 + d1 * d1 + d2 * d2;
}

std::size_t weighted_pick(const std::vector<Point>& pts, const std::vector<double>& scores, double total,
                          std::mt19937_64& rng) {
    const double target = uniform01(rng) * total;
    double acc = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        acc += scores[i];
        if (acc > target && scores[i] > 0) return i;
    }
    // Floating-point round-off: return the last candidate with a non-zero score.
    for (std::size_t i = pts.size(); i-- > 0;) {
        if (scores[i] > 0) return i;
    }
    return 0;
}

// k-means++ seeding (weighted by pixel count). Guarantees distinct starting
// colours, which the old "random pixel positions" approach did not.
std::vector<Vec3> seed_centroids(const std::vector<Point>& pts, int k, std::mt19937_64& rng) {
    std::vector<double> scores(pts.size());
    double total = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) total += (scores[i] = pts[i].weight);

    std::vector<Vec3> centroids;
    centroids.push_back(pts[weighted_pick(pts, scores, total, rng)].v);

    std::vector<double> d2(pts.size(), std::numeric_limits<double>::max());
    while (static_cast<int>(centroids.size()) < k) {
        total = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            d2[i] = std::min(d2[i], dist2(pts[i].v, centroids.back()));
            total += (scores[i] = pts[i].weight * d2[i]);
        }
        if (total <= 0.0) break;  // Every point already coincides with a centroid.
        centroids.push_back(pts[weighted_pick(pts, scores, total, rng)].v);
    }
    return centroids;
}

struct Clustering {
    std::vector<Vec3> centroids;
    std::vector<double> weights;
    double inertia = std::numeric_limits<double>::max();
};

void check_cancel(const Options& opts) {
    if (opts.cancel && opts.cancel->load(std::memory_order_relaxed)) throw Cancelled("cancelled");
}

Clustering lloyd(const std::vector<Point>& pts, std::vector<Vec3> centroids, int max_iterations,
                 const Options& opts) {
    const std::size_t k = centroids.size();
    std::vector<int> assign(pts.size(), -1);
    std::vector<double> d2(pts.size(), 0.0);
    constexpr double kShiftTolerance = 1e-6;  // squared distance, working-space units

    Clustering result;
    for (int iter = 0;; ++iter) {
        check_cancel(opts);
        // Assignment step.
        bool changed = false;
        double inertia = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            int best = 0;
            double best_d = dist2(pts[i].v, centroids[0]);
            for (std::size_t c = 1; c < k; ++c) {
                const double d = dist2(pts[i].v, centroids[c]);
                if (d < best_d) {
                    best_d = d;
                    best = static_cast<int>(c);
                }
            }
            if (assign[i] != best) {
                assign[i] = best;
                changed = true;
            }
            d2[i] = best_d;
            inertia += pts[i].weight * best_d;
        }

        // Accumulate in double: no overflow regardless of image size.
        std::vector<Vec3> sums(k, Vec3{0, 0, 0});
        std::vector<double> weights(k, 0.0);
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const auto c = static_cast<std::size_t>(assign[i]);
            for (int j = 0; j < 3; ++j) sums[c][j] += pts[i].v[j] * pts[i].weight;
            weights[c] += pts[i].weight;
        }

        if (!changed || iter >= max_iterations) {
            result.centroids = centroids;
            result.weights = weights;
            result.inertia = inertia;
            return result;
        }

        // Update step.
        double max_shift = 0.0;
        for (std::size_t c = 0; c < k; ++c) {
            Vec3 next;
            if (weights[c] > 0.0) {
                for (int j = 0; j < 3; ++j) next[j] = sums[c][j] / weights[c];
            } else {
                // Empty cluster: move it to the worst-served point instead of
                // leaving a dead centroid behind (old code produced duplicate swatches).
                std::size_t worst = 0;
                double worst_score = -1.0;
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    const double s = pts[i].weight * d2[i];
                    if (s > worst_score) {
                        worst_score = s;
                        worst = i;
                    }
                }
                next = pts[worst].v;
                d2[worst] = 0.0;
                max_shift = std::numeric_limits<double>::max();
            }
            max_shift = std::max(max_shift, dist2(next, centroids[c]));
            centroids[c] = next;
        }

        // Centroids have stopped moving: do one more assignment pass (so the
        // returned weights match the final centroids) then stop.
        if (max_shift < kShiftTolerance) max_iterations = iter + 1;
    }
}

void validate(const Options& o) {
    if (o.colors < kMinColors || o.colors > kMaxColors) {
        throw Error("colors must be between " + std::to_string(kMinColors) + " and " + std::to_string(kMaxColors));
    }
    if (o.max_samples < 1) throw Error("max_samples must be positive");
    if (o.attempts < 1) throw Error("attempts must be positive");
    if (o.max_iterations < 1) throw Error("max_iterations must be positive");
}

}  // namespace

std::vector<Swatch> extract(const Image& image, const Options& opts) {
    validate(opts);
    if (image.width <= 0 || image.height <= 0) throw Error("image is empty");

    std::mt19937_64 rng(opts.seed);
    const std::vector<Point> pts = collect_points(image, opts, rng);

    double total_weight = 0.0;
    for (const auto& p : pts) total_weight += p.weight;

    // Accumulate by final 8-bit colour, so two clusters that round to the same
    // RGB value are merged instead of producing duplicate swatches.
    std::unordered_map<std::uint32_t, double> shares;

    if (pts.size() <= static_cast<std::size_t>(opts.colors)) {
        // Fewer distinct colours than requested: the exact colours are the answer.
        for (const auto& p : pts) shares[p.key] += p.weight / total_weight;
    } else {
        Clustering best;
        for (int a = 0; a < opts.attempts; ++a) {
            check_cancel(opts);
            Clustering c = lloyd(pts, seed_centroids(pts, opts.colors, rng), opts.max_iterations, opts);
            if (c.inertia < best.inertia) best = std::move(c);
        }
        for (std::size_t c = 0; c < best.centroids.size(); ++c) {
            if (best.weights[c] <= 0.0) continue;
            shares[rgb_key(from_space(best.centroids[c], opts.space))] += best.weights[c] / total_weight;
        }
    }

    std::vector<Swatch> out;
    out.reserve(shares.size());
    for (const auto& [key, share] : shares) out.push_back({key_to_rgb(key), share});
    std::sort(out.begin(), out.end(), [](const Swatch& a, const Swatch& b) {
        if (a.share != b.share) return a.share > b.share;
        return rgb_key(a.color) < rgb_key(b.color);
    });
    return out;
}

}  // namespace palettegen
