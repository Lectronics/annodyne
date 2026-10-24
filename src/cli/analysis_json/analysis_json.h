#pragma once

#include "core/types/analysis_result.h"

#include <string>

// JSON export of an analysis for humans and tests (not a persistence format: that is Phase 7).
// nlohmann/json is linked here only (ADR-0007). Design: docs/design/phase3-orchestrator-cli.md §2.

namespace anno::cli {

struct AnalysisJsonOptions {
    bool include_pitch_track =
        false;                       // the per-frame f0, voicing, confidence and floor-curve arrays
    double source_duration_s = 0.0;  // written as source.duration_s when > 0
};

/// Pretty-printed JSON (2 spaces), ending in a newline:
///   { "source": {"id", "duration_s"?}, "version", "algorithm",
///     "notes": [{"id", "start_s", "attack_end_s", "release_start_s", "end_s",
///                "pitch_target": {"kind", "target_cents", "secondary_cents"|null, "core_start_s",
///                "core_end_s", "approach", "release", "flags": [], "confidence"},
///                "pitch_center_cents", "loudness_lufs", "confidence",
///                "separation_in": "hard"|"soft"}],
///     "onsets": [{"time_s", "strength", "kind"}],
///     "gaps": [{"id", "before"|null, "after"|null, "start_s", "end_s", "floor_dbfs",
///                "regions": [{"kind", "start_s", "end_s"}]}],  // only if the analysis has a
///                timeline
///     "noise_floor": {"hop_s", "start_s", "n_frames", "band_centre_hz": [], "band_dbfs": [],
///                "curve_dbfs": []},  // only if present; the curve only with the option
///     "pitch": {"hop_s", "start_s", "n_frames", "f0_cents": [number|null], "voicing": [],
///     "confidence": []}  // only with the option }
/// Times are source seconds, pitch is absolute cents (A4 = 6900), NaN is written as null. Pure;
/// thread-safe; not real-time safe.
[[nodiscard]] std::string analysis_to_json(const AnalysisResult& analysis,
                                           const AnalysisJsonOptions& options);

}  // namespace anno::cli
