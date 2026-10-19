#include "core/types/time_map.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

double raw(SourceTime t) {
    return t.unchecked_since_origin().value();
}
double raw(OutputTime t) {
    return t.unchecked_since_origin().value();
}

SourceTime make_source(double v) {
    return SourceTime::unchecked_from_origin(Seconds{v});
}
OutputTime make_output(double v) {
    return OutputTime::unchecked_from_origin(Seconds{v});
}

/// Index of the last anchor whose coordinate is <= x, or -1 if x is before the first.
template <class Key>
std::ptrdiff_t segment_index(std::span<const TimePair> anchors, double x, Key key) {
    const auto it = std::upper_bound(anchors.begin(), anchors.end(), x,
                                     [&](double v, const TimePair& a) { return v < key(a); });
    return (it - anchors.begin()) - 1;
}

}  // namespace

TimeMap::TimeMap(std::vector<TimePair> anchors, bool identity) noexcept
    : anchors_{std::move(anchors)}, identity_{identity} {}

TimeMap TimeMap::identity() {
    return TimeMap{{TimePair{}}, true};
}

Result<TimeMap> TimeMap::from_anchors(std::vector<TimePair> anchors) {
    if (anchors.empty()) {
        return fail(ErrorCode::invalid_argument, "a time map needs at least one anchor");
    }
    bool identity = true;
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        const double s = raw(anchors[i].source);
        const double t = raw(anchors[i].output);
        if (!std::isfinite(s) || !std::isfinite(t)) {
            return fail(ErrorCode::invalid_argument, "time map anchor is not finite");
        }
        if (i > 0 && (s <= raw(anchors[i - 1].source) || t <= raw(anchors[i - 1].output))) {
            return fail(ErrorCode::invalid_argument,
                        "time map anchors must be strictly increasing in source and output");
        }
        identity = identity && s == t;
    }
    return TimeMap{std::move(anchors), identity};
}

OutputTime TimeMap::to_output(SourceTime s) const noexcept {
    if (identity_) {
        return make_output(raw(s));
    }
    const double x = raw(s);
    const auto key = [](const TimePair& a) { return raw(a.source); };
    const auto i = segment_index(anchors_, x, key);
    if (i < 0) {
        return make_output(raw(anchors_.front().output) + (x - raw(anchors_.front().source)));
    }
    const auto& a = anchors_[static_cast<std::size_t>(i)];
    if (static_cast<std::size_t>(i) + 1 == anchors_.size()) {
        return make_output(raw(a.output) + (x - raw(a.source)));
    }
    const auto& b = anchors_[static_cast<std::size_t>(i) + 1];
    const double slope = (raw(b.output) - raw(a.output)) / (raw(b.source) - raw(a.source));
    return make_output(raw(a.output) + (x - raw(a.source)) * slope);
}

SourceTime TimeMap::to_source(OutputTime tau) const noexcept {
    if (identity_) {
        return make_source(raw(tau));
    }
    const double y = raw(tau);
    const auto key = [](const TimePair& a) { return raw(a.output); };
    const auto i = segment_index(anchors_, y, key);
    if (i < 0) {
        return make_source(raw(anchors_.front().source) + (y - raw(anchors_.front().output)));
    }
    const auto& a = anchors_[static_cast<std::size_t>(i)];
    if (static_cast<std::size_t>(i) + 1 == anchors_.size()) {
        return make_source(raw(a.source) + (y - raw(a.output)));
    }
    const auto& b = anchors_[static_cast<std::size_t>(i) + 1];
    const double inverse_slope = (raw(b.source) - raw(a.source)) / (raw(b.output) - raw(a.output));
    return make_source(raw(a.source) + (y - raw(a.output)) * inverse_slope);
}

Ratio TimeMap::stretch_ratio(SourceTime s) const noexcept {
    if (identity_) {
        return Ratio{1.0};
    }
    const auto key = [](const TimePair& a) { return raw(a.source); };
    const auto i = segment_index(anchors_, raw(s), key);
    if (i < 0 || static_cast<std::size_t>(i) + 1 == anchors_.size()) {
        return Ratio{1.0};
    }
    const auto& a = anchors_[static_cast<std::size_t>(i)];
    const auto& b = anchors_[static_cast<std::size_t>(i) + 1];
    return Ratio{(raw(b.output) - raw(a.output)) / (raw(b.source) - raw(a.source))};
}

}  // namespace anno
