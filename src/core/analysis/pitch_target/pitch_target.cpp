#include "core/analysis/pitch_target/pitch_target.h"

#include "core/analysis/pitch_target/detail/ornament.h"
#include "core/analysis/pitch_target/detail/reliability.h"
#include "core/analysis/pitch_target/detail/tonal_segments.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace anno {

namespace {

constexpr std::size_t k_edge_frames = 3;                // frames averaged at a segment end
constexpr double k_vibrato_min_crossings_per_s = 5.0;   // zero crossings per second of a
constexpr double k_vibrato_max_crossings_per_s = 20.0;  // 3-9 Hz oscillation: 6-18, with margin

/// The vibrato period of a modulation curve, only if it really oscillates: the autocorrelation
/// test of vibrato_period() also accepts a step or a slow wander, so the curve must cross its mean
/// at a vibrato rate as well.
std::optional<Seconds> detect_vibrato(std::span<const double> mod, Seconds hop) {
    const auto period = vibrato_period(mod, hop);
    if (!period) {
        return std::nullopt;
    }
    double mean = 0.0;
    for (const double v : mod) {
        mean += v;
    }
    mean /= static_cast<double>(mod.size());
    int crossings = 0;
    for (std::size_t i = 1; i < mod.size(); ++i) {
        crossings += ((mod[i - 1] - mean) * (mod[i] - mean) < 0.0) ? 1 : 0;
    }
    const double rate = crossings / (static_cast<double>(mod.size()) * hop.value());
    if (rate < k_vibrato_min_crossings_per_s || rate > k_vibrato_max_crossings_per_s) {
        return std::nullopt;
    }
    return period;
}

bool positive(double v) {
    return std::isfinite(v) && v > 0.0;
}

bool unit_interval(double v) {
    return std::isfinite(v) && v >= 0.0 && v <= 1.0;
}

bool reliability_ok(const ReliabilityParams& r) {
    return positive(r.consonant_zone.value()) && unit_interval(r.min_reliability) &&
           r.min_reliability < 1.0 && positive(r.creak_min_stretch.value()) &&
           positive(r.creak_tolerance.value()) && unit_interval(r.unpitched_voicing);
}

bool segmenting_ok(const SegmentingParams& s) {
    return positive(s.segment_smoothing.value()) && positive(s.turn_threshold.value()) &&
           positive(s.slope_merge_cents_per_s) && positive(s.plateau_merge.value()) &&
           positive(s.min_glide_extent.value()) && positive(s.glissando_g) &&
           positive(s.transient_window.value()) && positive(s.glissando_max_duration.value()) &&
           positive(s.step_min_extent.value()) && positive(s.zone_max_share) &&
           s.zone_max_share <= 0.5;
}

bool target_ok(const TargetParams& t) {
    return positive(t.weights.rate_scale_cents_per_s) && std::isfinite(t.weights.gamma) &&
           t.weights.gamma >= 0.0 && positive(t.weights.amp_range.value()) &&
           std::isfinite(t.end_bias) && t.end_bias >= 0.0 && positive(t.min_core.value()) &&
           positive(t.core_floor.value()) && positive(t.short_note.value()) &&
           positive(t.glide_min_duration.value()) && positive(t.multi_min_separation.value()) &&
           positive(t.overshoot_min.value()) && positive(t.preparation_min.value()) &&
           t.preparation_max.value() > t.preparation_min.value() &&
           positive(t.preparation_window.value()) && positive(t.legato_gap.value()) &&
           positive(t.legato_match.value()) && positive(t.ambiguous_low.value()) &&
           t.ambiguous_high.value() > t.ambiguous_low.value() &&
           positive(t.register_break_min.value()) && positive(t.register_break_max_step.value());
}

bool ornament_ok(const OrnamentParams& o) {
    return positive(o.min_extent.value()) && unit_interval(o.flat_top_share) &&
           o.flat_top_share > 0.0 && positive(o.still_cents_per_s) &&
           positive(o.min_plateau.value()) && o.min_transitions >= 1 && o.gamaka_min_segments >= 1;
}

bool take_ok(const TakeParams& k) {
    return positive(k.phrase_gap.value()) && positive(k.run_max_note.value()) &&
           k.run_min_notes >= 1 && positive(k.run_max_gap.value()) && k.off_grid_min_count >= 1 &&
           positive(k.off_grid_spread.value()) && positive(k.off_grid_min_offset.value()) &&
           positive(k.peak_tolerance.value()) && positive(k.peak_min_offset.value()) &&
           k.peak_tolerance.value() > k.peak_min_offset.value() && k.peak_min_phrase_notes >= 1;
}

struct FrameRange {
    std::size_t first = 0;  // inclusive
    std::size_t last = 0;   // inclusive
    [[nodiscard]] std::size_t frames() const { return last - first + 1; }
};

double mean_of(const std::vector<double>& v, std::size_t first, std::size_t count) {
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += v[first + i];
    }
    return sum / static_cast<double>(count);
}

