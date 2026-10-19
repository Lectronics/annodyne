#pragma once

#include "core/types/frame_grid.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <vector>

// Note: the central analysis object (architecture spec §3). Immutable once analysed; edits live
// in an EditList, never in a Note (CLAUDE.md rule 1).
// Design: docs/design/phase1-data-model.md.

namespace anno {

enum class SeparationIn { hard, soft };  // boundary type with the previous note

struct Note {
    NoteId id;
    TrackId track_id;
    int voice = 0;  // 0 for monophonic; 0..N inside chords

    // All on the source axis. start <= attack_end <= release_start <= end.
    SourceTime source_start;
    SourceTime attack_end;      // end of the transient/consonant
    SourceTime release_start;
    SourceTime source_end;

    Cents perceived_pitch;      // musically weighted pitch (blob position), absolute
    Cents pitch_center;         // stability-weighted centre, absolute

    FrameGrid grid;                // grid of the three curves below
    std::vector<Cents> pitch_drift;  // slow component, relative to pitch_center
    std::vector<Cents> pitch_mod;    // fast component (vibrato), relative to pitch_center
    std::vector<Decibels> amp;       // level over the note, dBFS
    LoudnessUnits loudness;          // single K-weighted value, LUFS

    HarmonicTrack harmonics;
    NoiseTrack noise;
    EnvelopeTrack envelope;

    SeparationIn separation_in = SeparationIn::hard;
    double confidence_prob = 1.0;  // 0..1
    bool is_active = true;         // false = ghost candidate (DNA) the user can switch on
};

/// Errors: invalid_argument if an id is empty, voice < 0, the four times are out of order, a
/// curve is not grid.n_frames long, a nested track's grid is neither empty (n_frames == 0) nor
/// equal to `grid`, a nested track is invalid, or confidence_prob is outside 0..1.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<void> validate(const Note& note);

}  // namespace anno
