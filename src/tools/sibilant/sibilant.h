#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Sibilant Tool (features spec 9, ROADMAP Phase 6): the level of a note's sibilants ("s", "sh"),
// breaths and plosive bursts, separately from the pitched part. Reads the analysis, never audio.
// Design: docs/design/phase6-expression-tools.md.

namespace anno {

/// Gains in dB for the three noise classes of one note; 0 leaves a class as analysed.
struct SibilantGains {
    NoteId note;
    Decibels sibilant;
    Decibels breath;
    Decibels plosive;
};

/// The edits `noise_gain_sibilant`, `noise_gain_breath` and `noise_gain_plosive` (op set, origin
/// "sibilant_tool", category sibilant), in that order, one group. The renderer applies them to the
/// frames of those classes (core/render/level.h); the voiced noise gain is not offered here.
/// Errors: invalid_argument (unknown or inactive note, a non-finite gain or one outside the legal
/// range of the parameter). Pure; thread-safe; not real-time safe (allocates the specs).
[[nodiscard]] Result<std::vector<EditSpec>> sibilant_edits(const AnalysisResult& analysis,
                                                           const SibilantGains& gains);

}  // namespace anno
