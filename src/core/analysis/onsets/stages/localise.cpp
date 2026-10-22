#include "core/analysis/onsets/stages/localise.h"

#include "core/analysis/onsets/detail/stage_check.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace anno {

namespace {

constexpr double k_energy_floor_db = -100.0;

struct Refined {
    std::int64_t sample = 0;
    bool rose = false;
};

/// Short-window energy in dB at sample `n` (window [n - w/2, n + w/2)).
double energy_db(std::span<const float> x, std::int64_t n, std::int64_t w) {
    const auto size = static_cast<std::int64_t>(x.size());
    const std::int64_t lo = std::max<std::int64_t>(0, n - w / 2);
    const std::int64_t hi = std::min<std::int64_t>(size, n + (w + 1) / 2);
    if (hi <= lo) {
        return k_energy_floor_db;
    }
    double sum = 0.0;
    for (std::int64_t i = lo; i < hi; ++i) {
        sum += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    }
    return std::max(k_energy_floor_db,
                    10.0 * std::log10(sum / static_cast<double>(hi - lo) + 1e-10));
}

/// The energy extremes of the search region [lo, hi] around `centre`.
struct Region {
    double floor_db = 1e9;      // quietest point at or before the centre
    std::int64_t floor_at = 0;  // the LAST such point
    double top_db = -1e9;       // loudest point
    double start_db = -1e9;     // loudest point in the first quarter of the region
    double end_db = -1e9;       // loudest point in the last quarter
};

Region measure_region(std::span<const float> x, std::int64_t lo, std::int64_t hi,
                      std::int64_t centre, std::int64_t w) {
    Region r;
    r.floor_at = lo;
    for (std::int64_t n = lo; n <= hi; ++n) {
        const double e = energy_db(x, n, w);
        if (n <= centre && e <= r.floor_db) {
            r.floor_db = e;
            r.floor_at = n;
        }
        r.top_db = std::max(r.top_db, e);
        if (n - lo <= (hi - lo) / 4) {
            r.start_db = std::max(r.start_db, e);
        }
        if (hi - n <= (hi - lo) / 4) {
            r.end_db = std::max(r.end_db, e);
        }
    }
    return r;
}

/// The sample where the energy rises through the halfway point (in dB) between the quietest point
/// before `centre` and the loudest point after it, searched within +-`radius` samples; `centre`
/// itself if there is no clear rise. Empty if the signal is clearly louder before `centre` than
/// after it: the end of a note makes spectral flux too, but it is not an onset.
std::optional<Refined> refine_to_energy_rise(std::span<const float> x, std::int64_t centre,
                                             std::int64_t radius, std::int64_t w,
                                             const LocaliseParams& p) {
    const std::int64_t lo = std::max<std::int64_t>(0, centre - radius);
    const std::int64_t hi =
        std::min<std::int64_t>(static_cast<std::int64_t>(x.size()) - 1, centre + radius);
    const Region r = measure_region(x, lo, hi, centre, w);
    if (r.start_db - r.end_db >= p.end_drop.value()) {
        return std::nullopt;
    }
    if (r.top_db - r.floor_db < p.min_rise.value()) {
        return Refined{.sample = centre, .rose = false};
    }
    const double halfway = r.floor_db + 0.5 * (r.top_db - r.floor_db);
    const std::int64_t from = p.scan == LocaliseScan::from_quietest_point ? r.floor_at : lo;
    for (std::int64_t n = from; n <= hi; ++n) {
        if (energy_db(x, n, w) >= halfway) {
            return Refined{.sample = n, .rose = true};
        }
    }
    return Refined{.sample = centre, .rose = false};
}

}  // namespace

Result<std::vector<LocalisedOnset>> localise(std::span<const float> mono, Hz rate,
                                             const PeakList& peaks, const LocaliseParams& params) {
    if (!detail::positive(rate.value()) || !detail::positive(peaks.hop.value()) ||
        !detail::positive(params.search.value()) ||
        !detail::positive(params.energy_window.value()) || !(params.min_rise.value() >= 0.0) ||
        !(params.end_drop.value() >= 0.0)) {
        return detail::bad_stage_argument("localise: invalid parameters");
    }
    const double sr = rate.value();
    const auto window = std::max<std::int64_t>(2, std::llround(params.energy_window.value() * sr));
    const auto radius = static_cast<std::int64_t>(std::llround(params.search.value() * sr));
    std::vector<LocalisedOnset> out;
    for (const ActivationPeak& p : peaks.peaks) {
        const auto centre = static_cast<std::int64_t>(
            std::llround(static_cast<double>(p.frame) * peaks.hop.value() * sr));
        if (std::cmp_greater(centre, mono.size())) {
            return detail::bad_stage_argument("localise: a peak lies beyond the signal");
        }
        const std::optional<Refined> refined =
            refine_to_energy_rise(mono, centre, radius, window, params);
        if (!refined) {
            continue;
        }
        out.push_back(
            {.sample = refined->sample,
             .time = SourceTime{} + Seconds{static_cast<double>(refined->sample) / sr},
             .strength = p.strength,
             .outcome = refined->rose ? LocaliseOutcome::rose : LocaliseOutcome::unchanged});
    }
    return out;
}

}  // namespace anno
