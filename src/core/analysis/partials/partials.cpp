#include "core/analysis/partials/partials.h"

#include "core/dsp/fft.h"
#include "core/dsp/window.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

namespace anno {

namespace {

constexpr double k_nyquist_margin = 0.45;  // harmonics above 0.45 * rate are not measured
constexpr double k_tiny = 1e-30;

bool in_range(double v, double lo, double hi) {
    return v >= lo && v <= hi;
}

Result<void> validate_params(const PartialsParams& p) {
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"analyse_partials: "} + what);
    };
    if (p.max_harmonics < 1 || p.max_harmonics > 200) {
        return bad("max_harmonics must be in 1..200");
    }
    if (!in_range(p.window_periods, 4.0, 8.0)) {
        return bad("window_periods must be in 4..8");
    }
    if (!in_range(p.fft_pad_factor, 4.0, 64.0)) {
        return bad("fft_pad_factor must be in 4..64");
    }
    if (!in_range(p.search_halfwidth.value(), 10.0, 100.0)) {
        return bad("search_halfwidth must be in 10..100 cents");
    }
    if (!std::isfinite(p.a4_reference.value()) || p.a4_reference.value() <= 0.0) {
        return bad("a4_reference must be finite and positive");
    }
    return {};
}

Result<void> validate_inputs(std::span<const float> mono, Hz rate, const NoteCurves& curves,
                             const PartialsParams& p) {
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"analyse_partials: "} + what);
    };
    if (mono.empty()) {
        return bad("empty signal");
    }
    if (!std::isfinite(rate.value()) || rate.value() <= 0.0) {
        return bad("the sample rate must be finite and positive");
    }
    if (auto ok = validate_params(p); !ok) {
        return ok;
    }
    if (const auto g = validate(curves.grid); !g) {
        return Unexpected{g.error()};
    }
    const auto n = static_cast<std::size_t>(curves.grid.n_frames);
    if (curves.pitch_drift.size() != n || curves.pitch_mod.size() != n) {
        return bad("the drift and mod curves must be grid.n_frames long");
    }
    if (!std::isfinite(curves.pitch_center.value())) {
        return bad("pitch_center must be finite");
    }
    return {};
}

/// f0 in Hz of frame i, or NaN when the frame is unvoiced.
double frame_f0_hz(const NoteCurves& c, std::size_t i, Hz a4) {
    const double cents = c.pitch_center.value() + c.pitch_drift[i].value() + c.pitch_mod[i].value();
    if (!std::isfinite(cents)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double hz = to_hz(Cents{cents}, a4).value();
    return hz > 0.0 ? hz : std::numeric_limits<double>::quiet_NaN();
}

/// The Blackman-Harris spectrum of one frame, zero-phase (window centre at buffer index 0, so a
/// bin's phase is the cosine phase at the frame centre).
struct FrameSpectrum {
    std::vector<std::complex<double>> bins;
    std::vector<double> mag;
    double window_sum = 0.0;
    double bin_hz = 0.0;
    std::vector<double> windowed;  // w[j] * x[centre - half + j], j = 0..win_len-1
    double rate_hz = 0.0;
};

/// False when the window misses the signal entirely.
Result<bool> frame_spectrum(std::span<const float> mono, double rate, std::int64_t centre,
                            double f0, const PartialsParams& params, FrameSpectrum& out) {
    int win_len = std::max(16, static_cast<int>(std::lround(params.window_periods * rate / f0)));
    win_len += win_len % 2;
    const auto n_signal = static_cast<std::int64_t>(mono.size());
    const std::int64_t first = centre - win_len / 2;
    if (first + win_len <= 0 || first >= n_signal) {
        return false;
    }
    auto n_fft = std::max(static_cast<std::size_t>(win_len),
                          static_cast<std::size_t>(std::lround(params.fft_pad_factor * win_len)));
    n_fft += n_fft % 2;
    auto fft = RealFft::create(n_fft);
    if (!fft) {
        return Unexpected{fft.error()};
    }
    const std::vector<double> window = make_window(WindowKind::blackman_harris, win_len);
    out.window_sum = 0.0;
    for (const double w : window) {
        out.window_sum += w;
    }
    std::vector<double> buffer(n_fft, 0.0);
    out.windowed.assign(static_cast<std::size_t>(win_len), 0.0);
    out.rate_hz = rate;
    const std::int64_t half = win_len / 2;
    for (std::int64_t j = -half; j < half; ++j) {
        const std::int64_t s = centre + j;
        if (s < 0 || s >= n_signal) {
            continue;
        }
        const auto n_fft_signed = static_cast<std::int64_t>(n_fft);
        const auto idx = static_cast<std::size_t>((j + n_fft_signed) % n_fft_signed);
        buffer[idx] = window[static_cast<std::size_t>(j + half)] *
                      static_cast<double>(mono[static_cast<std::size_t>(s)]);
        out.windowed[static_cast<std::size_t>(j + half)] = buffer[idx];
    }
    out.bins.assign(n_fft / 2 + 1, {});
    fft->forward(buffer, out.bins);
    out.mag.resize(out.bins.size());
    for (std::size_t b = 0; b < out.bins.size(); ++b) {
        out.mag[b] = std::abs(out.bins[b]);
    }
    out.bin_hz = rate / static_cast<double>(n_fft);
    return true;
}

struct Peak {
    double freq_hz = 0.0;
    double amp = 0.0;
    double phase = 0.0;
    bool found = false;
};

/// sum_j windowed[j] * exp(-i 2 pi f (j - half) / rate): the frame's complex amplitude at f, phase
/// relative to the frame centre.
std::complex<double> project(const FrameSpectrum& sp, double freq_hz) {
    const double w = 2.0 * std::numbers::pi * freq_hz / sp.rate_hz;
    const double half = 0.5 * static_cast<double>(sp.windowed.size());
    std::complex<double> sum{};
    for (std::size_t j = 0; j < sp.windowed.size(); ++j) {
        const double a = w * (static_cast<double>(j) - half);
        sum += sp.windowed[j] * std::complex<double>{std::cos(a), -std::sin(a)};
    }
    return sum;
}

/// Largest peak within the sieve window around `nominal`, refined by QIFFT on the log magnitude.
Peak find_peak(const FrameSpectrum& sp, double nominal, double halfwidth) {
    const auto last_bin = static_cast<std::int64_t>(sp.bins.size()) - 2;
    const auto centre_bin = static_cast<std::int64_t>(std::llround(nominal / sp.bin_hz));
    auto lo = static_cast<std::int64_t>(std::floor(nominal / halfwidth / sp.bin_hz));
    auto hi = static_cast<std::int64_t>(std::ceil(nominal * halfwidth / sp.bin_hz));
    lo = std::max<std::int64_t>(std::min(lo, centre_bin - 1), 1);
    hi = std::min(std::max(hi, centre_bin + 1), last_bin);
    if (lo > hi) {
        return {};
    }
    std::int64_t best = lo;
    for (std::int64_t b = lo; b <= hi; ++b) {
        if (sp.mag[static_cast<std::size_t>(b)] > sp.mag[static_cast<std::size_t>(best)]) {
            best = b;
        }
    }
    const auto bi = static_cast<std::size_t>(best);
    if (sp.mag[bi] <= k_tiny) {
        return {};
    }
    const double a = std::log(std::max(sp.mag[bi - 1], k_tiny));
    const double b = std::log(sp.mag[bi]);
    const double c = std::log(std::max(sp.mag[bi + 1], k_tiny));
    const double denom = a - 2.0 * b + c;
    const double delta = denom < -k_tiny ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
    const double freq = (static_cast<double>(best) + delta) * sp.bin_hz;
    // QIFFT locates the peak; amplitude and phase come from projecting the windowed frame on that
    // frequency (a one-bin DFT at the refined frequency), which has no interpolation bias.
    const std::complex<double> projected = project(sp, freq);
    return {.freq_hz = freq,
            .amp = 2.0 * std::abs(projected) / sp.window_sum,
            .phase = std::arg(projected),
            .found = true};
}

}  // namespace

