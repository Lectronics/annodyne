#pragma once

#include "core/types/units.h"

// Every test tolerance lives here (coding-standards §6, §8).
//
// Human-gated block: the starting tolerances of docs/AUDIO_VERIFICATION.md §1. They change only
// with a human decision recorded in an ADR, never to make a failing check pass.
//
// Engineering block: numerical tolerances for exact-maths tests, justified individually.

namespace anno::tolerance {

// ---- Human-gated (AUDIO_VERIFICATION §1; change only via ADR) ---------------------------------

/// Pitch round-trip: rendered note re-analysed is within ±3 cents of the requested pitch.
inline constexpr Cents k_pitch_roundtrip{3.0};
/// Vibrato preservation: rate within ±0.2 Hz of the original.
inline constexpr Hz k_vibrato_rate{0.2};
/// Vibrato preservation: extent within ±10% of the original.
inline constexpr Ratio k_vibrato_extent_rel{0.10};
/// Modulation/drift scaling: measured scale within ±10% of the requested mod_scale/drift_scale.
inline constexpr Ratio k_mod_drift_scaling_rel{0.10};
/// Timing round-trip: re-detected onset within ±2 ms of the requested position.
inline constexpr Seconds k_timing_roundtrip{0.002};
/// Length check: output duration within ±1 sample of the TimeMap prediction.
inline constexpr Samples k_length{1};
/// Level round-trip: re-measured note loudness within ±0.3 dB of the requested gain.
inline constexpr Decibels k_level_roundtrip{0.3};
/// Formant round-trip: envelope peaks within ±3% of the requested frequency shift.
inline constexpr Ratio k_formant_roundtrip_rel{0.03};
/// Formant preservation: envelope peaks within ±5% after a ±7 semitone pitch shift alone.
inline constexpr Ratio k_formant_preservation_rel{0.05};
/// Noise not pitch-shifted: sibilant spectral centroid within ±5% after a pitch shift.
inline constexpr Ratio k_noise_centroid_rel{0.05};
/// Click detector: no detections allowed.
inline constexpr int k_click_detections{0};
/// Sanity: no NaN or Inf samples allowed.
inline constexpr int k_nan_inf_samples{0};
/// Sanity: DC offset below -60 dBFS.
inline constexpr Decibels k_dc_offset_max_dbfs{-60.0};

// ---- Click detector (initial values, pending human review; AUDIO_VERIFICATION §1 names the method
// but gives no number). Calibrated by two permanent tests: a 10 ms crossfade is clean and a hard
// splice is detected (tests/audio/click_calibration). Never loosen to make a render pass.

/// A sample is a click candidate if its first difference exceeds this multiple of the RMS of the
/// first difference in the surrounding window. A 440 Hz sine peaks at ~1.4x its own RMS.
inline constexpr double k_click_ratio = 6.0;
/// Window (samples, each side) for the local RMS of the first difference.
inline constexpr int k_click_window = 128;
/// Absolute floor on the first difference (linear full scale) so digital silence cannot trigger.
inline constexpr double k_click_floor = 1e-3;

// ---- Engineering ----------------------------------------------------------------------------

/// TimeMap inverse: M^-1(M(s)) == s within 1e-9 s (ROADMAP Phase 1). Double has ~16 significant
/// digits, so a few operations on hour-long positions (~4e3 s) stay far below this.
inline constexpr Seconds k_timemap_roundtrip{1e-9};
/// Order-independence of summed `add` edits (up to ~30 values of magnitude <= 300): double
/// rounding error is ~1e-13; 1e-9 leaves room and is still far below anything audible.
inline constexpr double k_fold_commute_abs = 1e-9;
/// Order-independence of multiplied `scale` edits (factors in 0.25..4, up to ~30): relative.
inline constexpr double k_fold_commute_rel = 1e-9;
/// Cents <-> Hz conversions: one exp2/log2 plus a multiply on values up to ~1.5e4 cents; double
/// error is ~1e-12 cents. Absolute, because relative error is meaningless near 0 cents.
inline constexpr Cents k_cents_conversion_abs{1e-9};
/// Seconds <-> Samples: one multiply or divide; error ~1e-16 relative on values up to hours.
inline constexpr Seconds k_seconds_conversion_abs{1e-12};
/// Hz from cents: exp2 has < 1 ulp error in common libms; 1e-12 relative leaves ample margin.
inline constexpr Ratio k_hz_conversion_rel{1e-12};
/// dB <-> linear gain: pow/log10 with < 1 ulp error each.
inline constexpr Ratio k_gain_conversion_rel{1e-12};

// ---- Analysis DSP (Phase 3) ------------------------------------------------------------------

/// STFT -> ISTFT reconstruction: float spectrogram storage (~6e-8 relative) accumulated over a few
/// overlapping frames; 1e-5 absolute on a signal of peak <= 1.
inline constexpr double k_stft_roundtrip_abs = 1e-5;
/// Real FFT against a naive O(n^2) double DFT of the same data: both are double precision.
inline constexpr double k_fft_vs_dft_abs = 1e-9;
/// Resampled sine frequency (measured by zero crossings over ~1 s) within 0.1 %.
inline constexpr Ratio k_resample_freq_rel{1e-3};
/// Resampled sine level within 0.1 dB (kernel ripple at cutoff 0.97 is far below this mid-band).
inline constexpr Decibels k_resample_level{0.1};
/// K-weighted 1 kHz sine: the BS.1770 filter has +0.691 dB gain at 1 kHz (the -0.691 in the LUFS
/// formula compensates it); 0.1 dB covers the analytic-coefficient differences at other sample
/// rates.
inline constexpr Decibels k_k_weight_1khz{0.1};
/// Exactness of interpolation on data that the interpolator reproduces exactly (linear on lines,
/// cubic on cubics): double rounding only.
inline constexpr double k_interp_exact_abs = 1e-9;

}  // namespace anno::tolerance
