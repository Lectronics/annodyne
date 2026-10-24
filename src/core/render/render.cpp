#include "core/render/render.h"

#include "core/dsp/crossfade.h"
#include "core/edits/effective_gap.h"
#include "core/edits/effective_state.h"
#include "core/render/blend_plan.h"
#include "core/render/level.h"
#include "core/render/render_key.h"
#include "core/render/splice.h"
#include "core/synthesis/engine/engine_support.h"
#include "core/synthesis/engine/source_through_map.h"
#include "core/timemap/build_time_map.h"
#include "core/timemap/layout.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>

namespace anno {

namespace {

/// Everything planned before any audio is read.
struct Plan {
    std::int64_t frame_count = 0;  // output length of the map, samples
    Hz sample_rate;
    std::int64_t region_begin = 0;
    std::int64_t region_end = 0;
    std::int64_t margin = 0;
    std::vector<EditedNote> edited;
    std::vector<Ramp> ramps;
    std::vector<GateInput> gates;  // edited gaps (ADR-0024), in timeline order
};

/// An engine's output for one note's zone (clamped to the recording).
struct Zone {
    std::int64_t begin = 0;
    std::int64_t end = 0;
    std::optional<AudioBuffer> audio;  // empty while the zone does not meet the region
    bool identical = false;            // equal to the source over the zone, bit for bit
};

std::int64_t index_of(OutputTime t, Hz rate) {
    return to_sample_index(t, rate, Rounding::nearest).value();
}

Result<void> validate_inputs(const RenderInputs& in, const RenderParams& params) {
    if (in.analysis == nullptr || in.edits == nullptr) {
        return fail(ErrorCode::invalid_argument, "render_region: analysis and edits are required");
    }
    if (!(in.analysis->source_id == in.source.id())) {
        return fail(ErrorCode::invalid_argument,
                    "render_region: analysis belongs to another source");
    }
    if (params.crossfade_length < Seconds{0.0} || params.crossfade_length > k_max_crossfade) {
        return fail(ErrorCode::invalid_argument,
                    "render_region: crossfade_length must be in [0, k_max_crossfade]");
    }
    if (auto r = validate(*in.analysis); !r) {
        return fail(ErrorCode::invalid_argument,
                    "render_region: invalid analysis: " + r.error().message);
    }
    if (in.time_map != nullptr) {
        const double duration =
            to_seconds(in.source.frame_count(), in.source.sample_rate()).value();
        if (std::abs(in.time_map->source_length().value() - duration) *
                in.source.sample_rate().value() >
            0.5) {
            return fail(ErrorCode::invalid_argument,
                        "render_region: the TimeMap's source length does not match the recording");
        }
    }
    return {};
}

struct ResolvedMap {
    std::shared_ptr<const TimeMap> map;
    int repaired_notes = 0;
    bool layout_used = false;
};

/// The layout's parameters for this render: the note margin is the render's crossfade length.
LayoutParams layout_of(const RenderParams& params) {
    LayoutParams layout = params.layout;
    layout.note_margin = params.crossfade_length;
    return layout;
}

/// The track's map: the supplied one; else, with a segment timeline, the layout of the edits
/// (ADR-0025); else built from the edits with the tail shifted. Shifts are whole samples
/// (ADR-0023).
Result<ResolvedMap> resolve_time_map(const RenderInputs& in, const RenderParams& params) {
    if (in.time_map != nullptr) {
        return ResolvedMap{.map = in.time_map};
    }
    if (in.source.frame_count().value() == 0) {
        return ResolvedMap{.map = std::make_shared<const TimeMap>(TimeMap::identity(Seconds{0.0}))};
    }
    const Hz rate = in.source.sample_rate();
    const SourceTime end = to_time_point<SourceAxis>(in.source.frame_count(), rate);
    TimeMapParams tm{.source_end = end, .tail = TailPolicy::shift, .sample_rate = rate};
    Result<TimeMapBuild> built = fail(ErrorCode::internal, "render_region: no time map built");
    bool layout_used = false;
    if (in.analysis->timeline) {
        const LayoutParams layout = layout_of(params);
        auto step = shift_step_samples(rate, layout.grid);
        if (!step) {
            return Unexpected{step.error()};
        }
        tm.shift_step = *step;
        const SegmentTimeline& timeline = *in.analysis->timeline;
        if (std::abs((timeline.source_end() - end).value()) * rate.value() > 0.5) {
            return fail(
                ErrorCode::invalid_argument,
                "render_region: the segment timeline does not match the recording's length");
        }
        built = build_layout_time_map(in.analysis->notes, timeline, *in.edits, layout, tm);
        layout_used = layout.mode == LayoutMode::absorb_then_stretch;
    } else {
        built = build_time_map(in.analysis->notes, *in.edits, tm);
    }
    if (!built) {
        return Unexpected{built.error()};
    }
    return ResolvedMap{.map = std::make_shared<const TimeMap>(std::move(built->map)),
                       .repaired_notes = built->repaired_notes,
                       .layout_used = layout_used};
}

constexpr double k_slope_one_tolerance = 1e-9;

/// True if `span` is a warp whose slope is not 1.
bool is_stretched(const TimeSpan& span) {
    if (span.kind != SpanKind::warp) {
        return false;
    }
    const Seconds out_len = span.output_end - span.output_start;
    const Seconds src_len = span.source_end - span.source_start;
    return std::abs((out_len - src_len).value()) > k_slope_one_tolerance;
}

/// True if a cut or insert of `map` lies in or touches output samples [begin, end].
bool meets_gap_edit(const TimeMap& map, Hz rate, std::int64_t begin, std::int64_t end) {
    return std::ranges::any_of(map.spans(), [&](const TimeSpan& span) {
        return span.kind != SpanKind::warp && index_of(span.output_start, rate) <= end &&
               index_of(span.output_end, rate) >= begin;
    });
}

/// True if a stretched warp of `map` covers any output sample of [begin, end).
bool meets_stretch(const TimeMap& map, Hz rate, std::int64_t begin, std::int64_t end) {
    return std::ranges::any_of(map.spans(), [&](const TimeSpan& span) {
        return is_stretched(span) && index_of(span.output_start, rate) < end &&
               index_of(span.output_end, rate) > begin;
    });
}

Result<std::pair<std::int64_t, std::int64_t>> snap_region(const RenderInputs& in,
                                                          const TimeMap& map, OutputRange region) {
    const Hz rate = in.source.sample_rate();
    const std::int64_t begin = to_sample_index(region.start(), rate, Rounding::nearest).value();
    const std::int64_t end = to_sample_index(region.end(), rate, Rounding::nearest).value();
    if (begin < 0 || end > to_samples(map.output_length(), rate, Rounding::nearest).value()) {
        return fail(ErrorCode::out_of_range, "render_region: region lies outside the output");
    }
    return std::pair{begin, std::max(begin, end)};
}

/// The gap edits of the list: every gap id a live gap-category edit targets must be a gap of the
/// analysis's timeline (an orphaned id is ignored, as for notes). Edited gaps come back in
/// timeline order with their silence regions on the output axis.
Result<std::vector<GateInput>> plan_gates(const RenderInputs& in, const TimeMap& map, Hz rate) {
    std::set<SegmentId> targeted;
    for (const Edit& edit : in.edits->active()) {
        if (edit.spec.category != EditCategory::gap) {
            continue;
        }
        for (const SegmentId& id : edit.spec.target.segment_ids) {
            if (id.kind == SegmentKind::gap) {
                targeted.insert(id);
            }
        }
    }
    std::vector<GateInput> gates;
    if (targeted.empty()) {
        return gates;
    }
    const auto& timeline = in.analysis->timeline;
    for (const SegmentId& id : targeted) {
        const bool known = timeline && std::ranges::any_of(timeline->gaps(), [&](const Gap& g) {
                               return g.id == id;
                           });
        if (!known) {
            return fail(ErrorCode::invalid_argument,
                        "render_region: gap edit targets '" + id.value +
                            (timeline ? "', which is not a gap of the segment timeline"
                                      : "', but the analysis has no segment timeline"));
        }
    }
    for (const Gap& gap : timeline->gaps()) {
        if (!targeted.contains(gap.id)) {
            continue;
        }
        const EffectiveGap eff = effective_gap(gap.id, *in.edits);
        if (!eff.is_edited) {
            continue;
        }
        auto spans = gated_spans(gap, map, rate);
        if (!spans) {
            return Unexpected{spans.error()};
        }
        gates.push_back({.gap = &gap, .eff = eff, .spans = std::move(*spans)});
    }
    return gates;
}

bool gate_meets_region(const GateInput& g, std::int64_t begin, std::int64_t end) {
    return std::ranges::any_of(g.spans,
                               [&](const GatedSpan& s) { return s.first < end && s.last > begin; });
}

Result<Plan> make_plan(const RenderInputs& in, const TimeMap& map, const RenderParams& params,
                       std::pair<std::int64_t, std::int64_t> region) {
    Plan plan;
    plan.frame_count =
        to_samples(map.output_length(), in.source.sample_rate(), Rounding::nearest).value();
    plan.sample_rate = in.source.sample_rate();
    plan.region_begin = region.first;
    plan.region_end = region.second;
    plan.margin = to_samples(params.crossfade_length, plan.sample_rate, Rounding::nearest).value();
    for (const Note& note : in.analysis->notes) {
        if (!note.is_active) {
            continue;
        }
        EffectiveNote eff = effective_state(note, *in.edits);
        if (!eff.is_edited || only_timing_edited(eff, note.pitch_center)) {
            continue;  // timing edits live in the map: the unedited base renders them
        }
        if (note.source_start < SourceTime{} || note.source_end <= SourceTime{} ||
            note.source_end > SourceTime{} + map.source_length()) {
            return fail(ErrorCode::invalid_argument,
                        "render_region: note '" + note.id.value + "' lies outside the recording");
        }
        const auto out_start = map.to_output(note.source_start);
        const auto out_end = map.to_output_end(note.source_end);
        if (!out_start || !out_end) {
            return fail(ErrorCode::invalid_argument,
                        "render_region: note '" + note.id.value + "' starts or ends inside a cut");
        }
        const std::int64_t begin =
            std::clamp<std::int64_t>(index_of(*out_start, plan.sample_rate), 0, plan.frame_count);
        const std::int64_t end =
            std::clamp<std::int64_t>(index_of(*out_end, plan.sample_rate), begin, plan.frame_count);
        const std::int64_t zone_lo = std::max<std::int64_t>(0, begin - plan.margin);
        const std::int64_t zone_hi = std::min(plan.frame_count, end + plan.margin);
        if (meets_stretch(map, plan.sample_rate, zone_lo, zone_hi)) {
            return fail(ErrorCode::not_implemented,
                        "render_region: note '" + note.id.value +
                            "' has a pitch, level or formant edit and its zone meets a stretched "
                            "part of the time map (stretch combined with another edit is not "
                            "implemented yet)");
        }
        if (meets_gap_edit(map, plan.sample_rate, zone_lo, zone_hi)) {
            return fail(ErrorCode::not_implemented,
                        "render_region: note '" + note.id.value +
                            "' has a pitch, level or formant edit and its zone meets a cut or "
                            "insert of the layout (combining them is not implemented yet)");
        }
        plan.edited.push_back(
            {.note = &note, .eff = std::move(eff), .span = {.begin = begin, .end = end}});
    }
    std::vector<SampleSpan> spans;
    spans.reserve(plan.edited.size());
    for (const EditedNote& e : plan.edited) {
        spans.push_back(e.span);
    }
    plan.ramps = plan_blend(spans, plan.margin);
    auto gates = plan_gates(in, map, plan.sample_rate);
    if (!gates) {
        return Unexpected{gates.error()};
    }
    plan.gates = std::move(*gates);
    return plan;
}

std::int64_t zone_begin(const Plan& plan, const EditedNote& e) {
    return std::max<std::int64_t>(0, e.span.begin - plan.margin);
}
std::int64_t zone_end(const Plan& plan, const EditedNote& e) {
    return std::min(plan.frame_count, e.span.end + plan.margin);
}
bool zone_meets_region(const Plan& plan, const EditedNote& e) {
    return zone_begin(plan, e) < plan.region_end && zone_end(plan, e) > plan.region_begin &&
           zone_begin(plan, e) < zone_end(plan, e);
}

bool same_samples(const AudioBuffer& a, const AudioBuffer& b) {
    for (int c = 0; c < a.channel_count(); ++c) {
        const auto x = a.channel(c);
        const auto y = b.channel(c);
        if (!std::ranges::equal(x, y, [](float p, float q) {
                return std::bit_cast<std::uint32_t>(p) == std::bit_cast<std::uint32_t>(q);
            })) {
            return false;
        }
    }
    return true;
}

/// Output samples [begin, begin + count) of the recording through `map` with no edits: slope-1
/// warps and inserts as read_through_map gives them, every slope != 1 warp rendered by
/// stretch_span. The ranges handed to read_through_map exclude the stretched spans' samples, which
/// are snapped the same way, so it never meets a slope != 1 warp.
Result<AudioBuffer> render_unedited(const RenderInputs& in, const TimeMap& map,
                                    const StretchParams& stretch, const SpliceParams& splice,
                                    std::int64_t begin, std::int64_t count) {
    const Hz rate = in.source.sample_rate();
    const std::int64_t end = begin + count;
    auto out = AudioBuffer::create(in.source.channel_count(), Samples{count}, rate);
    if (!out) {
        return Unexpected{out.error()};
    }
    const auto copy_in = [&](const AudioBuffer& piece, std::int64_t at) {
        for (int c = 0; c < out->channel_count(); ++c) {
            std::ranges::copy(piece.channel(c), out->channel(c).begin() + (at - begin));
        }
    };
    std::int64_t cursor = begin;
    const auto plain_until = [&](std::int64_t stop) -> Result<void> {
        if (stop <= cursor) {
            return {};
        }
        auto piece = read_through_map(in.source, map, Samples{cursor}, Samples{stop - cursor});
        if (!piece) {
            return Unexpected{piece.error()};
        }
        copy_in(*piece, cursor);
        return {};
    };
    for (const TimeSpan& span : map.spans()) {
        if (!is_stretched(span)) {
            continue;
        }
        const std::int64_t lo = std::max(begin, index_of(span.output_start, rate));
        const std::int64_t hi = std::min(end, index_of(span.output_end, rate));
        if (hi <= lo) {
            continue;
        }
        if (auto r = plain_until(lo); !r) {
            return Unexpected{r.error()};
        }
        const OutputRange range = OutputRange::from_start(
            to_time_point<OutputAxis>(Samples{lo}, rate), to_seconds(Samples{hi - lo}, rate));
        auto piece = stretch_span(in.source, *in.analysis, span, range, stretch);
        if (!piece) {
            return Unexpected{piece.error()};
        }
        copy_in(piece->audio, lo);
        cursor = hi;
    }
    if (auto r = plain_until(end); !r) {
        return Unexpected{r.error()};
    }
    const SegmentTimeline* timeline = in.analysis->timeline ? &*in.analysis->timeline : nullptr;
    if (auto r = apply_splices(in.source, map, timeline, begin, *out, splice); !r) {
        return Unexpected{r.error()};
    }
    return out;
}

Result<Zone> render_zone(const RenderInputs& in, const TimeMap& map, const SynthesisEngine& engine,
                         const Plan& plan, const EditedNote& e) {
    Zone zone{.begin = zone_begin(plan, e),
              .end = zone_end(plan, e),
              .audio = std::nullopt,
              .identical = false};
    const Hz rate = plan.sample_rate;
    const OutputRange range =
        OutputRange::from_start(to_time_point<OutputAxis>(Samples{zone.begin}, rate),
                                to_seconds(Samples{zone.end - zone.begin}, rate));
    // The pitch engine is shown the pitch part only; gain, fades and flattening are applied to its
    // result as one gain curve (level.h, ADR-0028). A note with only level edits needs no engine.
    const EffectiveNote engine_eff = without_level(e.eff);
    const bool pitch_edited = !only_timing_edited(engine_eff, e.note->pitch_center);
    auto pristine =
        read_through_map(in.source, map, Samples{zone.begin}, Samples{zone.end - zone.begin});
    if (!pristine) {
        return Unexpected{pristine.error()};
    }
    std::optional<OutputAudio> rendered;
    if (pitch_edited) {
        auto from_engine = engine.render_note(in.source, *e.note, engine_eff, map, range);
        if (!from_engine) {
            return Unexpected{from_engine.error()};
        }
        rendered = std::move(*from_engine);
    } else {
        rendered = OutputAudio{.audio = *pristine, .start = range.start()};
    }
    const bool shape_ok =
        rendered->audio.frame_count().value() == zone.end - zone.begin &&
        rendered->audio.channel_count() == in.source.channel_count() &&
        rendered->audio.sample_rate() == rate &&
        to_sample_index(rendered->start, rate, Rounding::nearest).value() == zone.begin;
    if (!shape_ok) {
        return fail(ErrorCode::internal, "engine '" + std::string{engine.name()} +
                                             "' returned audio that does not match the zone");
    }
    if (has_level_edit(e.eff)) {
        auto shift = zone_shift(map, rate, range, "render_region");
        if (!shift) {
            return Unexpected{shift.error()};
        }
        auto gain = level_gain(*e.note, e.eff,
                               {.first = zone.begin,
                                .count = zone.end - zone.begin,
                                .note_first = e.span.begin,
                                .note_last = e.span.end,
                                .rate = rate,
                                .shift_samples = *shift});
        if (!gain) {
            return Unexpected{gain.error()};
        }
        if (auto applied = apply_gain(rendered->audio, *gain); !applied) {
            return Unexpected{applied.error()};
        }
    }
    zone.identical = same_samples(*pristine, rendered->audio);
    zone.audio = std::move(rendered->audio);
    return zone;
}

/// Samples of one channel over [lo, hi) of one signal: the pristine region or an engine zone.
struct Signals {
    const AudioBuffer* pristine;
    std::int64_t region_begin;
    const std::vector<Zone>* zones;

