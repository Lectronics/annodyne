#pragma once

#include "core/analysis/onsets/stages/band_spectrogram.h"
#include "core/analysis/onsets/stages/level_norm.h"
#include "core/analysis/onsets/stages/localise.h"
#include "core/analysis/onsets/stages/peak_pick.h"
#include "core/analysis/onsets/stages/spectral_flux.h"
#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <optional>
#include <span>
#include <vector>

// The composed onset pipeline: P0 -> P1 -> [P2] -> P3 -> D -> K -> L.
// Stage conventions (all stages): STFT magnitudes are core/dsp's (periodic Hann, no 1/N scaling: a
// sine of amplitude A has |X| about A * n_fft / 4) of a float signal in [-1, 1]. A band value is a
// unit-sum weighted MEAN of magnitudes. The activation is the MEAN over bands of the positive log
// difference (natural log). Every function is pure, thread-safe and not real-time safe (it
// allocates); inputs are never written. Times are on the source axis, frame i centred at i * hop
// from the start of the analysed signal. Design: docs/design/phase5-transient-pipeline.md.

namespace anno {

struct TransientParams {
    std::optional<LevelNormParams> level;  // P0 when set
    BandParams bands;
    std::optional<WhitenParams> whiten;  // P2 when set
    LogParams log{.log_gain = 1.0};      // the bench optimum (Phase 3's detect_onsets uses 1000); a
                                         // whitened path needs its own value
    FluxParams flux;
    WindowedPeakParams peaks;
    LocaliseParams localise;
};

/// The pipeline on a mono signal: P0 (if set) -> P1 -> P2 (if set) -> P3 -> D -> K -> L. Returns
/// the localised onsets sorted by time (the outcome tells whether the energy really rose). The
/// samples that L reads are the normalised ones when P0 is set. Errors: those of the stages.
[[nodiscard]] Result<std::vector<LocalisedOnset>>
transient_onsets(std::span<const float> mono, Hz rate, const TransientParams& params);

/// transient_onsets on the mono mix of `src`. Output: kind
/// attack, strength from K, sorted by time. An empty or too short source gives an empty list.
/// Errors: those of the stages.
[[nodiscard]] Result<OnsetList> detect_transients(const AudioSource& src,
                                                  const TransientParams& params);

}  // namespace anno
