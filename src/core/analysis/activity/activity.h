#pragma once

#include "core/dsp/mono.h"
#include "core/types/frame_grid.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Activity: which frames of a recording are above its noise floor (ADR-0011, an internal analysis
// stage; no gap, breath or segment-timeline work). Design: docs/design/phase3-activity.md §3.

namespace anno {

enum class FrameActivity : std::uint8_t { silent, active };

struct ActivityParams {
    Seconds window{0.025};  // RMS window, centred on each grid frame
    Hz highpass{60.0};      // rumble removed before the level is measured (zero phase)
    // Noise floor = the lowest 3 dB band of frame levels that holds at least this share of the
    // frames (digital silence excluded), so a take with only a few percent of silence still finds
    // its floor.
    double floor_mass_prob = 0.02;
    Decibels floor_band{3.0};
    double range_percentile_prob = 0.95;  // top of the dynamic-range test (min_range)
    Decibels on_margin{9.0};              // a frame becomes active at floor + on_margin
    Decibels off_margin{5.0};   // ... and stays active down to floor + off_margin (hysteresis)
    Seconds min_active{0.040};  // shorter active runs are silent (clicks, thumps)
    Seconds max_dip{0.120};     // silent runs shorter than this between active runs stay active
    Decibels min_range{10.0};   // (range percentile - floor) below this: no silence, all active
    Decibels absolute_floor{-90.0};  // frames below this (digital silence) are silent regardless
};

struct ActivityTrack {
    FrameGrid grid;                       // the grid it was computed for (source axis)
    std::vector<Decibels> level;          // dBFS of the high-passed signal, one per grid frame
    Decibels noise_floor{-120.0};         // the estimated floor, dBFS
    std::vector<FrameActivity> activity;  // one per grid frame
};

/// Pre: i < track.activity.size(). Pure; real-time safe.
[[nodiscard]] inline bool is_active(const ActivityTrack& track, std::size_t i) noexcept {
    return track.activity[i] == FrameActivity::active;
}

/// Level, noise floor and silent/active label of every frame of `grid` (source axis), from a mono
/// signal the caller has already read. A frame's level is the RMS of the high-passed signal in
/// `window` centred on the frame time (the part of the window inside the signal), in dBFS, floored
/// at -120. Errors: invalid_argument (window, highpass, min_active, max_dip, floor_band not finite
/// and > 0; margins not finite or off_margin > on_margin; floor_mass_prob outside (0, 0.5);
/// range_percentile_prob outside (0.5, 1); min_range negative; highpass at or above half the
/// sample rate; invalid grid; sample rate not finite and > 0). An empty signal gives all-silent
/// frames (or none for an empty grid). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<ActivityTrack> detect_activity(const MonoAudio& mono, const FrameGrid& grid,
                                                    const ActivityParams& params);

/// A copy of `track` with every silent frame unvoiced: f0 NaN, voicing_prob 0 and confidence_prob 1
/// (the confidence of an unvoiced frame is the unvoiced mass, as the estimator defines it), the
/// same in each alternate. `track` is not modified. Errors: invalid_argument (`activity.grid`
/// differs from `track.grid`, or an array of either does not match its grid). Pure; thread-safe;
/// not real-time safe (allocates).
[[nodiscard]] Result<PitchTrack> gate_pitch(const PitchTrack& track, const ActivityTrack& activity);

}  // namespace anno
