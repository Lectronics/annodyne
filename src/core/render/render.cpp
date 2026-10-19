#include "core/render/render.h"

#include "core/dsp/crossfade.h"
#include "core/edits/effective_state.h"
#include "core/render/blend_plan.h"
#include "core/render/render_key.h"
#include "core/timemap/build_time_map.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace anno {

namespace {

/// Everything planned before any audio is read.
struct Plan {
    std::int64_t frame_count = 0;  // recording length, samples
    Hz sample_rate;
    std::int64_t region_begin = 0;
    std::int64_t region_end = 0;
    std::int64_t margin = 0;
    std::vector<EditedNote> edited;
    std::vector<Ramp> ramps;
};

/// An engine's output for one note's zone (clamped to the recording).
struct Zone {
    std::int64_t begin = 0;
    std::int64_t end = 0;
    std::optional<AudioBuffer> audio;  // empty while the zone does not meet the region
    bool identical = false;            // equal to the source over the zone, bit for bit
};

std::int64_t index_of(SourceTime t, Hz rate) {
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
    return {};
}

Result<TimeMapBuild> identity_time_map(const RenderInputs& in) {
    const SourceTime end =
        to_time_point<SourceAxis>(in.source.frame_count(), in.source.sample_rate());
    auto built = build_time_map(in.analysis->notes, *in.edits, TimeMapParams{.source_end = end});
    if (!built) {
        return Unexpected{built.error()};
    }
    if (!built->map.is_identity()) {
        return fail(ErrorCode::not_implemented,
                    "render_region: timing edits change the TimeMap; stretching is Phase 6");
    }
    return built;
}

Result<std::pair<std::int64_t, std::int64_t>> snap_region(const RenderInputs& in,
                                                          OutputRange region) {
    const Hz rate = in.source.sample_rate();
    const std::int64_t begin = to_sample_index(region.start(), rate, Rounding::nearest).value();
    const std::int64_t end = to_sample_index(region.end(), rate, Rounding::nearest).value();
    if (begin < 0 || end > in.source.frame_count().value()) {
        return fail(ErrorCode::out_of_range, "render_region: region lies outside the recording");
    }
    return std::pair{begin, std::max(begin, end)};
}

Plan make_plan(const RenderInputs& in, const RenderParams& params,
               std::pair<std::int64_t, std::int64_t> region) {
    Plan plan;
    plan.frame_count = in.source.frame_count().value();
    plan.sample_rate = in.source.sample_rate();
    plan.region_begin = region.first;
    plan.region_end = region.second;
    plan.margin = to_samples(params.crossfade_length, plan.sample_rate, Rounding::nearest).value();
    for (const Note& note : in.analysis->notes) {
        if (!note.is_active) {
            continue;
        }
        EffectiveNote eff = effective_state(note, *in.edits);
        if (!eff.is_edited) {
            continue;
        }
        const std::int64_t begin = std::clamp<std::int64_t>(
            index_of(note.source_start, plan.sample_rate), 0, plan.frame_count);
        const std::int64_t end = std::clamp<std::int64_t>(
            index_of(note.source_end, plan.sample_rate), begin, plan.frame_count);
        plan.edited.push_back({.note = &note, .eff = std::move(eff), .span = {begin, end}});
    }
    std::vector<SampleSpan> spans;
    spans.reserve(plan.edited.size());
    for (const EditedNote& e : plan.edited) {
        spans.push_back(e.span);
    }
    plan.ramps = plan_blend(spans, plan.margin);
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
        if (!std::equal(x.begin(), x.end(), y.begin(), y.end(), [](float p, float q) {
                return std::bit_cast<std::uint32_t>(p) == std::bit_cast<std::uint32_t>(q);
            })) {
            return false;
        }
    }
    return true;
}

