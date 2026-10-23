#include "core/synthesis/harmonic_noise/harmonic_noise_engine.h"

#include "core/synthesis/engine/engine_support.h"
#include "core/synthesis/engine/source_through_map.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <optional>
#include <string_view>

namespace anno {

namespace {

constexpr double k_two_pi = 2.0 * std::numbers::pi;
constexpr double k_max_envelope_gain_db = 20.0;  // envelope re-read gain limit, both directions

// ---- Which edits the engine realises
// -------------------------------------------------------------

Result<void> check_supported(const AudioSource& src, const Note& note, const EffectiveNote& eff) {
    const auto no = [](const char* what) {
        return fail(ErrorCode::not_implemented, std::string{"HarmonicNoiseEngine: "} + what);
    };
    if (src.channel_count() != 1) {
        return no("mono sources only");
    }
    if (!only_pitch_edited(eff)) {
        return no("only a pitch offset (and a timing shift through the time map) is realised");
    }
    if (!note.is_active) {
        return no("the note is inactive");
    }
    if (note.harmonics.freq_hz.rows == 0 || note.envelope.log_mag_db.rows == 0) {
        return no("the note has no harmonic or envelope track (analyse it with Phase 4)");
    }
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"HarmonicNoiseEngine: "} + what);
    };
    if (!std::isfinite(eff.center.value())) {
        return bad("the effective center is not finite");
    }
    const std::int64_t frames = note.harmonics.freq_hz.rows;
    const bool classes_ok =
        note.noise.frame_class.empty() || std::cmp_equal(note.noise.frame_class.size(), frames);
    if (note.envelope.log_mag_db.rows != frames || note.harmonics.amp_linear.rows != frames ||
        note.harmonics.phase_rad.rows != frames || !classes_ok) {
        return bad("the harmonic, noise and envelope tracks disagree in frame count");
    }
    return {};
}

Result<void> check_params(const HarmonicNoiseEngineParams& p) {
    const bool ok = std::isfinite(p.class_edge_fade.value()) && p.class_edge_fade.value() >= 0.0 &&
                    p.taper_start_of_rate > 0.0 && p.taper_start_of_rate < p.taper_end_of_rate &&
                    p.taper_end_of_rate <= 0.5;
    if (!ok) {
        return fail(
            ErrorCode::invalid_argument,
            "HarmonicNoiseEngine: class_edge_fade must be >= 0 and 0 < taper_start < taper_end "
            "<= 0.5");
    }
    return {};
}

// ---- The harmonic model on the output sample axis
// --------------------------------------------------

/// One analysis frame (or a held copy of the first/last one) at an output sample position.
struct Frame {
    double position = 0.0;  // output sample position of the frame centre (fractional)
    bool shifted = false;   // the frame's class is shifted
    std::vector<double> freq_hz;
    std::vector<double> amp;
    std::vector<double> phase_rad;
    std::vector<double> amp_shifted;   // amp re-read from the envelope at the shifted frequency
    std::vector<double> freq_shifted;  // 0 where the harmonic leaves the band
};

double envelope_db(const EnvelopeTrack& env, std::size_t row, double hz) {
    const std::size_t cols = env.freqs.size();
    const auto it = std::ranges::upper_bound(env.freqs, Hz{hz});
    const auto row_at = [&](std::size_t b) {
        return static_cast<double>(env.log_mag_db.data[row * cols + b]);
    };
    if (it == env.freqs.begin()) {
        return row_at(0);
    }
    if (it == env.freqs.end()) {
        return row_at(cols - 1);
    }
    const auto hi = static_cast<std::size_t>(it - env.freqs.begin());
    const double f0 = env.freqs[hi - 1].value();
    const double f1 = env.freqs[hi].value();
    const double w = (hz - f0) / (f1 - f0);
    return row_at(hi - 1) + w * (row_at(hi) - row_at(hi - 1));
}

double taper(double hz, double rate, const HarmonicNoiseEngineParams& p) {
    const double lo = p.taper_start_of_rate * rate;
    const double hi = p.taper_end_of_rate * rate;
    if (hz <= lo) {
        return 1.0;
    }
    if (hz >= hi) {
        return 0.0;
    }
    return 0.5 * (1.0 + std::cos(std::numbers::pi * (hz - lo) / (hi - lo)));
}

bool class_is_shifted(const Note& note, std::size_t frame, const HarmonicNoiseEngineParams& p) {
    if (note.noise.frame_class.empty()) {
        return true;  // a note without a class track counts as modal
    }
    return p.shifted_classes.contains(note.noise.frame_class[frame]);
}

