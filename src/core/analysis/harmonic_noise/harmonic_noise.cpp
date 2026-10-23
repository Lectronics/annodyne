#include "core/analysis/harmonic_noise/harmonic_noise.h"

#include "core/dsp/fft.h"
#include "core/dsp/window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <optional>

namespace anno {

namespace {

constexpr double k_tiny = 1e-20;
constexpr double k_band_max_hz = 20000.0;
constexpr double k_nyquist_margin = 0.45;
constexpr double k_hnr_min_db = -40.0;
constexpr double k_hnr_max_db = 60.0;
constexpr double k_spectral_window_s = 0.040;  // centroid, HNR, band power (research: 20-50 ms)
constexpr double k_mask_bins = 2.5;            // Hann main lobe half-width is 2 bins
constexpr double k_flux_window_s = 0.010;      // flux, HF share, burst level (research: 5-10 ms)
constexpr double k_dip_far_s = 0.040;          // closure dip search: 40 ms ...
constexpr double k_dip_near_s = 0.010;         // ... to 10 ms before the burst frame

bool in_range(double v, double lo, double hi) {
    return v >= lo && v <= hi;
}

Result<void> validate_params(const HarmonicNoiseParams& p) {
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"harmonic_noise: "} + what);
    };
    if (p.n_bands < 8 || p.n_bands > 64) {
        return bad("n_bands must be in 8..64");
    }
    if (p.median_frames < 1 || p.median_frames > 9 || p.median_frames % 2 == 0) {
        return bad("median_frames must be odd and in 1..9");
    }
    if (!in_range(p.octave_ratio_min, 1.0, p.octave_ratio_max)) {
        return bad("octave_ratio_min must be in 1..octave_ratio_max");
    }
    if (!in_range(p.aperiodicity_threshold, 0.0, 1.0) || !in_range(p.hf_rise, 0.0, 1.0)) {
        return bad("aperiodicity_threshold and hf_rise must be in 0..1");
    }
    const bool positive = p.sibilant_centroid.value() > 0.0 && p.hf_edge.value() > 0.0 &&
                          p.breathy_mvf.value() > 0.0 && p.burst_min_gap.value() > 0.0 &&
                          p.burst_duration.value() > 0.0 && p.min_segment.value() >= 0.0 &&
                          p.flux_mad_factor >= 0.0 && p.flux_floor >= 0.0;
    if (!positive || !std::isfinite(p.silence_level.value()) ||
        !std::isfinite(p.closure_dip.value()) || !std::isfinite(p.breathy_hnr.value()) ||
        !std::isfinite(p.band_significance.value())) {
        return bad("invalid threshold");
    }
    return {};
}

Result<void> validate_inputs(std::span<const float> mono, Hz rate, const NoteCurves& curves,
                             const HarmonicTrack& harmonics, const PitchEvidence& evidence,
                             const HarmonicNoiseParams& p) {
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"harmonic_noise: "} + what);
    };
    if (mono.empty()) {
        return bad("empty signal");
    }
    if (!std::isfinite(rate.value()) || rate.value() < 8000.0) {
        return bad("the sample rate must be finite and at least 8 kHz");
    }
    if (auto ok = validate_params(p); !ok) {
        return ok;
    }
    if (auto ok = validate(curves.grid); !ok) {
        return ok;
    }
    const auto n = static_cast<std::size_t>(curves.grid.n_frames);
    if (harmonics.freq_hz.rows != curves.grid.n_frames ||
        harmonics.amp_linear.rows != curves.grid.n_frames ||
        harmonics.amp_linear.cols != harmonics.freq_hz.cols) {
        return bad("the harmonic track is not on the note's grid");
    }
    if (evidence.voicing_prob.size() != n ||
        (!evidence.alternate_f0.empty() && evidence.alternate_f0.size() != n)) {
        return bad("the pitch evidence is not on the note's grid");
    }
    const auto outside = [](double v) { return !in_range(v, 0.0, 1.0); };
    if (std::ranges::any_of(evidence.voicing_prob, outside)) {
        return bad("voicing_prob must lie in 0..1");
    }
    return {};
}

