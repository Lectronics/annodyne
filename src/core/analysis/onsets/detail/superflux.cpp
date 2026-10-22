#include "core/analysis/onsets/detail/superflux.h"

#include <cmath>

namespace anno {

namespace {

Result<void> validate(const OnsetParams& p) {
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (!positive(p.hop.value()) || !positive(p.window.value()) || !positive(p.lag.value()) ||
        !positive(p.threshold_window.value()) || !positive(p.min_gap.value()) ||
        !positive(p.refine_window.value()) || !positive(p.energy_step_window.value()) ||
        !positive(p.energy_step_db.value()) || !positive(p.energy_step_strength_db.value()) ||
        !positive(p.log_lambda) || !std::isfinite(p.delta) || p.delta < 0.0 ||
        p.filterbank_bands_per_octave < 1 || p.max_filter_bins < 1) {
        return fail(ErrorCode::invalid_argument, "onsets: invalid parameters");
    }
    return {};
}

}  // namespace

Result<Activation> superflux_activation(std::span<const float> mono, Hz rate,
                                        const OnsetParams& params) {
    if (auto r = validate(params); !r) {
        return Unexpected{r.error()};
    }
    auto bands = band_spectrogram(mono, rate,
                                  {.window = params.window,
                                   .hop = params.hop,
                                   .bands_per_octave = params.filterbank_bands_per_octave});
    if (!bands) {
        return Unexpected{bands.error()};
    }
    auto logged = log_compress(*bands, {.log_gain = params.log_lambda});
    if (!logged) {
        return Unexpected{logged.error()};
    }
    // An even width has always meant radius (w - 1) / 2; the stage takes odd widths only.
    return superflux(*logged, {.lag = params.lag,
                               .max_filter_bins = 2 * ((params.max_filter_bins - 1) / 2) + 1});
}

}  // namespace anno
