#pragma once

#include "core/analysis/pitch/pitch_estimator.h"

#include <optional>

// pYIN (Mauch & Dixon 2014) on top of YIN (de Cheveigne & Kawahara 2002).
// Design: docs/design/phase3-pitch-onsets.md.

namespace anno {

/// pYIN-only parameters.
struct PYinParams {
    Hz analysis_rate{
        24000.0};             // the signal is resampled to this first (never above the source rate)
    Seconds window{0.040};    // YIN integration window; must cover two periods of min_f0
    double bin_cents = 10.0;  // pitch resolution of the HMM
    double beta_mean = 0.3;   // threshold prior (Beta(2, b) distribution mean); 0.1 on synthetic
                              // tones lost 40 % of real voiced frames
    double voiced_to_unvoiced_prob = 0.01;
    double unvoiced_to_voiced_prob = 0.01;
    double subharmonic_margin = 0.1;    // 0 = off: CMND margin for moving to a 2x/3x longer lag
    double subharmonic_margin2 = 0.05;  // the same for the 2x lag (a smaller margin trades
                                        // octave-up errors for octave-down ones)
    double stay_relief = 0.5;           // see HmmParams::stay_relief; 0 lost short voiced stretches
    double max_jump_cents = 80.0;       // f0 change per hop with the smooth (triangular) transition
    double jump_prob = 1e-4;  // probability of any larger jump, so a legato step is representable
    // Zero-phase band-pass on the analysis-rate signal before the YIN frames (design:
    // docs/design/phase3-activity.md §2). The cutoff is the ratio times PitchParams::min_f0 /
    // max_f0 (the zero-phase pair is -6 dB at its cutoff); nullopt skips that side. The low-pass
    // cutoff is limited to 0.45 x the analysis rate.
    std::optional<double> highpass_min_f0_ratio = 1.0;
    std::optional<double> lowpass_max_f0_ratio = 1.0;
};

class PYinEstimator final : public PitchEstimator {
public:
    explicit PYinEstimator(PYinParams params = {}) : params_{params} {}

    [[nodiscard]] std::string_view name() const noexcept override { return "pyin"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 4; }
    [[nodiscard]] Result<PitchTrack> estimate(const AudioSource& src,
                                              const PitchParams& params) const override;

private:
    PYinParams params_;
};

}  // namespace anno