double to_db(double mean_square) {
    return 10.0 * std::log10(std::max(mean_square, k_tiny));
}

/// Mel-spaced band edges 0 .. min(0.45 rate, 20 kHz); n_bands + 1 values.
std::vector<double> band_edges(int n_bands, double rate) {
    const auto mel = [](double f) { return 2595.0 * std::log10(1.0 + f / 700.0); };
    const auto inv = [](double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); };
    const double top = mel(std::min(k_nyquist_margin * rate, k_band_max_hz));
    std::vector<double> edges(static_cast<std::size_t>(n_bands) + 1);
    for (std::size_t b = 0; b < edges.size(); ++b) {
        edges[b] = inv(top * static_cast<double>(b) / static_cast<double>(n_bands));
    }
    return edges;
}

std::size_t band_of(const std::vector<double>& edges, double f) {
    const auto it = std::upper_bound(edges.begin() + 1, edges.end() - 1, f);
    return static_cast<std::size_t>(it - (edges.begin() + 1));
}

/// A windowed spectrum of one frame. Hann window; `mag` and `power` are one-sided. `power[b]` is
/// the bin's share of the window-weighted mean square (Parseval), so a tone of amplitude A has
/// total A^2/2.
class FrameAnalyser {
public:
    static Result<FrameAnalyser> create(double rate, double window_s) {
        FrameAnalyser a;
        a.rate_ = rate;
        a.win_len_ = std::max(16, static_cast<int>(std::lround(window_s * rate)));
        a.win_len_ += a.win_len_ % 2;
        std::size_t n_fft = 1;
        while (n_fft < static_cast<std::size_t>(a.win_len_) * 2) {
            n_fft *= 2;
        }
        auto fft = RealFft::create(n_fft);
        if (!fft) {
            return Unexpected{fft.error()};
        }
        a.fft_ = *fft;
        a.window_ = make_window(WindowKind::hann_periodic, a.win_len_);
        a.window_power_ =
            std::inner_product(a.window_.begin(), a.window_.end(), a.window_.begin(), 0.0);
        a.buffer_.assign(n_fft, 0.0);
        a.bins_.assign(n_fft / 2 + 1, {});
        a.mag_.assign(a.bins_.size(), 0.0);
        a.power_.assign(a.bins_.size(), 0.0);
        return a;
    }

    /// Spectrum of the window centred on sample `centre` (samples outside the signal are zero).
    void analyse(std::span<const float> mono, std::int64_t centre) {
        std::ranges::fill(buffer_, 0.0);
        const auto n_signal = static_cast<std::int64_t>(mono.size());
        for (int j = 0; j < win_len_; ++j) {
            const std::int64_t s = centre - win_len_ / 2 + j;
            if (s >= 0 && s < n_signal) {
                buffer_[static_cast<std::size_t>(j)] =
                    window_[static_cast<std::size_t>(j)] *
                    static_cast<double>(mono[static_cast<std::size_t>(s)]);
            }
        }
        fft_->forward(buffer_, bins_);
        const double norm =
            1.0 / (static_cast<double>(buffer_.size()) * std::max(window_power_, k_tiny));
        for (std::size_t b = 0; b < bins_.size(); ++b) {
            const double p = std::norm(bins_[b]);
            mag_[b] = std::sqrt(p);
            const bool edge = b == 0 || b + 1 == bins_.size();
            power_[b] = (edge ? 1.0 : 2.0) * p * norm;
        }
    }

    [[nodiscard]] double bin_hz() const { return rate_ / static_cast<double>(buffer_.size()); }
    /// Frequency resolution of the window itself (one window bin), Hz.
    [[nodiscard]] double resolution_hz() const { return rate_ / static_cast<double>(win_len_); }
    [[nodiscard]] const std::vector<double>& mag() const { return mag_; }
    [[nodiscard]] const std::vector<double>& power() const { return power_; }

private:
    std::vector<double> mag_;
    std::vector<double> power_;
    FrameAnalyser() = default;
    double rate_ = 0.0;
    int win_len_ = 0;
    double window_power_ = 0.0;
    std::optional<RealFft> fft_;
    std::vector<double> window_;
    std::vector<double> buffer_;
    std::vector<std::complex<double>> bins_;
};

