#pragma once

#include "core/analysis/pitch_target/pitch_target.h"

#include <optional>
#include <vector>

// Trill / mordent detection on a note's pitch curve (design note §3 step 3). Internal to
// core/analysis/pitch_target.

namespace anno {

struct Ornament {
    double main_cents = 0.0;       // the level the note starts on
    double secondary_cents = 0.0;  // the other level
    double flat_share = 0.0;       // share of frames sitting on one of the two levels
};

/// Looks for two flat levels at least `min_extent` apart that the curve alternates between at
/// least `min_transitions` times, each stretch on a level lasting `min_plateau` or more, with a
/// share of at least `flat_top_share` of the frames in every plateau stationary (slope under
/// `still_cents_per_s`). A vibrato (continuously moving, rounded extremes) fails the flat-top test,
/// a single step is `multi`, not an ornament. Only frames with weight >= `min_weight` count. Pure;
/// thread-safe.
[[nodiscard]] std::optional<Ornament> find_ornament(const std::vector<double>& curve_cents,
                                                    const std::vector<double>& weight,
                                                    double min_weight, Seconds hop,
                                                    const OrnamentParams& params);

}  // namespace anno