    [[nodiscard]] std::span<const float> get(int signal, int channel, std::int64_t lo,
                                             std::int64_t hi) const {
        if (signal == k_pristine) {
            return pristine->channel(channel).subspan(static_cast<std::size_t>(lo - region_begin),
                                                      static_cast<std::size_t>(hi - lo));
        }
        const Zone& z = (*zones)[static_cast<std::size_t>(signal)];
        return z.audio->channel(channel).subspan(static_cast<std::size_t>(lo - z.begin),
                                                 static_cast<std::size_t>(hi - lo));
    }
    [[nodiscard]] bool is_original(int signal) const {
        return signal == k_pristine || (*zones)[static_cast<std::size_t>(signal)].identical;
    }
};

struct Interval {
    std::int64_t lo;
    std::int64_t hi;
    [[nodiscard]] bool empty() const { return hi <= lo; }
};

Interval between(std::int64_t lo, std::int64_t hi) {
    return Interval{.lo = lo, .hi = hi};
}

Interval intersect(Interval a, Interval b) {
    return between(std::max(a.lo, b.lo), std::min(a.hi, b.hi));
}

/// Writes one engine's samples where it owns the signal between two ramps.
void apply_plateau(const Signals& s, AudioBuffer& out, int signal, Interval span) {
    if (span.empty() || s.is_original(signal)) {
        return;
    }
    for (int c = 0; c < out.channel_count(); ++c) {
        const auto src = s.get(signal, c, span.lo, span.hi);
        std::ranges::copy(src, out.channel(c).begin() + (span.lo - s.region_begin));
    }
}

void apply_ramp(const Signals& s, AudioBuffer& out, const Ramp& ramp, Interval span,
                FadeShape shape) {
    if (span.empty() || ramp.end <= ramp.begin ||
        (s.is_original(ramp.from) && s.is_original(ramp.to))) {
        return;
    }
    for (int c = 0; c < out.channel_count(); ++c) {
        crossfade(s.get(ramp.from, c, span.lo, span.hi), s.get(ramp.to, c, span.lo, span.hi),
                  out.channel(c).subspan(static_cast<std::size_t>(span.lo - s.region_begin),
                                         static_cast<std::size_t>(span.hi - span.lo)),
                  shape, span.lo - ramp.begin, ramp.end - ramp.begin);
    }
}

void blend(const Plan& plan, const Signals& s, AudioBuffer& out, FadeShape shape) {
    const Interval visible = between(std::max<std::int64_t>(0, plan.region_begin),
                                     std::min(plan.region_end, plan.frame_count));
    std::int64_t plateau_begin = visible.lo;
    int state = k_pristine;
    for (const Ramp& ramp : plan.ramps) {
        apply_plateau(s, out, state, intersect(visible, between(plateau_begin, ramp.begin)));
        apply_ramp(s, out, ramp, intersect(visible, between(ramp.begin, ramp.end)), shape);
        plateau_begin = std::max(plateau_begin, ramp.end);
        state = ramp.to;
    }
    apply_plateau(s, out, state, intersect(visible, between(plateau_begin, visible.hi)));
}

/// Multiplies the finished region by each meeting gap's gate curve; samples with gain 1 are left
/// alone and a gain of 0 writes +0. Returns the gaps that met the region.
Result<std::vector<SegmentId>> apply_gates(const Plan& plan, const TimeMap& map,
                                           const GateParams& params, AudioBuffer& out) {
    std::vector<SegmentId> met;
    const OutputRange range = OutputRange::from_start(
        to_time_point<OutputAxis>(Samples{plan.region_begin}, plan.sample_rate),
        to_seconds(Samples{plan.region_end - plan.region_begin}, plan.sample_rate));
    for (const GateInput& g : plan.gates) {
        if (!gate_meets_region(g, plan.region_begin, plan.region_end)) {
            continue;
        }
        auto curve = gate_curve(*g.gap, g.eff, map, plan.sample_rate, range, params);
        if (!curve) {
            return Unexpected{curve.error()};
        }
        for (int c = 0; c < out.channel_count(); ++c) {
            auto channel = out.channel(c);
            for (std::size_t i = 0; i < curve->size(); ++i) {
                const float gain = (*curve)[i];
                if (gain != 1.0F) {
                    channel[i] = gain == 0.0F ? 0.0F : channel[i] * gain;
                }
            }
        }
        met.push_back(g.gap->id);
    }
    return met;
}

}  // namespace

Result<std::shared_ptr<const RenderResult>> render_region(const RenderInputs& inputs,
                                                          OutputRange region,
                                                          const SynthesisEngine& engine,
                                                          const RenderParams& params) {
    if (auto r = validate_inputs(inputs, params); !r) {
        return Unexpected{r.error()};
    }
    auto resolved = resolve_time_map(inputs, params);
    if (!resolved) {
        return Unexpected{resolved.error()};
    }
    const TimeMap& map = *resolved->map;
    auto snapped = snap_region(inputs, map, region);
    if (!snapped) {
        return Unexpected{snapped.error()};
    }
    if (region.end() < region.start()) {  // unreachable through OutputRange; kept for safety
        return fail(ErrorCode::invalid_argument, "render_region: region ends before it starts");
    }
    auto planned = make_plan(inputs, map, params, *snapped);
    if (!planned) {
        return Unexpected{planned.error()};
    }
    const Plan& plan = *planned;
    const auto gate_fade_samples =
        to_samples(params.gate.gate_fade, plan.sample_rate, Rounding::nearest).value();
    const SpliceParams splice{.splice_fade = params.layout.splice_fade,
                              .shape = FadeShape::equal_power};
    const auto splice_fade_samples =
        to_samples(params.layout.splice_fade, plan.sample_rate, Rounding::nearest).value();
    const SourceId source_id = inputs.source.id();
    const std::string key = make_render_key({.source_id = &source_id,
                                             .analysis_version = inputs.analysis->version,
                                             .engine = &engine,
                                             .margin_samples = plan.margin,
                                             .shape = params.crossfade_shape,
                                             .region_begin = plan.region_begin,
                                             .region_end = plan.region_end,
                                             .sample_rate_hz = plan.sample_rate.value(),
                                             .edited = plan.edited,
                                             .ramps = plan.ramps,
                                             .time_map = &map,
                                             .stretch = &params.stretch,
                                             .gates = plan.gates,
                                             .gate_fade_samples = gate_fade_samples,
                                             .splice_fade_samples = splice_fade_samples});
    if (params.cache != nullptr) {
        if (auto hit = params.cache->get(key)) {
            return hit;
        }
    }

    const Hz rate = plan.sample_rate;
    const Samples count{plan.region_end - plan.region_begin};
    auto pristine =
        render_unedited(inputs, map, params.stretch, splice, plan.region_begin, count.value());
    if (!pristine) {
        return Unexpected{pristine.error()};
    }
    AudioBuffer out = *pristine;

    std::vector<Zone> zones(plan.edited.size());
    std::vector<NoteId> rendered;
    for (std::size_t i = 0; i < plan.edited.size(); ++i) {
        if (!zone_meets_region(plan, plan.edited[i])) {
            continue;
        }
        auto zone = render_zone(inputs, map, engine, plan, plan.edited[i]);
        if (!zone) {
            return Unexpected{zone.error()};
        }
        zones[i] = std::move(*zone);
        rendered.push_back(plan.edited[i].note->id);
    }
    // Ramps may name a zone that does not meet the region only outside the region, where nothing is
    // written; Signals::get is never asked for it.
    blend(plan, Signals{.pristine = &*pristine, .region_begin = plan.region_begin, .zones = &zones},
          out, params.crossfade_shape);

    auto gated = apply_gates(plan, map, params.gate, out);
    if (!gated) {
        return Unexpected{gated.error()};
    }
    const bool passthrough = rendered.empty() && gated->empty();
    std::shared_ptr<const RenderResult> shared = std::make_shared<const RenderResult>(RenderResult{
        .buffer = OutputAudio{.audio = std::move(out),
                              .start = to_time_point<OutputAxis>(Samples{plan.region_begin}, rate)},
        .cache_id = short_id(key),
        .notes_rendered = std::move(rendered),
        .gaps_rendered = std::move(*gated),
        .passthrough = passthrough,
        .stretched = meets_stretch(map, rate, plan.region_begin, plan.region_end),
        .repaired_notes = resolved->repaired_notes,
        .layout_used = resolved->layout_used});
    if (params.cache != nullptr) {
        params.cache->put(key, shared);
    }
    return shared;
}

}  // namespace anno
