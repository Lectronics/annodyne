#pragma once

#include "core/analysis/onsets/stages/spectral_flux.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <cstddef>
#include <vector>

// Stage K: the research's windowed peak picking.
// Stage conventions (all stages): STFT magnitudes are core/dsp's (periodic Hann, no 1/N scaling: a
// sine of amplitude A has |X| about A * n_fft / 4) of a float signal in [-1, 1]. A band value is a
// unit-sum weighted MEAN of magnitudes. The activation is the MEAN over bands of the positive log
// difference (natural log). Every function is pure, thread-safe and not real-time safe (it
// allocates); inputs are never written. Times are on the source axis, frame i centred at i * hop
// from the start of the analysed signal. Design: docs/design/phase5-transient-pipeline.md.

namespace anno {

/// A picked frame and its strength: the detector's output at the frame over its threshold, >= 1 for
/// a picked peak (both pickers).
struct ActivationPeak {
    std::size_t frame = 0;
    double strength = 0.0;
};

struct WindowedPeakParams {
    Seconds pre_max{0.030};
    Seconds post_max{0.030};
    Seconds pre_avg{0.100};
    Seconds post_avg{0.070};
    Seconds min_gap{0.030};
    // An increment above the local mean, in activation units. Provisional and arbitrary (it is NOT
    // Phase 3's min_activation, which floors a threshold). Chosen on the bench of
    // tests/support/onset_bench.h (pooled F plateau 0.02-0.05 at log_gain 1, lag 15 ms) with the
    // signals it is scored on: optimistic.
    double delta_activation = 0.05;
};

struct PeakList {
    Seconds hop;
    std::vector<ActivationPeak> peaks;  // sorted by frame
};

/// The research's rule (Bock 2012; section 2.4), offline. With counts = round(seconds / hop), frame
/// n is a peak when (1) act(n) > 0, (2) act(n) is the maximum over [n - pre_max, n + post_max]
/// clamped to the signal, the FIRST frame winning a tie, (3) act(n) >= mean(act over [n - pre_avg,
/// n + post_avg]) + delta_activation, the mean taking 0 for frames outside the signal and dividing
/// by the full window width, and (4) n - previous kept peak > min_gap (first wins; the reference
/// compares with the previous raw detection). strength = act / max(mean + delta_activation, 1e-9).
/// Errors: invalid_argument for a negative window, a non-positive hop, a negative delta.
[[nodiscard]] Result<PeakList> pick_peaks_windowed(const Activation& activation,
                                                   const WindowedPeakParams& params);

}  // namespace anno
