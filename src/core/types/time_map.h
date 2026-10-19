#pragma once

#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/time_pair.h"
#include "core/types/units.h"

#include <cstddef>
#include <optional>
#include <span>
#include <variant>
#include <vector>

// TimeMap: the link between source time and output time (architecture spec §2; ADR-0008). An
// ordered list of spans tiling the source [0, source_length) and the output [0, output_length):
// warps (source played back, linearly time-scaled), cuts (source removed) and inserts (output with
// no source). The pure mapping lives here so synthesis (layer 2) can use it; building one from
// edits is build_time_map in core/timemap (ADR-0005 §11). Design: docs/design/phase1-data-model.md,
// docs/design/phase2-addendum-timemap-spans.md.

namespace anno {

enum class SpanKind { warp, cut, insert };

/// One piece of the map. Both ranges are always present; the one a kind does not use has zero
/// length and sits at the boundary position.
// clang-format off
///  warp:   source and output ranges both positive; slope = output / source (1 = untouched)
///  cut:    source range positive, output range zero length (source removed)
///  insert: source range zero length, output range positive (output with no source)
// clang-format on
struct TimeSpan {
    SpanKind kind = SpanKind::warp;
    SourceTime source_start;
    SourceTime source_end;
    OutputTime output_start;
    OutputTime output_end;
    std::optional<SegmentId> segment;  // the segment that produced it; empty without a timeline
};

/// A position inside an insert span.
struct GeneratedTime {
    std::size_t span = 0;  // index into TimeMap::spans() of the insert span (its segment is there)
    Seconds offset;        // from the start of the insert span; 0 <= offset <= its length
};

using SourceLookup = std::variant<SourceTime, GeneratedTime>;

/// Value type; immutable. Domain is finite: [0, source_length()] on the source axis and
/// [0, output_length()] on the output axis.
class TimeMap {
public:
    /// Source [0, source_length) played back unchanged: one slope-1 warp. identity(0) is the empty
    /// map.
    [[nodiscard]] static TimeMap identity(Seconds source_length);

    /// Errors: invalid_argument if a time is not finite, a span violates its kind's shape, the
    /// first span does not start at (0, 0), or a span does not start exactly where the previous one
    /// ended on both axes. Empty `spans` is the empty map. Whether cuts and inserts lie inside gap
    /// segments cannot be checked here (there is no timeline); build_time_map and solve_layout
    /// check it (Phase 5). Not real-time safe (allocates).
    [[nodiscard]] static Result<TimeMap> make(std::vector<TimeSpan> spans);

    /// A map of warps, linear between anchors. (0,0) is an implicit anchor when the first anchor is
    /// not at source 0; a first anchor at source 0 with output t > 0 means t seconds of silence (a
    /// leading insert span) before the audio, as when a note at the start of the recording is
    /// pushed later. After the last anchor the map continues at slope 1 to
    /// `source_length` (no span if the last anchor is already there, as with the builder's fixed
    /// end anchor). If every anchor has source == output the result is identity(source_length).
    /// Errors: invalid_argument if `anchors` is empty, a time is not finite, the anchors are not
    /// strictly increasing in source and output, the first anchor is at source 0 with output < 0,
    /// or the last anchor's source exceeds source_length. Not real-time safe (allocates).
    [[nodiscard]] static Result<TimeMap> from_anchors(std::vector<TimePair> anchors,
                                                      Seconds source_length);

    /// M(s). Right-continuous: at a span boundary the span with positive source length that starts
    /// there answers (a warp: its output_start; a cut: empty). s == source_length() maps to
    /// output_length(). Identity maps return their input exactly. Pre (asserted in debug, clamped
    /// in release): 0 <= s <= source_length(). Real-time safe.
    [[nodiscard]] std::optional<OutputTime> to_output(SourceTime s) const noexcept;

    /// M(s) for the END of a source range: at a boundary the span with positive source length that
    /// ends there answers (a warp: its output_end; a cut: empty). Equal to to_output elsewhere.
    /// Pre: 0 < s <= source_length() (same assert/clamp). Real-time safe.
    [[nodiscard]] std::optional<OutputTime> to_output_end(SourceTime s) const noexcept;

    /// M^-1(tau). Right-continuous. At the start of an insert: GeneratedTime{offset 0}; at its end:
    /// the following span's answer. Cuts have no output and never answer. tau == output_length():
    /// the end of the last span with output length (a warp: its source_end; an insert:
    /// GeneratedTime with offset == its length). The empty map answers SourceTime{} at tau 0. For
    /// non-cut s < source_length(), to_source(*to_output(s)) == s within 1e-9 s (s ==
    /// source_length() is the closing point: after a trailing insert it answers GeneratedTime).
    /// Pre: 0 <= tau <= output_length(). Real-time safe.
    [[nodiscard]] SourceLookup to_source(OutputTime tau) const noexcept;

    /// dtau/ds at tau (right-continuous): the warp's slope; 0 inside an insert span (a sentinel:
    /// never divide by it). Same precondition as to_source. Real-time safe.
    [[nodiscard]] Ratio slope_at(OutputTime tau) const noexcept;

    [[nodiscard]] std::span<const TimeSpan> spans() const noexcept { return spans_; }
    [[nodiscard]] Seconds source_length() const noexcept { return source_length_; }
    [[nodiscard]] Seconds output_length() const noexcept { return output_length_; }

    /// Structural: the empty map, or exactly one warp from (0,0) with output_end == source_end
    /// (exact comparison of the stored values). Then to_output(s) == s exactly.
    [[nodiscard]] bool is_identity() const noexcept { return identity_; }

private:
    TimeMap(std::vector<TimeSpan> spans, Seconds source_length, Seconds output_length,
            bool identity) noexcept;

    std::vector<TimeSpan> spans_;
    Seconds source_length_;
    Seconds output_length_;
    bool identity_;
};

}  // namespace anno