double edge_start(const std::vector<double>& v, const Seg& s) {
    return mean_of(v, s.first, std::min(k_edge_frames, s.frames()));
}

double edge_end(const std::vector<double>& v, const Seg& s) {
    const std::size_t count = std::min(k_edge_frames, s.frames());
    return mean_of(v, s.last + 1 - count, count);
}

/// The weighted pitch of `range`: the weighted mean over whole vibrato cycles when a vibrato is
/// found in the note, else the weighted median (research item 16: more robust than the mean).
double weighted_pitch(const Note& note, const std::vector<double>& curve,
                      const std::vector<double>& stability, const std::vector<double>& reliability,
                      double min_reliability, double end_bias, FrameRange range) {
    const double hop_s = note.grid.hop.value();
    std::vector<double> mod(note.pitch_mod.size());
    std::ranges::transform(note.pitch_mod, mod.begin(), [](Cents c) { return c.value(); });
    const std::span<const double> mod_range{mod.data() + range.first, range.frames()};
    const auto period = detect_vibrato(mod_range, note.grid.hop);
    std::size_t last = range.last;
    if (period) {
        const double frames = period->value() / hop_s;
        const double cycles = std::floor(static_cast<double>(range.frames()) / frames);
        if (cycles >= 1.0) {
            last = range.first + static_cast<std::size_t>(std::lround(cycles * frames)) - 1;
        }
    }
    std::vector<double> values;
    std::vector<double> weights;
    const auto length = static_cast<double>(last - range.first + 1);
    for (std::size_t i = range.first; i <= last; ++i) {
        const double u = static_cast<double>(i - range.first) / length;
        const double r = reliability[i] >= min_reliability ? reliability[i] : 0.0;
        values.push_back(curve[i]);
        weights.push_back(r * stability[i] * (1.0 + end_bias * u));
    }
    double total = 0.0;
    for (const double w : weights) {
        total += w;
    }
    if (total <= 0.0) {
        std::ranges::sort(values);
        return values[values.size() / 2];
    }
    if (period) {
        double sum = 0.0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            sum += values[i] * weights[i];
        }
        return sum / total;
    }
    return weighted_median(values, weights);
}

SourceTime frame_time(const Note& note, std::size_t frame) {
    return note.grid.start + Seconds{static_cast<double>(frame) * note.grid.hop.value()};
}

SourceRange range_of(const Note& note, FrameRange r) {
    // Frame times can sit an epsilon outside [start, end) (note_grid's rounding): clamp.
    const SourceTime start =
        std::clamp(frame_time(note, r.first), note.source_start, note.source_end);
    const SourceTime end = std::clamp(frame_time(note, r.last + 1), start, note.source_end);
    return SourceRange::make(start, end).value();
}

/// Maximal runs of consecutive level segments, as frame ranges.
std::vector<FrameRange> level_runs(const std::vector<Seg>& segs) {
    std::vector<FrameRange> runs;
    for (const Seg& s : segs) {
        if (s.moving) {
            continue;
        }
        if (!runs.empty() && runs.back().last + 1 == s.first) {
            runs.back().last = s.last;
        } else {
            runs.push_back({.first = s.first, .last = s.last});
        }
    }
    return runs;
}

