#include "core/analysis/perceived_pitch/perceived_pitch.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace anno {

namespace {

constexpr std::size_t k_min_frames = 3;
constexpr double k_vibrato_min_hz = 3.0;
constexpr double k_vibrato_max_hz = 9.0;
constexpr double k_vibrato_min_rms_cents = 5.0;
constexpr double k_vibrato_min_periodicity = 0.5;

bool positive(double v) {
    return std::isfinite(v) && v > 0.0;
}

bool non_negative(double v) {
    return std::isfinite(v) && v >= 0.0;
}

Result<void> validate_weights(const StabilityWeightParams& w) {
    if (positive(w.rate_scale_cents_per_s) && non_negative(w.gamma) &&
        positive(w.amp_range.value())) {
        return {};
    }
    return fail(ErrorCode::invalid_argument, "pitch description: invalid stability weights");
}

Result<void> validate_trim(const TrimParams& t) {
    if (non_negative(t.start.value()) && non_negative(t.end.value()) &&
        non_negative(t.max_fraction) && t.max_fraction < 0.5) {
        return {};
    }
    return fail(ErrorCode::invalid_argument, "pitch description: invalid trims");
}

struct Frames {
    std::size_t first = 0;  // [first, last)
    std::size_t last = 0;
};

/// The trimmed region: per side min(fixed trim, max_fraction of the note); the whole note if it
/// would have fewer than 3 frames.
Frames trimmed(std::size_t n, Seconds hop, const TrimParams& t) {
    const double length_s = static_cast<double>(n) * hop.value();
    const auto skip = [&](Seconds fixed) {
        return static_cast<std::size_t>(
            std::lround(std::min(fixed.value(), t.max_fraction * length_s) / hop.value()));
    };
    const std::size_t a = skip(t.start);
    const std::size_t b = skip(t.end);
    if (a + b + k_min_frames > n) {
        return {.first = 0, .last = n};
    }
    return {.first = a, .last = n - b};
}

/// Linear interpolation across NaN in cents, hold at the ends. Pre: at least one finite value.
std::vector<double> fill_gaps(std::vector<double> v) {
    const std::size_t n = v.size();
    std::size_t first = 0;
    while (std::isnan(v[first])) {
        ++first;
    }
    for (std::size_t i = 0; i < first; ++i) {
        v[i] = v[first];
    }
    std::size_t prev = first;
    for (std::size_t i = first + 1; i < n; ++i) {
        if (std::isnan(v[i])) {
            continue;
        }
        for (std::size_t k = prev + 1; k < i; ++k) {
            const double w = static_cast<double>(k - prev) / static_cast<double>(i - prev);
            v[k] = v[prev] + w * (v[i] - v[prev]);
        }
        prev = i;
    }
    for (std::size_t i = prev + 1; i < n; ++i) {
        v[i] = v[prev];
    }
    return v;
}

/// One-pole low-pass, forward then backward (zero phase), applied twice; state starts at the end
/// value.
std::vector<double> zero_phase_lowpass(const std::vector<double>& x, double cutoff_hz,
                                       double hop_s) {
    const double alpha = 1.0 - std::exp(-2.0 * std::numbers::pi * cutoff_hz * hop_s);
    std::vector<double> y = x;
    for (int pass = 0; pass < 2; ++pass) {
        double state = y.front();
        for (double& v : y) {
            state += alpha * (v - state);
            v = state;
        }
        state = y.back();
        for (std::size_t i = y.size(); i-- > 0;) {
            state += alpha * (y[i] - state);
            y[i] = state;
        }
    }
    return y;
}

double weighted_mean(const std::vector<double>& v, const std::vector<double>& w, Frames r) {
    double sum = 0.0;
    double weight = 0.0;
    for (std::size_t i = r.first; i < r.last; ++i) {
        sum += w[i] * v[i];
        weight += w[i];
    }
    if (weight <= 0.0) {  // every frame weightless (all far below the loudest): a plain mean
        for (std::size_t i = r.first; i < r.last; ++i) {
            sum += v[i];
        }
        return sum / static_cast<double>(r.last - r.first);
    }
    return sum / weight;
}

std::vector<Cents> to_cents_vector(const std::vector<double>& v) {
    std::vector<Cents> out(v.size());
    std::ranges::transform(v, out.begin(), [](double c) { return Cents{c}; });
    return out;
}

/// The vibrato period in frames (autocorrelation peak of the modulation at 3-9 Hz), or 0 if the
/// modulation is too small or not periodic.
double vibrato_period_frames(const std::vector<double>& mod, Frames r, double hop_s) {
    const std::size_t n = r.last - r.first;
    double energy = 0.0;
    for (std::size_t i = r.first; i < r.last; ++i) {
        energy += mod[i] * mod[i];
    }
    if (std::sqrt(energy / static_cast<double>(n)) < k_vibrato_min_rms_cents) {
        return 0.0;
    }
    const auto lag_lo = static_cast<std::size_t>(std::floor(1.0 / (k_vibrato_max_hz * hop_s)));
    const auto lag_hi = static_cast<std::size_t>(std::ceil(1.0 / (k_vibrato_min_hz * hop_s)));
    double best = 0.0;
    std::size_t best_lag = 0;
    for (std::size_t lag = std::max<std::size_t>(1, lag_lo); lag <= lag_hi && lag + 2 < n; ++lag) {
        double c = 0.0;
        for (std::size_t i = r.first; i + lag < r.last; ++i) {
            c += mod[i] * mod[i + lag];
        }
        c /= energy;
        if (c > best) {
            best = c;
            best_lag = lag;
        }
    }
    return best >= k_vibrato_min_periodicity ? static_cast<double>(best_lag) : 0.0;
}

}  // namespace

