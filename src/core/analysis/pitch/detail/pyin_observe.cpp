#include "core/analysis/pitch/detail/pyin_observe.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr int k_threshold_count = 100;
constexpr double k_beta_alpha = 2.0;
constexpr double k_global_min_boost =
    0.01;  // pYIN: mass given to the global minimum when no threshold passes
constexpr double k_octave_candidate_mass = 1e-3;  // relative to the strongest candidate
constexpr double k_a4_hz = 440.0;

/// Parabolic interpolation of the normalised cross-correlation peak next to `tau0`: the fractional
/// lag of the local maximum. Falls back to the integer lag at the edges of the lag range.
double refine_lag(const std::vector<double>& nccf, int tau0, int tau_min, int tau_max) {
    int p = tau0;
    for (int i = 0; i < 3; ++i) {  // climb to the local maximum of the correlation
        if (p + 1 <= tau_max && nccf[idx(p + 1)] > nccf[idx(p)]) {
            ++p;
        } else if (p - 1 >= tau_min && nccf[idx(p - 1)] > nccf[idx(p)]) {
            --p;
        } else {
            break;
        }
    }
    if (p <= tau_min || p >= tau_max) {
        return static_cast<double>(p);
    }
    const double y0 = nccf[idx(p - 1)];
    const double y1 = nccf[idx(p)];
    const double y2 = nccf[idx(p + 1)];
    const double curvature = y0 - 2.0 * y1 + y2;
    if (curvature >= 0.0) {
        return static_cast<double>(p);
    }
    return static_cast<double>(p) + std::clamp(0.5 * (y0 - y2) / curvature, -1.0, 1.0);
}

/// The lag of the first value below `threshold` (descended to its local minimum), or -1.
int first_minimum_below(const std::vector<double>& cmnd, double threshold, int tau_min,
                        int tau_max) {
    int tau = tau_min;
    while (tau <= tau_max && cmnd[idx(tau)] >= threshold) {
        ++tau;
    }
    if (tau > tau_max) {
        return -1;
    }
    while (tau + 1 <= tau_max && cmnd[idx(tau + 1)] < cmnd[idx(tau)]) {
        ++tau;
    }
    return tau;
}

/// Probability mass per integer lag from the threshold distribution.
std::vector<double> lag_masses(const std::vector<double>& cmnd, const ObserveSetup& s) {
    std::vector<double> mass(idx(s.tau_max) + 1, 0.0);
    int global_min = s.tau_min;
    for (int tau = s.tau_min; tau <= s.tau_max; ++tau) {
        if (cmnd[idx(tau)] < cmnd[idx(global_min)]) {
            global_min = tau;
        }
    }
    for (int k = 0; k < k_threshold_count; ++k) {
        const double threshold = static_cast<double>(k + 1) / k_threshold_count;
        const int tau = first_minimum_below(cmnd, threshold, s.tau_min, s.tau_max);
        if (tau < 0) {
            mass[idx(global_min)] += k_global_min_boost * s.weights[idx(k)];
        } else {
            mass[idx(tau)] += s.weights[idx(k)];
        }
    }
    return mass;
}

/// Adds (or strengthens) the candidate of `lag` in its bin; false if it lies outside the bins.
void add_candidate(FrameObservation& obs, const ObserveSetup& s, double lag, double mass) {
    const double cents = to_cents(Hz{s.rate / lag}, Hz{k_a4_hz}).value();
    const int bin = s.bins.of(cents);
    if (bin < 0 || bin >= s.bins.count) {
        return;
    }
    auto it = std::ranges::find(obs.candidates, bin, &Candidate::bin);
    if (it == obs.candidates.end()) {
        obs.candidates.push_back({.bin = bin, .cents = cents, .tau = lag, .mass = mass});
        return;
    }
    if (mass > it->mass) {  // keep the better-supported refinement
        it->cents = cents;
        it->tau = lag;
    }
    it->mass += mass;
}

