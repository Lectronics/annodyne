#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Strip Silence, gate mode (ROADMAP Phase 5): mutes (or attenuates) the silence between notes by
// writing one gap_gain edit. Sibilants, breaths, tails and every other protected region of a gap
// are never touched (the gate acts on `silence` regions only; ADR-0024 amendment 1). Reads the
// analysis, never audio. Design: docs/design/phase5-gaps-core.md (slice 7d).

namespace anno {

enum class StripScope {
    all_gaps,      // every gap, including the leading and trailing ones
    between_notes  // only gaps with a note on both sides
};

struct StripSilenceParams {
    Seconds min_silence{0.05};  // a gap needs at least this much `silence` region time in total
    Decibels gain{-120.0};      // set on every qualifying gap; k_gap_mute_db (the default) mutes
    StripScope scope = StripScope::all_gaps;
};

/// The edits that gate the recording's silence: a single gap_gain `set` (origin "strip_silence")
/// whose target lists every gap of the timeline in scope whose `silence` regions add up to at
/// least `min_silence`, in timeline order. An empty vector if no gap qualifies.
/// Errors: invalid_argument (the analysis has no segment timeline; min_silence negative or not
/// finite; gain not finite or outside [k_gap_mute_db, +24 dB]). Pure; thread-safe; not real-time
/// safe.
[[nodiscard]] Result<std::vector<EditSpec>> strip_silence_edits(const AnalysisResult& analysis,
                                                                const StripSilenceParams& params);

}  // namespace anno
