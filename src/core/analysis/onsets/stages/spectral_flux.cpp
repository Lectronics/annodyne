#include "core/analysis/onsets/stages/spectral_flux.h"

#include "core/analysis/onsets/detail/stage_check.h"

#include <algorithm>
#include <cmath>

namespace anno {

Result<BandSpectrogram> adaptive_whiten(const BandSpectrogram& spec, const WhitenParams& params) {
    if (auto r = validate(spec); !r) {
        return Unexpected{r.error()};
    }
    if (!detail::positive(params.relax.value()) || !detail::positive(params.floor_magnitude)) {
        return detail::bad_stage_argument(
            "adaptive_whiten: relax and floor_magnitude must be positive");
    }
    const double decay = std::exp(-spec.hop.value() / params.relax.value());
    BandSpectrogram out = spec;
    const auto bands = static_cast<std::size_t>(spec.n_bands);
    std::vector<double> peak(bands, 0.0);
    const auto frames = static_cast<std::size_t>(spec.n_frames);
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t b = 0; b < bands; ++b) {
            const double x = spec.data[f * bands + b];
            peak[b] = f == 0 ? std::max(x, params.floor_magnitude)
                             : std::max({x, decay * peak[b], params.floor_magnitude});
            out.data[f * bands + b] = x / peak[b];
        }
    }
    return out;
}

Result<BandSpectrogram> log_compress(const BandSpectrogram& spec, const LogParams& params) {
    if (auto r = validate(spec); !r) {
        return Unexpected{r.error()};
    }
    if (!detail::positive(params.log_gain)) {
        return detail::bad_stage_argument("log_compress: log_gain must be positive");
    }
    BandSpectrogram out = spec;
    for (double& v : out.data) {
        v = std::log1p(params.log_gain * v);
    }
    return out;
}

Result<Activation> superflux(const BandSpectrogram& spec, const FluxParams& params) {
    if (auto r = validate(spec); !r) {
        return Unexpected{r.error()};
    }
    if (!detail::positive(params.lag.value()) || params.max_filter_bins < 1 ||
        params.max_filter_bins % 2 == 0) {
        return detail::bad_stage_argument(
            "superflux: lag must be positive and max_filter_bins odd and >= 1");
    }
    Activation out{.values = {}, .hop = spec.hop};
    const auto frames = static_cast<std::size_t>(spec.n_frames);
    const auto bands = static_cast<std::size_t>(spec.n_bands);
    if (frames == 0 || bands == 0) {
        return out;
    }
    const auto lag =
        static_cast<std::size_t>(std::max(1L, std::lround(params.lag.value() / out.hop.value())));
    const auto radius = static_cast<std::ptrdiff_t>((params.max_filter_bins - 1) / 2);
    out.values.assign(frames, 0.0);
    for (std::size_t f = lag; f < frames; ++f) {
        double sum = 0.0;
        for (std::size_t b = 0; b < bands; ++b) {
            const auto lo = static_cast<std::size_t>(
                std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(b) - radius));
            const auto hi = std::min(bands - 1, b + static_cast<std::size_t>(radius));
            double reference = 0.0;
            for (std::size_t k = lo; k <= hi; ++k) {
                reference = std::max(reference, spec.data[(f - lag) * bands + k]);
            }
            sum += std::max(0.0, spec.data[f * bands + b] - reference);
        }
        out.values[f] = sum / static_cast<double>(bands);
    }
    return out;
}

}  // namespace anno