Result<Zone> render_zone(const RenderInputs& in, const SynthesisEngine& engine, const Plan& plan,
                         const EditedNote& e) {
    Zone zone{.begin = zone_begin(plan, e), .end = zone_end(plan, e)};
    const Hz rate = plan.sample_rate;
    const OutputRange range =
        OutputRange::from_start(to_time_point<OutputAxis>(Samples{zone.begin}, rate),
                                to_seconds(Samples{zone.end - zone.begin}, rate));
    auto rendered = engine.render_note(in.source, *e.note, e.eff, TimeMap::identity(), range);
    if (!rendered) {
        return Unexpected{rendered.error()};
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
    auto pristine = in.source.read(Samples{zone.begin}, Samples{zone.end - zone.begin});
    if (!pristine) {
        return Unexpected{pristine.error()};
    }
    zone.identical = same_samples(*pristine, rendered->audio);
    zone.audio = std::move(rendered->audio);
    return zone;
}

/// Samples of one channel over [lo, hi) of one signal: the pristine region or an engine zone.
struct Signals {
    const AudioBuffer& pristine;
    std::int64_t region_begin;
    const std::vector<Zone>& zones;

    [[nodiscard]] std::span<const float> get(int signal, int channel, std::int64_t lo,
                                             std::int64_t hi) const {
        if (signal == k_pristine) {
            return pristine.channel(channel).subspan(static_cast<std::size_t>(lo - region_begin),
                                                     static_cast<std::size_t>(hi - lo));
        }
        const Zone& z = zones[static_cast<std::size_t>(signal)];
        return z.audio->channel(channel).subspan(static_cast<std::size_t>(lo - z.begin),
                                                 static_cast<std::size_t>(hi - lo));
    }
    [[nodiscard]] bool is_original(int signal) const {
        return signal == k_pristine || zones[static_cast<std::size_t>(signal)].identical;
    }
};

struct Interval {
    std::int64_t lo;
    std::int64_t hi;
    [[nodiscard]] bool empty() const { return hi <= lo; }
};

Interval intersect(Interval a, Interval b) {
    return {std::max(a.lo, b.lo), std::min(a.hi, b.hi)};
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
    const Interval visible{std::max<std::int64_t>(0, plan.region_begin),
                           std::min(plan.region_end, plan.frame_count)};
    std::int64_t plateau_begin = visible.lo;
    int state = k_pristine;
    for (const Ramp& ramp : plan.ramps) {
        apply_plateau(s, out, state, intersect(visible, {plateau_begin, ramp.begin}));
        apply_ramp(s, out, ramp, intersect(visible, {ramp.begin, ramp.end}), shape);
        plateau_begin = std::max(plateau_begin, ramp.end);
        state = ramp.to;
    }
    apply_plateau(s, out, state, intersect(visible, {plateau_begin, visible.hi}));
}

}  // namespace

Result<std::shared_ptr<const RenderResult>> render_region(const RenderInputs& inputs,
                                                          OutputRange region,
                                                          const SynthesisEngine& engine,
                                                          const RenderParams& params) {
    if (auto r = validate_inputs(inputs, params); !r) {
        return Unexpected{r.error()};
    }
    auto snapped = snap_region(inputs, region);
    if (!snapped) {
        return Unexpected{snapped.error()};
    }
    if (region.end() < region.start()) {  // unreachable through OutputRange; kept for safety
        return fail(ErrorCode::invalid_argument, "render_region: region ends before it starts");
    }
    if (inputs.source.frame_count().value() > 0) {
        if (auto r = identity_time_map(inputs); !r) {
            return Unexpected{r.error()};
        }
    }
    const Plan plan = make_plan(inputs, params, *snapped);
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
                                             .ramps = plan.ramps});
    if (params.cache != nullptr) {
        if (auto hit = params.cache->get(key)) {
            return hit;
        }
    }

    const Hz rate = plan.sample_rate;
    const Samples count{plan.region_end - plan.region_begin};
    auto pristine = inputs.source.read(Samples{plan.region_begin}, count);
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
        auto zone = render_zone(inputs, engine, plan, plan.edited[i]);
        if (!zone) {
            return Unexpected{zone.error()};
        }
        zones[i] = std::move(*zone);
        rendered.push_back(plan.edited[i].note->id);
    }
    // Ramps may name a zone that does not meet the region only outside the region, where nothing is
    // written; Signals::get is never asked for it.
    blend(plan, Signals{*pristine, plan.region_begin, zones}, out, params.crossfade_shape);

    const bool passthrough = rendered.empty();
    std::shared_ptr<const RenderResult> shared = std::make_shared<const RenderResult>(RenderResult{
        .buffer = OutputAudio{.audio = std::move(out),
                              .start = to_time_point<OutputAxis>(Samples{plan.region_begin}, rate)},
        .source_range = SourceRange::from_start(
            to_time_point<SourceAxis>(Samples{plan.region_begin}, rate), to_seconds(count, rate)),
        .cache_id = short_id(key),
        .notes_rendered = std::move(rendered),
        .passthrough = passthrough});
    if (params.cache != nullptr) {
        params.cache->put(key, shared);
    }
    return shared;
}

}  // namespace anno
