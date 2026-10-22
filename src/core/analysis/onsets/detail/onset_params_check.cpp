#include "core/analysis/onsets/detail/onset_params_check.h"

#include "core/analysis/onsets/detail/stage_check.h"

#include <cmath>

namespace anno {

namespace {

Result<void> check_shared(const OnsetParams& p) {
    using detail::positive;
    if (!positive(p.min_gap.value()) || !positive(p.energy_step_window.value()) ||
        !positive(p.energy_step_db.value()) || !positive(p.energy_step_strength_db.value())) {
        return fail(ErrorCode::invalid_argument, "onsets: invalid parameters");
    }
    return {};
}

Result<void> check_adaptive(const OnsetParams& p) {
    using detail::positive;
    if (!positive(p.hop.value()) || !positive(p.window.value()) || !positive(p.lag.value()) ||
        !positive(p.threshold_window.value()) || !positive(p.refine_window.value()) ||
        !positive(p.log_lambda) || !std::isfinite(p.delta) || p.delta < 0.0 ||
        p.filterbank_bands_per_octave < 1 || p.max_filter_bins < 1) {
        return fail(ErrorCode::invalid_argument, "onsets: invalid parameters");
    }
    return {};
}

}  // namespace

Result<void> check_onset_params(const OnsetParams& params) {
    if (auto r = check_shared(params); !r) {
        return r;
    }
    return params.method == SpectralMethod::adaptive_threshold ? check_adaptive(params)
                                                               : Result<void>{};
}

Result<void> check_adaptive_chain_params(const OnsetParams& params) {
    if (auto r = check_shared(params); !r) {
        return r;
    }
    return check_adaptive(params);
}

}  // namespace anno
