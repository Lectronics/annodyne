#include "core/analysis/envelope/envelope.h"

#include "core/dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <optional>
#include <vector>

namespace anno {

namespace {

constexpr double k_nyquist_margin = 0.45;
constexpr double k_tiny = 1e-30;

bool in_range(double v, double lo, double hi) {
    return v >= lo && v <= hi;
}

Result<void> validate_inputs(const NoteCurves& curves, const HarmonicTrack& h, Hz rate,
                             const EnvelopeParams& p) {
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"estimate_envelope: "} + what);
    };
    if (!std::isfinite(rate.value()) || rate.value() <= 0.0) {
        return bad("the sample rate must be finite and positive");
    }
    if (!in_range(p.order_scale, 0.25, 1.0) || p.max_iterations < 5 || p.max_iterations > 200 ||
        p.n_bins < 65 || p.n_bins > 2049 || !(p.tolerance.value() >= 0.0) ||
        !std::isfinite(p.floor.value())) {
        return bad("invalid parameters");
    }
    if (auto ok = validate(curves.grid); !ok) {
        return ok;
    }
    if (h.freq_hz.rows != curves.grid.n_frames || h.amp_linear.rows != curves.grid.n_frames ||
        h.amp_linear.cols != h.freq_hz.cols) {
        return bad("the harmonic track is not on the note's grid");
    }
    if (!std::ranges::any_of(h.amp_linear.data, [](float a) { return a > 0.0F; })) {
        return bad("the note has no measured harmonic");
    }
    return {};
}

/// Highest f0 of the note, Hz (finite frames only); 0 if none.
double highest_f0(const NoteCurves& c) {
    double top = 0.0;
    for (std::size_t i = 0; i < c.pitch_drift.size() && i < c.pitch_mod.size(); ++i) {
        const double cents =
            c.pitch_center.value() + c.pitch_drift[i].value() + c.pitch_mod[i].value();
        if (std::isfinite(cents)) {
            top = std::max(top, to_hz(Cents{cents}, k_a4_standard_hz).value());
        }
    }
    return top;
}

struct Peak {
    double freq_hz;
    double level_db;
};

std::vector<Peak> frame_peaks(const HarmonicTrack& h, std::size_t frame, double max_hz) {
    std::vector<Peak> peaks;
    const auto cols = static_cast<std::size_t>(h.freq_hz.cols);
    for (std::size_t k = 0; k < cols; ++k) {
        const auto amp = static_cast<double>(h.amp_linear.data[frame * cols + k]);
        const auto f = static_cast<double>(h.freq_hz.data[frame * cols + k]);
        if (amp > 0.0 && f > 0.0 && f <= max_hz) {
            peaks.push_back({.freq_hz = f, .level_db = 20.0 * std::log10(std::max(amp, k_tiny))});
        }
    }
    std::ranges::sort(peaks, {}, &Peak::freq_hz);
    return peaks;
}

/// The peak levels interpolated linearly in frequency onto the grid; flat outside the first and
/// last.
std::vector<double> interpolate_peaks(const std::vector<Peak>& peaks,
                                      const std::vector<Hz>& freqs) {
    std::vector<double> out(freqs.size());
    std::size_t j = 0;
    for (std::size_t b = 0; b < freqs.size(); ++b) {
        const double f = freqs[b].value();
        while (j + 1 < peaks.size() && peaks[j + 1].freq_hz < f) {
            ++j;
        }
        if (f <= peaks.front().freq_hz) {
            out[b] = peaks.front().level_db;
        } else if (f >= peaks.back().freq_hz) {
            out[b] = peaks.back().level_db;
        } else {
            const double w = (f - peaks[j].freq_hz) / (peaks[j + 1].freq_hz - peaks[j].freq_hz);
            out[b] = peaks[j].level_db + w * (peaks[j + 1].level_db - peaks[j].level_db);
        }
    }
    return out;
}

