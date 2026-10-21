// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/analysis/pitch/pitch_estimator.h"

// pYIN as implemented by c4dm/pyin (Mauch & Dixon 2014; the Vamp plugin that Tony uses), vendored
// in third_party/c4dm_pyin (GPL-2.0-or-later, ADR-0014). This file and c4dm_pyin.cpp re-implement
// the plugin's pipeline (PYinVamp::process / getRemainingFeatures) around the vendored YIN and
// pitch HMM, so they are covered by the same licence. Design: docs/design/c4dm-pyin-integration.md.

namespace anno {

/// Parameters of the c4dm pipeline; defaults are the plugin's.
struct C4dmPYinParams {
    Hz max_analysis_rate{44100.0};   // the signal is resampled down to this (never up); the plugin
                                     // has no resampler and is used at 44.1 / 48 kHz
    int threshold_distribution = 2;  // YIN threshold prior: 0 uniform, 1..4 Beta mean 0.10 / 0.15 /
                                     // 0.20 / 0.30, 5..7 single values 0.10 / 0.15 / 0.20 (plugin
                                     // default 2: Beta mean 0.15)
    double low_amp_rms = 0.1;  // block RMS (full scale 1) below which candidate mass is scaled by
                               // (rms + 0.01 * low_amp_rms) / (1.01 * low_amp_rms); 0 = off
    // Sub-octave guard (not in the plugin): when the strongest candidate has a CMND dip at 1/3
    // (`down_margin3`) or 1/2 (`down_margin2`) of its lag that is at most that much shallower, its
    // mass moves to that shorter lag. Classic YIN takes the first dip under the threshold; pYIN's
    // mass can favour a deeper dip at a subharmonic: on Vocadito track 34 the plugin reports f0 / 3
    // in half of the frames. The smaller margin for 1/2 is because a true low pitch with strong
    // harmonics also dips at its half period (ADR-0012). Negative = off (the plugin's behaviour).
    double down_margin3 = 0.1;
    double down_margin2 = 0.02;
    int fixed_lag_frames =
        0;  // > 0: fixed-lag smoothing over this many frames (plugin default 100)
    // 0 with a source longer than this: a lag of `long_source_lag_frames` is used, because full
    // Viterbi keeps 690 back-pointers (2.8 KB) per frame, about 55 MB per 100 s of audio.
    Seconds full_viterbi_max_duration{300.0};
    int long_source_lag_frames = 100;
};

/// The c4dm/pyin pitch tracker behind the PitchEstimator interface. Frames are analysed in blocks
/// of 2048 samples at 44.1 kHz (scaled with the rate, even), the plugin's size, one block per grid
/// frame, positioned so the plugin's reported time (block start + block / 4) is the frame time. The
/// HMM covers 61.7 Hz upward over 69 semitones, so the effective lowest pitch is
/// max(PitchParams::min_f0, 61.7 Hz) (a lower min_f0 is accepted and has no effect); min_f0 /
/// max_f0 drop YIN candidates outside the range before the HMM, and a guard step that would move a
/// candidate's mass above max_f0 leaves the frame unvoiced. Memory is O(frames) whatever the lag
/// (the signal copy, the candidate lists and the per-frame states); the lag only bounds the HMM's
/// back-pointers. voicing_prob follows the decoded path (>= 0.5 voiced, < 0.5
/// unvoiced), carrying the summed YIN candidate probability; `alternates` are NaN (the plugin has
/// one path).
class C4dmPYinEstimator final : public PitchEstimator {
public:
    explicit C4dmPYinEstimator(C4dmPYinParams params = {}) : params_{params} {}

    [[nodiscard]] std::string_view name() const noexcept override { return "c4dm_pyin"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 1; }
    /// Errors: invalid_argument (min_f0/max_f0/hop not finite and positive, min_f0 >= max_f0,
    /// n_alternates < 0, the analysis rate (the source rate, at most max_analysis_rate) below
    /// 2 x max_f0, threshold_distribution outside 0..7,
    /// low_amp_rms negative or not finite, fixed_lag_frames negative, max_analysis_rate not finite
    /// and >= 8 kHz); anything the source read returns. Pure; thread-safe; not real-time safe.
    [[nodiscard]] Result<PitchTrack> estimate(const AudioSource& src,
                                              const PitchParams& params) const override;

private:
    C4dmPYinParams params_;
};

}  // namespace anno
