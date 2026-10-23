#pragma once

#include "core/edits/edit_list.h"
#include "core/types/effective_note.h"
#include "core/types/note.h"
#include "core/types/result.h"

#include <span>
#include <vector>

// The anchor machinery shared by build_time_map (Flex) and the layout path (ADR-0025): a note's
// timing edits as anchors, their merge, the minimum-segment repair and the whole-sample
// quantisation. Moved out of build_time_map.cpp unchanged. Raw seconds are allowed here (ADR-0003).

namespace anno::timemap_detail {

/// Floating-point dust below which two durations count as equal.
inline constexpr double k_same_length_s = 1e-12;
inline constexpr int k_no_note = -1;

struct Anchor {
    double s;  // source seconds
    double t;  // output seconds
    int note;  // index into the note span; k_no_note for the origin and end anchors
};

[[nodiscard]] double raw(SourceTime t);
[[nodiscard]] double raw(OutputTime t);

/// True if any timing edit survives in the note's effective state. Only such notes pin anchors.
[[nodiscard]] bool has_timing_edit(const EffectiveNote& eff);

/// The anchors a note asks for, before any repair: start, attack end, handles in the body, end.
[[nodiscard]] std::vector<Anchor> desired_anchors(int index, const Note& note,
                                                  const EffectiveNote& eff);

/// Appends `anchors` after `done`. A coincident source point keeps the later output; an output
/// that differs from the earlier request marks the later note as repaired.
void append(std::vector<Anchor>& done, const std::vector<Anchor>& anchors,
            std::vector<bool>& repaired);

/// Pushes outputs later so every segment keeps its needed length; pulls earlier anchors back from
/// the fixed end when `fixed_end`. Returns false if the two passes cannot both be satisfied.
[[nodiscard]] bool enforce_min_segments(std::vector<Anchor>& done, bool fixed_end,
                                        double min_segment_s);

/// Rounds the shift t - s of every anchor to a multiple of `step_samples` samples (1: whole
/// samples); false (anchors restored) if that would move the fixed end.
[[nodiscard]] bool quantise_shifts(std::vector<Anchor>& anchors, bool fixed_end, double rate_hz,
                                   std::int64_t step_samples = 1);

/// invalid_argument: a note of another track, overlapping or unsorted active notes, an invalid
/// note, a note ending after a non-origin `source_end`.
[[nodiscard]] Result<void> check_notes(std::span<const Note> notes, const EditList& edits,
                                       SourceTime source_end);

[[nodiscard]] bool is_identity(const std::vector<Anchor>& anchors);

}  // namespace anno::timemap_detail