/// Cepstral low-pass of a log spectrum given on 0..Nyquist (n_bins points, n_fft = 2 (n_bins - 1)).
class CepstralSmoother {
public:
    static Result<CepstralSmoother> create(int n_bins, int order) {
        const std::size_t n_bins_u = static_cast<std::size_t>(n_bins);
        const std::size_t n_fft = 2 * (n_bins_u - 1);
        auto fft = RealFft::create(n_fft);
        if (!fft) {
            return Unexpected{fft.error()};
        }
        CepstralSmoother s;
        s.fft_ = *fft;
        s.order_ = static_cast<std::size_t>(std::min(order, n_bins - 2));
        s.cepstrum_.assign(n_fft, 0.0);
        s.bins_.assign(n_bins_u, {});
        return s;
    }

    void smooth(const std::vector<double>& log_spectrum, std::vector<double>& out) {
        for (std::size_t b = 0; b < log_spectrum.size(); ++b) {
            bins_[b] = {log_spectrum[b], 0.0};
        }
        fft_->inverse(bins_, cepstrum_);  // real even spectrum -> real cepstrum (times n_fft)
        const std::size_t n = cepstrum_.size();
        for (std::size_t q = order_ + 1; q + order_ + 1 <= n; ++q) {
            cepstrum_[q] = 0.0;  // keep quefrencies 0..order and their mirror n-order..n-1
        }
        fft_->forward(cepstrum_, bins_);
        const double scale = 1.0 / static_cast<double>(n);
        for (std::size_t b = 0; b < out.size(); ++b) {
            out[b] = bins_[b].real() * scale;
        }
    }

private:
    CepstralSmoother() = default;
    std::optional<RealFft> fft_;
    std::size_t order_ = 0;
    std::vector<double> cepstrum_;
    std::vector<std::complex<double>> bins_;
};

/// True Envelope of one frame: raise the smoothed curve until it lies on the peaks (Röbel & Rodet
/// 2005: A <- max(A, V)), stopping once no peak is more than `tolerance` above the envelope.
std::vector<double> true_envelope(const std::vector<Peak>& peaks, const std::vector<Hz>& freqs,
                                  CepstralSmoother& smoother, const EnvelopeParams& p) {
    std::vector<double> target = interpolate_peaks(peaks, freqs);
    std::vector<double> envelope(target.size());
    for (int it = 0; it < p.max_iterations; ++it) {
        smoother.smooth(target, envelope);
        double worst = 0.0;
        for (std::size_t b = 0; b < target.size(); ++b) {
            worst = std::max(worst, target[b] - envelope[b]);
            target[b] = std::max(target[b], envelope[b]);
        }
        if (worst <= p.tolerance.value()) {
            break;
        }
    }
    return envelope;
}

}  // namespace

Result<EnvelopeTrack> estimate_envelope(const NoteCurves& curves, const HarmonicTrack& harmonics,
                                        Hz sample_rate, const EnvelopeParams& params) {
    if (auto ok = validate_inputs(curves, harmonics, sample_rate, params); !ok) {
        return Unexpected{ok.error()};
    }
    const double rate = sample_rate.value();
    const double top_f0 = std::max(highest_f0(curves), 1.0);
    const int order =
        std::max(2, static_cast<int>(std::floor(params.order_scale * rate / (2.0 * top_f0))));
    auto smoother = CepstralSmoother::create(params.n_bins, order);
    if (!smoother) {
        return Unexpected{smoother.error()};
    }
    const auto n_bins = static_cast<std::size_t>(params.n_bins);
    EnvelopeTrack track;
    track.grid = curves.grid;
    track.freqs.resize(n_bins);
    for (std::size_t b = 0; b < n_bins; ++b) {
        track.freqs[b] = Hz{0.5 * rate * static_cast<double>(b) / static_cast<double>(n_bins - 1)};
    }
    const auto n_frames = static_cast<std::size_t>(curves.grid.n_frames);
    track.log_mag_db = {
        .rows = curves.grid.n_frames,
        .cols = params.n_bins,
        .data = std::vector<float>(n_frames * n_bins, static_cast<float>(params.floor.value()))};
    for (std::size_t i = 0; i < n_frames; ++i) {
        const std::vector<Peak> peaks = frame_peaks(harmonics, i, k_nyquist_margin * rate);
        if (peaks.empty()) {
            continue;
        }
        const std::vector<double> env = true_envelope(peaks, track.freqs, *smoother, params);
        for (std::size_t b = 0; b < n_bins; ++b) {
            track.log_mag_db.data[i * n_bins + b] = static_cast<float>(env[b]);
        }
    }
    return track;
}

}  // namespace anno
