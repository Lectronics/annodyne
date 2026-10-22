#pragma once

#include "core/analysis/onsets/stages/peak_pick.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <cstdint>
#include <span>
#include <vector>

// Stage L: sample-accurate placement of the picked peaks.
// Stage conventions (all stages): STFT magnitudes are core/dsp's (periodic Hann, no 1/N scaling: a
// sine of amplitude A has |X| about A * n_fft / 4) of a float signal in [-1, 1]. A band value is a
// unit-sum weighted MEAN of magnitudes. The activation is the MEAN over bands of the positive log
// difference (natural log). Every function is pure, thread-safe and not real-time safe (it
// allocates); inputs are never written. Times are on the source axis, frame i centred at i * hop
// from the start of the analysed signal. Design: docs/design/phase5-transient-pipeline.md.

namespace anno {

/// Where the search for the halfway crossing starts. `from_window_start` (Phase 3): at the start of
/// the +-search window, so in a dense passage the first sample above the halfway level can be the
/// PREVIOUS note's release tail. `from_quietest_point`: at the quietest point before the peak, i.e.
/// the energy must rise through the halfway point after its valley (fast notes, legato).
enum class LocaliseScan { from_window_start, from_quietest_point };

struct LocaliseParams {
    LocaliseScan scan = LocaliseScan::from_window_start;
    Seconds search{0.025};         // +- around the peak
    Seconds energy_window{0.002};  // short-window energy
    Decibels min_rise{6.0};        // a smaller rise leaves the frame time
    Decibels end_drop{
        3.0};  // louder at the start of the region than at its end by this: a note end
};

enum class LocaliseOutcome { rose, unchanged };

struct LocalisedOnset {
    std::int64_t sample = 0;
    SourceTime time;  // SourceTime{} + Seconds{sample / rate}
    double strength = 0.0;
    LocaliseOutcome outcome =
        LocaliseOutcome::unchanged;  // rose: the short-window energy really rose
};

/// Moves each peak to the sample where the short-window energy (energy_window, in dB) rises through
/// the halfway point between the quietest point before the peak and the loudest after it, searched
/// within +-search; a rise under min_rise leaves the frame's own sample (outcome unchanged). A peak
/// where the signal is louder at the start of the region than at its end by end_drop is a note END
/// (spectral flux responds to offsets) and is dropped. Sorted as the peaks. Errors:
/// invalid_argument for a non-positive search or window, a negative min_rise or end_drop, a peak
/// beyond the signal.
[[nodiscard]] Result<std::vector<LocalisedOnset>>
localise(std::span<const float> mono, Hz rate, const PeakList& peaks, const LocaliseParams& params);

}  // namespace anno
