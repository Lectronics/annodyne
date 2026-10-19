#pragma once

#include "core/analysis/segmentation/segmentation.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// The pitch a listener hears, and the split of a note's f0 into centre, slow drift and fast
// modulation (architecture spec §4.2 perceived_pitch, decompose_pitch_curve; implementation guide
// B4; ADR-0010). Design: docs/design/phase3-notes.md §2.

namespace anno {

/// Weights of the "musically weighted" average, shared by decompose_pitch_curve and
/// perceived_pitch.
struct StabilityWeightParams {
    double rate_scale_cents_per_s = 600.0;  // |d f0 / dt| at which the stability weight is e^-1
    double gamma = 1.0;                     // exponent on the stability weight
    Decibels amp_range{30.0};               // frames this far below the note's loudest get weight 0
};

/// w_i = exp(-(|d f0_i / dt| / rate_scale)^2)^gamma * a_i with the derivative a central difference
/// of f0 (cents) per second (one-sided at the ends), and a_i = clamp(1 - (amp_max - amp_i) /
/// amp_range, 0, 1) from the note's amp curve (a_i = 1 when `amp` is empty). Pre: f0 has no NaN,
/// amp is empty or as long as f0. Pure; thread-safe.
[[nodiscard]] std::vector<double> stability_weights(std::span<const Cents> f0, Seconds hop,
                                                    std::span<const Decibels> amp,
                                                    const StabilityWeightParams& params);

enum class CyclePolicy { whole_cycles, all_frames };

/// Attack / release trims shared by both functions: per side min(fixed trim, max_fraction x note
/// length); if the trimmed region has fewer than 3 frames the whole note is used.
struct TrimParams {
    Seconds start{0.040};
    Seconds end{0.060};
    double max_fraction = 0.25;
};

struct PitchDecompositionParams {
    Hz drift_cutoff{2.5};  // below: drift, above: modulation (vibrato)
    TrimParams trim;
    StabilityWeightParams weights;
};

struct PitchDecomposition {
    Cents center;              // absolute, constant over the note
    std::vector<Cents> drift;  // slow component relative to center, one per note_grid frame
    std::vector<Cents> mod;    // fast component relative to center
    double voiced_fraction =
        0;  // share of the note's frames that were voiced before gaps were filled
};

/// f0 at the frames of note_grid(span, pitch.grid) (unvoiced gaps filled by linear interpolation in
/// cents, held at the ends: the fill is internal), low-passed at drift_cutoff (zero phase: a
/// forward and a backward one-pole pass, twice) into slow = center + drift; mod = f0 - slow. center
/// is the stability-weighted mean of `slow` over the trimmed region (`stability_weights` on `slow`,
/// with `amp`: the note's amp curve on the same grid, or empty for no amp term). drift = slow -
/// center. Exact: center + drift[i] + mod[i] == the filled f0[i] to double rounding. Errors:
/// invalid_argument (bad params, amp not empty and not one per frame, fewer than 3 frames, or fewer
/// than 3 voiced frames). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<PitchDecomposition>
decompose_pitch_curve(const NoteSpan& span, const PitchTrack& pitch, std::span<const Decibels> amp,
                      const PitchDecompositionParams& params);

struct PerceivedPitchParams {
    StabilityWeightParams weights;
    TrimParams trim;
    CyclePolicy cycles = CyclePolicy::whole_cycles;
};

/// The pitch a listener hears: the stability-weighted mean (cents = log frequency) of
/// pitch_center + pitch_drift + pitch_mod over the trimmed note (d'Alessandro & Castellengo 1994;
/// Mesz & Eguia; pyAMPACT). With CyclePolicy::whole_cycles and a vibrato found in pitch_mod
/// (autocorrelation peak at 3-9 Hz, at least 5 cents rms) the average runs over a whole number of
/// vibrato cycles. A scoop in the first `trim.start` is excluded by the trim and by the weights.
/// Reads the finished curves of the note (grid, pitch_drift, pitch_mod, amp, pitch_center). Errors:
/// invalid_argument (fewer than 3 frames, curves of inconsistent length, amp neither empty nor one
/// per frame). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<Cents> perceived_pitch(const Note& note, const PerceivedPitchParams& params);

}  // namespace anno
