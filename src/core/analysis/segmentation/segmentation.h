#pragma once

#include "core/types/frame_grid.h"
#include "core/types/ids.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <vector>

// Note segmentation (architecture spec §4.2 segment_notes; implementation guide B3; ADR-0010).
// Design: docs/design/phase3-notes.md §1.

namespace anno {

/// One segmented note before it is described: no curves, no loudness. NOT a Note.
struct NoteSpan {
    NoteId id;
    SourceTime source_start;
    SourceTime attack_end;
    SourceTime release_start;
    SourceTime source_end;  // start <= attack_end <= release_start <= end
    Cents rough_pitch;      // the note's median pitch (absolute); refined later by perceived_pitch
    SeparationIn separation_in = SeparationIn::hard;
    double confidence_prob = 1.0;  // 0..1
};

/// The frame grid of a note: the pitch-track frames in [source_start, source_end): start =
/// frame_time of the first pitch frame >= source_start, hop = pitch_grid.hop, n_frames = how many
/// such frames there are (0 if none; segment_notes never produces such a note). Curves (drift, mod,
/// amp) are values AT those frames; nothing is interpolated off-grid. Pure; real-time safe.
[[nodiscard]] FrameGrid note_grid(const NoteSpan& span, const FrameGrid& pitch_grid);

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
        0.120};               // how far before the voiced part a consonant onset may pull the start
    Cents stable_band{40.0};  // attack/release: within this of the note pitch counts as stable
    Seconds stable_min{0.030};  // ... for at least this long
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