double zero_crossing_rate(std::span<const float> mono, std::int64_t centre, std::int64_t half) {
    const auto n_signal = static_cast<std::int64_t>(mono.size());
    const std::int64_t lo = std::max<std::int64_t>(centre - half, 1);
    const std::int64_t hi = std::min(centre + half, n_signal - 1);
    if (hi <= lo) {
        return 0.0;
    }
    int crossings = 0;
    for (std::int64_t s = lo; s <= hi; ++s) {
        const float a = mono[static_cast<std::size_t>(s - 1)];
        const float b = mono[static_cast<std::size_t>(s)];
        if ((a < 0.0F && b > 0.0F) || (a > 0.0F && b < 0.0F)) {
            ++crossings;
        }
    }
    return static_cast<double>(crossings) / static_cast<double>(hi - lo + 1);
}

double centroid_of(const FrameAnalyser& a) {
    double num = 0.0;
    double den = 0.0;
    for (std::size_t b = 0; b < a.mag().size(); ++b) {
        num += static_cast<double>(b) * a.bin_hz() * a.mag()[b];
        den += a.mag()[b];
    }
    return den > k_tiny ? num / den : 0.0;
}

void band_power_all(const FrameAnalyser& a, const std::vector<double>& edges,
                    std::vector<double>& out) {
    std::ranges::fill(out, 0.0);
    for (std::size_t b = 0; b < a.power().size(); ++b) {
        const double f = static_cast<double>(b) * a.bin_hz();
        if (f >= edges.front() && f < edges.back()) {
            out[band_of(edges, f)] += a.power()[b];
        }
    }
}

/// Bins within k_mask_bins window bins of a measured harmonic of `frame`.
std::vector<char> harmonic_mask(const FrameAnalyser& a, const HarmonicTrack& h, std::size_t frame) {
    std::vector<char> masked(a.power().size(), 0);
    const auto cols = static_cast<std::size_t>(h.freq_hz.cols);
    const double half = k_mask_bins * a.resolution_hz();
    for (std::size_t k = 0; k < cols; ++k) {
        if (h.amp_linear.data[frame * cols + k] <= 0.0F) {
            continue;
        }
        const auto f = static_cast<double>(h.freq_hz.data[frame * cols + k]);
        const auto lo =
            static_cast<std::size_t>(std::max(0.0, std::floor((f - half) / a.bin_hz())));
        const auto hi = static_cast<std::size_t>(std::max(0.0, std::ceil((f + half) / a.bin_hz())));
        for (std::size_t b = lo; b <= hi && b < masked.size(); ++b) {
            masked[b] = 1;
        }
    }
    return masked;
}

/// Noise density per band: the mean power of its unmasked bins; a band with none takes the nearest
/// band's density (0 when no band has any).
std::vector<double> nearest_density(const std::vector<double>& free_power,
                                    const std::vector<double>& free_count) {
    const std::size_t n = free_power.size();
    std::vector<double> own(n, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t b = 0; b < n; ++b) {
        if (free_count[b] > 0.0) {
            own[b] = free_power[b] / free_count[b];
        }
    }
    std::vector<double> out(n, 0.0);
    for (std::size_t b = 0; b < n; ++b) {
        for (std::size_t dist = 0; dist < n; ++dist) {
            if (b >= dist && !std::isnan(own[b - dist])) {
                out[b] = own[b - dist];
                break;
            }
            if (b + dist < n && !std::isnan(own[b + dist])) {
                out[b] = own[b + dist];
                break;
            }
        }
    }
    return out;
}

