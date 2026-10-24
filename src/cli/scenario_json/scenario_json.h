#pragma once

#include "cli/scenario/scenario.h"

#include <filesystem>
#include <string_view>

// JSON reader for scenario files (docs/AUDIO_VERIFICATION.md §2; ADR-0007).

namespace anno::cli {

/// Parses scenario JSON text. `base_dir` resolves a relative "source": first against `base_dir`
/// (the scenario file's folder), then against `repo_root` if the file is not there.
/// Top-level keys: name, source (required); algorithm, analysis, notes, edits, expect (optional).
/// Unknown keys are errors; "expect" is kept verbatim and not checked here (scripts/
/// phase4_audition.py reads expect.engines). "analysis" is "given" (default: `notes` are listed by
/// hand) or "analyse" (the source is analysed at load time; `notes` is then an error, $.notes).
/// A raw edit is {category, op, param?, value?, target:{note_id | note_index | gap}} with the names
/// of to_string(...) in core/types/edit.h; value by parameter kind: number (cents, ratio, decibels,
/// seconds), string (fade_shape), {"source_s","output_s"} (time_handle); reset has no value. In
/// A gap target is {"gap": {"before": id|null, "after": id|null}} (null at the recording's
/// edge) for category gap; the gap's timeline is built from the audio when the scenario runs. In
/// analyse mode a raw edit may not use note_index (the ids are not known before the analysis).
/// An edit with a "tool" key is a tool edit, allowed only with "analysis": "analyse": the only
/// tool is {"tool": "pitch_drag", "target": {"note_index": i >= 0}, exactly one of "offset_cents"
/// (relative to the note's analysed target) | "target_cents" (absolute, A4 = 6900), "snap":
/// "chromatic"?}; unknown keys are errors. An "edits" array is all tool edits or all raw edits. A
/// note_index past the analysed notes is not a parse error: load_run reports it (invalid_argument).
/// Errors: corrupt_data (not JSON), invalid_argument (a bad field; the message names its JSON
/// path, e.g. "edits[2].value").
/// Pure apart from path checks; thread-safe; not real-time safe.
[[nodiscard]] Result<Scenario> parse_scenario(std::string_view json_text,
                                              const std::filesystem::path& base_dir,
                                              const std::filesystem::path& repo_root);

/// Reads and parses `path`. Errors: not_found, io_error, plus those of parse_scenario.
[[nodiscard]] Result<Scenario> load_scenario(const std::filesystem::path& path,
                                             const std::filesystem::path& repo_root);

}  // namespace anno::cli