Frame build_frame(const Note& note, std::size_t i, double rate, std::int64_t d, double ratio,
                  const HarmonicNoiseEngineParams& p) {
    const auto cols = static_cast<std::size_t>(note.harmonics.freq_hz.cols);
    Frame f;
    const double t =
        (frame_time(note.harmonics.grid, static_cast<std::int64_t>(i)) - SourceTime{}).value();
    f.position = t * rate + static_cast<double>(d);
    for (auto* v : {&f.freq_hz, &f.amp, &f.phase_rad, &f.amp_shifted, &f.freq_shifted}) {
        v->assign(cols, 0.0);
    }
    for (std::size_t k = 0; k < cols; ++k) {
        const auto flat = i * cols + k;
        f.freq_hz[k] = static_cast<double>(note.harmonics.freq_hz.data[flat]);
        f.amp[k] = static_cast<double>(note.harmonics.amp_linear.data[flat]);
        f.phase_rad[k] = static_cast<double>(note.harmonics.phase_rad.data[flat]);
        const double fs = f.freq_hz[k] * ratio;
        const double gain_taper = taper(fs, rate, p);
        if (f.amp[k] <= 0.0 || gain_taper <= 0.0) {
            continue;
        }
        const double db = std::clamp(envelope_db(note.envelope, i, fs) -
                                         envelope_db(note.envelope, i, f.freq_hz[k]),
                                     -k_max_envelope_gain_db, k_max_envelope_gain_db);
        f.freq_shifted[k] = fs;
        f.amp_shifted[k] = f.amp[k] * std::pow(10.0, db / 20.0) * gain_taper;
    }
    // A frame without any measured harmonic is passed through whatever its class (R5).
    f.shifted = class_is_shifted(note, i, p) &&
                std::ranges::any_of(f.amp, [](double a) { return a > 0.0; });
    return f;
}

/// A held copy of `edge` at output sample position `position`: frequency and amplitude as the
/// edge's, phase running on.
Frame held_copy(const Frame& edge, double position, double rate) {
    Frame f = edge;
    f.position = position;
    const double dt = (position - edge.position) / rate;
    for (std::size_t k = 0; k < f.phase_rad.size(); ++k) {
        f.phase_rad[k] = edge.phase_rad[k] + k_two_pi * edge.freq_hz[k] * dt;
    }
    return f;
}

std::vector<Frame> build_frames(const Note& note, double rate, std::int64_t d, double ratio,
                                std::int64_t zone_begin, std::int64_t zone_end,
                                const HarmonicNoiseEngineParams& p) {
    std::vector<Frame> frames;
    const auto n = static_cast<std::size_t>(note.harmonics.freq_hz.rows);
    frames.reserve(n + 2);
    for (std::size_t i = 0; i < n; ++i) {
        frames.push_back(build_frame(note, i, rate, d, ratio, p));
    }
    const double before = std::min(static_cast<double>(zone_begin), frames.front().position) - 1.0;
    const double after = std::max(static_cast<double>(zone_end), frames.back().position) + 1.0;
    frames.insert(frames.begin(), held_copy(frames.front(), before, rate));
    frames.push_back(held_copy(frames.back(), after, rate));
    return frames;
}

// ---- Weights: 1 on shifted runs, raised-cosine edges
// ------------------------------------------------

/// w[n - first] for the zone samples [first, first + count): run_weights over the frames' classes.
std::vector<double> build_weights(const std::vector<Frame>& frames, double hop_samples,
                                  double fade_samples, std::int64_t first, std::int64_t count) {
    std::vector<RunFrame> run;
    run.reserve(frames.size());
    for (const Frame& f : frames) {
        run.push_back({.position = f.position, .shifted = f.shifted});
    }
    return run_weights(run, hop_samples, fade_samples, first, count);
}

// ---- Additive synthesis of one segment between two frames
// --------------------------------------------

