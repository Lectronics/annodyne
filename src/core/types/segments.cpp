#include "core/types/segments.h"

#include "core/types/contract.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace anno {

namespace {

Unexpected invalid(std::string message) {
    return fail(ErrorCode::invalid_argument, "SegmentTimeline: " + std::move(message));
}

double raw(SourceTime t) {
    return (t - SourceTime{}).value();
}

std::string prefixed(const std::optional<NoteId>& id) {
    if (!id) {
        return "0:";
    }
    return std::to_string(id->value.size()) + ":" + id->value;
}

/// Range of a segment while building (the gap index is checked before use).
struct Bounds {
    SourceTime start;
    SourceTime end;
};

Result<Bounds> bounds_of(const Segment& s, const std::vector<Gap>& gaps) {
    if (const auto* n = std::get_if<NoteSegment>(&s)) {
        return Bounds{.start = n->start, .end = n->end};
    }
    const std::size_t index = std::get<std::size_t>(s);
    if (index >= gaps.size()) {
        return invalid("a gap segment names a gap index out of range");
    }
    return Bounds{.start = gaps[index].start, .end = gaps[index].end};
}

Result<void> check_gap(const Gap& g) {
    if (g.id.kind != SegmentKind::gap || !(g.id == gap_segment_id(g.before, g.after))) {
        return invalid("a gap id differs from the id derived from its before/after notes");
    }
    if (!std::isfinite(g.floor_dbfs.value())) {
        return invalid("a gap floor is not finite");
    }
    if (g.regions.empty() || !(g.regions.front().start == g.start) ||
        !(g.regions.back().end == g.end)) {
        return invalid("the regions of a gap do not cover it");
    }
    for (std::size_t i = 0; i < g.regions.size(); ++i) {
        const GapRegion& r = g.regions[i];
        if (!(r.start < r.end) || (i > 0 && !(r.start == g.regions[i - 1].end))) {
            return invalid("the regions of a gap are not contiguous or have zero length");
        }
    }
    return {};
}

}  // namespace

SegmentId gap_segment_id(const std::optional<NoteId>& before, const std::optional<NoteId>& after) {
    return SegmentId{.kind = SegmentKind::gap,
                     .value = "gap:" + prefixed(before) + prefixed(after)};
}

SegmentTimeline::SegmentTimeline(std::vector<Segment> segments, std::vector<Gap> gaps,
                                 SourceTime source_end, std::vector<SourceTime> starts) noexcept
    : segments_{std::move(segments)}, gaps_{std::move(gaps)}, source_end_{source_end},
      starts_{std::move(starts)} {}