Result<PitchDecomposition> decompose_pitch_curve(const NoteSpan& span, const PitchTrack& pitch,
                                                 std::span<const Decibels> amp,
                                                 const PitchDecompositionParams& params) {
    if (auto r = validate_weights(params.weights); !r) {
        return Unexpected{r.error()};
    }
    if (auto r = validate_trim(params.trim); !r) {
        return Unexpected{r.error()};
    }
    if (!positive(params.drift_cutoff.value())) {
        return fail(ErrorCode::invalid_argument, "decompose_pitch_curve: drift_cutoff must be > 0");
    }
    const FrameGrid grid = note_grid(span, pitch.grid);
    const auto n = static_cast<std::size_t>(grid.n_frames);
    if (n < k_min_frames) {
        return fail(ErrorCode::invalid_argument,
                    "decompose_pitch_curve: the note has fewer than 3 pitch frames");
    }
    if (!amp.empty() && amp.size() != n) {
        return fail(ErrorCode::invalid_argument,
                    "decompose_pitch_curve: amp must have one value per note frame");
    }
    const auto first = static_cast<std::size_t>(
        std::llround((grid.start - pitch.grid.start).value() / pitch.grid.hop.value()));
    std::vector<double> f0(n);
    std::size_t voiced = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const bool ok =
            !std::isnan(pitch.f0[first + i].value()) && pitch.voicing_prob[first + i] >= 0.5;
        f0[i] = ok ? pitch.f0[first + i].value() : std::nan("");
        voiced += ok ? 1 : 0;
    }
    if (voiced < k_min_frames) {
        return fail(ErrorCode::invalid_argument,
                    "decompose_pitch_curve: the note has fewer than 3 voiced frames");
    }
    f0 = fill_gaps(std::move(f0));
    const double hop_s = grid.hop.value();
    const std::vector<double> slow = zero_phase_lowpass(f0, params.drift_cutoff.value(), hop_s);
    const std::vector<Cents> slow_cents = to_cents_vector(slow);
    const std::vector<double> w = stability_weights(slow_cents, grid.hop, amp, params.weights);
    const double center = weighted_mean(slow, w, trimmed(n, grid.hop, params.trim));
    PitchDecomposition out;
    out.center = Cents{center};
    out.voiced_fraction = static_cast<double>(voiced) / static_cast<double>(n);
    out.drift.resize(n);
    out.mod.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.drift[i] = Cents{slow[i] - center};
        out.mod[i] = Cents{f0[i] - slow[i]};
    }
    return out;
}

Result<Cents> perceived_pitch(const Note& note, const PerceivedPitchParams& params) {
    if (auto r = validate_weights(params.weights); !r) {
        return Unexpected{r.error()};
    }
    if (auto r = validate_trim(params.trim); !r) {
        return Unexpected{r.error()};
    }
    const auto n = static_cast<std::size_t>(std::max<std::int64_t>(0, note.grid.n_frames));
    if (n < k_min_frames || note.pitch_drift.size() != n || note.pitch_mod.size() != n ||
        (!note.amp.empty() && note.amp.size() != n)) {
        return fail(ErrorCode::invalid_argument,
                    "perceived_pitch: the note needs at least 3 frames and curves of equal length");
    }
    std::vector<double> f(n);
    std::vector<double> mod(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = note.pitch_center.value() + note.pitch_drift[i].value() + note.pitch_mod[i].value();
        mod[i] = note.pitch_mod[i].value();
    }
    const double hop_s = note.grid.hop.value();
    Frames region = trimmed(n, note.grid.hop, params.trim);
    if (params.cycles == CyclePolicy::whole_cycles) {
        const double period = vibrato_period_frames(mod, region, hop_s);
        const auto length = static_cast<double>(region.last - region.first);
        if (period > 0.0 && length >= period) {
            const double cycles = std::floor(length / period);
            region.last = region.first + static_cast<std::size_t>(std::lround(cycles * period));
        }
    }
    const std::vector<double> w =
        stability_weights(to_cents_vector(f), note.grid.hop, note.amp, params.weights);
    return Cents{weighted_mean(f, w, region)};
}

}  // namespace anno
