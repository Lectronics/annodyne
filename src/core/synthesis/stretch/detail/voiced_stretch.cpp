#include "core/synthesis/stretch/detail/voiced_stretch.h"

#include "core/synthesis/grain/marks.h"
#include "core/synthesis/grain/overlap_add.h"

#include <algorithm>
#include <cmath>

namespace anno::stretch_detail {

namespace {

constexpr double k_min_window_sum = 1e-6;

/// Synthesis positions (output samples) from `p_first` to `p_last`: steps of the output period
/// (the source period at the source time of each position, or with a regenerated vibrato), the
/// spacing then scaled so the chain ends exactly on p_last. Includes one extra position before the
/// first and after the last, so the edges are covered.
std::vector<double> synthesis_positions(const OutputPitch& output, double p_first, double p_last) {
    std::vector<double> raw{p_first};
    while (raw.back() < p_last - 0.5 * output.period_samples(raw.back())) {
        raw.push_back(raw.back() + output.period_samples(raw.back()));
    }
    const double scale = raw.size() > 1 ? (p_last - p_first) / (raw.back() - p_first) : 1.0;
    std::vector<double> positions;
    positions.reserve(raw.size() + 2);
    positions.push_back(p_first - output.period_samples(p_first));
    for (const double q : raw) {
        positions.push_back(p_first + (q - p_first) * scale);
    }
    positions.back() = raw.size() > 1 ? p_last : positions.back();
    positions.push_back(positions.back() + output.period_samples(positions.back()));
    return positions;
}

/// Index of the mark nearest `position`.
std::size_t nearest_mark(const std::vector<double>& marks, double position) {
    const auto it = std::ranges::lower_bound(marks, position);
    if (it == marks.begin()) {
        return 0;
    }
    if (it == marks.end()) {
        return marks.size() - 1;
    }
    const auto i = static_cast<std::size_t>(it - marks.begin());
    return std::abs(marks[i] - position) < std::abs(marks[i - 1] - position) ? i : i - 1;
}

}  // namespace

std::optional<std::vector<double>> synthesise_voiced_run(const grain::Signal& x,
                                                         const grain::PeriodCurve& period,
                                                         const OutputPitch& output,
                                                         const SpanGeometry& g, std::int64_t begin,
                                                         std::int64_t end) {
    const double src_from = g.source_at(static_cast<double>(begin));
    const double src_to = g.source_at(static_cast<double>(end));
    const std::vector<double> marks = grain::analysis_marks(
        x, period, src_from - period.at(src_from), src_to + period.at(src_to));
    const auto first_in = std::ranges::lower_bound(marks, src_from);
    const auto last_in = std::ranges::upper_bound(marks, src_to);
    if (last_in - first_in < 2) {
        return std::nullopt;
    }
    const auto i0 = static_cast<std::size_t>(first_in - marks.begin());
    const auto i1 = static_cast<std::size_t>(last_in - marks.begin()) - 1;
    const std::vector<double> positions =
        synthesis_positions(output, static_cast<double>(begin) + (marks[i0] - src_from),
                            static_cast<double>(end) - (src_to - marks[i1]));

    grain::Accumulator acc{
        .first = begin,
        .sum = std::vector<double>(static_cast<std::size_t>(end - begin), 0.0),
        .window_sum = std::vector<double>(static_cast<std::size_t>(end - begin), 0.0),
        .window_square_sum = std::vector<double>(static_cast<std::size_t>(end - begin), 0.0),
        .output_period = std::vector<double>(static_cast<std::size_t>(end - begin), 0.0)};
    for (std::size_t j = 0; j < positions.size(); ++j) {
        const double p = positions[j];
        std::size_t i = nearest_mark(marks, g.source_at(p));
        if (j == 1) {
            i = i0;  // the first and last synthesis marks sit on their analysis marks
        } else if (j + 2 == positions.size()) {
            i = i1;
        }
        const double t = period.at(marks[i]);
        const double before = i > 0 ? marks[i] - marks[i - 1] : t;
        const double after = i + 1 < marks.size() ? marks[i + 1] - marks[i] : t;
        grain::add_grain(x,
                         {.analysis_mark = marks[i],
                          .synthesis_position = p,
                          .before = before,
                          .after = after,
                          .output_period = t},
                         acc);
    }
    std::vector<double> out(acc.sum.size(), 0.0);
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[n] = acc.window_sum[n] > k_min_window_sum ? acc.sum[n] / acc.window_sum[n] : 0.0;
    }
    return out;
}

}  // namespace anno::stretch_detail
