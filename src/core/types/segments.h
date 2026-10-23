#pragma once

#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <cstddef>
#include <optional>
#include <span>
#include <variant>
#include <vector>

// The segment timeline (ADR-0008, ADR-0024; design: docs/design/phase5-gaps-core.md): a track's
// source time partitioned into note and gap segments. Data and the partition invariant only; the
// analysis that builds it is core/analysis/gaps (layer 1).

namespace anno {

/// What a stretch of a gap is. `tail` is the previous note's decay, `protected_preroll` the guard
/// before the next note's start; both are left alone by the gate. `audible` is a stretch inside a
/// gap that stands above the floor (a fricative, a breath, a room event) wherever it sits: the gate
/// leaves it alone too, whatever it is (the consonant detector, a later slice, refines it into
/// sibilant, breath and plosive kinds). `silence` is the rest.
enum class GapRegionKind { tail, protected_preroll, silence, audible };

struct GapRegion {
    GapRegionKind kind = GapRegionKind::silence;
    SourceTime start;
    SourceTime end;  // end > start
};

struct Gap {
    SegmentId id;  // kind gap; the id gap_segment_id(before, after) gives
    SourceTime start;
    SourceTime end;                  // end > start
    std::vector<GapRegion> regions;  // contiguous, in order, covering [start, end] exactly
    Decibels floor_dbfs{-120.0};     // median of the floor curve over the gap, RMS dBFS
    std::optional<NoteId> before;    // the note ending at `start` (none: a leading gap)
    std::optional<NoteId> after;     // the note starting at `end` (none: a trailing gap)
};

/// A note segment holds the note's id and its range; a gap segment the index of its Gap, which
/// holds the id and the range (no second copy).
struct NoteSegment {
    NoteId note;
    SourceTime start;
    SourceTime end;
};
using Segment = std::variant<NoteSegment, std::size_t>;

/// The id of the gap between two notes: "gap:" + <len>:<before> + <len>:<after>, each note id
/// length-prefixed (an absent side is "0:"), so no note id text can make two pairs collide.
/// Pure; thread-safe.
[[nodiscard]] SegmentId gap_segment_id(const std::optional<NoteId>& before,
                                       const std::optional<NoteId>& after);

/// Invariant: segments ordered, contiguous, non-overlapping, covering [0, source_end] exactly,
/// none of zero length; two gaps are never adjacent (two notes may touch: legato); every gap is
/// used by exactly one segment, in order; a gap's regions tile it; ids are unique and a gap's id is
/// gap_segment_id(before, after); a gap's before/after name its neighbouring note segments (absent
/// where the neighbour is the recording's edge). Value type; immutable.
class SegmentTimeline {
public:
    /// Errors: invalid_argument for any violation above, a non-finite time or floor, or a gap index
    /// out of range. An empty list with source_end == 0 is the empty timeline.
    /// Pure; thread-safe; not real-time safe (allocates).
    [[nodiscard]] static Result<SegmentTimeline> make(std::vector<Segment> segments,
                                                      std::vector<Gap> gaps, SourceTime source_end);

    [[nodiscard]] std::span<const Segment> segments() const noexcept { return segments_; }
    [[nodiscard]] std::span<const Gap> gaps() const noexcept { return gaps_; }
    [[nodiscard]] SourceTime source_end() const noexcept { return source_end_; }

    /// The segment holding `t`: a boundary belongs to the later segment (right-continuous); the
    /// source end to the last. Pre: the timeline is not empty and 0 <= t <= source_end() (asserted
    /// in debug, clamped in release). Real-time safe.
    [[nodiscard]] const Segment& at(SourceTime t) const noexcept;

    /// The range of a segment of this timeline (a gap segment has none without the timeline).
    /// Pre: `s` is one of segments(). Real-time safe.
    [[nodiscard]] SourceRange range_of(const Segment& s) const noexcept;

    /// The gap that ends where the note starts / starts where the note ends; nullptr if the note
    /// touches another note or the recording's edge there, or is not in the timeline.
    [[nodiscard]] const Gap* gap_before(const NoteId& note) const noexcept;
    [[nodiscard]] const Gap* gap_after(const NoteId& note) const noexcept;

private:
    SegmentTimeline(std::vector<Segment> segments, std::vector<Gap> gaps, SourceTime source_end,
                    std::vector<SourceTime> starts) noexcept;

    std::vector<Segment> segments_;
    std::vector<Gap> gaps_;
    SourceTime source_end_;
    std::vector<SourceTime> starts_;  // start of each segment, for at()
};

}  // namespace anno
