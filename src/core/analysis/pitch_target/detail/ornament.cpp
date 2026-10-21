#include "core/analysis/pitch_target/detail/ornament.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace anno {

namespace {

constexpr std::size_t k_min_frames = 8;

double median_of(std::vector<double> v) {
    std::ranges::sort(v);
    return v[v.size() / 2];
}

double quantile_of(std::vector<double> v, double q) {
    std::ranges::sort(v);
    return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
}

struct Levels {
    double middle = 0.0;
    double upper = 0.0;
    double lower = 0.0;
};

/// The two levels of the curve (medians of the frames above and below the middle of its 5-95 %
/// range), if they are at least min_extent apart.
std::optional<Levels> two_levels(const std::vector<double>& used, const OrnamentParams& params) {
    const double lo = quantile_of(used, 0.05);
    const double hi = quantile_of(used, 0.95);
    if (hi - lo < params.min_extent.value()) {
        return std::nullopt;
    }
    Levels out;
    out.middle = (lo + hi) / 2.0;
    std::vector<double> upper;
    std::vector<double> lower;
    for (const double c : used) {
        (c >= out.middle ? upper : lower).push_back(c);
    }
    out.upper = median_of(upper);
    out.lower = median_of(lower);
    if (out.upper - out.lower < params.min_extent.value()) {
        return std::nullopt;
    }
    return out;
}

/// What walking the curve found: stretches on one side of the middle.
struct Walk {
    int plateaus = 0;     // stretches of at least min_plateau
    int transitions = 0;  // changes of side between those
    double first_level = 0.0;
    double flat_share = 0.0;  // stationary frames over all frames of the plateaus
    bool ramp = false;        // a plateau was not flat: a scoop or fall, not an ornament level
};

/// The end (exclusive) of the stretch that starts at `from` on one side of `middle`, and how many
/// of its frames are stationary.
template <class Still>
std::pair<std::size_t, std::size_t> stretch_from(const std::vector<double>& curve, std::size_t from,
                                                 double middle, const Still& still) {
    const bool up = curve[from] >= middle;
    std::size_t j = from;
    std::size_t stationary = 0;
    while (j < curve.size() && (curve[j] >= middle) == up) {
        stationary += still(j) ? 1 : 0;
        ++j;
    }
    return {j, stationary};
}

Walk walk_plateaus(const std::vector<double>& curve, const std::vector<double>& weight,
                   double min_weight, Seconds hop, const Levels& levels,
                   const OrnamentParams& params) {
    const auto min_frames = static_cast<std::size_t>(
        std::max(1L, std::lround(params.min_plateau.value() / hop.value())));
    const auto still = [&](std::size_t k) {
        const std::size_t a = k > 0 ? k - 1 : k;
        const std::size_t b = std::min(curve.size() - 1, k + 1);
        const double span = static_cast<double>(b - a) * hop.value();
        return span > 0.0 && std::abs(curve[b] - curve[a]) / span <= params.still_cents_per_s;
    };
    Walk out;
    std::size_t flat = 0;
    std::size_t counted = 0;
    int side_of_last = 0;
    bool have_first = false;
    std::size_t i = 0;
    while (i < curve.size()) {
        if (weight[i] < min_weight) {
            ++i;
            continue;
        }
        const bool up = curve[i] >= levels.middle;
        const auto [j, stationary] = stretch_from(curve, i, levels.middle, still);
        const std::size_t length = j - i;
        if (length >= min_frames) {
            // Every plateau must itself be flat: a ramp (scoop, fall) is not a trill level.
            out.ramp = out.ramp || static_cast<double>(stationary) <
                                       params.flat_top_share * static_cast<double>(length);
            flat += stationary;
            counted += length;
            ++out.plateaus;
            const int side = up ? 1 : -1;
            out.transitions += (side_of_last != 0 && side != side_of_last) ? 1 : 0;
            side_of_last = side;
            if (!have_first) {
                out.first_level = up ? levels.upper : levels.lower;
                have_first = true;
            }
        }
        i = j;
    }
    out.flat_share = counted == 0 ? 0.0 : static_cast<double>(flat) / static_cast<double>(counted);
    return out;
}

}  // namespace

std::optional<Ornament> find_ornament(const std::vector<double>& curve_cents,
                                      const std::vector<double>& weight, double min_weight,
                                      Seconds hop, const OrnamentParams& params) {
    std::vector<double> used;
    for (std::size_t i = 0; i < curve_cents.size(); ++i) {
        if (weight[i] >= min_weight) {
            used.push_back(curve_cents[i]);
        }
    }
    if (used.size() < k_min_frames) {
        return std::nullopt;
    }
    const auto levels = two_levels(used, params);
    if (!levels) {
        return std::nullopt;
    }
    const Walk walk = walk_plateaus(curve_cents, weight, min_weight, hop, *levels, params);
    if (walk.ramp || walk.transitions < params.min_transitions ||
        walk.plateaus < params.min_transitions + 1) {
        return std::nullopt;
    }
    const double other = walk.first_level == levels->upper ? levels->lower : levels->upper;
    return Ornament{
        .main_cents = walk.first_level, .secondary_cents = other, .flat_share = walk.flat_share};
}

}  // namespace anno
