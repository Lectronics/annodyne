#pragma once

#include "core/analysis/activity/activity.h"
#include "core/analysis/gaps/gap_evidence.h"
#include "core/analysis/harmonic_noise/harmonic_noise.h"
#include "core/dsp/mono.h"
#include "core/types/noise_floor.h"
#include "core/types/result.h"

// Consonant and breath evidence for the gap regions (ADR-0017 features over the gaps; design:
// docs/design/phase5-gap-consonants.md).

namespace anno {

struct GapEvidenceParams {
    // ADR-0017 thresholds (provisional). `silence_level` is IGNORED: it is replaced by
    // k_level_floor_db so the features and the floor, not an absolute level, decide.
    HarmonicNoiseParams features;
};

/// Runs measure_frames and classify_frames (analysis/harmonic_noise) over the whole recording with
/// voicing 0 on every frame (zero pitch curves, a HarmonicTrack without harmonics, no alternates),
/// so every frame is judged by its noise features: centroid, zero-crossing rate, flux, high-band
/// share, closure dip. Keeps the class and `activity.level - floor.curve` per frame of
/// `activity.grid`; entries of note frames are unspecified. An empty grid gives empty evidence.
/// Errors: invalid_argument (empty signal; sample rate below 8 kHz, which measure_frames rejects,
/// so the caller passes neutral_gap_evidence instead; the activity grid and the floor on different
/// grids or arrays that do not match; bad params). Pure; thread-safe; not real-time safe
/// (allocates). Cost: one STFT pass over the recording (a 4096 and a 1024 point FFT per frame,
/// about 25 MB transient for 10 minutes).
[[nodiscard]] Result<GapEvidence> classify_gap_frames(const MonoAudio& mono,
                                                      const ActivityTrack& activity,
                                                      const NoiseFloor& floor,
                                                      const GapEvidenceParams& params);

}  // namespace anno
