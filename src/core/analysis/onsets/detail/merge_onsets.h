#pragma once

#include "core/analysis/onsets/detail/raw_onset.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <vector>

// Merging the onsets of the different detectors into one list. Internal to core/analysis/onsets.

namespace anno {

/// Sorts by time and merges onsets closer than `min_gap`: the stronger is kept. A spectral onset
/// without an energy rise (legato: the timbre or pitch changed under a steady level) that meets a
/// pitch-change onset is that pitch change: the pitch-change entry survives with the larger
/// strength. A spectral onset with an energy rise (a new attack) wins against a pitch change; on
/// equal strength the spectral one wins. Pure; thread-safe.
[[nodiscard]] std::vector<RawOnset> merge_onsets(std::vector<RawOnset> all, Seconds min_gap);

/// The public list: parallel arrays of time, strength and kind. Pure; thread-safe.
[[nodiscard]] OnsetList to_onset_list(const std::vector<RawOnset>& onsets);

}  // namespace anno