/// Noise power per band of one voiced frame. Bins within k_mask_bins window bins of a measured
/// harmonic count as harmonic; the noise density of a band is the mean power of its unmasked bins,
/// extended over all its bins (a band with no unmasked bin takes the nearest band's density).
/// Returns the total power per band in `total`.
void noise_band_power(const FrameAnalyser& a, const HarmonicTrack& h, std::size_t frame,
                      const std::vector<double>& edges, std::vector<double>& total,
                      std::vector<double>& noise) {
    const std::size_t n_bands = total.size();
    const std::vector<char> masked = harmonic_mask(a, h, frame);
    std::vector<double> free_power(n_bands, 0.0);
    std::vector<double> free_count(n_bands, 0.0);
    std::vector<double> count(n_bands, 0.0);
    std::ranges::fill(total, 0.0);
    for (std::size_t b = 0; b < a.power().size(); ++b) {
        const double f = static_cast<double>(b) * a.bin_hz();
        if (f < edges.front() || f >= edges.back()) {
            continue;
        }
        const std::size_t band = band_of(edges, f);
        total[band] += a.power()[b];
        count[band] += 1.0;
        if (masked[b] == 0) {
            free_power[band] += a.power()[b];
            free_count[band] += 1.0;
        }
    }
    const std::vector<double> density = nearest_density(free_power, free_count);
    for (std::size_t b = 0; b < n_bands; ++b) {
        noise[b] = std::min(density[b] * count[b], total[b]);
    }
}

/// Harmonic-to-noise ratio over the bands above `lowest_hz` (rumble under the voice is neither).
double harmonic_to_noise_db(const std::vector<double>& edges, const std::vector<double>& total,
                            const std::vector<double>& noise, double lowest_hz) {
    double harm = 0.0;
    double nz = 0.0;
    for (std::size_t b = 0; b < total.size(); ++b) {
        if (edges[b + 1] > lowest_hz) {
            harm += total[b] - noise[b];
            nz += noise[b];
        }
    }
    return std::clamp(10.0 * std::log10(std::max(harm, k_tiny) / std::max(nz, k_tiny)),
                      k_hnr_min_db, k_hnr_max_db);
}

/// Maximum voiced frequency: the lower edge of the first significant aperiodic band above
/// `lowest_hz` (0.75 of the first harmonic); the top of the analysed range when every significant
/// band is periodic.
double max_voiced_frequency(const std::vector<double>& edges, const std::vector<double>& total,
                            const std::vector<double>& noise, double lowest_hz,
                            const HarmonicNoiseParams& p) {
    const double frame_power = std::accumulate(total.begin(), total.end(), 0.0);
    const double floor_power = frame_power * std::pow(10.0, -p.band_significance.value() / 10.0);
    for (std::size_t b = 0; b < total.size(); ++b) {
        if (edges[b + 1] <= lowest_hz) {
            continue;  // below the voice (rumble, DC): not part of the voiced range
        }
        if (total[b] > floor_power && noise[b] / total[b] > p.aperiodicity_threshold) {
            return std::max(edges[b], lowest_hz);
        }
    }
    return edges.back();
}

