#pragma once

#include "cli/commands/render.h"

#include <string>

// `anno-cli audition <scenario.json> --out <dir>`: the listening package of
// docs/AUDIO_VERIFICATION.md §3. Design: docs/design/phase2-render-skeleton.md.

namespace anno::cli {

struct AuditionSummary {
    bool all_checks_passed = false;
    std::string report;  // the text written to report.md
};

/// Renders the scenario and writes original.wav, edited.wav, original_matched.wav (the original
/// scaled to the edited RMS so A/B level is fair; the scale is in the report), difference.wav
/// (edited - original; only meaningful without timing edits), edits.json, report.md and INDEX.md
/// into `out_dir` (created if needed). The report lists the objective checks with measured values
/// and PASS/FAIL; it never judges how the audio sounds, and says a human still has to listen.
/// Checks: null test (empty edit list renders the source), untouched regions (outside
/// dirty_regions), click detector, sanity, determinism, length.
/// Errors: as run_render (an engine refusal returns its not_implemented error and writes no
/// package; scripts/phase4_audition.py records it), plus io_error for the output folder. A failing
/// check is not an error; it is `all_checks_passed == false`. Not real-time safe.
[[nodiscard]] Result<AuditionSummary> run_audition(const std::filesystem::path& scenario_path,
                                                   const std::filesystem::path& out_dir,
                                                   EngineChoice engine,
                                                   const std::filesystem::path& repo_root);

}  // namespace anno::cli
