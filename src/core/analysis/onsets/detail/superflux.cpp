#include "core/analysis/onsets/detail/superflux.h"

#include "core/analysis/onsets/detail/onset_params_check.h"

namespace anno {

Result<Activation> superflux_activation(std::span<const float> mono, Hz rate,
                                        const OnsetParams& params) {
    if (auto r = check_adaptive_chain_params(params); !r) {
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