int sign_of(double v) {
    if (v > 0.0) {
        return 1;
    }
    return v < 0.0 ? -1 : 0;
}

ApproachKind approach_of(const std::vector<double>& smoothed, std::size_t core_first,
                         double core_pitch, const NoteContext& ctx, const TargetParams& tp) {
    if (core_first == 0) {
        return ApproachKind::none;
    }
    const double start = smoothed[0];
    if (ctx.previous_pitch && ctx.gap_before < tp.legato_gap &&
        std::abs(start - ctx.previous_pitch->value()) <= tp.legato_match.value()) {
        return ApproachKind::legato;
    }
    const int direction = sign_of(core_pitch - start);
    if (direction != 0) {
        double past = 0.0;
        for (std::size_t i = 0; i < core_first; ++i) {
            past = std::max(past, direction * (smoothed[i] - core_pitch));
        }
        if (past >= tp.overshoot_min.value()) {
            return ApproachKind::overshoot;
        }
    }
    return ApproachKind::scoop;
}

/// Counts the frames at the end of the note, before `last`, over which the curve stays on the
/// opposite side of the core pitch from the next note by at least preparation_min.
std::size_t preparation_frames(const std::vector<double>& smoothed, std::size_t last,
                               double core_pitch, const NoteContext& ctx, const TargetParams& tp,
                               double hop_s) {
    if (!ctx.next_pitch || ctx.gap_after >= tp.legato_gap) {
        return 0;
    }
    const int toward_next = sign_of(ctx.next_pitch->value() - core_pitch);
    if (toward_next == 0) {
        return 0;
    }
    const auto window =
        static_cast<std::size_t>(std::max(1L, std::lround(tp.preparation_window.value() / hop_s)));
    const std::size_t from = last + 1 > window ? last + 1 - window : 0;
    double dip = 0.0;
    for (std::size_t i = from; i <= last; ++i) {
        dip = std::max(dip, -toward_next * (smoothed[i] - core_pitch));
    }
    return (dip >= tp.preparation_min.value() && dip <= tp.preparation_max.value())
               ? last + 1 - from
               : 0;
}

/// Everything the steps of estimate_pitch_target share about one note.
struct Work {
    // Non-owning views that outlive the call (never null).
    const Note* note = nullptr;
    const PitchTargetParams* params = nullptr;
    const NoteContext* context = nullptr;
    std::size_t n = 0;
    double hop_s = 0.0;
    std::vector<double> curve;  // pitch_center + drift + mod, absolute cents
    Reliability rel;
    std::vector<double> stability;  // weights of the target (see TargetParams::weights)
    std::vector<double> smoothed;   // the curve the segments are cut from
    std::vector<Seg> segs;
    std::size_t reliable_frames = 0;

    [[nodiscard]] double frames_s(std::size_t frames) const {
        return static_cast<double>(frames) * hop_s;
    }
    [[nodiscard]] double pitch_of(FrameRange r) const {
        return weighted_pitch(*note, curve, stability, rel.weight,
                              params->reliability.min_reliability, params->target.end_bias, r);
    }
    [[nodiscard]] FrameRange all() const { return {.first = 0, .last = n - 1}; }
};

