#pragma once

#include "core/analysis/activity/activity.h"
#include "core/analysis/envelope/envelope.h"
#include "core/analysis/harmonic_noise/harmonic_noise.h"
#include "core/analysis/loudness/loudness.h"
#include "core/analysis/onsets/onsets.h"
#include "core/analysis/partials/partials.h"
#include "core/analysis/perceived_pitch/perceived_pitch.h"
#include "core/analysis/pitch/c4dm_pyin.h"
#include "core/analysis/pitch/pyin.h"
#include "core/analysis/pitch_target/pitch_target.h"
#include "core/analysis/pitch_target/take_annotation.h"
#include "core/analysis/segmentation/segmentation.h"
#include "core/types/analysis_result.h"
#include "core/types/audio_source.h"
#include "core/types/ids.h"
#include "core/types/result.h"

#include <cstdint>
#include <memory>

// analyse(): the orchestrator of the analysis stages (architecture spec §4.2; ROADMAP Phase 3).
// Design: docs/design/phase3-orchestrator-cli.md §1.

namespace anno {

/// Which pitch tracker analyse() runs: the vendored c4dm/pyin pipeline (ADR-0014) or the earlier
/// annodyne pYIN (kept for comparison until Phase 13).
enum class PitchEngine { c4dm_pyin, annodyne_pyin };

struct AnalyseParams {
    Algorithm algorithm = Algorithm::melodic;  // others: not_implemented (Phases 11+)
    TrackId track{"t1"};                       // track_id of every Note
    std::uint32_t version = 1;  // AnalysisResult::version; the caller bumps it on re-analysis
    PitchEngine engine = PitchEngine::c4dm_pyin;
    PitchParams pitch;
    ActivityParams activity;   // ADR-0011: silent frames are forced unvoiced before onsets
    C4dmPYinParams c4dm_pyin;  // PitchEngine::c4dm_pyin
    PYinParams pyin;           // PitchEngine::annodyne_pyin
    OnsetParams onsets = analysis_onset_params();  // the staged pipeline (ADR-0022)
    SegmentParams segmentation;
    PitchDecompositionParams decomposition;
    PitchTargetParams pitch_target;  // ADR-0013
    LoudnessParams loudness;
    PartialsParams partials;             // Phase 4: harmonic tracks (ADR-0016)
    HarmonicNoiseParams harmonic_noise;  // Phase 4: noise bands and sound classes (ADR-0017)
    EnvelopeParams envelope;             // Phase 4: spectral envelope
};

/// Runs, in order: read_mono + k_weight (once), detect pitch (params.engine: the vendored c4dm/pyin
/// by default, ADR-0014), detect_activity on the pitch grid and gate_pitch (silent frames become
/// unvoiced; ADR-0011), detect_onsets (with the gated pitch track), segment_notes, then per
/// NoteSpan: note_grid, measure_loudness, decompose_pitch_curve, then builds the Note with centre,
/// drift, mod, amp and loudness filled and calls estimate_pitch_target on it (with the neighbouring
/// spans' rough pitches and gaps, the gated pitch track and the onsets) and stores the PitchTarget;
/// then analyse_partials, split_harmonic_noise (with the pitch track's voicing and second-best path
/// on the note's frames) and estimate_envelope fill the note's harmonics, noise and envelope
/// tracks; only then is the Note final. `source_id = src.id()`; the tuning reference stays 440 Hz
/// (estimated in Phase 8). The result passes validate(AnalysisResult) and is returned as a shared
/// immutable snapshot. Every per-note stage is guaranteed to succeed for a span segment_notes
/// returned (at least 10 pitch frames, voiced); if one fails anyway the whole analysis fails with
/// the note id in the message (a bug, never silently dropped). Errors: not_implemented (algorithm
/// other than melodic), invalid_argument (bad params, or a source rate below 4 kHz: the loudness
/// K-weighting needs it and pitch needs twice max_f0), or any stage's error (the message names the
/// stage). The source is never modified. The same source and params give a bit-identical result on
/// the same build. Pure; thread-safe; not real-time safe. A cancel token (spec §4 EngineContext) is
/// not needed yet and arrives with the Phase 9 scheduler.
[[nodiscard]] Result<std::shared_ptr<const AnalysisResult>> analyse(const AudioSource& src,
                                                                    const AnalyseParams& params);

}  // namespace anno
