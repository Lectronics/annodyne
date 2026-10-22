#pragma once

#include "core/types/tracks.h"
#include "core/types/units.h"

// One onset on its way through detect_onsets, before it becomes an OnsetList entry. Internal to
// core/analysis/onsets.

namespace anno {

struct RawOnset {
    SourceTime time;
    double strength = 0.0;
    OnsetKind kind = OnsetKind::attack;
    bool spectral = true;
    bool energy_rise = false;  // spectral onsets: the short-window energy really rose (not legato)
};

}  // namespace anno