/// Cuts the smoothed curve into level and moving segments and records them in `out`.
void build_segments(Work& w, PitchTarget& out) {
    const SegmentingParams& sp = w.params->segmenting;
    const auto window =
        static_cast<std::size_t>(std::max(1L, std::lround(sp.segment_smoothing.value() / w.hop_s)));
    // A vibrato would be split into half-cycle "moving" segments; segment its slow part instead
    // (centre + drift), which the decomposition already separated from the modulation.
    std::vector<double> mod_values(w.n);
    std::ranges::transform(w.note->pitch_mod, mod_values.begin(),
                           [](Cents c) { return c.value(); });
    std::vector<double> segmenting = w.curve;
    if (detect_vibrato(mod_values, w.note->grid.hop)) {
        for (std::size_t i = 0; i < w.n; ++i) {
            segmenting[i] = w.note->pitch_center.value() + w.note->pitch_drift[i].value();
        }
    }
    w.smoothed =
        smooth_curve(segmenting, w.rel.weight, w.params->reliability.min_reliability, window);
    w.segs = split_curve(w.smoothed, w.note->grid.hop, sp);
    label_segments(w.segs, w.note->grid.hop, w.n, sp);
    for (const Seg& s : w.segs) {
        out.segments.push_back({.range = range_of(*w.note, {.first = s.first, .last = s.last}),
                                .shape = s.moving ? TonalShape::moving : TonalShape::level,
                                .start_pitch = Cents{edge_start(w.smoothed, s)},
                                .end_pitch = Cents{edge_end(w.smoothed, s)}});
    }
    out.target_whole = Cents{w.pitch_of(w.all())};
    out.target_end = Cents{edge_end(w.smoothed, w.segs.back())};
}

/// Kind, core and secondary pitch from the level/moving structure. Returns the core frames.
FrameRange classify(const Work& w, PitchTarget& out) {
    const TargetParams& tp = w.params->target;
    if (!std::ranges::any_of(w.segs, [](const Seg& s) { return s.moving; })) {
        out.kind = PitchTargetKind::whole;
        return w.all();
    }
    std::vector<FrameRange> runs = level_runs(w.segs);
    std::erase_if(
        runs, [&](const FrameRange& r) { return w.frames_s(r.frames()) < tp.core_floor.value(); });
    if (runs.empty()) {
        const double net = std::abs(w.smoothed[w.n - 1] - w.smoothed[0]);
        const bool going_nowhere = net < w.params->segmenting.step_min_extent.value() ||
                                   w.frames_s(w.n) < tp.glide_min_duration.value();
        if (going_nowhere) {  // a wobbling short note is judged as a whole
            out.kind = PitchTargetKind::whole;
        } else {  // moving only: end target, start in secondary
            out.kind = PitchTargetKind::glide;
            out.target = Cents{edge_end(w.smoothed, w.segs.back())};
            out.secondary = Cents{edge_start(w.smoothed, w.segs.front())};
            out.target_settled = out.target_whole;
        }
        return w.all();
    }
    const auto longest =
        std::ranges::max_element(runs, {}, [](const FrameRange& r) { return r.frames(); });
    FrameRange core = *longest;
    out.kind = PitchTargetKind::settled;
    // Another core of about the same pitch is one core; a clearly different one is `multi`.
    const double main_pitch = w.pitch_of(core);
    for (const FrameRange& r : runs) {
        if (&r == &*longest) {
            continue;
        }
        const double other = w.pitch_of(r);
        if (std::abs(other - main_pitch) >= tp.multi_min_separation.value()) {
            out.kind = PitchTargetKind::multi;
            out.secondary = Cents{other};
        } else {
            core.first = std::min(core.first, r.first);
            core.last = std::max(core.last, r.last);
        }
    }
    return core;
}

/// Target, approach and release of a note with a core (everything but a glide). Returns the core
/// after a preparation was cut off its end.
FrameRange settle_core(const Work& w, FrameRange core, PitchTarget& out) {
    const TargetParams& tp = w.params->target;
    double pitch = w.pitch_of(core);
    // A dip at the end opposite to the next interval is a preparation, not part of the pitch.
    std::size_t prep = 0;
    if (out.kind == PitchTargetKind::whole || out.kind == PitchTargetKind::settled) {
        prep = preparation_frames(w.smoothed, core.last, pitch, *w.context, tp, w.hop_s);
        if (prep > 0 && core.frames() > prep + 3) {
            core.last -= prep;
            pitch = w.pitch_of(core);
            out.kind = PitchTargetKind::settled;
            out.release = ReleaseKind::preparation;
        } else {
            prep = 0;
        }
    }
    out.target = Cents{pitch};
    out.target_settled = out.target;
    out.approach = approach_of(w.smoothed, core.first, pitch, *w.context, tp);
    if (prep == 0 && core.last + 1 < w.n) {
        out.release = w.smoothed[w.n - 1] - pitch > 0.0 ? ReleaseKind::rise : ReleaseKind::fall;
    }
    out.core = range_of(*w.note, core);
    return core;
}

