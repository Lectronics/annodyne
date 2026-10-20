#include "core/analysis/pitch/pitch_estimator.h"

#include "core/analysis/pitch/c4dm_pyin.h"

namespace anno {

PitchEstimator::~PitchEstimator() = default;

Result<PitchTrack> detect_pitch(const AudioSource& src, const PitchParams& params) {
    return C4dmPYinEstimator{}.estimate(src, params);
}

}  // namespace anno