/// Confirmed plosive bursts (research: positive flux peak above median + k MAD, 30 ms apart, a
/// closure dip before it and a high-frequency rise). `rms` is the 10 ms frame RMS (linear).
std::vector<std::uint8_t> detect_bursts(const std::vector<double>& flux,
                                        const std::vector<double>& hf,
                                        const std::vector<double>& rms, double hop_s,
                                        const HarmonicNoiseParams& p) {
    const std::size_t n = flux.size();
    std::vector<std::uint8_t> burst(n, 0);
    if (n < 3) {
        return burst;
    }
    auto sorted = flux;
    std::ranges::sort(sorted);
    const double median = sorted[n / 2];
    std::vector<double> dev(n);
    for (std::size_t i = 0; i < n; ++i) {
        dev[i] = std::abs(flux[i] - median);
    }
    std::ranges::sort(dev);
    const double threshold = std::max(median + p.flux_mad_factor * dev[n / 2], p.flux_floor);
    std::vector<std::size_t> peaks;
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (flux[i] > threshold && flux[i] >= flux[i - 1] && flux[i] > flux[i + 1]) {
            peaks.push_back(i);
        }
    }
    std::ranges::sort(peaks, [&](std::size_t a, std::size_t b) { return flux[a] > flux[b]; });
    const auto gap = static_cast<std::size_t>(std::lround(p.burst_min_gap.value() / hop_s));
    const auto length =
        static_cast<std::size_t>(std::max(1L, std::lround(p.burst_duration.value() / hop_s)));
    const auto far = static_cast<std::size_t>(std::lround(k_dip_far_s / hop_s));
    const auto near = static_cast<std::size_t>(std::lround(k_dip_near_s / hop_s));
    std::vector<std::size_t> accepted;
    for (const std::size_t i : peaks) {
        const bool crowded = std::ranges::any_of(
            accepted, [&](std::size_t a) { return (a > i ? a - i : i - a) < gap; });
        if (crowded || i < near + 1) {
            continue;
        }
        const std::size_t from = i > far ? i - far : 0;
        const std::size_t to = i - near;
        const double quiet = *std::min_element(rms.begin() + static_cast<std::ptrdiff_t>(from),
                                               rms.begin() + static_cast<std::ptrdiff_t>(to) + 1);
        const double loud =
            *std::max_element(rms.begin() + static_cast<std::ptrdiff_t>(i),
                              rms.begin() + static_cast<std::ptrdiff_t>(std::min(i + 3, n)));
        const double hf_before =
            *std::min_element(hf.begin() + static_cast<std::ptrdiff_t>(from),
                              hf.begin() + static_cast<std::ptrdiff_t>(to) + 1);
        const double hf_peak = std::max(hf[i], hf[std::min(i + 1, n - 1)]);
        const bool dip = 20.0 * std::log10(std::max(loud, k_tiny) / std::max(quiet, k_tiny)) >=
                         p.closure_dip.value();
        if (dip && hf_peak - hf_before >= p.hf_rise) {
            accepted.push_back(i);
            for (std::size_t k = (i > 0 ? i - 1 : 0); k < std::min(i - 1 + length, n); ++k) {
                burst[k] = 1;
            }
        }
    }
    return burst;
}

std::uint8_t octave_flagged(double f0_cents, double alt_cents, const HarmonicNoiseParams& p) {
    if (!std::isfinite(f0_cents) || !std::isfinite(alt_cents)) {
        return 0;
    }
    const double ratio = std::exp2(std::abs(alt_cents - f0_cents) / 1200.0);
    return ratio >= p.octave_ratio_min && ratio <= p.octave_ratio_max ? 1 : 0;
}

}  // namespace

