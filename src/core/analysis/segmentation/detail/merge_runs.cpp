#include "core/analysis/segmentation/detail/merge_runs.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace anno {

namespace {

constexpr double k_transition_s = 0.060;  // longest transition between two plateaus of a trill

double time_of(const PitchTrack& p, std::size_t i) {
    return (p.grid.start - SourceTime{}).value() + static_cast<double>(i) * p.grid.hop.value();
}

std::optional<std::size_t> last_voiced(const PitchTrack& p, const NoteRun& r) {
    for (std::size_t t = r.last + 1; t-- > r.first;) {
        if (is_voiced(p, t)) {
            return t;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> first_voiced(const PitchTrack& p, const NoteRun& r) {
    for (std::size_t t = r.first; t <= r.last; ++t) {
        if (is_voiced(p, t)) {
            return t;
        }
    }
    return std::nullopt;
}

double median_of(const MergeInput& in, const NoteRun& r) {
    std::vector<double> v;
    for (std::size_t t = r.first; t <= r.last; ++t) {
        if (is_voiced(*in.pitch, t) && !std::isnan((*in.smoothed_cents)[t])) {
            v.push_back((*in.smoothed_cents)[t]);
        }
    }
    if (v.empty()) {
        return 0.0;
    }
    std::ranges::sort(v);
    return v[v.size() / 2];
}

/// A step of the raw f0: where it moves by at least `k_step_cents` within `k_step_frames`.
struct Step {
    double time_s = 0.0;
    int direction = 0;  // +1 up, -1 down
};

constexpr double k_step_cents = 60.0;
constexpr std::size_t k_step_frames = 3;

std::vector<Step> find_steps(const PitchTrack& p) {
    std::vector<Step> steps;
    for (std::size_t i = 0; i + k_step_frames < p.f0.size(); ++i) {
        if (!is_voiced(p, i) || !is_voiced(p, i + k_step_frames)) {
            continue;
        }
        const double d = p.f0[i + k_step_frames].value() - p.f0[i].value();
        if (std::abs(d) < k_step_cents) {
            continue;
        }
        const double t =
            time_of(p, i) + 0.5 * static_cast<double>(k_step_frames) * p.grid.hop.value();
        const int dir = d > 0.0 ? 1 : -1;
        if (!steps.empty() && dir == steps.back().direction &&
            t - steps.back().time_s <=
                2.0 * static_cast<double>(k_step_frames) * p.grid.hop.value()) {
            continue;  // the same step seen again
        }
        steps.push_back({.time_s = t, .direction = dir});
    }
    return steps;
}

/// Time spans [from, to] of trills: >= trill_min_steps alternating-direction steps, consecutive
/// steps no further apart than a plateau (trill_max_plateau) plus a transition.
std::vector<std::pair<double, double>> trill_regions(const MergeInput& in) {
    const std::vector<Step> steps = find_steps(*in.pitch);
    const double max_spacing = in.params->trill_max_plateau.value() + k_transition_s;
    std::vector<std::pair<double, double>> regions;
    std::size_t begin = 0;
    while (begin < steps.size()) {
        std::size_t end = begin + 1;
        while (end < steps.size() && steps[end].direction != steps[end - 1].direction &&
               steps[end].time_s - steps[end - 1].time_s <= max_spacing) {
            ++end;
        }
        if (end - begin >= static_cast<std::size_t>(in.params->trill_min_steps)) {
            regions.emplace_back(steps[begin].time_s, steps[end - 1].time_s);
        }
        begin = end;
    }
    return regions;
}

bool inside_any(const std::vector<std::pair<double, double>>& regions, double t, double slack) {
    return std::ranges::any_of(
        regions, [&](const auto& r) { return t >= r.first - slack && t <= r.second + slack; });
}

/// An onset that separates the voiced end `from_s` of one run from the voiced start `to_s` of the
/// next. Onsets inside a trill do not.
bool onset_between(const MergeInput& in, const std::vector<std::pair<double, double>>& trills,
                   double from_s, double to_s) {
    const OnsetList& on = *in.onsets;
    const double zone = in.params->merge_onset_zone.value();
    for (std::size_t i = 0; i < on.times.size(); ++i) {
        const double t = (on.times[i] - SourceTime{}).value();
        if (t < from_s - zone || t > to_s + zone || inside_any(trills, t, zone)) {
            continue;
        }
        const bool transient =
            on.kind[i] == OnsetKind::attack || on.kind[i] == OnsetKind::consonant;
        if (on.kind[i] == OnsetKind::pitch_change ||
            (transient && on.strength[i] >= in.params->merge_onset_strength)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<NoteRun> merge_runs(std::vector<NoteRun> runs, const MergeInput& in) {
    if (runs.size() < 2) {
        return runs;
    }
    const std::vector<std::pair<double, double>> trills = trill_regions(in);
    const double hop = in.pitch->grid.hop.value();
    std::vector<NoteRun> out{runs.front()};
    double previous_median = median_of(in, runs.front());
    for (std::size_t i = 1; i < runs.size(); ++i) {
        const NoteRun& next = runs[i];
        NoteRun& current = out.back();
        const auto end_voiced = last_voiced(*in.pitch, current);
        const auto start_voiced = first_voiced(*in.pitch, next);
        const double next_median = median_of(in, next);
        bool apart =
            !end_voiced || !start_voiced ||
            (std::abs(next_median - previous_median) > in.params->merge_max_cents.value() &&
             !inside_any(trills, time_of(*in.pitch, first_voiced(*in.pitch, next).value_or(0)),
                         in.params->merge_onset_zone.value()));
        if (!apart) {
            const double from = time_of(*in.pitch, *end_voiced);
            const double to = time_of(*in.pitch, *start_voiced);
            const double gap = to - from - hop;  // unvoiced time between the two voiced parts
            apart = gap >= in.params->min_gap_for_split.value() - 1e-9 ||
                    onset_between(in, trills, from + hop, to);
        }
        if (apart) {
            out.push_back(next);
        } else {
            if (next.last - next.first > current.last - current.first) {
                current.bin = next.bin;
            }
            current.last = next.last;
        }
        previous_median = next_median;
    }
    return out;
}

}  // namespace anno
