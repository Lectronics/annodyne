#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Fade Tool (features spec §7, ROADMAP Phase 6): fade in and out of a note. Reads the analysis,
// never audio. Design: docs/design/phase6-level.md.

namespace anno {

/// How a note fades: `in` seconds from its start and `out` seconds before its end, with `shape`.
struct Fade {
    NoteId note;
    Seconds in;
    Seconds out;
    FadeShape shape = FadeShape::equal_power;
};

/// The edits for a fade: `fade_in`, `fade_out` and `fade_shape`, all op set, origin "fade_tool",
/// category fade, in that order (add them as one group). Fades longer than the note are accepted
/// (the renderer shrinks them in proportion).
/// Errors: invalid_argument (unknown or inactive note, a non-finite or negative duration, an
/// unknown shape). Pure; thread-safe; not real-time safe (allocates the specs).
[[nodiscard]] Result<std::vector<EditSpec>> fade_edits(const AnalysisResult& analysis,
                                                       const Fade& fade);

}  // namespace anno