Result<FrameMetrics> measure_frames(std::span<const float> mono, Hz sample_rate,
                                    const NoteCurves& curves, const HarmonicTrack& harmonics,
                                    const PitchEvidence& evidence,
                                    const HarmonicNoiseParams& params) {
    if (auto ok = validate_inputs(mono, sample_rate, curves, harmonics, evidence, params); !ok) {
        return Unexpected{ok.error()};
    }
    const double rate = sample_rate.value();
    const auto n = static_cast<std::size_t>(curves.grid.n_frames);
    const auto n_bands = static_cast<std::size_t>(params.n_bands);
    const std::vector<double> edges = band_edges(params.n_bands, rate);
    auto wide = FrameAnalyser::create(rate, k_spectral_window_s);
    auto fast = FrameAnalyser::create(rate, k_flux_window_s);
    if (!wide || !fast) {
        return Unexpected{wide ? fast.error() : wide.error()};
    }

    FrameMetrics m;
    for (auto* v : {&m.level_db, &m.centroid_hz, &m.zero_crossing_rate, &m.hnr_db, &m.mvf_hz,
                    &m.flux, &m.hf_share}) {
        v->assign(n, 0.0);
    }
    m.burst.assign(n, 0);
    m.octave_flag.assign(n, 0);
    m.noise_band_power = {.rows = curves.grid.n_frames,
                          .cols = params.n_bands,
                          .data = std::vector<float>(n * n_bands, 0.0F)};
    std::vector<double> rms_fast(n, 0.0);
    std::vector<double> total(n_bands);
    std::vector<double> noise(n_bands);
    std::vector<double> prev_mag;
    const auto hop_samples = static_cast<std::int64_t>(std::lround(curves.grid.hop.value() * rate));
    const auto zcr_half = static_cast<std::int64_t>(std::lround(0.005 * rate));
    const double hf_bin = params.hf_edge.value() / fast->bin_hz();

    for (std::size_t i = 0; i < n; ++i) {
        const double t =
            (frame_time(curves.grid, static_cast<std::int64_t>(i)) - SourceTime{}).value();
        const auto centre = static_cast<std::int64_t>(std::llround(t * rate));
        if (i == 0) {
            fast->analyse(mono, centre - hop_samples);
            prev_mag = fast->mag();
        }
        wide->analyse(mono, centre);
        m.level_db[i] = to_db(std::accumulate(wide->power().begin(), wide->power().end(), 0.0));
        m.centroid_hz[i] = centroid_of(*wide);
        m.zero_crossing_rate[i] = zero_crossing_rate(mono, centre, zcr_half);

        const bool voiced = evidence.voicing_prob[i] >= k_voiced_probability;
        if (voiced) {
            noise_band_power(*wide, harmonics, i, edges, total, noise);
            const auto first_harmonic = static_cast<double>(
                harmonics.freq_hz.data[i * static_cast<std::size_t>(harmonics.freq_hz.cols)]);
            const double lowest_hz = 0.75 * first_harmonic;
            m.hnr_db[i] = harmonic_to_noise_db(edges, total, noise, lowest_hz);
            m.mvf_hz[i] = max_voiced_frequency(edges, total, noise, lowest_hz, params);
        } else {
            m.hnr_db[i] = k_hnr_min_db;
            band_power_all(*wide, edges, total);
            noise = total;
        }
        for (std::size_t b = 0; b < n_bands; ++b) {
            m.noise_band_power.data[i * n_bands + b] = static_cast<float>(noise[b]);
        }

        fast->analyse(mono, centre);
        double sum_mag = 0.0;
        double rise = 0.0;
        double hf_power = 0.0;
        double all_power = 0.0;
        for (std::size_t b = 0; b < fast->mag().size(); ++b) {
            sum_mag += fast->mag()[b];
            rise += std::max(0.0, fast->mag()[b] - prev_mag[b]);
            all_power += fast->power()[b];
            if (static_cast<double>(b) >= hf_bin) {
                hf_power += fast->power()[b];
            }
        }
        m.flux[i] = rise / std::max(sum_mag, k_tiny);
        m.hf_share[i] = hf_power / std::max(all_power, k_tiny);
        rms_fast[i] = std::sqrt(all_power);
        prev_mag = fast->mag();

        const double alt = evidence.alternate_f0.empty() ? std::numeric_limits<double>::quiet_NaN()
                                                         : evidence.alternate_f0[i].value();
        const double f0 = curves.pitch_center.value() + curves.pitch_drift[i].value() +
                          curves.pitch_mod[i].value();
        m.octave_flag[i] = voiced ? octave_flagged(f0, alt, params) : 0;
    }
    m.burst = detect_bursts(m.flux, m.hf_share, rms_fast, curves.grid.hop.value(), params);
    return m;
}

