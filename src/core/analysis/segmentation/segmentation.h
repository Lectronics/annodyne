#pragma once

#include "core/types/frame_grid.h"
#include "core/types/ids.h"
#include "core/types/note.h"
#include "core/types/note_span.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <vector>

// Note segmentation (architecture spec §4.2 segment_notes; implementation guide B3; ADR-0010).
// Design: docs/design/phase3-notes.md §1.

namespace anno {

struct SegmentParams {
    Seconds min_note{0.060};
    Seconds vibrato_smooth{0.150};        // moving-mean window of f0 (cents) before the HMM
    double note_bin_cents = 100.0 / 3.0;  // 3 pitch states per semitone
    double pitch_sigma_cents = 45.0;      // observation spread around a pitch bin
    double stay_stable = 0.995;           // P(stable -> stable)
    double stable_to_silence = 0.005;     // P(stable -> silence)
    double new_note_prob = 1e-3;          // P(a new note starts) per frame without an onset
    double onset_new_note_prob =
        0.98;  // ... at the frames of an onset (+-10 ms): onsets are strong evidence
    Seconds preroll_max{
        0.030};               // how far before the voiced part a consonant onset may pull the start
    Cents stable_band{40.0};  // attack/release: within this of the note pitch counts as stable
    Seconds stable_min{0.030};  // ... for at least this long
    // Transient-driven boundaries (docs/design/phase3-segmentation-transient.md): a pitch change
    // alone does not make a new note.
    double merge_onset_strength = 1.5;  // an onset weaker than this (OnsetList::strength) is not
                                        // evidence of a new note
    Cents merge_max_cents{80.0};       // neighbouring runs further apart than this are never merged
    Seconds merge_onset_zone{0.040};   // an onset this close to a run boundary separates the runs
    Seconds min_gap_for_split{0.030};  // an unvoiced gap this long always separates two notes
    Seconds trill_max_plateau{0.150};  // pitch_change onsets of a trill cluster (>= trill_min_steps
    int trill_min_steps = 3;           // alternating steps, plateaus up to this long) do not split
    Seconds snap_before{0.030};        // a start snaps to an onset up to this much before it ...
    Seconds snap_after{0.040};         // ... or this much after the voiced start
};

/// Spans sorted by source_start, non-overlapping (monophonic). The id is "n" + start in
/// milliseconds (6 digits; a collision gets "_1", "_2"). separation_in is soft when the previous
/// note ends within 20 ms of this note's start, else hard. confidence_prob = mean voicing
/// probability over the note times the share of its voiced frames within 60 cents of its pitch.
/// A run shorter than min_note (or than 10 pitch frames) is absorbed by an adjacent longer run (a
/// glide becomes the attack of the note it reaches) or dropped if it has none. Time clamps: every
/// time lies in [0, last pitch frame time]; a preroll never reaches into the previous note; the
/// four times of each note are ordered. An empty or all-unvoiced track gives no notes.
/// Errors: invalid_argument (bad params, invalid pitch track or onset list).
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<NoteSpan>>
segment_notes(const PitchTrack& pitch, const OnsetList& onsets, const SegmentParams& params);

}  // namespace anno
