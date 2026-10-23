#include "core/analysis/gaps/build_segment_timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace anno {

namespace {

Unexpected invalid(const char* what) {
    return fail(ErrorCode::invalid_argument, std::string{"build_segment_timeline: "} + what);
}

double raw(SourceTime t) {
    return (t - SourceTime{}).value();
}

Result<void> check_params(const GapParams& p) {
    const bool bad = !std::isfinite(p.tail_margin.value()) || p.tail_margin.value() < 0.0 ||
                     !std::isfinite(p.max_tail.value()) || p.max_tail.value() <= 0.0 ||
                     !std::isfinite(p.preroll_guard.value()) || p.preroll_guard.value() < 0.0;
    return bad ? Result<void>{invalid("tail_margin, preroll_guard >= 0 and max_tail > 0 required")}
               : Result<void>{};
}

/// The active notes in order; errors for any that overlap, are unsorted, have no length or lie
/// outside [0, source_end].
Result<std::vector<const Note*>> active_notes(std::span<const Note> notes, double end) {
    std::vector<const Note*> out;
    for (const Note& n : notes) {
        if (!n.is_active) {
            continue;
        }
        if (!(raw(n.source_start) >= 0.0) || !(raw(n.source_end) <= end) ||
            !(raw(n.source_end) > raw(n.source_start))) {
            return invalid("an active note has no length or lies outside the recording");
        }
        if (!out.empty() && raw(n.source_start) < raw(out.back()->source_end)) {
            return invalid("active notes overlap or are not sorted");
        }
        out.push_back(&n);
    }
    return out;
}

/// The median of the floor curve over the frames in [start, end) (a boundary frame belongs to the
/// later segment); the nearest frame when none.
Decibels floor_over(const NoiseFloor& floor, double start, double end) {
    const double hop = floor.grid.hop.value();
    const double origin = raw(floor.grid.start);
    const auto last_frame = floor.grid.n_frames - 1;
    const auto first = static_cast<std::int64_t>(std::ceil((start - origin) / hop));
    const auto last = static_cast<std::int64_t>(std::ceil((end - origin) / hop)) - 1;
    std::vector<double> values;
    for (std::int64_t i = std::max<std::int64_t>(0, first); i <= std::min(last, last_frame); ++i) {
        values.push_back(floor.curve_dbfs[static_cast<std::size_t>(i)].value());
    }
    if (values.empty()) {
        const auto nearest = std::clamp<std::int64_t>(
            std::llround((0.5 * (start + end) - origin) / hop), 0, last_frame);
        return floor.curve_dbfs[static_cast<std::size_t>(nearest)];
    }
    std::ranges::sort(values);
    const std::size_t mid = values.size() / 2;
    return Decibels{values.size() % 2 == 1 ? values[mid] : 0.5 * (values[mid - 1] + values[mid])};
}

Gap make_gap(const NoiseFloor& floor, double start, double end, const Note* before,
             const Note* after) {
    Gap g;
    g.before = before != nullptr ? std::optional<NoteId>{before->id} : std::nullopt;
    g.after = after != nullptr ? std::optional<NoteId>{after->id} : std::nullopt;
    g.id = gap_segment_id(g.before, g.after);
    g.start = SourceTime{} + Seconds{start};
    g.end = SourceTime{} + Seconds{end};
    g.regions = {{.kind = GapRegionKind::silence, .start = g.start, .end = g.end}};
    g.floor_dbfs = floor_over(floor, start, end);
    return g;
}

}  // namespace

Result<SegmentTimeline> build_segment_timeline(std::span<const Note> notes, SourceTime source_end,
                                               const ActivityTrack& activity,
                                               const NoiseFloor& floor, const GapParams& params) {
    if (auto r = check_params(params); !r) {
        return Unexpected{r.error()};
    }
    const double end = raw(source_end);
    if (!std::isfinite(end) || end < 0.0) {
        return invalid("source_end must be finite and >= 0");
    }
    if (!(activity.grid == floor.grid)) {
        return invalid("the floor and the activity track are on different grids");
    }
    auto active = active_notes(notes, end);
    if (!active) {
        return Unexpected{active.error()};
    }
    if (end == 0.0) {
        return SegmentTimeline::make({}, {}, source_end);
    }
    std::vector<Segment> segments;
    std::vector<Gap> gaps;
    const auto push_gap = [&](double start, double stop, const Note* before, const Note* after) {
        gaps.push_back(make_gap(floor, start, stop, before, after));
        segments.emplace_back(gaps.size() - 1);
    };
    double at = 0.0;
    const Note* previous = nullptr;
    for (const Note* note : *active) {
        if (raw(note->source_start) > at) {
            push_gap(at, raw(note->source_start), previous, note);
        }
        segments.emplace_back(
            NoteSegment{.note = note->id, .start = note->source_start, .end = note->source_end});
        at = raw(note->source_end);
        previous = note;
    }
    if (end > at) {
        push_gap(at, end, previous, nullptr);
    }
    return SegmentTimeline::make(std::move(segments), std::move(gaps), source_end);
}

}  // namespace anno
