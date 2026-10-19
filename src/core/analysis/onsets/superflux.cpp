#include "core/analysis/onsets/superflux.h"

#include "core/dsp/stft.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_fmin_hz = 30.0;
constexpr double k_fmax_hz = 16000.0;
constexpr double k_fmax_nyquist_share = 0.45;
constexpr int k_min_bands = 4;

struct Filter {
    std::size_t first = 0;
    std::vector<double> weights;  // for bins first, first + 1, ...
};

/// Log-spaced triangular filters over FFT bins (centres on distinct bins), unit sum each.
std::vector<Filter> log_filterbank(int bands_per_octave, double bin_hz, int n_bins, double rate) {
    const double fmax = std::min(k_fmax_hz, k_fmax_nyquist_share * rate);
    std::vector<int> centres;
    for (int k = 0;; ++k) {
        const double f = k_fmin_hz * std::pow(2.0, static_cast<double>(k) / bands_per_octave);
        if (f > fmax) {
            break;
        }
        const int bin = std::clamp(static_cast<int>(std::lround(f / bin_hz)), 1, n_bins - 2);
        if (centres.empty() || bin > centres.back()) {
            centres.push_back(bin);
        }
    }
    if (static_cast<int>(centres.size()) <
        k_min_bands) {  // very coarse spectrum: fall back to a linear split
        centres.clear();
        for (int b = 1; b < n_bins - 1; b += std::max(1, (n_bins - 2) / 16)) {
            centres.push_back(b);
        }
    }
    std::vector<Filter> bank;
    for (std::size_t i = 0; i < centres.size(); ++i) {
        const int left = i == 0 ? centres[i] - 1 : centres[i - 1];
        const int right = i + 1 == centres.size() ? centres[i] + 1 : centres[i + 1];
        Filter f{.first = static_cast<std::size_t>(std::max(0, left)), .weights = {}};
        double sum = 0.0;
        for (int b = std::max(0, left); b <= std::min(n_bins - 1, right); ++b) {
            const double w = b <= centres[i]
                                 ? static_cast<double>(b - left) / std::max(1, centres[i] - left)
                                 : static_cast<double>(right - b) / std::max(1, right - centres[i]);
            f.weights.push_back(std::max(0.0, w));
            sum += std::max(0.0, w);
        }
        if (sum > 0.0) {
            for (double& w : f.weights) {
                w /= sum;
            }
            bank.push_back(std::move(f));
        }
    }
    return bank;
}

int nearest_pow2(double n) {
    const int exponent = std::max(8, static_cast<int>(std::lround(std::log2(std::max(1.0, n)))));
    return static_cast<int>(std::pow(2.0, exponent));
}

Result<void> validate(const OnsetParams& p) {
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (!positive(p.hop.value()) || !positive(p.window.value()) || !positive(p.lag.value()) ||
        !positive(p.threshold_window.value()) || !positive(p.min_gap.value()) ||
        !positive(p.refine_window.value()) || !positive(p.log_lambda) || !std::isfinite(p.delta) ||
        p.delta < 0.0 || p.filterbank_bands_per_octave < 1 || p.max_filter_bins < 1) {
        return fail(ErrorCode::invalid_argument, "onsets: invalid parameters");
    }
    return {};
}

}  // namespace

Result<Activation> superflux_activation(std::span<const float> mono, Hz rate,
                                        const OnsetParams& params) {
    if (auto r = validate(params); !r) {
        return Unexpected{r.error()};
    }
    const int hop_samples =
        std::max(1, static_cast<int>(std::lround(params.hop.value() * rate.value())));
    Activation out{.values = {}, .hop = Seconds{static_cast<double>(hop_samples) / rate.value()}};
    const int n_fft = nearest_pow2(params.window.value() * rate.value());
    if (mono.size() < static_cast<std::size_t>(n_fft)) {
        return out;  // shorter than the window: no meaningful spectral change
    }
    auto spec = stft(mono, rate,
                     {.n_fft = n_fft,
                      .hop_samples = std::min(hop_samples, n_fft),
                      .window = WindowKind::hann_periodic});
    if (!spec) {
        return Unexpected{spec.error()};
    }
    const std::vector<Filter> bank =
        log_filterbank(params.filterbank_bands_per_octave, rate.value() / n_fft,
                       static_cast<int>(spec->n_bins), rate.value());
    const auto frames = static_cast<std::size_t>(spec->n_frames);
    const std::size_t bands = bank.size();
    std::vector<double> log_spec(frames * bands);
    for (std::size_t f = 0; f < frames; ++f) {
        const auto frame = static_cast<std::int64_t>(f);
        for (std::size_t b = 0; b < bands; ++b) {
            double s = 0.0;
            const auto bin_first = static_cast<std::int64_t>(bank[b].first);
            for (std::size_t k = 0; k < bank[b].weights.size(); ++k) {
                s += bank[b].weights[k] *
                     std::abs(spec->at(frame, bin_first + static_cast<std::int64_t>(k)));
            }
            log_spec[f * bands + b] = std::log1p(params.log_lambda * s);
        }
    }
    const auto lag =
        static_cast<std::size_t>(std::max(1L, std::lround(params.lag.value() / out.hop.value())));
    const auto radius = static_cast<std::ptrdiff_t>((params.max_filter_bins - 1) / 2);
    out.values.assign(frames, 0.0);
    for (std::size_t f = lag; f < frames; ++f) {
        double sum = 0.0;
        for (std::size_t b = 0; b < bands; ++b) {
            const auto lo = static_cast<std::size_t>(
                std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(b) - radius));
            const auto hi = std::min(bands - 1, b + static_cast<std::size_t>(radius));
            double reference = 0.0;
            for (std::size_t k = lo; k <= hi; ++k) {
                reference = std::max(reference, log_spec[(f - lag) * bands + k]);
            }
            sum += std::max(0.0, log_spec[f * bands + b] - reference);
        }
        out.values[f] = sum / static_cast<double>(bands);
    }
    return out;
}

}  // namespace anno
