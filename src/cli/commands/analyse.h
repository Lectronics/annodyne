#pragma once

#include "core/types/ids.h"
#include "core/types/result.h"

#include <filesystem>
#include <optional>
#include <string>

// `anno-cli analyse <file.wav> [--json <out.json>] [--plot-dir <dir>] [--pitch-track] [--track-id
// <id>]`. Design: docs/design/phase3-orchestrator-cli.md §2.

namespace anno::cli {

struct AnalyseArgs {
    std::filesystem::path wav;
    std::optional<std::filesystem::path> json;      // --json
    std::optional<std::filesystem::path> plot_dir;  // --plot-dir
    bool pitch_track_in_json = false;               // --pitch-track (adds the per-frame arrays)
    TrackId track{"t1"};                            // --track-id
};

/// Reads the WAV (core/io), runs analyse(), writes the JSON and plots that were asked for and
/// returns a one-line summary (name, notes, duration) ending in a newline. Errors: not_found /
/// corrupt_data / io_error from the WAV reader or the writers; any analysis error. Not thread-safe
/// against writers of the same output files; not real-time safe.
[[nodiscard]] Result<std::string> run_analyse(const AnalyseArgs& args);

}  // namespace anno::cli
