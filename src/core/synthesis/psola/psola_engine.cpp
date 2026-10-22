#include "core/synthesis/psola/psola_engine.h"

#include "core/synthesis/engine/engine_support.h"
#include "core/synthesis/engine/source_through_map.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>

namespace anno {

namespace {

constexpr double k_min_period_s = 1.0 / 2000.0;  // pitch periods are kept inside 40..2000 Hz
constexpr double k_max_period_s = 1.0 / 40.0;
constexpr double k_context_periods = 3.0;  // source read around the zone, in longest periods
constexpr double k_snap_halfwidth = 0.2;   // mark search window, fraction of a period
constexpr double k_min_window_sum = 1e-6;
constexpr int k_max_separation_cycles = 8;
constexpr double k_margin_s = 0.1;  // how far a shifted edge frame's run continues past the note

Result<void> check_params(const PsolaEngineParams& p) {
    if (!std::isfinite(p.class_edge_fade.value()) || p.class_edge_fade.value() < 0.0 ||
        !std::isfinite(p.max_shift.value()) || p.max_shift.value() <= 0.0 ||
        p.separation_cycles < 0 || p.separation_cycles > k_max_separation_cycles) {
        return fail(ErrorCode::invalid_argument,
                    "PsolaEngine: class_edge_fade must be >= 0, max_shift finite and > 0 and "
                    "separation_cycles in 0..8");
    }
    return {};
}

Result<void> check_supported(const AudioSource& src, const Note& note, const EffectiveNote& eff) {
    const auto no = [](const char* what) {
        return fail(ErrorCode::not_implemented, std::string{"PsolaEngine: "} + what);
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
    if (note.grid.n_frames == 0 && note.pitch_drift.empty() && note.pitch_mod.empty()) {
        return no("the note has no pitch curves");
    }
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"PsolaEngine: "} + what);
    };
    if (!std::isfinite(eff.center.value())) {
        return bad("the effective center is not finite");
    }
    const auto frames = static_cast<std::size_t>(std::max<std::int64_t>(note.grid.n_frames, 0));
    if (note.pitch_drift.size() != frames || note.pitch_mod.size() != frames ||
        (!note.noise.frame_class.empty() && note.noise.frame_class.size() != frames)) {
        return bad("the pitch curves and the noise classes disagree with the note's grid");
    }
    return {};
}

// ---- The note's pitch on the zone's sample axis
// ------------------------------------------------

struct PitchFrame {
    double position = 0.0;  // sample position of the frame centre on the output axis
    double cents = 0.0;     // absolute pitch of the unedited note
    bool shifted = false;
};

bool class_is_shifted(const Note& note, std::size_t frame, const PsolaEngineParams& p) {
    return note.noise.frame_class.empty() ||
           p.shifted_classes.contains(note.noise.frame_class[frame]);
}

/// The note's frames, then a held copy before and after the zone (the margin). A frame whose pitch
/// is not a number is never shifted.
std::vector<PitchFrame> build_pitch_frames(const Note& note, double rate, std::int64_t d,
                                           std::int64_t zone_begin, std::int64_t zone_end,
                                           const PsolaEngineParams& p) {
    std::vector<PitchFrame> frames;
    const auto n = static_cast<std::size_t>(note.grid.n_frames);
    frames.reserve(n + 2);
    for (std::size_t i = 0; i < n; ++i) {
        const double t =
            (frame_time(note.grid, static_cast<std::int64_t>(i)) - SourceTime{}).value();
        const double cents =
            note.pitch_center.value() + note.pitch_drift[i].value() + note.pitch_mod[i].value();
        frames.push_back({.position = t * rate + static_cast<double>(d),
                          .cents = cents,
                          .shifted = std::isfinite(cents) && class_is_shifted(note, i, p)});
    }
    // The margin: a held copy of each edge frame (shifted like the edge) up to k_margin_s beyond
    // the note and inside the zone, then an unshifted sentinel, so samples further out stay
    // original.
    PitchFrame before = frames.front();
    before.position = std::min(static_cast<double>(zone_begin), frames.front().position) + 0.0;
    before.position = std::max(before.position, frames.front().position - k_margin_s * rate);
    PitchFrame after = frames.back();
    after.position = std::max(static_cast<double>(zone_end), frames.back().position);
    after.position = std::min(after.position, frames.back().position + k_margin_s * rate);
    PitchFrame sentinel_before{
        .position = before.position - 1.0, .cents = before.cents, .shifted = false};
    PitchFrame sentinel_after{
        .position = after.position + 1.0, .cents = after.cents, .shifted = false};
    frames.insert(frames.begin(), {sentinel_before, before});
    frames.push_back(after);
    frames.push_back(sentinel_after);
    return frames;
}

