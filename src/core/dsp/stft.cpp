#include "core/dsp/stft.h"

#include "core/dsp/fft.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_min_window_power = 1e-3;

std::int64_t frames_for(std::int64_t length, int hop) {
    return length / hop + 1;
}

}  // namespace

Result<void> validate(const Spectrogram& spec) {
    if (spec.n_fft < 2 || spec.hop_samples < 1 || spec.n_frames < 0 ||
        spec.n_bins != spec.n_fft / 2 + 1 ||
        static_cast<std::int64_t>(spec.data.size()) != spec.n_frames * spec.n_bins) {
        return fail(ErrorCode::invalid_argument, "spectrogram shape is inconsistent");
    }
    return {};
}

Result<Spectrogram> stft(std::span<const float> mono, Hz sample_rate, const StftParams& params) {
    if (params.n_fft < 2 || params.n_fft % 2 != 0 || params.hop_samples < 1 ||
        params.hop_samples > params.n_fft) {
        return fail(ErrorCode::invalid_argument,
                    "stft: n_fft must be even and >= 2, 0 < hop <= n_fft");
    }
    if (!std::isfinite(sample_rate.value()) || sample_rate.value() <= 0.0) {
        return fail(ErrorCode::invalid_argument, "stft: sample rate must be finite and > 0");
    }
    if (mono.empty()) {
        return fail(ErrorCode::invalid_argument, "stft: empty signal");
    }
    auto fft = RealFft::create(static_cast<std::size_t>(params.n_fft));
    if (!fft) {
        return Unexpected{fft.error()};
    }
    const auto n = static_cast<std::int64_t>(mono.size());
    const std::vector<double> window = make_window(params.window, params.n_fft);
    Spectrogram spec{.n_fft = params.n_fft,
                     .hop_samples = params.hop_samples,
                     .window = params.window,
                     .sample_rate = sample_rate,
                     .n_frames = frames_for(n, params.hop_samples),
                     .n_bins = params.n_fft / 2 + 1,
                     .data = {}};
    spec.data.resize(static_cast<std::size_t>(spec.n_frames * spec.n_bins));
    std::vector<double> frame(static_cast<std::size_t>(params.n_fft));
    std::vector<std::complex<double>> bins(static_cast<std::size_t>(spec.n_bins));
    const std::int64_t half = params.n_fft / 2;
    for (std::int64_t f = 0; f < spec.n_frames; ++f) {
        const std::int64_t first = f * params.hop_samples - half;
        for (std::int64_t k = 0; k < params.n_fft; ++k) {
            const std::int64_t i = first + k;
            const double v =
                (i >= 0 && i < n) ? static_cast<double>(mono[static_cast<std::size_t>(i)]) : 0.0;
            frame[static_cast<std::size_t>(k)] = v * window[static_cast<std::size_t>(k)];
        }
        fft->forward(frame, bins);
        for (std::int64_t b = 0; b < spec.n_bins; ++b) {
            const auto& c = bins[static_cast<std::size_t>(b)];
            spec.data[static_cast<std::size_t>(f * spec.n_bins + b)] = {
                static_cast<float>(c.real()), static_cast<float>(c.imag())};
        }
    }
    return spec;
}

Result<std::vector<float>> istft(const Spectrogram& spec, std::int64_t length) {
    if (auto r = validate(spec); !r) {
        return Unexpected{r.error()};
    }
    if (length < 0 || frames_for(length, spec.hop_samples) > spec.n_frames) {
        return fail(ErrorCode::invalid_argument, "istft: length does not fit the frame count");
    }
    auto fft = RealFft::create(static_cast<std::size_t>(spec.n_fft));
    if (!fft) {
        return Unexpected{fft.error()};
    }
    const std::vector<double> window = make_window(spec.window, spec.n_fft);
    std::vector<double> sum(static_cast<std::size_t>(length), 0.0);
    std::vector<double> power(static_cast<std::size_t>(length), 0.0);
    std::vector<std::complex<double>> bins(static_cast<std::size_t>(spec.n_bins));
    std::vector<double> frame(static_cast<std::size_t>(spec.n_fft));
    const std::int64_t half = spec.n_fft / 2;
    for (std::int64_t f = 0; f < spec.n_frames; ++f) {
        for (std::int64_t b = 0; b < spec.n_bins; ++b) {
            const auto c = spec.at(f, b);
            bins[static_cast<std::size_t>(b)] = {static_cast<double>(c.real()),
                                                 static_cast<double>(c.imag())};
        }
        fft->inverse(bins, frame);
        const std::int64_t first = f * spec.hop_samples - half;
        for (std::int64_t k = 0; k < spec.n_fft; ++k) {
            const std::int64_t i = first + k;
            if (i < 0 || i >= length) {
                continue;
            }
            const double w = window[static_cast<std::size_t>(k)];
            sum[static_cast<std::size_t>(i)] += frame[static_cast<std::size_t>(k)] / spec.n_fft * w;
            power[static_cast<std::size_t>(i)] += w * w;
        }
    }
    std::vector<float> out(static_cast<std::size_t>(length), 0.0F);
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (power[i] > k_min_window_power) {
            out[i] = static_cast<float>(sum[i] / power[i]);
        }
    }
    return out;
}

}  // namespace anno