/// Weak octave candidates, so the wrong octave is available when it is the right one (Note
/// Assignment).
void add_octave_candidates(FrameObservation& obs, const std::vector<double>& nccf,
                           const ObserveSetup& s) {
    if (obs.candidates.empty()) {
        return;
    }
    const Candidate top = *std::ranges::max_element(obs.candidates, {}, &Candidate::mass);
    for (const double factor : {0.5, 2.0}) {
        const int lag0 = static_cast<int>(std::lround(top.tau * factor));
        if (lag0 < s.tau_min || lag0 > s.tau_max) {
            continue;
        }
        const double lag = refine_lag(nccf, lag0, s.tau_min, s.tau_max);
        const double cents = to_cents(Hz{s.rate / lag}, Hz{k_a4_hz}).value();
        const int bin = s.bins.of(cents);
        const bool new_bin =
            std::ranges::find(obs.candidates, bin, &Candidate::bin) == obs.candidates.end();
        if (new_bin) {
            add_candidate(obs, s, lag, k_octave_candidate_mass * top.mass);
        }
    }
}

/// A weak fundamental under strong harmonics gives a deep CMND dip at 1/2 or 1/3 of the true
/// period. When the strongest candidate has a clearly deeper dip at 3x or 2x its lag, its mass
/// moves there. The octave-neighbour candidates are added afterwards from the new top.
void correct_subharmonic(FrameObservation& obs, const std::vector<double>& cmnd,
                         const std::vector<double>& nccf, const ObserveSetup& s) {
    if (s.subharmonic_margin <= 0.0 || obs.candidates.empty()) {
        return;
    }
    const auto top_it = std::ranges::max_element(obs.candidates, {}, &Candidate::mass);
    const double top_value =
        cmnd[idx(std::clamp(static_cast<int>(std::lround(top_it->tau)), s.tau_min, s.tau_max))];
    for (const int factor : {3, 2}) {
        const double centre = top_it->tau * factor;
        const int lo = std::max(s.tau_min, static_cast<int>(std::floor(centre * 0.97)));
        const int hi = std::min(s.tau_max, static_cast<int>(std::ceil(centre * 1.03)));
        if (lo > hi) {
            continue;
        }
        int best = lo;
        for (int tau = lo; tau <= hi; ++tau) {
            if (cmnd[idx(tau)] < cmnd[idx(best)]) {
                best = tau;
            }
        }
        const double margin = factor == 2 ? s.subharmonic_margin2 : s.subharmonic_margin;
        if (cmnd[idx(best)] + margin < top_value) {
            const double mass = top_it->mass;
            const double lag = refine_lag(nccf, best, s.tau_min, s.tau_max);
            top_it->mass *= k_octave_candidate_mass;
            add_candidate(obs, s, lag, mass);
            return;
        }
    }
}

}  // namespace

int Bins::of(double cents) const {
    return static_cast<int>(std::lround((cents - min_cents) / step_cents));
}

std::vector<double> threshold_weights(double mean) {
    const double beta = k_beta_alpha * (1.0 - mean) / mean;
    std::vector<double> w(idx(k_threshold_count));
    double sum = 0.0;
    for (int k = 0; k < k_threshold_count; ++k) {
        const double s = static_cast<double>(k + 1) / k_threshold_count;
        w[idx(k)] = std::pow(s, k_beta_alpha - 1.0) * std::pow(1.0 - s, beta - 1.0);
        sum += w[idx(k)];
    }
    for (double& v : w) {
        v /= sum;
    }
    return w;
}

FrameObservation observe(const std::vector<double>& cmnd, const std::vector<double>& nccf,
                         const ObserveSetup& setup) {
    FrameObservation obs;
    const std::vector<double> mass = lag_masses(cmnd, setup);
    double total = 0.0;
    for (int tau = setup.tau_min; tau <= setup.tau_max; ++tau) {
        if (mass[idx(tau)] > 0.0) {
            total += mass[idx(tau)];
            add_candidate(obs, setup, refine_lag(nccf, tau, setup.tau_min, setup.tau_max),
                          mass[idx(tau)]);
        }
    }
    obs.unvoiced = std::max(0.0, 1.0 - total);
    correct_subharmonic(obs, cmnd, nccf, setup);
    add_octave_candidates(obs, nccf, setup);
    return obs;
}

}  // namespace anno
