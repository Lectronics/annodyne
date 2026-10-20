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

// ---- Analysis accuracy (ROADMAP Phase 3 gate) --------------------------------------------------

/// Stationary synthetic tones: every voiced frame within this of the true f0 (ROADMAP: +-3 cents).
inline constexpr Cents k_pitch_stationary{3.0};
/// Moving synthetic pitch (vibrato, glides, noisy harmonics): the "10-cent" accuracy tolerance.
inline constexpr Cents k_pitch_dynamic{10.0};
/// Minimum share of voiced frames within k_pitch_dynamic (ROADMAP: 10-cent RPA >= 95 %).
inline constexpr double k_rpa_min = 0.95;
/// Synthetic onset times (bursts with abrupt starts): within +-5 ms of the truth.
inline constexpr Seconds k_onset_synthetic{0.005};
/// Golden recordings (Vocadito subset, 5 held-out tracks; tests/golden/vocals/README.md). These are
/// a REGRESSION GUARD: each threshold is the measured value of the held-out set rounded down with
/// a margin, not a quality target. Measured: voicing recall 0.967, RPA(50 c) 0.948, octave/harmonic
/// error rate 0.013 (octave, or a third harmonic: +-1200, +-1902, +-2400 cents), onset F-measure at
/// +-20 / +-50 ms 0.472 / 0.758 (better annotator of the two, who agree with each other at only
/// ~0.75), matched-note pitch within 50 cents 0.740. The owner's goal stays onset F >= 0.9 and no
/// octave errors (docs/decisions/0012); the guard rises as the analysis improves.
inline constexpr double k_golden_voicing_recall_min = 0.92;
inline constexpr double k_golden_rpa_min = 0.90;
inline constexpr double k_golden_octave_rate_max = 0.03;
inline constexpr double k_golden_onset_f20_min = 0.43;
inline constexpr double k_golden_onset_f50_min = 0.69;
inline constexpr double k_golden_note_pitch_ok_min = 0.70;

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
/// Activity labels (25 ms window, 5 ms hop): a burst edge is smeared by half a window, so frames
/// within 15 ms of an edge are not judged; the noise-floor estimate is within 2 dB of the true
/// noise RMS (the lowest-3-dB-band estimator sits slightly under the mean of a fluctuating level).
inline constexpr Seconds k_activity_edge{0.015};
inline constexpr Decibels k_activity_floor{2.0};
/// Zero-phase Butterworth filters (2nd order, forward and backward): passband flatness 0.1 dB at
/// 4 x the high-pass / 0.25 x the low-pass cutoff (the true error there is below 0.02 dB), the
/// cutoff gain is -6.02 dB (squared magnitude, +-0.3 dB for the finite test length), and one octave
/// outside the attenuation is 24.6 dB, so 24 dB is the gate (a 30 dB gate is unreachable).
inline constexpr Decibels k_filter_passband{0.1};
inline constexpr Decibels k_filter_cutoff{0.3};
inline constexpr Decibels k_filter_octave_db{24.0};
/// Energy of k_weight(1 kHz sine, 48 kHz, 1 s, amplitude 0.5), recorded before the biquad was
/// shared with the zero-phase filters: the refactor must not change a single bit.
inline constexpr double k_k_weight_reference_energy = 7045.4655434886545;
/// Exactness of interpolation on data that the interpolator reproduces exactly (linear on lines,
/// cubic on cubics): double rounding only.
inline constexpr double k_interp_exact_abs = 1e-9;

}  // namespace anno::tolerance
