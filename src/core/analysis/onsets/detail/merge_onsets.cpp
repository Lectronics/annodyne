#include "core/analysis/onsets/detail/merge_onsets.h"

#include <algorithm>

namespace anno {

std::vector<RawOnset> merge_onsets(std::vector<RawOnset> all, Seconds min_gap) {
    std::ranges::stable_sort(all, {}, [](const RawOnset& o) { return o.time; });
    std::vector<RawOnset> kept;
    for (const RawOnset& o : all) {
        if (kept.empty() || (o.time - kept.back().time) >= min_gap) {
            kept.push_back(o);
            continue;
        }
        RawOnset& last = kept.back();
        const bool one_legato_spectral = (last.spectral && !last.energy_rise && !o.spectral) ||
                                         (o.spectral && !o.energy_rise && !last.spectral);
        if (one_legato_spectral) {
            RawOnset change = o.spectral ? last : o;
            change.strength = std::max(o.strength, last.strength);
            last = change;
        } else if (o.strength > last.strength ||
                   (o.strength == last.strength && o.spectral && !last.spectral)) {
            last = o;
        }
    }
    return kept;
}

OnsetList to_onset_list(const std::vector<RawOnset>& onsets) {
    OnsetList list;
    for (const RawOnset& o : onsets) {
        list.times.push_back(o.time);
        list.strength.push_back(o.strength);
        list.kind.push_back(o.kind);
    }
    return list;
}

}  // namespace anno
