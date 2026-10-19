#pragma once

#include "cli/scenario/scenario.h"

#include <filesystem>
#include <string_view>

// JSON reader for scenario files (docs/AUDIO_VERIFICATION.md §2; ADR-0007).

namespace anno::cli {

/// Parses scenario JSON text. `base_dir` resolves a relative "source": first against `base_dir`
/// (the scenario file's folder), then against `repo_root` if the file is not there.
/// Top-level keys: name, source (required); algorithm, notes, edits, expect (optional). Unknown
/// keys are errors; "expect" is kept verbatim and not checked. An edit is
/// {category, op, param?, value?, target:{note_id | note_index}} with the names of to_string(...)
/// in core/types/edit.h; value by parameter kind: number (cents, ratio, decibels, seconds),
/// string (fade_shape), {"source_s","output_s"} (time_handle); reset has no value.
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