/// Mean reliability of the core, scaled by the share of reliable frames and by how long the core
/// is.
double confidence_of(const Work& w, FrameRange core, bool short_note) {
    double sum = 0.0;
    for (std::size_t i = core.first; i <= core.last; ++i) {
        sum += w.rel.weight[i];
    }
    double conf = sum / static_cast<double>(core.frames());
    conf *= static_cast<double>(w.reliable_frames) / static_cast<double>(w.n);
    conf *= std::min(1.0, w.frames_s(core.frames()) / (3.0 * w.params->target.min_core.value()));
    if (short_note) {
        conf = std::min(conf, 0.55);
    }
    return std::clamp(conf, 0.0, 1.0);
}

/// Distance (cents) from `pitch` to the nearest grid line (semitone, A4 = 6900 shifted by the
/// tuning offset); in (-50, 50], positive when the pitch is sharp.
double grid_offset(double pitch_cents, double tuning_offset_cents) {
    const double rel = pitch_cents - tuning_offset_cents;
    return rel - 100.0 * std::round(rel / 100.0);
}

/// Flags decided from the note alone: `ambiguous_intent` (between two grid lines) and
/// `register_break` (two plateaus a large, abrupt step apart).
void set_note_flags(const Work& w, PitchTarget& out) {
    const TargetParams& tp = w.params->target;
    const bool has_target =
        out.kind == PitchTargetKind::whole || out.kind == PitchTargetKind::settled ||
        out.kind == PitchTargetKind::glide || out.kind == PitchTargetKind::ornament;
    if (has_target) {
        const double off =
            std::abs(grid_offset(out.target.value(), w.params->tuning_offset.value()));
        if (off >= tp.ambiguous_low.value() && off <= tp.ambiguous_high.value()) {
            out.flags.set(PitchFlag::ambiguous_intent);
        }
    }
    if (out.kind == PitchTargetKind::multi && out.secondary &&
        std::abs(out.secondary->value() - out.target.value()) >= tp.register_break_min.value()) {
        const bool abrupt = std::ranges::any_of(w.segs, [&](const Seg& s) {
            return s.moving &&
                   w.frames_s(s.frames()) <= tp.register_break_max_step.value() + w.hop_s;
        });
        if (abrupt) {
            out.flags.set(PitchFlag::register_break);
        }
    }
}

/// Many turning points and no level stretch long enough to be a core (style `gamaka`, provisional).
bool is_gamaka(const Work& w) {
    if (std::cmp_less(w.segs.size(), w.params->ornament.gamaka_min_segments)) {
        return false;
    }
    const std::vector<FrameRange> runs = level_runs(w.segs);
    return std::ranges::none_of(runs, [&](const FrameRange& r) {
        return w.frames_s(r.frames()) >= w.params->target.min_core.value();
    });
}

double furthest_from(const std::vector<double>& curve, double centre) {
    return *std::ranges::max_element(
        curve, [&](double a, double b) { return std::abs(a - centre) < std::abs(b - centre); });
}

bool evidence_matches(const Note& note, const NoteEvidence& evidence, std::size_t n) {
    return evidence.pitch != nullptr && n > 0 && note.pitch_drift.size() == n &&
           note.pitch_mod.size() == n &&
           (evidence.disagreement.empty() || evidence.disagreement.size() == n) &&
           evidence.pitch->f0.size() == evidence.pitch->voicing_prob.size();
}

}  // namespace

