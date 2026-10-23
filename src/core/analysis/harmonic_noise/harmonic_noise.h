#pragma once

#include "core/types/note_curves.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <cstdint>
#include <span>
#include <vector>

// Harmonic + noise split and sound-class classifier of one note (architecture spec §4.2
// split_harmonic_noise; implementation guide B6; ADR-0017, ADR-0018). Design:
// docs/design/phase4-pitch-edit.md and docs/design/vocal-resynthesis-research.md. Every threshold
// is a provisional starting value from that research, to be calibrated per singer (the fuzzy
// layer); each is named here with its source.

namespace anno {

struct HarmonicNoiseParams {
    int n_bands = 32;  // mel-spaced bands over 0..0.45 * rate, 8..64

    // Frame level below which the frame is silence (frame mean square, dBFS). Guess.
    Decibels silence_level{-70.0};

    // Unvoiced frames: sibilant above this spectral centroid, breath below (research: /sh/ means
    // 4.5-5.4 kHz; the cut-off is its guess).
    Hz sibilant_centroid{4000.0};

    // Plosive burst: positive spectral flux (normalised by the frame's magnitude sum) is a local
    // maximum above median + flux_mad_factor * MAD of the note and above flux_floor, bursts at
    // least burst_min_gap apart, confirmed by a closure dip (the level 5..40 ms before is at least
    // closure_dip lower) and a high-frequency rise (share of power above hf_edge grows by hf_rise).
    // Research gives median + 3 MAD and 30 ms; the other values are guesses.
    double flux_mad_factor = 3.0;
    double flux_floor = 0.1;  // dimensionless
    Seconds burst_min_gap{0.030};
    Decibels closure_dip{6.0};
    Hz hf_edge{4000.0};
    double hf_rise = 0.15;  // change of a power share, dimensionless
    Seconds burst_duration{0.015};

    // Rough (fry, growl): f0 against the second-best path's f0 is an octave-like ratio in
    // [octave_ratio_min, octave_ratio_max] or its inverse (research: US 9,047,865 window).
    double octave_ratio_min = 1.74;
    double octave_ratio_max = 2.3;

    // Breathy: HNR below breathy_hnr or maximum voiced frequency below breathy_mvf. A band is
    // aperiodic above aperiodicity_threshold (share of band power not explained by harmonics) and
    // counts only if its power is within band_significance of the frame total.
    Decibels breathy_hnr{10.0};
    Hz breathy_mvf{2000.0};
    double aperiodicity_threshold = 0.5;  // dimensionless
    Decibels band_significance{30.0};

    // Label smoothing: median over median_frames (odd, 1..9) labels, then runs shorter than
    // min_segment join their longer neighbour; plosive bursts are never smoothed away.
    int median_frames = 5;
    Seconds min_segment{0.020};
};

/// What the pitch tracker says about each frame of the note, on curves.grid.
struct PitchEvidence {
    std::span<const double> voicing_prob;  // grid.n_frames long, 0..1
    // f0 of the second-best path in absolute cents (NaN where none); empty = no octave test.
    std::span<const Cents> alternate_f0;
};

/// The measurements behind each decision, one entry per frame of the note's grid. Returned so tests
/// and the calibration harness can see why a frame got its class (ADR-0017 item 5).
struct FrameMetrics {
    std::vector<double> level_db;            // frame mean square, dBFS
    std::vector<double> centroid_hz;         // spectral centroid of the magnitude spectrum
    std::vector<double> zero_crossing_rate;  // crossings per sample
    std::vector<double> hnr_db;              // harmonic-to-noise ratio, -40..60; -40 when unvoiced
    std::vector<double> mvf_hz;              // maximum voiced frequency; 0 when unvoiced
    std::vector<double> flux;                // normalised positive spectral flux
    std::vector<double> hf_share;            // power share above hf_edge (10 ms window)
    std::vector<std::uint8_t> burst;         // 1 inside a confirmed plosive burst
    std::vector<std::uint8_t> octave_flag;   // 1 where the octave test fired
    FrameMatrix noise_band_power;  // frames x n_bands residual power (linear, mean square)
};

/// Measures every frame (see FrameMetrics). `mono` starts at SourceTime 0 as for analyse_partials.
/// The harmonic power of a frame is sum(amp^2 / 2) over its harmonics, counted only on frames the
/// pitch tracker calls voiced; the residual of a band is its power minus its harmonic power,
/// floored at 0. Errors: invalid_argument (bad params or rate, empty signal, harmonics or evidence
/// not on curves.grid, voicing_prob outside 0..1). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<FrameMetrics> measure_frames(std::span<const float> mono, Hz sample_rate,
                                                  const NoteCurves& curves,
                                                  const HarmonicTrack& harmonics,
                                                  const PitchEvidence& evidence,
                                                  const HarmonicNoiseParams& params);

/// Routing order of ADR-0017, first match wins: silence, plosive, sibilant, breath, rough, breathy,
/// voiced; then label smoothing. Pure; thread-safe. Errors: invalid_argument (bad params, metrics
/// of unequal length, voicing_prob not matching).
[[nodiscard]] Result<std::vector<FrameClass>> classify_frames(const FrameMetrics& metrics,
                                                              std::span<const double> voicing_prob,
                                                              Seconds hop,
                                                              const HarmonicNoiseParams& params);

/// measure_frames, then classify_frames; the NoiseTrack holds the residual band power
/// (band_energy_linear) and the frame classes. The residual audio itself is not stored (ADR-0016
/// item 7). Same errors as the two. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<NoiseTrack> split_harmonic_noise(std::span<const float> mono, Hz sample_rate,
                                                      const NoteCurves& curves,
                                                      const HarmonicTrack& harmonics,
                                                      const PitchEvidence& evidence,
                                                      const HarmonicNoiseParams& params);

}  // namespace anno
