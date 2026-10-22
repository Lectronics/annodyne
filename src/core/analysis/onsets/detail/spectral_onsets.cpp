#include "core/analysis/onsets/detail/spectral_onsets.h"

#include "core/analysis/onsets/detail/peak_picking.h"
#include "core/analysis/onsets/detail/superflux.h"
#include "core/analysis/onsets/onset_stages.h"

namespace anno {

namespace {

constexpr double k_min_rise_db = 6.0;  // a smaller energy rise is not refined
constexpr double k_falling_db =
    3.0;  // louder before than after by this much: an offset, not an onset
constexpr double k_energy_window_s = 0.002;  // short-window energy for the refinement

RawOnset to_raw(const LocalisedOnset& o) {
    return {.time = o.time,
            .strength = o.strength,
            .kind = OnsetKind::attack,
            .spectral = true,
            .energy_rise = o.outcome == LocaliseOutcome::rose};
}

Result<std::vector<RawOnset>> adaptive_threshold_onsets(const MonoAudio& mono,
                                                        const OnsetParams& params) {

    auto activation = superflux_activation(mono.samples, mono.sample_rate, params);
    if (!activation) {
        return Unexpected{activation.error()};
    }
    PeakList peaks{.hop = activation->hop,
                   .peaks =
                       pick_peaks(activation->values, {.hop = activation->hop,
                                                       .threshold_window = params.threshold_window,
                                                       .delta = params.delta,
                                                       .floor = params.min_activation,
                                                       .min_gap = params.min_gap})};
    auto localised = localise(mono.samples, mono.sample_rate, peaks,
                              {.search = params.refine_window,
                               .energy_window = Seconds{k_energy_window_s},
                               .min_rise = Decibels{k_min_rise_db},
                               .end_drop = Decibels{k_falling_db}});
    if (!localised) {
        return Unexpected{localised.error()};
    }
    std::vector<RawOnset> out;
    out.reserve(localised->size());
    for (const LocalisedOnset& o : *localised) {
        out.push_back(to_raw(o));
    }
    return out;
}

Result<std::vector<RawOnset>> windowed_peak_onsets(const MonoAudio& mono,
                                                   const OnsetParams& params) {
    auto localised = transient_onsets(mono.samples, mono.sample_rate, params.pipeline);
    if (!localised) {
        return Unexpected{localised.error()};
    }
    std::vector<RawOnset> out;
    out.reserve(localised->size());
    for (const LocalisedOnset& o : *localised) {
        out.push_back(to_raw(o));
    }
    return out;
}

}  // namespace

Result<std::vector<RawOnset>> spectral_onsets(const MonoAudio& mono, const OnsetParams& params) {
    return params.method == SpectralMethod::windowed_peaks
               ? windowed_peak_onsets(mono, params)
               : adaptive_threshold_onsets(mono, params);
}

}  // namespace anno
