#pragma once

#include "core/types/analysis_result.h"

#include <string>

// JSON export of an analysis for humans and tests (not a persistence format: that is Phase 7).
// nlohmann/json is linked here only (ADR-0007). Design: docs/design/phase3-orchestrator-cli.md §2.

namespace anno::cli {

struct AnalysisJsonOptions {
    bool include_pitch_track = false;  // the per-frame f0, voicing and confidence arrays (large)
    double source_duration_s = 0.0;    // written as source.duration_s when > 0
};

/// Pretty-printed JSON (2 spaces), ending in a newline:
///   { "source": {"id", "duration_s"?}, "version", "algorithm",
///     "notes": [{"id", "start_s", "attack_end_s", "release_start_s", "end_s",
///                "perceived_pitch_cents", "pitch_center_cents", "loudness_lufs", "confidence",
///                "separation_in": "hard"|"soft"}],
///     "onsets": [{"time_s", "strength", "kind"}],
///     "pitch": {"hop_s", "start_s", "n_frames", "f0_cents": [number|null], "voicing": [],
///     "confidence": []}  // only with the option }
/// Times are source seconds, pitch is absolute cents (A4 = 6900), NaN is written as null. Pure;
/// thread-safe; not real-time safe.
[[nodiscard]] std::string analysis_to_json(const AnalysisResult& analysis,
                                           const AnalysisJsonOptions& options);

}  // namespace anno::cli