/// One maximal run of shifted frames, as indices into the frame list.
struct Run {
    std::size_t first;
    std::size_t last;
};

std::vector<Run> shifted_runs(const std::vector<PitchFrame>& frames) {
    std::vector<Run> runs;
    for (std::size_t i = 0; i < frames.size();) {
        if (!frames[i].shifted) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j + 1 < frames.size() && frames[j + 1].shifted) {
            ++j;
        }
        runs.push_back({.first = i, .last = j});
        i = j + 1;
    }
    return runs;
}

/// Pitch period in samples at sample position `pos` inside `run` (cents interpolated linearly
/// between frame centres, held outside the run's frames), kept inside the period limits.
class PeriodCurve {
public:
    PeriodCurve(std::span<const PitchFrame> frames, Run run, double rate)
        : frames_{frames}, run_{run}, rate_{rate} {}

    [[nodiscard]] double at(double pos) const {
        const PitchFrame* lo = &frames_[run_.first];
        const PitchFrame* hi = &frames_[run_.last];
        double cents = 0.0;
        if (pos <= lo->position) {
            cents = lo->cents;
        } else if (pos >= hi->position) {
            cents = hi->cents;
        } else {
            const auto begin = frames_.begin() + static_cast<std::ptrdiff_t>(run_.first);
            const auto end = frames_.begin() + static_cast<std::ptrdiff_t>(run_.last) + 1;
            const auto upper = std::upper_bound(
                begin, end, pos, [](double v, const PitchFrame& f) { return v < f.position; });
            const PitchFrame& b = *upper;
            const PitchFrame& a = *(upper - 1);
            const double w = (pos - a.position) / (b.position - a.position);
            cents = a.cents + w * (b.cents - a.cents);
        }
        const double period = rate_ / to_hz(Cents{cents}, k_a4_standard_hz).value();
        return std::clamp(period, k_min_period_s * rate_, k_max_period_s * rate_);
    }

private:
    std::span<const PitchFrame> frames_;
    Run run_;
    double rate_;
};

// ---- Marks and grains
// ------------------------------------------------------------------------------

/// The source samples around the zone with a way to read them at fractional positions.
class Signal {
public:
    Signal(std::span<const float> samples, std::int64_t first) : samples_{samples}, first_{first} {}

    [[nodiscard]] std::int64_t first() const { return first_; }
    [[nodiscard]] std::int64_t end() const {
        return first_ + static_cast<std::int64_t>(samples_.size());
    }

    [[nodiscard]] double at(std::int64_t global) const {
        const std::int64_t i = global - first_;
        return i < 0 || std::cmp_greater_equal(i, samples_.size())
                   ? 0.0
                   : static_cast<double>(samples_[static_cast<std::size_t>(i)]);
    }