namespace {

FrameClass first_match(const FrameMetrics& m, std::size_t i, bool voiced,
                       const HarmonicNoiseParams& p) {
    if (m.level_db[i] < p.silence_level.value()) {
        return FrameClass::silence;
    }
    if (m.burst[i] != 0) {
        return FrameClass::plosive;
    }
    if (!voiced) {
        return m.centroid_hz[i] > p.sibilant_centroid.value() ? FrameClass::sibilant
                                                              : FrameClass::breath;
    }
    if (m.octave_flag[i] != 0) {
        return FrameClass::rough;
    }
    if (m.hnr_db[i] < p.breathy_hnr.value() || m.mvf_hz[i] < p.breathy_mvf.value()) {
        return FrameClass::breathy;
    }
    return FrameClass::voiced;
}

/// Most frequent label among the non-plosive frames of the window; the frame's own label on a tie.
FrameClass window_mode(const std::vector<FrameClass>& labels, std::size_t i, std::size_t half) {
    std::array<int, 7> votes{};
    const std::size_t lo = i > half ? i - half : 0;
    const std::size_t hi = std::min(i + half, labels.size() - 1);
    for (std::size_t k = lo; k <= hi; ++k) {
        if (labels[k] != FrameClass::plosive) {
            ++votes[static_cast<std::size_t>(labels[k])];
        }
    }
    FrameClass best = labels[i];
    for (std::size_t c = 0; c < votes.size(); ++c) {
        if (votes[c] > votes[static_cast<std::size_t>(best)]) {
            best = static_cast<FrameClass>(c);
        }
    }
    return best;
}

struct Run {
    std::size_t begin;
    std::size_t end;  // exclusive
};

std::vector<Run> runs_of(const std::vector<FrameClass>& labels) {
    std::vector<Run> runs;
    for (std::size_t i = 0; i < labels.size();) {
        std::size_t j = i;
        while (j < labels.size() && labels[j] == labels[i]) {
            ++j;
        }
        runs.push_back({.begin = i, .end = j});
        i = j;
    }
    return runs;
}

/// A run shorter than min_len joins the longer of its non-plosive neighbours.
void absorb_short_runs(std::vector<FrameClass>& labels, std::size_t min_len) {
    const std::vector<Run> runs = runs_of(labels);
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const Run run = runs[r];
        if (run.end - run.begin >= min_len || labels[run.begin] == FrameClass::plosive) {
            continue;
        }
        const auto usable = [&](std::size_t k) {
            return k < runs.size() && labels[runs[k].begin] != FrameClass::plosive;
        };
        const auto length = [&](std::size_t k) { return runs[k].end - runs[k].begin; };
        const bool left = r > 0 && usable(r - 1);
        const bool right = usable(r + 1);
        if (!left && !right) {
            continue;
        }
        const std::size_t from =
            (left && (!right || length(r - 1) >= length(r + 1))) ? r - 1 : r + 1;
        std::fill(labels.begin() + static_cast<std::ptrdiff_t>(run.begin),
                  labels.begin() + static_cast<std::ptrdiff_t>(run.end), labels[runs[from].begin]);
    }
}

}  // namespace

Result<std::vector<FrameClass>> classify_frames(const FrameMetrics& metrics,
                                                std::span<const double> voicing_prob, Seconds hop,
                                                const HarmonicNoiseParams& params) {
    if (auto ok = validate_params(params); !ok) {
        return Unexpected{ok.error()};
    }
    const std::size_t n = voicing_prob.size();
    const bool sized = metrics.level_db.size() == n && metrics.centroid_hz.size() == n &&
                       metrics.hnr_db.size() == n && metrics.mvf_hz.size() == n &&
                       metrics.burst.size() == n && metrics.octave_flag.size() == n;
    if (!sized || !(hop.value() > 0.0)) {
        return fail(ErrorCode::invalid_argument,
                    "classify_frames: metrics, voicing and hop do not agree");
    }
    std::vector<FrameClass> labels(n);
    for (std::size_t i = 0; i < n; ++i) {
        labels[i] = first_match(metrics, i, voicing_prob[i] >= k_voiced_probability, params);
    }
    if (n == 0) {
        return labels;
    }
    const std::vector<FrameClass> raw = labels;
    const auto half = static_cast<std::size_t>(params.median_frames / 2);
    for (std::size_t i = 0; i < n; ++i) {
        if (raw[i] != FrameClass::plosive) {
            labels[i] = window_mode(raw, i, half);
        }
    }
    absorb_short_runs(labels, static_cast<std::size_t>(
                                  std::ceil(params.min_segment.value() / hop.value() - 1e-9)));
    return labels;
}

Result<NoiseTrack> split_harmonic_noise(std::span<const float> mono, Hz sample_rate,
                                        const NoteCurves& curves, const HarmonicTrack& harmonics,
                                        const PitchEvidence& evidence,
                                        const HarmonicNoiseParams& params) {
    auto metrics = measure_frames(mono, sample_rate, curves, harmonics, evidence, params);
    if (!metrics) {
        return Unexpected{metrics.error()};
    }
    auto classes = classify_frames(*metrics, evidence.voicing_prob, curves.grid.hop, params);
    if (!classes) {
        return Unexpected{classes.error()};
    }
    return NoiseTrack{.grid = curves.grid,
                      .band_energy_linear = std::move(metrics->noise_band_power),
                      .frame_class = std::move(*classes)};
}

}  // namespace anno
