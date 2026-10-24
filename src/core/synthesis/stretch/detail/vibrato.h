#pragma once

#include "core/synthesis/grain/period_curve.h"
#include "core/synthesis/stretch/detail/class_map.h"
#include "core/synthesis/stretch/detail/geometry.h"

#include <optional>
#include <vector>

// The vibrato clock of a stretched voiced run (ADR-0023 amendment 3; docs/design/
// phase5-vibrato-stretch.md): the vibrato term of the output pitch, regenerated at the source's
// rate and depth instead of re-timed with the audio. Kernel code in source/output samples
// (ADR-0003).

namespace anno::stretch_detail {

inline constexpr double k_vibrato_min_hz = 3.0;
inline constexpr double k_vibrato_max_hz = 9.0;
inline constexpr double k_vibrato_scan_step_hz = 0.05;
inline constexpr double k_vibrato_min_depth_cents = 5.0;
inline constexpr double k_vibrato_min_cycles = 2.0;
inline constexpr double k_demod_cycles = 1.5;  // Hann window of the amplitude/phase estimate

/// What the clock measured, for tests and reports.
struct ClockInfo {
    double rate_hz = 0.0;         // f_v: the run's vibrato rate (DFT peak)
    double depth_cents = 0.0;     // amplitude of the run's vibrato
    double rate_change_hz = 0.0;  // f_v * eps: what closing the phase at a join changed
};

/// `pitch_mod` of the notes along the run's source interval: (source sample, cents), sorted.
using VibratoSource = std::vector<grain::CentsPoint>;

/// The frames of a run's fast pitch component and the local amplitude/phase estimate at `f_hz`.
struct Demod {
    std::vector<double> t_s;  // frame times, source seconds, sorted
    std::vector<double> v;    // fast pitch component, cents, mean removed
    double mean = 0.0;        // what was removed from `v`
    double f_hz = 0.0;

    /// {amplitude in cents, total phase 2 pi f t + arg z at `t_s`}: z is the Hann-windowed
    /// (1.5 cycles) complex demodulation, taken half a window inside the frames when `t_s` is
    /// nearer an edge (so the window is whole there and the values are held, not extrapolated).
    [[nodiscard]] std::pair<double, double> amplitude_and_phase(double t_s) const;

    /// The fast component (cents, mean included) at `t_s`, linear between frames, held at the ends.
    [[nodiscard]] double fast_at(double t_s) const;
};

class VibratoClock {
public:
    /// The clock of `run` (absolute output samples of the span), or nullopt if there is no vibrato
    /// to preserve: the rate outside 3..9 Hz (or on the edge of the scan), the depth under 5
    /// cents, or fewer than two cycles in the run. `join_begin`/`join_end`: the run continues
    /// voiced into slope-1 audio at that edge, so the phase must agree there. Uses only the run,
    /// the geometry and `source` (frames of the run's source interval): never the requested range.
    /// Pure; thread-safe; not real-time safe.
    [[nodiscard]] static std::optional<VibratoClock> make(const VibratoSource& source,
                                                          const SpanGeometry& geometry, Run run,
                                                          bool join_begin, bool join_end);

    /// The fast pitch term (cents) at output position `out_position` (fractional output samples):
    /// the source's fast component at the time the audio plays, with its fitted sinusoid taken out
    /// and the sinusoid regenerated at the source's rate. What is not vibrato (glides, jitter)
    /// stays with the audio; at slope 1 the term is the source's own, exactly.
    [[nodiscard]] double cents(double out_position) const;

    [[nodiscard]] const ClockInfo& info() const { return info_; }

private:
    VibratoClock() = default;

    SpanGeometry geometry_;
    double run_begin_ = 0.0;        // output samples
    double phase_begin_rad_ = 0.0;  // total phase at run_begin_
    double phase_step_rad_ = 0.0;   // per output sample, (1 + eps) * 2 pi f_v / rate
    Demod demod_;
    ClockInfo info_;
};

/// The pitch the synthesis marks follow: the curve at the source time of the position, plus the
/// regenerated vibrato when there is a clock (then `curve` holds the slow part only).
struct OutputPitch {
    const grain::PeriodCurve* curve = nullptr;
    const SpanGeometry* geometry = nullptr;
    const VibratoClock* clock = nullptr;
    double rate_hz = 0.0;

    /// Period in output samples at `out_position`, inside the period limits.
    [[nodiscard]] double period_samples(double out_position) const;
};

}  // namespace anno::stretch_detail
