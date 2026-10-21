#pragma once

#include "core/analysis/segmentation/detail/note_hmm.h"
#include "core/analysis/segmentation/segmentation.h"

#include <vector>

// Transient-driven merging of the note HMM's runs. Internal to core/analysis/segmentation.
// Design: docs/design/phase3-segmentation-transient.md.

namespace anno {

/// What merge_runs reads besides the runs (a data clump, passed together).
struct MergeInput {
    // Non-owning views that outlive the call (never null).
    const PitchTrack* pitch;
    const std::vector<double>* smoothed_cents;  // vibrato-smoothed f0 per pitch frame (A4 = 6900)
    const OnsetList* onsets;                    // SourceTime seconds
    const SegmentParams* params;
};

/// Merges adjacent runs (pitch-grid frames) that nothing separates. Two neighbours stay apart if
///  - an `attack`/`consonant` onset of strength >= merge_onset_strength lies within
///    merge_onset_zone of the boundary between their voiced parts (other kinds are ignored), or
///  - a `pitch_change` onset lies there and does not belong to a trill cluster (>= trill_min_steps
///    alternating steps, plateaus up to trill_max_plateau), or
///  - the unvoiced gap between them is at least min_gap_for_split, or
///  - their medians (smoothed f0) differ by more than merge_max_cents (a missed transient, not a
///    wobble). The comparison is between neighbouring original runs, so a chain of small steps can
///    merge into one run.
/// A merged run spans the first to the last frame (the unvoiced frames between included) and keeps
/// the bin of its longer part. Order and the remaining runs are unchanged. Pure; thread-safe; not
/// real-time safe.
[[nodiscard]] std::vector<NoteRun> merge_runs(std::vector<NoteRun> runs, const MergeInput& in);

}  // namespace anno
