#pragma once

#include "core/types/analysis_result.h"
#include "core/types/units.h"

#include <optional>
#include <span>

// The pitch-edit measurements of docs/AUDIO_VERIFICATION.md §1 (pitch round trip, vibrato and
// formant preservation, noise not shifted) that `anno-cli audition` reports and tests/audio
// asserts. Each compares an analysis of the original with an analysis of the render, both made by
// the same analyse(). The limits are parameters whose defaults are the values of tests/tolerances.h
// (a unit test checks they agree). Design: docs/design/phase4-pitch-edit.md (stage 9).

namespace anno::cli {

struct PitchCheckParams {
    Cents pitch_roundtrip{3.0};            // median shift within this of the requested one
    Hz vibrato_rate{0.2};                  // rate within this of the original
    Ratio vibrato_extent_rel{0.10};        // extent within this fraction of the original
    Ratio formant_preservation_rel{0.05};  // first formant within this fraction
    Ratio noise_centroid_rel{0.05};        // spectral centroid within this fraction
    Seconds edge_margin{0.05};             // frames this close to the note's ends are not measured
    int min_voiced_frames = 10;            // fewer and the measurement is refused (nullopt)
};

/// The note of `analysis` that overlaps `note` the most (nullptr if none overlap). Pure.
[[nodiscard]] const Note* matching_note(const AnalysisResult& analysis, const Note& note);

/// Median over the frames inside `note` (less the edge margin) that both analyses call voiced of
/// f0(edited) - f0(original), cents; nullopt if there are fewer than min_voiced_frames or either
/// analysis has no pitch track. Both pitch tracks come from analyse() on the same grid. Pure.
[[nodiscard]] std::optional<Cents> median_pitch_shift(const AnalysisResult& original,
                                                      const AnalysisResult& edited,
                                                      const Note& note,
                                                      const PitchCheckParams& params = {});

struct VibratoStats {
    Hz rate;       // zero crossings of the modulation curve about its mean, per second / 2
    Cents extent;  // half the peak-to-peak of the modulation curve
};

/// Vibrato of a note from its pitch_mod curve (frames inside the edge margin skipped); nullopt if
/// too few finite frames or the curve never crosses its mean. Pure.
[[nodiscard]] std::optional<VibratoStats> vibrato_of(const Note& note,
                                                     const PitchCheckParams& params = {});

/// Frequency of the largest envelope peak between lo and hi at the note's middle frame, refined by
/// a parabola through the peak bin and its neighbours; nullopt if the note has no envelope or the
/// maximum is at the edge of the search band. Pure.
[[nodiscard]] std::optional<Hz> first_formant(const Note& note, Hz lo = Hz{300.0},
                                              Hz hi = Hz{1300.0});

/// Magnitude-weighted mean frequency (1 kHz to Nyquist) of a Hann-windowed 8192-sample piece taken
/// from the middle of `samples`; 0 if `samples` is shorter than 512 samples or silent. Pure.
[[nodiscard]] Hz spectral_centroid(std::span<const float> samples, Hz sample_rate);

}  // namespace anno::cli
