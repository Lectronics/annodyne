#include "core/analysis/onsets/stages/band_spectrogram.h"

#include "core/analysis/onsets/detail/stage_check.h"
#include "core/dsp/stft.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_fmax_nyquist_share = 0.45;
constexpr int k_min_bands = 4;

struct Filter {
    std::size_t first = 0;
    std::size_t centre_bin = 0;
    std::vector<double> weights;  // for bins first, first + 1, ...
};

/// Log-spaced triangular filters over FFT bins (centres on distinct bins), unit sum each.
std::vector<Filter> log_filterbank(const BandParams& p, double bin_hz, int n_bins, double rate) {
    const double fmax = std::min(p.f_max.value(), k_fmax_nyquist_share * rate);
    std::vector<int> centres;
    for (int k = 0;; ++k) {
        const double f =
            p.f_min.value() * std::pow(2.0, static_cast<double>(k) / p.bands_per_octave);
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
        Filter f{.first = static_cast<std::size_t>(std::max(0, left)),
                 .centre_bin = static_cast<std::size_t>(centres[i]),
                 .weights = {}};
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

/// The weighted mean of the magnitudes under each filter, frame-major (frames x bands). The sum
/// runs in bin order from 0.0, as it always has: the Phase 3 activation is pinned bit for bit.
std::vector<double> reduce_to_bands(const Spectrogram& spec, const std::vector<Filter>& bank) {
    const auto frames = static_cast<std::size_t>(spec.n_frames);
    const std::size_t bands = bank.size();
    std::vector<double> data(frames * bands);
    for (std::size_t f = 0; f < frames; ++f) {
        const auto frame = static_cast<std::int64_t>(f);
        for (std::size_t b = 0; b < bands; ++b) {
            double s = 0.0;
            const auto bin_first = static_cast<std::int64_t>(bank[b].first);
            for (std::size_t k = 0; k < bank[b].weights.size(); ++k) {
                s += bank[b].weights[k] *
                     std::abs(spec.at(frame, bin_first + static_cast<std::int64_t>(k)));
            }
            data[f * bands + b] = s;
        }
    }
    return data;
}

}  // namespace

Result<void> validate(const BandSpectrogram& spec) {
    if (spec.n_frames < 0 || spec.n_bands < 0 ||
        spec.centres.size() != static_cast<std::size_t>(spec.n_bands) ||
        spec.data.size() != static_cast<std::size_t>(spec.n_frames * spec.n_bands)) {
        return detail::bad_stage_argument("BandSpectrogram sizes disagree");
    }
    for (std::size_t i = 1; i < spec.centres.size(); ++i) {
        if (!(spec.centres[i].value() > spec.centres[i - 1].value())) {
            return detail::bad_stage_argument(
                "BandSpectrogram centres are not strictly increasing");
        }
    }
    if (!std::ranges::all_of(spec.data, [](double v) { return std::isfinite(v) && v >= 0.0; })) {
        return detail::bad_stage_argument("BandSpectrogram holds a negative or non-finite value");
    }
    return {};
}

/// The level in dB per `mode`, empty for silence (no non-zero sample or no active frame).
Result<BandSpectrogram> band_spectrogram(std::span<const float> mono, Hz rate,
                                         const BandParams& params) {
    if (!detail::positive(rate.value()) || !detail::positive(params.window.value()) ||
        !detail::positive(params.hop.value()) || !detail::positive(params.f_min.value()) ||
        !std::isfinite(params.f_max.value()) || params.f_min.value() >= params.f_max.value() ||
        params.bands_per_octave < 1) {
        return detail::bad_stage_argument("invalid band parameters");
    }
    const int hop_samples =
        std::max(1, static_cast<int>(std::lround(params.hop.value() * rate.value())));
    BandSpectrogram out{.hop = Seconds{static_cast<double>(hop_samples) / rate.value()},
                        .n_frames = 0,
                        .n_bands = 0,
                        .centres = {},
                        .data = {}};
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
    const double bin_hz = rate.value() / n_fft;
    const std::vector<Filter> bank =
        log_filterbank(params, bin_hz, static_cast<int>(spec->n_bins), rate.value());
    out.n_frames = spec->n_frames;
    out.n_bands = static_cast<std::int64_t>(bank.size());
    for (const Filter& f : bank) {
        out.centres.emplace_back(static_cast<double>(f.centre_bin) * bin_hz);
    }
    out.data = reduce_to_bands(*spec, bank);
    return out;
}

}  // namespace anno
