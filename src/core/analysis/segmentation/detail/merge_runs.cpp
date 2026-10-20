#include "core/analysis/segmentation/detail/merge_runs.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace anno {

namespace {

constexpr double k_transition_s = 0.060;  // longest transition between two plateaus of a trill
constexpr double k_side_s = 0.040;        // pitch is read this far before / after a pitch_change

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

/// Mean raw f0 of the voiced frames in [from_s, to_s]; NaN if none.
double mean_f0(const PitchTrack& p, double from_s, double to_s) {
    double sum = 0.0;
    int n = 0;
    for (std::size_t i = 0; i < p.f0.size(); ++i) {
        const double t = time_of(p, i);
        if (t >= from_s && t <= to_s && is_voiced(p, i)) {
            sum += p.f0[i].value();
            ++n;
        }
    }
    return n == 0 ? std::nan("") : sum / n;
}

/// Indices (into onsets) of the pitch_change onsets that belong to a trill cluster.
std::vector<bool> trill_members(const MergeInput& in) {
    const OnsetList& on = *in.onsets;
    std::vector<bool> member(on.times.size(), false);
    std::vector<std::size_t> steps;  // pitch_change onsets, in time order
    for (std::size_t i = 0; i < on.times.size(); ++i) {
        if (on.kind[i] == OnsetKind::pitch_change) {
            steps.push_back(i);
        }
    }
    const double max_spacing = in.params->trill_max_plateau.value() + k_transition_s;
    const auto direction = [&](std::size_t onset) {
        const double t = (on.times[onset] - SourceTime{}).value();
        const double before = mean_f0(*in.pitch, t - k_side_s - 0.02, t - 0.02);
        const double after = mean_f0(*in.pitch, t + 0.02, t + k_side_s + 0.02);
        if (std::isnan(before) || std::isnan(after)) {
            return 0;
        }
        return after > before ? 1 : -1;
    };
    std::size_t begin = 0;
    while (begin < steps.size()) {
        std::size_t end = begin + 1;
        int last_direction = direction(steps[begin]);
        while (end < steps.size() &&
               (on.times[steps[end]] - on.times[steps[end - 1]]).value() <= max_spacing) {
            const int d = direction(steps[end]);
            if (d == 0 || d == last_direction || last_direction == 0) {
                break;
            }
            last_direction = d;
            ++end;
        }
        if (end - begin >= static_cast<std::size_t>(in.params->trill_min_steps)) {
            for (std::size_t k = begin; k < end; ++k) {
                member[steps[k]] = true;
            }
        }
        begin = std::max(end, begin + 1);
    }
    return member;
}

/// An onset that separates the voiced end `from_s` of one run from the voiced start `to_s` of the
/// next.
bool onset_between(const MergeInput& in, const std::vector<bool>& trill, double from_s,
                   double to_s) {
    const OnsetList& on = *in.onsets;
    const double zone = in.params->merge_onset_zone.value();
    for (std::size_t i = 0; i < on.times.size(); ++i) {
        const double t = (on.times[i] - SourceTime{}).value();
        if (t < from_s - zone || t > to_s + zone) {
            continue;
        }
        if (on.kind[i] == OnsetKind::pitch_change) {
            if (!trill[i]) {
                return true;
            }
        } else if ((on.kind[i] == OnsetKind::attack || on.kind[i] == OnsetKind::consonant) &&
                   on.strength[i] >= in.params->merge_onset_strength) {
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
    const std::vector<bool> trill = trill_members(in);
    const double hop = in.pitch->grid.hop.value();
    std::vector<NoteRun> out{runs.front()};
    double previous_median = median_of(in, runs.front());
    for (std::size_t i = 1; i < runs.size(); ++i) {
        const NoteRun& next = runs[i];
        NoteRun& current = out.back();
        const auto end_voiced = last_voiced(*in.pitch, current);
        const auto start_voiced = first_voiced(*in.pitch, next);
        const double next_median = median_of(in, next);
        bool apart = !end_voiced || !start_voiced ||
                     std::abs(next_median - previous_median) > in.params->merge_max_cents.value();
        if (!apart) {
            const double from = time_of(*in.pitch, *end_voiced);
            const double to = time_of(*in.pitch, *start_voiced);
            const double gap = to - from - hop;  // unvoiced time between the two voiced parts
            apart = gap >= in.params->min_gap_for_split.value() - 1e-9 ||
                    onset_between(in, trill, from + hop, to);
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