Result<PitchTarget> estimate_pitch_target(const Note& note, const NoteEvidence& evidence,
                                          const NoteContext& context,
                                          const PitchTargetParams& params) {
    if (auto r = validate(params); !r) {
        return Unexpected{r.error()};
    }
    const auto n = static_cast<std::size_t>(std::max<std::int64_t>(note.grid.n_frames, 0));
    if (!evidence_matches(note, evidence, n)) {
        return fail(
            ErrorCode::invalid_argument,
            "estimate_pitch_target: the note's curves and the evidence must match its grid");
    }
    Work w;
    w.note = &note;
    w.params = &params;
    w.context = &context;
    w.n = n;
    w.hop_s = note.grid.hop.value();
    w.curve.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        w.curve[i] =
            note.pitch_center.value() + note.pitch_drift[i].value() + note.pitch_mod[i].value();
    }
    w.rel = frame_reliability(note, evidence, w.curve, params.reliability);

    PitchTarget out;  // starts as `unpitched` at the note's centre
    out.target = note.pitch_center;
    if (w.rel.creak) {
        out.flags.set(PitchFlag::creak);
    }
    if (w.rel.median_voicing < params.reliability.unpitched_voicing) {
        return out;  // breathy / spoken / no periodicity: no target, never corrected
    }

    std::vector<Cents> curve_cents(n);
    std::ranges::transform(w.curve, curve_cents.begin(), [](double c) { return Cents{c}; });
    w.stability = stability_weights(curve_cents, note.grid.hop, note.amp, params.target.weights);
    const double min_rel = params.reliability.min_reliability;
    w.reliable_frames = static_cast<std::size_t>(
        std::ranges::count_if(w.rel.weight, [&](double v) { return v >= min_rel; }));
    const bool short_note = w.frames_s(w.reliable_frames) < params.target.short_note.value();
    if (short_note) {
        out.flags.set(PitchFlag::short_note);
    }
    if (w.reliable_frames < 3 || n < 3) {
        out.kind = PitchTargetKind::whole;
        out.target = Cents{w.pitch_of(w.all())};
        out.core = range_of(note, w.all());
        out.confidence_prob = 0.2;
        out.target_whole = out.target;
        return out;
    }

    if (const auto orn =
            find_ornament(w.curve, w.rel.weight, min_rel, note.grid.hop, params.ornament)) {
        out.kind = PitchTargetKind::ornament;  // never averaged: both levels are the targets
        out.target = Cents{orn->main_cents};
        out.secondary = Cents{orn->secondary_cents};
        out.core = range_of(note, w.all());
        out.target_whole = Cents{w.pitch_of(w.all())};
        set_note_flags(w, out);
        out.confidence_prob = std::clamp(orn->flat_share * static_cast<double>(w.reliable_frames) /
                                             static_cast<double>(n),
                                         0.0, 1.0);
        return out;
    }
    build_segments(w, out);
    if (params.ornament.detect_gamaka && is_gamaka(w)) {
        out.kind = PitchTargetKind::ornament;  // the movement is the note: the median is the target
        out.target = Cents{w.pitch_of(w.all())};
        out.secondary = Cents{furthest_from(w.smoothed, out.target.value())};
        out.core = range_of(note, w.all());
        out.confidence_prob = confidence_of(w, w.all(), short_note);
        return out;
    }
    FrameRange core = classify(w, out);
    if (out.kind == PitchTargetKind::glide) {
        out.core = range_of(note, core);
    } else {
        core = settle_core(w, core, out);
    }
    out.confidence_prob = confidence_of(w, core, short_note);
    set_note_flags(w, out);
    return out;
}

Result<void> validate(const PitchTargetParams& p) {
    if (!reliability_ok(p.reliability) || !segmenting_ok(p.segmenting) || !target_ok(p.target) ||
        !ornament_ok(p.ornament) || !take_ok(p.take) || !std::isfinite(p.tuning_offset.value())) {
        return fail(ErrorCode::invalid_argument, "pitch target: invalid parameters");
    }
    return {};
}

PitchTargetParams pitch_target_params_for(StylePreset preset) {
    const StyleDefaults style = style_defaults(preset);
    PitchTargetParams p;
    p.segmenting.glissando_g = style.glissando_g;
    p.ornament.detect_gamaka = style.detect_gamaka;
    return p;
}

}  // namespace anno