namespace {

/// Contiguity from 0 to source_end, non-zero lengths, gap indices valid and used exactly once.
/// Returns the start of every segment.
Result<std::vector<SourceTime>> check_partition(const std::vector<Segment>& segments,
                                                const std::vector<Gap>& gaps,
                                                SourceTime source_end) {
    std::vector<SourceTime> starts;
    starts.reserve(segments.size());
    std::vector<bool> gap_used(gaps.size(), false);
    SourceTime cursor{};
    for (const Segment& segment : segments) {
        const auto bounds = bounds_of(segment, gaps);
        if (!bounds) {
            return Unexpected{bounds.error()};
        }
        if (!std::isfinite(raw(bounds->start)) || !std::isfinite(raw(bounds->end)) ||
            !(bounds->start < bounds->end)) {
            return invalid("a segment has zero or negative length or a non-finite time");
        }
        if (!(bounds->start == cursor)) {
            return invalid(bounds->start < cursor ? "segments overlap" : "segments leave a hole");
        }
        cursor = bounds->end;
        starts.push_back(bounds->start);
        if (const auto* index = std::get_if<std::size_t>(&segment)) {
            if (gap_used[*index]) {
                return invalid("a gap is used by two segments");
            }
            gap_used[*index] = true;
        }
    }
    if (!(cursor == source_end)) {
        return invalid("segments do not end at source_end");
    }
    if (std::ranges::find(gap_used, false) != gap_used.end()) {
        return invalid("a gap is not used by any segment");
    }
    return starts;
}

/// A gap's before/after name the note segments next to it.
Result<void> check_neighbours(const std::vector<Segment>& segments, std::size_t i, const Gap& g) {
    const NoteSegment* before = i > 0 ? std::get_if<NoteSegment>(&segments[i - 1]) : nullptr;
    const NoteSegment* after =
        i + 1 < segments.size() ? std::get_if<NoteSegment>(&segments[i + 1]) : nullptr;
    if (before == nullptr && i > 0) {
        return invalid("two gaps are adjacent");
    }
    if (before != nullptr ? !(g.before && *g.before == before->note) : g.before.has_value()) {
        return invalid("a gap's before does not name the note segment before it");
    }
    if (after != nullptr ? !(g.after && *g.after == after->note) : g.after.has_value()) {
        return invalid("a gap's after does not name the note segment after it");
    }
    return {};
}

/// Every gap is well formed and next to the right notes; ids are unique.
Result<void> check_gaps(const std::vector<Segment>& segments, const std::vector<Gap>& gaps) {
    std::set<SegmentId> ids;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        SegmentId id;
        if (const auto* index = std::get_if<std::size_t>(&segments[i])) {
            if (auto r = check_gap(gaps[*index]); !r) {
                return r;
            }
            if (auto r = check_neighbours(segments, i, gaps[*index]); !r) {
                return r;
            }
            id = gaps[*index].id;
        } else {
            id = segment_id(std::get<NoteSegment>(segments[i]).note);
        }
        if (!ids.insert(id).second) {
            return invalid("duplicate segment id");
        }
    }
    return {};
}

}  // namespace

Result<SegmentTimeline> SegmentTimeline::make(std::vector<Segment> segments, std::vector<Gap> gaps,
                                              SourceTime source_end) {
    if (!std::isfinite(raw(source_end)) || raw(source_end) < 0.0) {
        return invalid("source_end must be finite and >= 0");
    }
    auto starts = check_partition(segments, gaps, source_end);
    if (!starts) {
        return Unexpected{starts.error()};
    }
    if (auto r = check_gaps(segments, gaps); !r) {
        return Unexpected{r.error()};
    }
    return SegmentTimeline{std::move(segments), std::move(gaps), source_end, std::move(*starts)};
}

const Segment& SegmentTimeline::at(SourceTime t) const noexcept {
    ANNO_EXPECTS(!segments_.empty());
    ANNO_EXPECTS(t >= SourceTime{} && t <= source_end_);
    const auto it = std::ranges::upper_bound(starts_, t);
    const std::size_t index =
        it == starts_.begin() ? 0 : static_cast<std::size_t>(it - starts_.begin()) - 1;
    return segments_[index];
}

SourceRange SegmentTimeline::range_of(const Segment& s) const noexcept {
    if (const auto* n = std::get_if<NoteSegment>(&s)) {
        return SourceRange::from_start(n->start, n->end - n->start);
    }
    const Gap& g = gaps_[*std::get_if<std::size_t>(&s)];
    return SourceRange::from_start(g.start, g.end - g.start);
}

const Gap* SegmentTimeline::gap_before(const NoteId& note) const noexcept {
    for (std::size_t i = 1; i < segments_.size(); ++i) {
        const auto* n = std::get_if<NoteSegment>(&segments_[i]);
        if (n != nullptr && n->note == note) {
            const auto* index = std::get_if<std::size_t>(&segments_[i - 1]);
            return index != nullptr ? &gaps_[*index] : nullptr;
        }
    }
    return nullptr;
}

const Gap* SegmentTimeline::gap_after(const NoteId& note) const noexcept {
    for (std::size_t i = 0; i + 1 < segments_.size(); ++i) {
        const auto* n = std::get_if<NoteSegment>(&segments_[i]);
        if (n != nullptr && n->note == note) {
            const auto* index = std::get_if<std::size_t>(&segments_[i + 1]);
            return index != nullptr ? &gaps_[*index] : nullptr;
        }
    }
    return nullptr;
}

}  // namespace anno
