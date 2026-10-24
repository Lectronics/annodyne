#include "core/dsp/band_split.h"

#include "core/dsp/stft.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anno {

namespace {

constexpr int k_n_fft = 2048;
constexpr int k_hop = 512;

/// 1 below cutoff - transition/2, 0 above cutoff + transition/2, a raised cosine between. A cut-off
/// of 0 is "no low band" at every frequency, including the DC bin.
double mask_at(double bin_hz, double cutoff_hz, double transition_hz) {
    if (cutoff_hz <= 0.0) {
        return 0.0;
    }
    const double lo = cutoff_hz - 0.5 * transition_hz;
    const double hi = cutoff_hz + 0.5 * transition_hz;
    if (bin_hz <= lo) {
        return 1.0;
    }
    if (bin_hz >= hi) {
        return 0.0;
    }
    return 0.5 * (1.0 + std::cos(std::numbers::pi * (bin_hz - lo) / transition_hz));
}

}  // namespace

Result<BandSplit> split_band(std::span<const float> x, Hz sample_rate,
                             std::span<const double> cutoff_hz, Hz transition) {
    if (x.empty() || !std::isfinite(sample_rate.value()) || sample_rate.value() <= 0.0 ||
        !std::isfinite(transition.value()) || transition.value() <= 0.0 ||
        cutoff_hz.size() != x.size()) {
        return fail(ErrorCode::invalid_argument,
                    "split_band: bad signal, rate, transition or cutoff");
    }
    if (std::ranges::any_of(cutoff_hz, [](double c) { return !std::isfinite(c) || c < 0.0; })) {
        return fail(ErrorCode::invalid_argument, "split_band: cut-off must be finite and >= 0");
    }
    auto spec = stft(x, sample_rate, StftParams{.n_fft = k_n_fft, .hop_samples = k_hop});
    if (!spec) {
        return Unexpected{spec.error()};
    }
    const double bin_width_hz = sample_rate.value() / k_n_fft;
    for (std::int64_t f = 0; f < spec->n_frames; ++f) {
        const auto centre =
            std::min<std::size_t>(static_cast<std::size_t>(f * k_hop), x.size() - 1);
        const double cutoff = cutoff_hz[centre];
        for (std::int64_t b = 0; b < spec->n_bins; ++b) {
            const auto m = static_cast<float>(
                mask_at(static_cast<double>(b) * bin_width_hz, cutoff, transition.value()));
            spec->data[static_cast<std::size_t>(f * spec->n_bins + b)] *= m;
        }
    }
    auto low = istft(*spec, static_cast<std::int64_t>(x.size()));
    if (!low) {
        return Unexpected{low.error()};
    }
    BandSplit out{.low = std::move(*low), .high = std::vector<float>(x.size())};
    for (std::size_t i = 0; i < x.size(); ++i) {
        out.high[i] = x[i] - out.low[i];
    }
    return out;
}

}  // namespace anno
