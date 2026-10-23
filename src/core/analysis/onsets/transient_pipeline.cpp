#include "core/analysis/onsets/transient_pipeline.h"

#include "core/dsp/mono.h"

#include <algorithm>

namespace anno {

Result<std::vector<LocalisedOnset>> transient_onsets(std::span<const float> mono, Hz rate,
                                                     const TransientParams& params) {
    std::vector<float> normalised;
    if (params.level) {
        auto level = normalise_level(mono, rate, *params.level);
        if (!level) {
            return Unexpected{level.error()};
        }
        normalised = std::move(level->samples);
        mono = normalised;
    }
    auto bands = band_spectrogram(mono, rate, params.bands);
    if (!bands) {
        return Unexpected{bands.error()};
    }
    if (params.whiten) {
        bands = adaptive_whiten(*bands, *params.whiten);
        if (!bands) {
            return Unexpected{bands.error()};
        }
    }
    auto logged = log_compress(*bands, params.log);
    if (!logged) {
        return Unexpected{logged.error()};
    }
    auto activation = superflux(*logged, params.flux);
    if (!activation) {
        return Unexpected{activation.error()};
    }
    auto peaks = pick_peaks_windowed(*activation, params.peaks);
    if (!peaks) {
        return Unexpected{peaks.error()};
    }
    auto onsets = localise(mono, rate, *peaks, params.localise);
    if (!onsets) {
        return Unexpected{onsets.error()};
    }
    std::ranges::stable_sort(*onsets, {}, [](const LocalisedOnset& o) { return o.time; });
    return onsets;
}

Result<OnsetList> detect_transients(const AudioSource& src, const TransientParams& params) {
    auto mono = read_mono(src);
    if (!mono) {
        return Unexpected{mono.error()};
    }
    auto onsets = transient_onsets(mono->samples, mono->sample_rate, params);
    if (!onsets) {
        return Unexpected{onsets.error()};
    }
    OnsetList list;
    for (const LocalisedOnset& o : *onsets) {
        list.times.push_back(o.time);
        list.strength.push_back(o.strength);
        list.kind.push_back(OnsetKind::attack);
    }
    return list;
}

}  // namespace anno
