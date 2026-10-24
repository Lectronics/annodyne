#include "core/timemap/map_diff.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_same_s = 1e-9;

double sec(SourceTime t) {
    return (t - SourceTime{}).value();
}
double sec(OutputTime t) {
    return (t - OutputTime{}).value();
}

/// What the map does over the source piece [x, y]: its output position of both ends, or "cut".
struct Piece {
    bool cut = false;
    double out_x = 0.0;
    double out_y = 0.0;
};

Piece piece_at(const TimeMap& map, double x, double y) {
    for (const TimeSpan& span : map.spans()) {
        const double a = sec(span.source_start);
        const double b = sec(span.source_end);
        if (b - a <= 0.0 || x < a - k_same_s || y > b + k_same_s) {
            continue;
        }
        if (span.kind == SpanKind::cut) {
            return {.cut = true};
        }
        const double slope = (sec(span.output_end) - sec(span.output_start)) / (b - a);
        const double o = sec(span.output_start);
        return {.cut = false, .out_x = o + (x - a) * slope, .out_y = o + (y - a) * slope};
    }
    return {.cut = true};
}

bool same(const Piece& a, const Piece& b) {
    return a.cut == b.cut && (a.cut || (std::abs(a.out_x - b.out_x) < k_same_s &&
                                        std::abs(a.out_y - b.out_y) < k_same_s));
}

}  // namespace

Result<std::vector<SourceRange>> changed_source_ranges(const TimeMap& old_map,
                                                       const TimeMap& new_map, Seconds margin) {
    if (margin < Seconds{0.0} ||
        std::abs((old_map.source_length() - new_map.source_length()).value()) > k_same_s) {
        return fail(ErrorCode::invalid_argument,
                    "changed_source_ranges: maps of different recordings or a negative margin");
    }
    std::vector<double> cuts{0.0, old_map.source_length().value()};
    for (const TimeMap* m : {&old_map, &new_map}) {
        for (const TimeSpan& span : m->spans()) {
            cuts.push_back(sec(span.source_start));
            cuts.push_back(sec(span.source_end));
        }
    }
    std::ranges::sort(cuts);
    std::vector<std::pair<double, double>> dirty;
    for (std::size_t k = 1; k < cuts.size(); ++k) {
        const double x = cuts[k - 1];
        const double y = cuts[k];
        if (y - x <= k_same_s || same(piece_at(old_map, x, y), piece_at(new_map, x, y))) {
            continue;
        }
        const double lo = std::max(0.0, x - margin.value());
        const double hi = std::min(old_map.source_length().value(), y + margin.value());
        if (!dirty.empty() && lo <= dirty.back().second) {
            dirty.back().second = std::max(dirty.back().second, hi);
        } else {
            dirty.emplace_back(lo, hi);
        }
    }
    std::vector<SourceRange> out;
    out.reserve(dirty.size());
    for (const auto& [lo, hi] : dirty) {
        auto range = SourceRange::make(SourceTime::unchecked_from_origin(Seconds{lo}),
                                       SourceTime::unchecked_from_origin(Seconds{hi}));
        if (!range) {
            return Unexpected{range.error()};
        }
        out.push_back(*range);
    }
    return out;
}

}  // namespace anno
