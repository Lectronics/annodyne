#include "core/analysis/pitch/detail/pitch_hmm.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace anno {

namespace {

constexpr double k_emission_floor = 1e-4;

/// Emission costs of one frame: [0, bins) voiced, [bins] unvoiced.
void fill_emission(const FrameObservation& obs, const Bins& bins, const std::vector<int>& excluded,
                   int exclusion_bins, std::vector<double>& emit) {
    const double floor_cost = -std::log(k_emission_floor);
    std::ranges::fill(emit, floor_cost);
    for (const Candidate& c : obs.candidates) {
        emit[idx(c.bin)] = -std::log(c.mass + k_emission_floor);
    }
    for (const int e : excluded) {
        const int lo = std::max(0, e - exclusion_bins);
        const int hi = std::min(bins.count - 1, e + exclusion_bins);
        for (int b = lo; b <= hi; ++b) {
            emit[idx(b)] = floor_cost;
        }
    }
    emit[idx(bins.count)] = -std::log(obs.unvoiced + k_emission_floor);
}

struct BestVoiced {
    double cost = 0.0;
    int bin = 0;
};

BestVoiced best_voiced(const std::vector<double>& cost, int bin_count) {
    const auto it = std::min_element(cost.begin(), cost.begin() + bin_count);
    return {.cost = *it, .bin = static_cast<int>(it - cost.begin())};
}

/// One Viterbi step into the voiced bin `b`: the cheapest predecessor and its cost.
std::pair<double, int> into_voiced(const std::vector<double>& prev, const HmmCosts& costs,
                                   const BestVoiced& global, int bin_count, int b) {
    const int u = bin_count;
    double best = prev[idx(u)] + costs.unvoiced_to_voiced;
    int from = u;
    if (global.cost + costs.jump < best) {
        best = global.cost + costs.jump;
        from = global.bin;
    }
    const int lo = std::max(0, b - costs.reach);
    const int hi = std::min(bin_count - 1, b + costs.reach);
    for (int a = lo; a <= hi; ++a) {
        const double v = prev[idx(a)] + costs.smooth[idx(std::abs(a - b))];
        if (v < best) {
            best = v;
            from = a;
        }
    }
    return {best, from};
}

std::pair<double, int> into_unvoiced(const std::vector<double>& prev, const HmmCosts& costs,
                                     const BestVoiced& global, int bin_count) {
    const int u = bin_count;
    double best = prev[idx(u)] + costs.unvoiced_to_unvoiced;
    int from = u;
    if (global.cost + costs.voiced_to_unvoiced < best) {
        best = global.cost + costs.voiced_to_unvoiced;
        from = global.bin;
    }
    return {best, from};
}

}  // namespace

HmmCosts make_hmm_costs(const HmmParams& p, int bin_count) {
    HmmCosts c;
    c.reach = std::max(1, static_cast<int>(std::lround(p.max_jump_cents / p.bin_cents)));
    std::vector<double> tri(idx(c.reach) + 1);
    double tri_sum = 0.0;
    for (int d = 0; d <= c.reach; ++d) {
        tri[idx(d)] = 1.0 - static_cast<double>(d) / (c.reach + 1);
        tri_sum += d == 0 ? tri[0] : 2.0 * tri[idx(d)];
    }
    const double jump_mass = p.jump_prob * std::max(0, bin_count - (2 * c.reach + 1));
    const double smooth_mass = std::max(1e-3, 1.0 - p.voiced_to_unvoiced_prob - jump_mass);
    c.smooth.resize(tri.size());
    for (std::size_t d = 0; d < tri.size(); ++d) {
        c.smooth[d] = -std::log(smooth_mass * tri[d] / tri_sum);
    }
    c.jump = -std::log(p.jump_prob);
    const double relief = p.stay_relief * c.smooth[0];  // the same for every voiced-to-voiced step
    for (double& v : c.smooth) {
        v -= relief;
    }
    c.jump -= relief;
    c.voiced_to_unvoiced = -std::log(p.voiced_to_unvoiced_prob);
    c.unvoiced_to_unvoiced = -std::log(1.0 - p.unvoiced_to_voiced_prob);
    c.unvoiced_to_voiced = -std::log(p.unvoiced_to_voiced_prob / bin_count);
    return c;
}

std::vector<int> viterbi(const std::vector<FrameObservation>& obs, const Bins& bins,
                         const HmmCosts& costs, const std::vector<std::vector<int>>& excluded,
                         int exclusion_bins) {
    assert(!obs.empty() && excluded.size() == obs.size());
    const auto frames = obs.size();
    const auto states = idx(bins.count) + 1;  // the last state is "unvoiced"
    std::vector<int> back(frames * states, 0);
    std::vector<double> prev(states);
    std::vector<double> cur(states);
    std::vector<double> emit(states);

    fill_emission(obs[0], bins, excluded[0], exclusion_bins, prev);
    for (std::size_t t = 1; t < frames; ++t) {
        fill_emission(obs[t], bins, excluded[t], exclusion_bins, emit);
        const BestVoiced global = best_voiced(prev, bins.count);
        for (int b = 0; b < bins.count; ++b) {
            const auto [cost, from] = into_voiced(prev, costs, global, bins.count, b);
            cur[idx(b)] = cost + emit[idx(b)];
            back[t * states + idx(b)] = from;
        }
        const auto [u_cost, u_from] = into_unvoiced(prev, costs, global, bins.count);
        cur[idx(bins.count)] = u_cost + emit[idx(bins.count)];
        back[t * states + idx(bins.count)] = u_from;
        prev.swap(cur);
    }
    std::vector<int> path(frames, -1);
    auto state = static_cast<std::size_t>(std::ranges::min_element(prev) - prev.begin());
    for (std::size_t t = frames; t-- > 0;) {
        path[t] = state == idx(bins.count) ? -1 : static_cast<int>(state);
        state = static_cast<std::size_t>(back[t * states + state]);
    }
    return path;
}

}  // namespace anno