/// McAulay-Quatieri cubic phase through (theta_start, omega_start) -> (theta_end, omega_end) over
/// `duration_s`; frequencies in rad/s.
class CubicPhase {
public:
    CubicPhase(double theta_start, double theta_end, double omega_start, double omega_end,
               double duration_s)
        : theta0_{theta_start}, omega0_{omega_start} {
        const double m = std::round(((theta_start + omega_start * duration_s - theta_end) +
                                     0.5 * (omega_end - omega_start) * duration_s) /
                                    k_two_pi);
        const double x = theta_end + k_two_pi * m - theta_start - omega_start * duration_s;
        const double y = omega_end - omega_start;
        alpha_ = 3.0 * x / (duration_s * duration_s) - y / duration_s;
        beta_ = -2.0 * x / (duration_s * duration_s * duration_s) + y / (duration_s * duration_s);
    }
    [[nodiscard]] double at(double tau) const {
        return theta0_ + omega0_ * tau + alpha_ * tau * tau + beta_ * tau * tau * tau;
    }

private:
    double theta0_;
    double omega0_;
    double alpha_ = 0.0;
    double beta_ = 0.0;
};

struct SegmentRange {
    std::int64_t begin;  // first zone-relative sample
    std::int64_t end;    // one past the last
};

/// Everything needed to synthesise one harmonic over the segment between frames a and b.
struct HarmonicPath {
    bool original_on = false;
    bool shifted_on = false;
    CubicPhase cubic;
    double shifted_freq_a = 0.0;  // Hz
    double shifted_freq_b = 0.0;
};

/// The original path: a dead end takes the other end's frequency and a phase extrapolated to it.
/// The shifted path ramps its frequency linearly between the two frames.
HarmonicPath path_of(const Frame& a, const Frame& b, std::size_t k, double duration_s) {
    double wa = k_two_pi * a.freq_hz[k];
    double wb = k_two_pi * b.freq_hz[k];
    double ta = a.phase_rad[k];
    double tb = b.phase_rad[k];
    if (a.amp[k] <= 0.0 && b.amp[k] > 0.0) {
        wa = wb;
        ta = tb - wb * duration_s;
    } else if (b.amp[k] <= 0.0 && a.amp[k] > 0.0) {
        wb = wa;
        tb = ta + wa * duration_s;
    }
    return {.original_on = a.amp[k] > 0.0 || b.amp[k] > 0.0,
            .shifted_on = a.amp_shifted[k] > 0.0 || b.amp_shifted[k] > 0.0,
            .cubic = CubicPhase{ta, tb, wa, wb, duration_s},
            .shifted_freq_a = a.amp_shifted[k] > 0.0 ? a.freq_shifted[k] : b.freq_shifted[k],
            .shifted_freq_b = b.amp_shifted[k] > 0.0 ? b.freq_shifted[k] : a.freq_shifted[k]};
}

/// One sample of equal-power-weighted (shifted - original) for harmonic k.
double harmonic_delta(const HarmonicPath& path, const Frame& a, const Frame& b, std::size_t k,
                      double u_fade, double tau, double duration_s, double start_phase) {
    const double u = tau / duration_s;
    double original = 0.0;
    double shifted = 0.0;
    if (path.original_on) {
        original = ((1.0 - u) * a.amp[k] + u * b.amp[k]) * std::cos(path.cubic.at(tau));
    }
    if (path.shifted_on) {
        const double amp = (1.0 - u) * a.amp_shifted[k] + u * b.amp_shifted[k];
        const double fa = path.shifted_freq_a;
        const double fb = path.shifted_freq_b;
        const double phase =
            start_phase + k_two_pi * (fa * tau + (fb - fa) * tau * tau / (2.0 * duration_s));
        shifted = amp * std::cos(phase);
    }
    const double quarter =
        0.5 * std::numbers::pi * u_fade;  // equal-power crossfade, u_fade in 0..1
    return std::sin(quarter) * shifted - (1.0 - std::cos(quarter)) * original;
}

/// Adds the weighted (shifted - original) of every harmonic over the segment between frames a and
/// b. `alive[k]` and `phase_s[k]` carry the running phase of the shifted harmonic across segments;
/// a harmonic is born at the measured phase.
void add_segment(const Frame& a, const Frame& b, SegmentRange range, std::int64_t first,
                 double rate, const std::vector<double>& w, std::vector<char>& alive,
                 std::vector<double>& phase_s, std::vector<double>& delta) {
    const double duration_s = (b.position - a.position) / rate;
    for (std::size_t k = 0; k < a.amp.size(); ++k) {
        const HarmonicPath path = path_of(a, b, k, duration_s);
        if (!path.shifted_on) {
            alive[k] = 0;
        }
        if (!path.original_on && !path.shifted_on) {
            continue;
        }
        if (path.shifted_on && alive[k] == 0) {
            phase_s[k] = a.amp_shifted[k] > 0.0
                             ? a.phase_rad[k]
                             : b.phase_rad[k] - k_two_pi * path.shifted_freq_a * duration_s;
            alive[k] = 1;
        }
        const double start_phase = phase_s[k];
        for (std::int64_t n = range.begin; n < range.end; ++n) {
            const double u_fade = w[static_cast<std::size_t>(n)];
            if (u_fade > 0.0) {
                const double tau = (static_cast<double>(first + n) - a.position) / rate;
                delta[static_cast<std::size_t>(n)] +=
                    harmonic_delta(path, a, b, k, u_fade, tau, duration_s, start_phase);
            }
        }
        if (path.shifted_on) {
            phase_s[k] =
                start_phase +
                k_two_pi * (path.shifted_freq_a * duration_s +
                            0.5 * (path.shifted_freq_b - path.shifted_freq_a) * duration_s);
        }
    }
}

}  // namespace

