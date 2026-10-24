#pragma once

#include "core/analysis/activity/activity.h"
#include "core/types/audio_source.h"
#include "core/types/noise_floor.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <span>

// The recording's noise floor over time and its average spectrum (ADR-0024; design:
// docs/design/phase5-gaps-core.md, slice 4). All levels are RMS dBFS (full-scale sine = -3.01 dB).

namespace anno {

struct NoiseFloorParams {
    Seconds window{3.0};  // sliding window of the floor curve; clamped to the recording
    double percentile_prob =
        0.1;         // of the frame levels in the window that are not in a note, (0, 1)
    int bands = 24;  // >= 1, log-spaced from band_low to band_high
    Hz band_low{80.0};
    Hz band_high{12000.0};      // bands whose upper edge exceeds 0.45 x the sample rate are dropped
    Seconds min_support{0.1};   // a window needs at least this much outside-note time to give an
                                // estimate (a quantile of a few frames is noise, and the high-pass
                                // rings ahead of a loud onset)
    Seconds note_guard{0.015};  // frames this close to a note do not count as outside it: their
                                // RMS window reaches into the note (>= half the activity window)
    Seconds spectrum_window{0.025};  // the analysis frame of the band spectrum
    Decibels spectrum_margin{3.0};   // a frame feeds the spectrum if its level is at most the curve
                                     // + this and it lies outside every note
    int max_spectrum_frames = 1000;  // cost cap: floor frames are taken evenly strided to this many
};

/// The time-varying noise floor and the average floor spectrum of `source`.
/// The caller passes the same `ActivityTrack` that was computed from this source (the mono mix,
/// 60 Hz high-pass front end, `activity.level` = RMS dBFS per grid frame); only its grid and array
/// sizes are checked. Curve: at frame i the `percentile_prob` quantile (sorted ascending, element
/// floor(p x (m - 1)) of the m candidates) of `activity.level` over the frames within window/2 of i
/// that lie outside every active note (and further than `note_guard` from it); a window with less
/// than `min_support` of such frames takes the nearest estimate on either side (fewest frames
/// away), else the global `activity.noise_floor`. Results are clamped at k_level_floor_db.
/// Spectrum: the mean power per log-spaced band (nominal edges band_low x r^j, r = (band_high /
/// band_low)^(1 / bands); the band centre is the geometric mean of its edges), in RMS dBFS, over
/// the floor frames (outside every note, level <= curve + spectrum_margin), each analysed in a Hann
/// window of `spectrum_window` centred on the frame (zero beyond the signal); with no floor frame
/// the bands are flat at `activity.noise_floor`. It is a mean, not a percentile, and not summable
/// to the curve (band-limited, a different statistic). Errors: invalid_argument (window,
/// spectrum_window not finite and > 0; note_guard negative or not finite; percentile_prob outside
/// (0, 1); bands < 1; band_low, band_high not finite, 0 < band_low < band_high; spectrum_margin
/// negative or not finite; max_spectrum_frames < 1; an activity grid or arrays that do not match;
/// every band above 0.45 x the sample rate), out_of_range (empty recording). A recording shorter
/// than the window is fine. Pure; thread-safe; not real-time safe. Cost: O(frames x window frames)
/// for the curve, O(max_spectrum_frames x FFT) for the spectrum.
[[nodiscard]] Result<NoiseFloor> estimate_noise_floor(const AudioSource& source,
                                                      const ActivityTrack& activity,
                                                      std::span<const Note> notes,
                                                      const NoiseFloorParams& params);

}  // namespace anno