Result<HarmonicTrack> analyse_partials(std::span<const float> mono, Hz sample_rate,
                                       const NoteCurves& curves, const PartialsParams& params) {
    if (auto ok = validate_inputs(mono, sample_rate, curves, params); !ok) {
        return Unexpected{ok.error()};
    }
    const double rate = sample_rate.value();
    const auto n_frames = static_cast<std::size_t>(curves.grid.n_frames);

    // The lowest voiced f0 fixes the number of columns (a fixed width for the whole note).
    double lowest = std::numeric_limits<double>::infinity();
    std::vector<double> f0(n_frames);
    for (std::size_t i = 0; i < n_frames; ++i) {
        f0[i] = frame_f0_hz(curves, i, params.a4_reference);
        if (std::isfinite(f0[i])) {
            lowest = std::min(lowest, f0[i]);
        }
    }
    if (!std::isfinite(lowest)) {
        return fail(ErrorCode::invalid_argument, "analyse_partials: the note has no voiced frame");
    }
    const auto k_count = static_cast<std::size_t>(
        std::max(1, std::min(params.max_harmonics,
                             static_cast<int>(std::floor(k_nyquist_margin * rate / lowest)))));

    HarmonicTrack track;
    track.grid = curves.grid;
    for (FrameMatrix* m : {&track.freq_hz, &track.amp_linear, &track.phase_rad}) {
        m->rows = curves.grid.n_frames;
        m->cols = static_cast<std::int64_t>(k_count);
        m->data.assign(n_frames * k_count, 0.0F);
    }

    const double halfwidth = std::exp2(params.search_halfwidth.value() / 1200.0);
    FrameSpectrum spectrum;
    for (std::size_t i = 0; i < n_frames; ++i) {
        const bool voiced = std::isfinite(f0[i]);
        const double f = voiced ? f0[i] : lowest;
        float* freq_row = &track.freq_hz.data[i * k_count];
        for (std::size_t k = 0; k < k_count; ++k) {  // nominal frequencies keep freq_hz meaningful
            freq_row[k] = static_cast<float>(static_cast<double>(k + 1) * f);
        }
        if (!voiced) {
            continue;
        }
        const double t =
            (frame_time(curves.grid, static_cast<std::int64_t>(i)) - SourceTime{}).value();
        auto ok = frame_spectrum(mono, rate, static_cast<std::int64_t>(std::llround(t * rate)), f,
                                 params, spectrum);
        if (!ok) {
            return Unexpected{ok.error()};
        }
        if (!*ok) {
            continue;
        }
        for (std::size_t k = 0; k < k_count; ++k) {
            const double nominal = static_cast<double>(k + 1) * f;
            if (nominal > k_nyquist_margin * rate) {
                break;
            }
            const Peak peak = find_peak(spectrum, nominal, halfwidth);
            if (peak.found) {
                const std::size_t flat = i * k_count + k;
                track.freq_hz.data[flat] = static_cast<float>(peak.freq_hz);
                track.amp_linear.data[flat] = static_cast<float>(peak.amp);
                track.phase_rad.data[flat] = static_cast<float>(peak.phase);
            }
        }
    }
    return track;
}

}  // namespace anno
