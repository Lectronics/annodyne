#pragma once

#include "core/analysis/activity/activity.h"
#include "core/analysis/gaps/build_segment_timeline.h"
#include "core/analysis/gaps/classify_gap_frames.h"
#include "core/analysis/gaps/estimate_noise_floor.h"
#include "core/dsp/mono.h"
#include "core/types/audio_source.h"
#include "core/types/noise_floor.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/segments.h"

#include <span>

// The gap analysis chain in one call: noise floor, consonant evidence, segment timeline
// (ADR-0024; design: docs/design/phase5-gaps-core.md). analyse() and the scenario loader use it.

namespace anno {

struct GapAnalysisParams {
    NoiseFloorParams noise_floor;
    GapEvidenceParams evidence;
    GapParams gaps;
};

struct GapAnalysis {
    NoiseFloor noise_floor;
    SegmentTimeline timeline;
};

/// estimate_noise_floor, classify_gap_frames, then build_segment_timeline for `notes` over the
/// whole of `source`; `mono` is the source's mono mix and `activity` its activity track (the same
/// as detect_activity gave for this source).
/// Errors: those of the three stages (invalid_argument for a rate below 8 kHz or overlapping active
/// notes, out_of_range for an empty recording), the message names the stage. Pure; thread-safe;
/// not real-time safe.
[[nodiscard]] Result<GapAnalysis> analyse_gaps(const AudioSource& source, const MonoAudio& mono,
                                               std::span<const Note> notes,
                                               const ActivityTrack& activity,
                                               const GapAnalysisParams& params);

}  // namespace anno
