#pragma once

#include "core/types/audio_source.h"
#include "core/types/ids.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <cstdint>
#include <optional>
#include <vector>

// AnalysisResult: everything the analysis found in one source (architecture spec §3). Shared as
// std::shared_ptr<const AnalysisResult>; nobody holds a mutable reference (architecture.md).
// BeatGrid, TempoCurve, KeyEstimate and ChordTrack are added in Phase 8 (ADR-0005 §10).
// Design: docs/design/phase1-data-model.md.

namespace anno {

enum class Algorithm {
    melodic,
    percussive,
    percussive_pitched,
    poly_sustain,
    poly_decay,
    universal
};

struct AnalysisResult {
    std::uint32_t version = 0;  // increments on every re-analysis
    SourceId source_id;
    Algorithm algorithm = Algorithm::melodic;
    std::optional<PitchTrack> pitch;
    OnsetList onsets;
    std::vector<Note> notes;  // sorted by source_start, then voice
    std::optional<NoiseTrack> residual;  // energy not explained by any note (polyphonic)
    Hz tuning_ref{440.0};     // detected reference for A4
};

/// Errors: invalid_argument if a note, track or the onset list is invalid, note ids are not
/// unique, notes are not sorted by (source_start, voice), or tuning_ref is not finite and > 0.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<void> validate(const AnalysisResult& analysis);

/// The note with this id, or nullptr. Linear search. Pure; real-time safe; the pointer is valid
/// while `analysis` is.
[[nodiscard]] const Note* find_note(const AnalysisResult& analysis, const NoteId& id) noexcept;

}  // namespace anno