    /// Catmull-Rom interpolation at a fractional global sample position.
    [[nodiscard]] double cubic(double pos) const {
        const double floor_pos = std::floor(pos);
        const auto i = static_cast<std::int64_t>(floor_pos);
        const double f = pos - floor_pos;
        const double p0 = at(i - 1);
        const double p1 = at(i);
        const double p2 = at(i + 1);
        const double p3 = at(i + 2);
        return p1 +
               0.5 * f *
                   (p2 - p0 +
                    f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
    }

private:
    std::span<const float> samples_;
    std::int64_t first_;
};

/// Analysis marks over [from, to] (fractional global samples): the first at the strongest sample
/// of one period, each next one placed where the waveform best matches the previous mark's, within
/// +-k_snap_halfwidth periods of one period later (the period at the previous mark), refined to a
/// fraction of a sample. A peak pick jumps between features of a real voice and misaligns the
/// grains it places (the hiccups, tests/audio phase 4 listening); matching the previous period
/// keeps every mark on the same feature.
std::vector<double> analysis_marks(const Signal& x, const PeriodCurve& period, double from,
                                   double to) {
    std::vector<double> marks;
    const auto lo = static_cast<std::int64_t>(std::floor(from));
    const auto first_hi = lo + static_cast<std::int64_t>(std::ceil(period.at(from)));
    std::int64_t first = lo;
    for (std::int64_t n = lo; n <= first_hi; ++n) {
        if (x.at(n) > x.at(first)) {
            first = n;
        }
    }
    marks.push_back(static_cast<double>(first));
    const auto similarity = [&](double prev, double candidate, std::int64_t reach) {
        double dot = 0.0;
        double e_prev = 0.0;
        double e_cand = 0.0;
        for (std::int64_t k = -reach; k <= reach; ++k) {
            const double a = x.cubic(prev + static_cast<double>(k));
            const double b = x.cubic(candidate + static_cast<double>(k));
            dot += a * b;
            e_prev += a * a;
            e_cand += b * b;
        }
        return dot / std::sqrt(e_prev * e_cand + 1e-30);
    };
    while (true) {
        const double prev = marks.back();
        const double t = period.at(prev);
        const double nominal = prev + t;
        if (nominal > to) {
            break;
        }
        const auto half = static_cast<std::int64_t>(std::ceil(k_snap_halfwidth * t));
        const auto reach = static_cast<std::int64_t>(std::ceil(0.5 * t));
        const auto centre = static_cast<std::int64_t>(std::llround(nominal));
        std::int64_t best = centre;
        double best_score = -2.0;
        for (std::int64_t n = centre - half; n <= centre + half; ++n) {
            const double score = similarity(prev, static_cast<double>(n), reach);
            if (score > best_score) {
                best_score = score;
                best = n;
            }
        }
        // Parabola through the score at best-1, best, best+1 for the fractional position.
        const double left = similarity(prev, static_cast<double>(best - 1), reach);
        const double right = similarity(prev, static_cast<double>(best + 1), reach);
        const double curve = left - 2.0 * best_score + right;
        const double offset =
            curve < -1e-12 ? std::clamp(0.5 * (left - right) / curve, -0.5, 0.5) : 0.0;
        marks.push_back(static_cast<double>(best) + offset);
    }
    return marks;
}

/// Adds one two-period Hann grain around analysis mark `a` (periods before and after it:
/// before/after, samples) at synthesis position `s` into acc and window_sum.
struct Grain {
    double analysis_mark;  // fractional global sample
    double synthesis_position;
    double before;
    double after;
    double output_period;  // the shifted period at this mark, samples
};

struct Accumulator {
    std::int64_t first;  // global sample of index 0
    std::vector<double> sum;
    std::vector<double> window_sum;
    std::vector<double> output_period;  // of the latest grain covering the sample (0 where none)
};

void add_grain(const Signal& x, const Grain& g, Accumulator& acc) {
    const auto lo = static_cast<std::int64_t>(std::ceil(g.synthesis_position - g.before));
    const auto hi = static_cast<std::int64_t>(std::floor(g.synthesis_position + g.after));
    for (std::int64_t n = std::max(lo, acc.first);
         n <= hi && std::cmp_less(n - acc.first, acc.sum.size()); ++n) {
        const double u = static_cast<double>(n) - g.synthesis_position;
        const double span = u < 0.0 ? g.before : g.after;
        const double window = 0.5 * (1.0 + std::cos(std::numbers::pi * u / span));
        const auto idx = static_cast<std::size_t>(n - acc.first);
        acc.sum[idx] += window * x.cubic(g.analysis_mark + u);
        acc.window_sum[idx] += window;
        acc.output_period[idx] = g.output_period;  // the latest grain covering the sample
    }
}

/// Mean square of the source over the cycle around each mark (a tiny floor keeps ratios finite).
std::vector<double> cycle_energies(const Signal& x, const std::vector<double>& marks,
                                   const std::vector<double>& periods) {
    std::vector<double> energy(marks.size(), 0.0);
    for (std::size_t j = 0; j < marks.size(); ++j) {
        const auto from = static_cast<std::int64_t>(std::llround(marks[j] - 0.5 * periods[j]));
        const auto to = static_cast<std::int64_t>(std::llround(marks[j] + 0.5 * periods[j]));
        double sum = 0.0;
        for (std::int64_t n = from; n < to; ++n) {
            sum += x.at(n) * x.at(n);
        }
        energy[j] = sum / std::max<double>(1.0, static_cast<double>(to - from)) + 1e-12;
    }
    return energy;
}

/// The marks' own spacing: the distance between the neighbours on each side over the steps.
std::vector<double> mark_periods(const std::vector<double>& marks) {
    std::vector<double> periods(marks.size(), 0.0);
    for (std::size_t j = 0; j < marks.size(); ++j) {
        const std::size_t a = j > 0 ? j - 1 : 0;
        const std::size_t b = j + 1 < marks.size() ? j + 1 : marks.size() - 1;
        periods[j] = (marks[b] - marks[a]) / static_cast<double>(b - a);
    }
    return periods;
}

/// Writes into `periodic` (samples from global `first`) the periodic part of the source around
/// each analysis mark: the average of the same point of up to `cycles` cycles on each side, Hann
/// weighted, and by how close the cycle's energy is to the centre cycle's (smaller over larger),
/// each neighbour being scaled to the centre cycle's level, so an attack, a decay or a neighbouring
/// sound does not smear the level. The point is the offset from the mark, scaled by the ratio of
/// the local periods, so the cycles line up on the mark's feature through a changing pitch (the
/// marks already follow the waveform). Whatever does not repeat from cycle to cycle is left out
/// and stays in the source.
void average_cycles(const Signal& x, const std::vector<double>& marks, int cycles,
                    std::int64_t first, std::vector<float>& periodic) {
    const std::size_t count = marks.size();
    if (count < 2) {
        return;
    }
    const std::vector<double> periods = mark_periods(marks);
    const std::vector<double> energy = cycle_energies(x, marks, periods);
    const auto average_at = [&](std::size_t i, double n) {
        const double u = n - marks[i];
        double sum = 0.0;
        double weights = 0.0;
        for (int k = -cycles; k <= cycles; ++k) {
            const auto j = static_cast<std::int64_t>(i) + k;
            if (j < 0 || std::cmp_greater_equal(j, count)) {
                continue;
            }
            const auto ju = static_cast<std::size_t>(j);
            const double w = 0.5 * (1.0 + std::cos(std::numbers::pi * k / (cycles + 1))) *
                             std::min(energy[ju], energy[i]) / std::max(energy[ju], energy[i]);
            const double level = std::sqrt(energy[i] / energy[ju]);  // to the centre cycle's level
            sum += w * level * x.cubic(marks[ju] + u * periods[ju] / periods[i]);
            weights += w;
        }
        return sum / weights;
    };
    const auto end = static_cast<std::int64_t>(periodic.size());
    for (std::size_t i = 0; i < count; ++i) {
        const double lo = i > 0 ? 0.5 * (marks[i - 1] + marks[i]) : marks[i] - 0.5 * periods[i];
        const double hi =
            i + 1 < count ? 0.5 * (marks[i] + marks[i + 1]) : marks[i] + 0.5 * periods[i];
        const auto begin = std::max(static_cast<std::int64_t>(std::ceil(lo)), first);
        for (std::int64_t n = begin; static_cast<double>(n) < hi && n - first < end; ++n) {
            periodic[static_cast<std::size_t>(n - first)] =
                static_cast<float>(average_at(i, static_cast<double>(n)));
        }
    }
}

/// Gain for what the periodic average leaves out. For white noise of unit power the average
/// (Hann weights w_k, a = w_0 / sum w, b = sum w^2 / (sum w)^2) holds a fraction b of the power and
/// is correlated with the sample itself by a, so the remainder x - average has power 1 - 2a + b,
/// and the re-spaced average brings back about b. This gain makes the two add up to the original
/// power. It is exact for stationary noise and 1 for no separation.
double remainder_gain(int cycles) {
    if (cycles <= 0) {
        return 1.0;
    }
    double sum = 0.0;
    double sum_squares = 0.0;
    for (int k = -cycles; k <= cycles; ++k) {
        const double w = 0.5 * (1.0 + std::cos(std::numbers::pi * k / (cycles + 1)));
        sum += w;
        sum_squares += w * w;
    }
    const double a = 1.0 / sum;
    const double b = sum_squares / (sum * sum);
    return std::sqrt((1.0 - b) / (1.0 - 2.0 * a + b));
}

/// Re-spaces the grains of one run: synthesis marks one shifted period apart from `from`, each
/// taking the nearest analysis mark.
void synthesise_run(const Signal& x, const PeriodCurve& period, double ratio, double from,
                    double to, int cycles, std::vector<float>& periodic, Accumulator& acc) {
    const std::vector<double> marks =
        analysis_marks(x, period, from - period.at(from), to + period.at(to));
    if (cycles > 0) {
        average_cycles(x, marks, cycles, acc.first, periodic);
    }
    const Signal grains_from{periodic, acc.first};
    double s = from - period.at(from) / ratio;
    std::size_t i = 0;
    while (s <= to + period.at(to)) {
        while (i + 1 < marks.size() && std::abs(marks[i + 1] - s) <= std::abs(marks[i] - s)) {
            ++i;
        }
        const double t = period.at(marks[i]);
        const double before = i > 0 ? marks[i] - marks[i - 1] : t;
        const double after = i + 1 < marks.size() ? marks[i + 1] - marks[i] : t;
        const double output_period = period.at(s) / ratio;
        add_grain(grains_from,
                  {.analysis_mark = marks[i],
                   .synthesis_position = s,
                   .before = before,
                   .after = after,
                   .output_period = output_period},
                  acc);
        s += output_period;
    }
}

/// The overlap-add result per sample (window-normalised) with its local mean over one shifted
/// period taken out. Down shifts space the grains wider than the source period, and the gaps are
/// filled from different phases of the waveform, which leaves a small constant (DC) in the run.
/// A mean over exactly one period is zero for every harmonic of the shifted pitch, so harmonic
/// content is untouched. Samples without a grain are 0.
std::vector<double> run_signal(const Accumulator& acc) {
    const std::size_t n = acc.sum.size();
    std::vector<double> y(n, 0.0);
    std::vector<double> prefix(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (acc.window_sum[i] > k_min_window_sum) {
            y[i] = acc.sum[i] / acc.window_sum[i];
        }
        prefix[i + 1] = prefix[i] + y[i];
    }
    std::vector<double> out = y;
    for (std::size_t i = 0; i < n; ++i) {
        if (acc.output_period[i] < 2.0) {
            continue;
        }
        const auto half = static_cast<std::size_t>(0.5 * acc.output_period[i]);
        const std::size_t lo = i > half ? i - half : 0;
        const std::size_t hi = std::min(n, i + half + 1);
        out[i] = y[i] - (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
    }
    return out;
}

/// The zone [first, last) and the source read around it [ext_first, ext_last), global samples on
/// the output axis.
struct Zone {
    std::int64_t first;
    std::int64_t last;
    std::int64_t ext_first;
    std::int64_t ext_last;
    std::int64_t shift_samples;  // output sample n plays source sample n - shift_samples
    double rate;
    double ratio;  // frequency ratio of the edit
};

/// Replaces the shifted runs of `samples` (the zone, holding the original) by their re-spaced
/// version, crossfaded with the original by the run weights.
void respace_zone(const Signal& x, const Note& note, const Zone& zone, const PsolaEngineParams& p,
                  std::span<float> samples) {
    const std::vector<PitchFrame> frames =
        build_pitch_frames(note, zone.rate, zone.shift_samples, zone.first, zone.last, p);
    std::vector<RunFrame> run_frames;
    run_frames.reserve(frames.size());
    for (const PitchFrame& f : frames) {
        run_frames.push_back({.position = f.position, .shifted = f.shifted});
    }
    const double hop_samples = note.grid.hop.value() * zone.rate;
    const std::vector<double> w =
        run_weights(run_frames, hop_samples, std::max(1.0, p.class_edge_fade.value() * zone.rate),
                    zone.first, zone.last - zone.first);
    const auto ext_count = static_cast<std::size_t>(zone.ext_last - zone.ext_first);
    Accumulator acc{.first = zone.ext_first,
                    .sum = std::vector<double>(ext_count, 0.0),
                    .window_sum = std::vector<double>(ext_count, 0.0),
                    .output_period = std::vector<double>(ext_count, 0.0)};
    std::vector<float> periodic(
        ext_count);  // the source until a run replaces it by its periodic part
    for (std::size_t i = 0; i < ext_count; ++i) {
        periodic[i] = static_cast<float>(x.at(zone.ext_first + static_cast<std::int64_t>(i)));
    }
    const double rest_gain = remainder_gain(p.separation_cycles);
    const auto zone_begin = static_cast<double>(zone.first);
    const auto zone_end = static_cast<double>(zone.last);
    for (const Run& run : shifted_runs(frames)) {
        const double left =
            run.first == 0 ? zone_begin
                           : std::max(frames[run.first].position - 0.5 * hop_samples, zone_begin);
        const double right =
            run.last + 1 == frames.size()
                ? zone_end
                : std::min(frames[run.last].position + 0.5 * hop_samples, zone_end);
        if (right > left) {
            synthesise_run(x, PeriodCurve{frames, run, zone.rate}, zone.ratio, left, right,
                           p.separation_cycles, periodic, acc);
        }
    }
    const std::vector<double> y = run_signal(acc);
    const auto zone_offset = static_cast<std::size_t>(zone.first - zone.ext_first);
    for (std::size_t n = 0; n < samples.size(); ++n) {
        const std::size_t idx = n + zone_offset;
        if (w[n] > 0.0 && acc.window_sum[idx] > k_min_window_sum) {
            const double q = 0.5 * std::numbers::pi * w[n];
            // What the periodic average left out (zero wherever it was not computed) stays.
            const double rest = rest_gain * (x.at(zone.first + static_cast<std::int64_t>(n)) -
                                             static_cast<double>(periodic[idx]));
            samples[n] = static_cast<float>(std::cos(q) * static_cast<double>(samples[n]) +
                                            std::sin(q) * (y[idx] + rest));
        }
    }
}

}  // namespace

PsolaEngine::PsolaEngine(PsolaEngineParams params) : params_{params} {}

std::string PsolaEngine::settings_key() const {
    const auto pattern = [](double v) { return hex64(std::bit_cast<std::uint64_t>(v)); };
    return "v2;classes=" + hex64(params_.shifted_classes.bits()) +
           ";fade=" + pattern(params_.class_edge_fade.value()) +
           ";max_shift=" + pattern(params_.max_shift.value()) +
           ";sep=" + std::to_string(params_.separation_cycles);
}

Result<OutputAudio> PsolaEngine::render_note(const AudioSource& src, const Note& note,
                                             const EffectiveNote& eff, const TimeMap& time_map,
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
        return fail(ErrorCode::invalid_argument, "PsolaEngine: a negative range");
    }
    const double shift_cents = (eff.center - note.pitch_center).value();
    if (std::abs(shift_cents) > params_.max_shift.value()) {
        return fail(ErrorCode::not_implemented,
                    "PsolaEngine: the shift is larger than max_shift (use another engine)");
    }
    auto shift_samples = zone_shift(time_map, rate, out_range, "PsolaEngine");
    if (!shift_samples) {
        return Unexpected{shift_samples.error()};
    }
    const double sr = rate.value();
    const auto context =
        static_cast<std::int64_t>(std::ceil(k_context_periods * k_max_period_s * sr));
    const Samples total =
        to_sample_index(OutputTime{} + time_map.output_length(), rate, Rounding::nearest);
    const std::int64_t ext_first = std::max<std::int64_t>(first.value() - context, 0);
    const std::int64_t ext_last = std::min<std::int64_t>(last.value() + context, total.value());
    auto ext = read_through_map(src, time_map, Samples{ext_first}, Samples{ext_last - ext_first});
    if (!ext) {
        return Unexpected{ext.error()};
    }
    auto result = AudioBuffer::create(1, last - first, rate);
    if (!result) {
        return Unexpected{result.error()};
    }
    const auto zone_offset = static_cast<std::size_t>(first.value() - ext_first);
    std::ranges::copy_n(ext->channel(0).begin() + static_cast<std::ptrdiff_t>(zone_offset),
                        (last - first).value(), result->channel(0).begin());
    if (std::abs(shift_cents) > k_neutral_snap_offset) {
        const Zone zone{.first = first.value(),
                        .last = last.value(),
                        .ext_first = ext_first,
                        .ext_last = ext_last,
                        .shift_samples = *shift_samples,
                        .rate = sr,
                        .ratio = std::exp2(shift_cents / 1200.0)};
        respace_zone(Signal{ext->channel(0), ext_first}, note, zone, params_, result->channel(0));
    }
    return OutputAudio{.audio = std::move(*result),
                       .start = to_time_point<OutputAxis>(first, rate)};
}

}  // namespace anno
