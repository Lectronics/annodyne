#pragma once

#include "core/types/segments.h"
#include "support/test_data.h"

#include <cstddef>
#include <vector>

// Small builders for SegmentTimeline fixtures (gap slices 2 and 3).

namespace anno::test {

inline NoteSegment note_seg(const char* id, double a, double b) {
    return {.note = NoteId{id}, .start = src(a), .end = src(b)};
}

inline GapRegion region(GapRegionKind kind, double a, double b) {
    return {.kind = kind, .start = src(a), .end = src(b)};
}

inline Gap gap_between(const char* before, const char* after, double a, double b) {
    Gap g;
    if (before != nullptr) {
        g.before = NoteId{before};
    }
    if (after != nullptr) {
        g.after = NoteId{after};
    }
    g.id = gap_segment_id(g.before, g.after);
    g.start = src(a);
    g.end = src(b);
    g.regions = {{.kind = GapRegionKind::silence, .start = src(a), .end = src(b)}};
    return g;
}

/// The timeline of consecutive notes: a leading gap, a gap between every two notes that do not
/// touch, and a trailing gap up to `end`. A note starting at 0 or ending at `end` has no edge gap.
inline SegmentTimeline timeline_of(const std::vector<Note>& notes, double end) {
    std::vector<Segment> segments;
    std::vector<Gap> gaps;
    const auto push_gap = [&](const Note* before, const Note* after, double a, double b) {
        gaps.push_back(gap_between(before != nullptr ? before->id.value.c_str() : nullptr,
                                   after != nullptr ? after->id.value.c_str() : nullptr, a, b));
        segments.emplace_back(gaps.size() - 1);
    };
    double at = 0.0;
    const Note* previous = nullptr;
    for (const Note& n : notes) {
        const double start = n.source_start.unchecked_since_origin().value();
        const double stop = n.source_end.unchecked_since_origin().value();
        if (start > at) {
            push_gap(previous, &n, at, start);
        }
        segments.emplace_back(note_seg(n.id.value.c_str(), start, stop));
        at = stop;
        previous = &n;
    }
    if (end > at) {
        push_gap(previous, nullptr, at, end);
    }
    return SegmentTimeline::make(segments, gaps, src(end)).value();
}

}  // namespace anno::test
