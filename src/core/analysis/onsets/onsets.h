#pragma once

#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

// Onset detection (architecture spec §4.2 detect_onsets; implementation guide B2).
// Design: docs/design/phase3-pitch-onsets.md §2.

namespace anno {

struct OnsetParams {
    Seconds hop{0.005};     // rounded to whole samples; onset times use the actual hop
    Seconds window{0.043};  // STFT window; n_fft = the power of two nearest to window * rate
    int filterbank_bands_per_octave = 24;  // log-spaced triangular filters (SuperFlux), from 30 Hz
    double log_lambda = 1000.0;  // log(1 + lambda * |X|) on magnitudes of a [-1, 1] signal
    int max_filter_bins =
        3;               // frequency max filter over neighbouring bands; 1 = plain spectral flux
    Seconds lag{0.015};  // difference lag (at least one frame)
    Seconds threshold_window{0.5};  // adaptive threshold: local mean + delta * local spread
    double delta = 2.0;
    double min_activation =
        0.15;  // absolute floor on the activation (mean positive log difference)
    Seconds min_gap{0.030};
    Cents pitch_change_threshold{80.0};  // f0 step between stable regions counted as an onset
    Seconds pitch_stable{0.040};         // each side of a step must be stable this long
    Seconds refine_window{0.025};        // the energy refinement searches +- this around the peak
};

/// Spectral onsets (SuperFlux; Bock & Widmer 2013): log-filterbank STFT, frequency max filter over
/// the reference frame, positive lagged difference averaged over the bands, adaptive-threshold peak
/// picking with `min_gap`, then each peak is refined to the point where the short-window energy
/// rises (sample-accurate attacks). `OnsetList::strength` is the activation divided by its
/// threshold (>= 1); every onset is `OnsetKind::attack` (no voicing data). Sorted by time. Silence
/// and signals shorter than the STFT window give an empty list; an empty source gives an empty
/// list. Errors: invalid_argument (hop, window, lag, threshold window, delta, min_gap not positive
/// and finite, filterbank_bands_per_octave < 1, max_filter_bins < 1). Pure; thread-safe; not
/// real-time safe.
[[nodiscard]] Result<OnsetList> detect_onsets(const AudioSource& src, const OnsetParams& params);

/// As above, plus pitch-change onsets (legato note changes, `OnsetKind::pitch_change`, strength =
/// step / threshold) from `pitch`, merged with the spectral ones (within `min_gap` the stronger is
/// kept, the spectral one on a tie because its time is sample-accurate), and spectral onsets are
/// classed `consonant` when the pitch frame 20 ms after them is unvoiced (voicing < 0.5), else
/// `attack`. Errors: as above, and invalid_argument if `pitch` is invalid.
[[nodiscard]] Result<OnsetList> detect_onsets(const AudioSource& src, const PitchTrack& pitch,
                                              const OnsetParams& params);

}  // namespace anno
