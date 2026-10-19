#pragma once

#include "core/types/analysis_result.h"
#include "core/types/result.h"

#include <filesystem>
#include <string>

// Human-inspectable plots of an analysis: CSV tables and a dependency-free SVG (pitch curve, note
// blobs, onset ticks). No third-party code. Design: docs/design/phase3-orchestrator-cli.md §2.

namespace anno::cli {

/// `pitch.csv`: header `time_s,f0_cents,voicing,confidence` and one row per pitch frame (empty f0
/// when unvoiced). Empty text apart from the header if the analysis has no pitch track. Pure.
[[nodiscard]] std::string pitch_csv(const AnalysisResult& analysis);

/// `notes.csv`: header `id,start_s,attack_end_s,release_start_s,end_s,perceived_pitch_cents,
/// pitch_center_cents,loudness_lufs,confidence,separation_in`, one row per note. Pure.
[[nodiscard]] std::string notes_csv(const AnalysisResult& analysis);

/// A standalone SVG: time on the x axis (150 px per second), pitch in semitones on the y axis with
/// a line per semitone and a label per C, the f0 curve as polylines over its voiced runs, each note
/// as a rectangle (class "note") spanning its time and +-0.4 semitone around its perceived pitch
/// with its id, each onset as a tick (class "onset"). Pure.
[[nodiscard]] std::string plot_svg(const AnalysisResult& analysis);

/// Creates `dir` if needed and writes pitch.csv, notes.csv and plot.svg into it.
/// Errors: io_error if a file cannot be written. Not thread-safe against writers of the same files.
[[nodiscard]] Result<void> write_plots(const AnalysisResult& analysis,
                                       const std::filesystem::path& dir);

}  // namespace anno::cli