HarmonicNoiseEngine::HarmonicNoiseEngine(HarmonicNoiseEngineParams params) : params_{params} {}

std::string HarmonicNoiseEngine::settings_key() const {
    const auto pattern = [](double v) { return hex64(std::bit_cast<std::uint64_t>(v)); };
    return "v1;classes=" + hex64(params_.shifted_classes.bits()) +
           ";fade=" + pattern(params_.class_edge_fade.value()) +
           ";taper_start=" + pattern(params_.taper_start_of_rate) +
           ";taper_end=" + pattern(params_.taper_end_of_rate);
}

Result<OutputAudio> HarmonicNoiseEngine::render_note(const AudioSource& src, const Note& note,
                                                     const EffectiveNote& eff,
                                                     const TimeMap& time_map,
                                                     OutputRange out_range) const {
    if (auto ok = check_params(params_); !ok) {
        return Unexpected{ok.error()};
    }
    if (auto ok = check_supported(src, note, eff); !ok) {
        return Unexpected{ok.error()};
    }
    const Hz rate = src.sample_rate();
    const Samples first = to_sample_index(out_range.start(), rate, Rounding::nearest);
    const Samples last = to_sample_index(out_range.end(), rate, Rounding::nearest);
    if (last.value() < first.value()) {
        return fail(ErrorCode::invalid_argument, "HarmonicNoiseEngine: a negative range");
    }
    auto shift_samples = zone_shift(time_map, rate, out_range, "HarmonicNoiseEngine");
    if (!shift_samples) {
        return Unexpected{shift_samples.error()};
    }
    auto original = read_through_map(src, time_map, first, last - first);
    if (!original) {
        return Unexpected{original.error()};
    }
    const double shift_cents = (eff.center - note.pitch_center).value();
    if (std::abs(shift_cents) > k_neutral_snap_offset && original->channel_count() == 1) {
        const double sr = rate.value();
        const double ratio = std::exp2(shift_cents / 1200.0);
        const auto count = static_cast<std::int64_t>(original->frame_count().value());
        const std::vector<Frame> frames =
            build_frames(note, sr, *shift_samples, ratio, first.value(), last.value(), params_);
        const double hop_samples = note.harmonics.grid.hop.value() * sr;
        const double fade_samples = std::max(1.0, params_.class_edge_fade.value() * sr);
        const std::vector<double> w =
            build_weights(frames, hop_samples, fade_samples, first.value(), count);
        std::vector<double> delta(static_cast<std::size_t>(count), 0.0);
        std::vector<char> alive(frames.front().amp.size(), 0);
        std::vector<double> phase_s(frames.front().amp.size(), 0.0);
        for (std::size_t s = 0; s + 1 < frames.size(); ++s) {
            const auto begin = std::max<std::int64_t>(
                static_cast<std::int64_t>(std::ceil(frames[s].position)) - first.value(), 0);
            const auto end = std::min<std::int64_t>(
                static_cast<std::int64_t>(std::ceil(frames[s + 1].position)) - first.value(),
                count);
            if (begin >= end) {
                continue;
            }
            add_segment(frames[s], frames[s + 1], {.begin = begin, .end = end}, first.value(), sr,
                        w, alive, phase_s, delta);
        }
        auto samples = original->channel(0);
        for (std::size_t n = 0; n < samples.size(); ++n) {
            if (w[n] > 0.0) {
                samples[n] = static_cast<float>(static_cast<double>(samples[n]) + delta[n]);
            }
        }
    }
    return OutputAudio{.audio = std::move(*original),
                       .start = to_time_point<OutputAxis>(first, rate)};
}

}  // namespace anno
