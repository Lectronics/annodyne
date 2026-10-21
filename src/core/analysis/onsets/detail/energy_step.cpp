#include "core/analysis/onsets/detail/energy_step.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace anno {

namespace {

constexpr double k_rms_window_s = 0.020;
constexpr double k_hop_s = 0.005;
constexpr double k_db_floor = -120.0;

std::vector<double> level_db(std::span<const float> x, std::size_t window, std::size_t hop) {
    std::vector<double> db;
    if (x.size() < window) {
        return db;
    }
    for (std::size_t i = 0; i + window <= x.size(); i += hop) {
        double sum = 0.0;
        for (std::size_t k = 0; k < window; ++k) {
            const double v = x[i + k];
            sum += v * v;
        }
        db.push_back(
            std::max(k_db_floor, 10.0 * std::log10(sum / static_cast<double>(window) + 1e-12)));
    }
    return db;
}

double mean_of(const std::vector<double>& v, std::size_t from, std::size_t to) {
    return std::accumulate(v.begin() + static_cast<std::ptrdiff_t>(from),
                           v.begin() + static_cast<std::ptrdiff_t>(to), 0.0) /
           static_cast<double>(to - from);
}

}  // namespace

std::vector<EnergyStep> find_energy_steps(std::span<const float> mono, Hz rate,
                                          const EnergyStepParams& params) {
    const double r = rate.value();
    const auto window = static_cast<std::size_t>(std::llround(k_rms_window_s * r));
    const auto hop = static_cast<std::size_t>(std::llround(k_hop_s * r));
    const std::vector<double> db = level_db(mono, window, hop);
    const auto w = static_cast<std::size_t>(std::llround(params.window.value() / k_hop_s));
    std::vector<EnergyStep> steps;
    if (db.size() < 2 * w + 1 || w == 0) {
        return steps;
    }
    const double top = *std::ranges::max_element(db);
    std::vector<double> diff(db.size(), 0.0);
    for (std::size_t i = w; i + w < db.size(); ++i) {
        diff[i] = mean_of(db, i, i + w) - mean_of(db, i - w, i);
    }
    for (std::size_t i = w; i + w < db.size(); ++i) {
        const auto lo = diff.begin() + static_cast<std::ptrdiff_t>(i - w);
        const auto hi = diff.begin() + static_cast<std::ptrdiff_t>(i + w + 1);
        if (diff[i] < params.min_rise.value() || diff[i] < *std::max_element(lo, hi) ||
            mean_of(db, i - w, i) < top - params.floor_below_peak.value()) {
            continue;
        }
        const double mid = 0.5 * (mean_of(db, i - w, i) + mean_of(db, i, i + w));
        std::size_t cross = i;
        for (std::size_t k = i - w; k < i + w; ++k) {
            if (db[k] >= mid) {
                cross = k;
                break;
            }
        }
        const double t = (static_cast<double>(cross * hop) + 0.5 * static_cast<double>(window)) / r;
        steps.push_back({.time = SourceTime{} + Seconds{t}, .rise_db = diff[i]});
    }
    return steps;
}

}  // namespace anno
