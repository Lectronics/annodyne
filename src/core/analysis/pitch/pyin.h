#pragma once

#include "core/analysis/pitch/pitch_estimator.h"

// pYIN (Mauch & Dixon 2014) on top of YIN (de Cheveigne & Kawahara 2002).
// Design: docs/design/phase3-pitch-onsets.md.

namespace anno {

/// pYIN-only parameters.
struct PYinParams {
    Hz analysis_rate{
        24000.0};             // the signal is resampled to this first (never above the source rate)
    Seconds window{0.040};    // YIN integration window; must cover two periods of min_f0
    double bin_cents = 10.0;  // pitch resolution of the HMM
    double beta_mean = 0.1;   // threshold prior (Beta(2, b) distribution mean)
    double voiced_to_unvoiced_prob = 0.01;
    double unvoiced_to_voiced_prob = 0.01;
    double max_jump_cents = 80.0;  // f0 change per hop with the smooth (triangular) transition
    double jump_prob = 1e-4;  // probability of any larger jump, so a legato step is representable
};

class PYinEstimator final : public PitchEstimator {
public:
    explicit PYinEstimator(PYinParams params = {}) : params_{params} {}

    [[nodiscard]] std::string_view name() const noexcept override { return "pyin"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 1; }
    [[nodiscard]] Result<PitchTrack> estimate(const AudioSource& src,
                                              const PitchParams& params) const override;

private:
    PYinParams params_;
};

}  // namespace anno
