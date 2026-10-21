#pragma once

#include "cli/commands/render.h"

#include <string>
#include <vector>

// `anno-cli audition <scenario.json> --out <dir>`: the listening package of
// docs/AUDIO_VERIFICATION.md §3. Design: docs/design/phase2-render-skeleton.md; the pitch rows:
// docs/design/phase4-pitch-edit.md (stages 8 and 9).

namespace anno::cli {

/// The kinds of check a report row can be (tests select rows by kind, not by display name).
enum class AuditionCheck {
    null_test,
    untouched,
    click,
    sanity,
    determinism,
    length,
    pitch_round_trip,
    vibrato,
    first_formant,
};

/// One objective check of the report.
struct AuditionRow {
    AuditionCheck kind = AuditionCheck::null_test;
    std::string name;  // display name, e.g. "Pitch round trip (n000200)"
    bool passed = false;
    std::string measured;  // the numbers, as written in report.md
};

struct AuditionSummary {
    bool all_checks_passed = false;
    std::vector<AuditionRow> rows;
    std::string report;  // the text written to report.md
};

/// Renders the scenario and writes original.wav, edited.wav, original_matched.wav (the original
/// scaled to the edited RMS so A/B level is fair; the scale is in the report), difference.wav
/// (edited - original; only meaningful without timing edits), edits.json, report.md and INDEX.md
/// into `out_dir` (created if needed). The report lists the objective checks with measured values
/// and PASS/FAIL; it never judges how the audio sounds, and says a human still has to listen.
/// Checks: null test (empty edit list renders the source), untouched regions (outside
/// dirty_regions), click detector, sanity, determinism, length; for an analysed scenario also, per
/// edited note, pitch round trip, vibrato preservation and first formant (cli/checks/pitch_checks;
/// the render is analysed again and compared with the analysis of the source; the vibrato row only
/// for a note whose source vibrato reaches 10 cents; a formant found in the source but not in the
/// render is a FAIL row; an unmeasurable pitch round trip is a FAIL row). The requested shift is
/// the edits' effective centre offset; `expect` is echoed, not checked. Equal-length render and
/// source are assumed (no timing edits). Errors: as run_render (an engine refusal returns its
/// not_implemented error and writes no package; scripts/phase4_audition.py records it), plus
/// io_error for the output folder. A failing check is not an error; it is `all_checks_passed ==
/// false`. Not real-time safe.
[[nodiscard]] Result<AuditionSummary> run_audition(const std::filesystem::path& scenario_path,
                                                   const std::filesystem::path& out_dir,
                                                   EngineChoice engine,
                                                   const std::filesystem::path& repo_root);

}  // namespace anno::cli
