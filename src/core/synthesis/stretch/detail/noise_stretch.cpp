#include "core/synthesis/stretch/detail/noise_stretch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anno::stretch_detail {

namespace {

/// splitmix64: a well-mixed 64-bit hash of the seed and two counters.
std::uint64_t mix(std::uint64_t seed, std::uint64_t a, std::uint64_t b) {
    std::uint64_t z = seed + 0x9e3779b97f4a7c15ULL * (a + 1) + 0xbf58476d1ce4e5b9ULL * (b + 1);
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
}

/// Uniform in [-1, 1) from the top 53 bits of a hash.
double signed_unit(std::uint64_t h) {
    return static_cast<double>(h >> 11U) / 4503599627370496.0 - 1.0;
}

/// Over a zone of `zone` samples at each end of the span the grain sum is blended (equal power)
/// into an exact slope-1 copy of the source that continues the neighbouring audio, so the span
/// meets its neighbours on the source's own samples (the grains read jittered positions and, for
/// tonal or onset material, do not add up to the source). The blend is a function of the span only,
/// not of the requested range.
void anchor_edges(const grain::Signal& x, const SpanGeometry& g, std::int64_t begin,
                  std::int64_t end, double zone, std::vector<double>& out) {
    const double zone_samples = std::min(zone, 0.33 * static_cast<double>(g.last - g.first));
    if (zone_samples < 1.0) {
        return;
    }
    const auto start_source = static_cast<std::int64_t>(std::llround(g.src_start));
    const auto end_source =
        static_cast<std::int64_t>(std::llround(g.source_at(static_cast<double>(g.last))));
    for (std::int64_t n = begin; n < end; ++n) {
        const double from_start = static_cast<double>(n - g.first);
        const double from_end = static_cast<double>(g.last - 1 - n);
        const bool at_start = from_start < zone_samples;
        const bool at_end = from_end < zone_samples;
        if (!at_start && !at_end) {
            continue;
        }
        const double d = at_start ? from_start : from_end;
        const double angle = 0.5 * std::numbers::pi * d / zone_samples;
        const double copy_gain = std::cos(angle);
        const double noise_gain = std::sin(angle);
        const double copy =
            at_start ? x.at(start_source + (n - g.first)) : x.at(end_source + (n - g.last));
        double& o = out[static_cast<std::size_t>(n - begin)];
        o = noise_gain * o + copy_gain * copy;
    }
}

}  // namespace

std::vector<double> synthesise_noise(const grain::Signal& x, const SpanGeometry& g,
                                     std::int64_t begin, std::int64_t end, const StretchParams& p) {
    std::vector<double> out(static_cast<std::size_t>(end - begin), 0.0);
    const auto length = std::max<std::int64_t>(
        4, 2 * std::llround(0.5 * p.noise_grain.value() * g.rate_hz));  // even
    const double hop = static_cast<double>(length) / 2.0;
    const double half = hop;
    const auto k_lo = static_cast<std::int64_t>(
                          std::floor((static_cast<double>(begin) - g.out_start - half) / hop)) -
                      1;
    const auto k_hi = static_cast<std::int64_t>(
                          std::ceil((static_cast<double>(end) - g.out_start + half) / hop)) +
                      1;
    for (std::int64_t k = k_lo; k <= k_hi; ++k) {
        const double centre = g.out_start + static_cast<double>(k) * hop;
        const double jitter = p.noise_jitter_hop_fraction * hop *
                              signed_unit(mix(p.noise_seed, static_cast<std::uint64_t>(g.first),
                                              static_cast<std::uint64_t>(k)));
        // Whole-sample reads: an interpolated read at a random fraction low-passes white noise.
        const auto source_centre =
            static_cast<std::int64_t>(std::llround(g.source_at(centre) + jitter));
        const auto centre_sample = static_cast<std::int64_t>(std::llround(centre));
        const auto n_lo =
            std::max<std::int64_t>(begin, static_cast<std::int64_t>(std::ceil(centre - half)));
        const auto n_hi =
            std::min<std::int64_t>(end, static_cast<std::int64_t>(std::ceil(centre + half)));
        for (std::int64_t n = n_lo; n < n_hi; ++n) {
            const double u = static_cast<double>(n) - centre;
            const double window =
                std::sin(std::numbers::pi * (u + half) / static_cast<double>(length));
            out[static_cast<std::size_t>(n - begin)] +=
                window * x.at(source_centre + (n - centre_sample));
        }
    }
    anchor_edges(x, g, begin, end, hop, out);
    return out;
}

}  // namespace anno::stretch_detail
