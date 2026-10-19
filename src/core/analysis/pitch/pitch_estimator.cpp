#include "core/analysis/pitch/pitch_estimator.h"

#include "core/analysis/pitch/pyin.h"

namespace anno {

PitchEstimator::~PitchEstimator() = default;

Result<PitchTrack> detect_pitch(const AudioSource& src, const PitchParams& params) {
    return PYinEstimator{}.estimate(src, params);
}

}  // namespace anno
