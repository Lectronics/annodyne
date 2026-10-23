#include "core/analysis/activity/activity.h"

#include "core/dsp/filter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace anno {

namespace {

constexpr double k_tiny_power = 1e-12;    // keeps log10 finite; equals k_level_floor_db.value()
constexpr double k_run_epsilon_s = 1e-9;  // run-length comparisons in seconds

bool positive(double v) {
    return std::isfinite(v) && v > 0.0;
}

Result<void> validate_params(const ActivityParams& p, const FrameGrid& grid, Hz rate) {
    const bool bad = !positive(p.window.value()) || !positive(p.highpass.value()) ||
                     !positive(p.min_active.value()) || !positive(p.max_dip.value()) ||
                     !positive(p.floor_band.value()) || !std::isfinite(p.on_margin.value()) ||
                     !std::isfinite(p.off_margin.value()) || p.off_margin > p.on_margin ||
                     !std::isfinite(p.min_range.value()) || p.min_range.value() < 0.0 ||
                     !std::isfinite(p.absolute_floor.value()) || !(p.floor_mass_prob > 0.0) ||
                     !(p.floor_mass_prob < 0.5) || !(p.range_percentile_prob > 0.5) ||
                     !(p.range_percentile_prob < 1.0);
    if (bad) {
        return fail(ErrorCode::invalid_argument, "activity: invalid parameters");
    }
    if (!positive(rate.value()) || p.highpass.value() >= rate.value() / 2.0) {
        return fail(ErrorCode::invalid_argument,
                    "activity: the sample rate must be finite and above twice the high-pass");
    }
    if (auto r = validate(grid); !r) {
        return r;
    }
    return {};
}

/// dBFS of the RMS of `power_sum` over `count` samples.
double level_db(double power_sum, std::int64_t count) {
    if (count <= 0) {
        return k_level_floor_db.value();
    }
    const double ms = power_sum / static_cast<double>(count);
    return std::max(k_level_floor_db.value(), 10.0 * std::log10(ms + k_tiny_power));
}

/// Level of every grid frame: RMS of the high-passed signal in a window centred on the frame.
Result<std::vector<Decibels>> frame_levels(const MonoAudio& mono, const FrameGrid& grid,
                                           const ActivityParams& params) {
    const auto n_frames = static_cast<std::size_t>(grid.n_frames);
    std::vector<Decibels> out(n_frames, Decibels{k_level_floor_db.value()});
    if (mono.samples.empty() || n_frames == 0) {
        return out;
    }
    const std::vector<double> raw(mono.samples.begin(), mono.samples.end());
    auto filtered = zero_phase_highpass(raw, mono.sample_rate, params.highpass);
    if (!filtered) {
        return Unexpected{filtered.error()};
    }
    // Prefix sums of the squares: any window's power in O(1).
    std::vector<double> cumulative(filtered->size() + 1, 0.0);
    for (std::size_t i = 0; i < filtered->size(); ++i) {
        cumulative[i + 1] = cumulative[i] + (*filtered)[i] * (*filtered)[i];
    }
    const double rate = mono.sample_rate.value();
    const auto size = static_cast<std::int64_t>(filtered->size());
    const double half = params.window.value() * rate / 2.0;
    for (std::size_t i = 0; i < n_frames; ++i) {
        const double centre =
            (frame_time(grid, static_cast<std::int64_t>(i)) - SourceTime{}).value() * rate;
        const auto lo = std::clamp<std::int64_t>(std::llround(centre - half), 0, size);
        const auto hi = std::clamp<std::int64_t>(std::llround(centre + half), 0, size);
        out[i] = Decibels{level_db(cumulative[static_cast<std::size_t>(hi)] -
                                       cumulative[static_cast<std::size_t>(lo)],
                                   hi - lo)};
    }
    return out;
}

struct FloorEstimate {
    double floor_db = 0.0;
    double range_top_db = 0.0;
    bool found = false;  // false: every frame is digital silence
};

/// The lowest `floor_band` of levels holding at least `floor_mass_prob` of the frames (digital
/// silence excluded); its median is the floor. Also the `range_percentile_prob` quantile.
FloorEstimate estimate_floor(const std::vector<Decibels>& levels, const ActivityParams& p) {
    std::vector<double> v;
    for (const Decibels l : levels) {
        if (l.value() >= p.absolute_floor.value()) {
            v.push_back(l.value());
        }
    }
    if (v.empty()) {
        return {};
    }
    std::ranges::sort(v);
    const auto need = std::max<std::ptrdiff_t>(
        1,
        static_cast<std::ptrdiff_t>(std::ceil(p.floor_mass_prob * static_cast<double>(v.size()))));
    FloorEstimate est;
    est.found = true;
    est.floor_db = v.back();
    for (std::size_t i = 0; i < v.size(); ++i) {
        const auto end = std::ranges::upper_bound(v, v[i] + p.floor_band.value());
        const auto count = end - (v.begin() + static_cast<std::ptrdiff_t>(i));
        if (count >= need) {
            est.floor_db = v[i + static_cast<std::size_t>(count / 2)];
            break;
        }
    }
    est.range_top_db =
        v[static_cast<std::size_t>(p.range_percentile_prob * static_cast<double>(v.size() - 1))];
    return est;
}

/// Schmitt trigger: on at `on_db`, off below `off_db`. Digital-silence frames are never active.
std::vector<FrameActivity> hysteresis(const std::vector<Decibels>& levels, double on_db,
                                      double off_db, double absolute_floor_db) {
    std::vector<FrameActivity> out(levels.size(), FrameActivity::silent);
    bool on = false;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const double l = levels[i].value();
        if (l < absolute_floor_db) {
            on = false;
        } else if (on) {
            on = l >= off_db;
        } else {
            on = l >= on_db;
        }
        out[i] = on ? FrameActivity::active : FrameActivity::silent;
    }
    return out;
}

/// Sets every maximal run of `from` frames that is shorter than `limit_s` (and, for `bounded`,
/// has `other` frames on both sides) to `to`.
void rewrite_short_runs(std::vector<FrameActivity>& a, FrameActivity from, FrameActivity to,
                        double limit_s, double hop_s, bool bounded) {
    std::size_t i = 0;
    while (i < a.size()) {
        if (a[i] != from) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < a.size() && a[j] == from) {
            ++j;
        }
        const bool short_run = static_cast<double>(j - i) * hop_s < limit_s + k_run_epsilon_s;
        const bool enclosed = i > 0 && j < a.size();
        if (short_run && (enclosed || !bounded)) {
            std::fill(a.begin() + static_cast<std::ptrdiff_t>(i),
                      a.begin() + static_cast<std::ptrdiff_t>(j), to);
        }
        i = j;
    }
}

}  // namespace

Result<ActivityTrack> detect_activity(const MonoAudio& mono, const FrameGrid& grid,
                                      const ActivityParams& params) {
    if (auto r = validate_params(params, grid, mono.sample_rate); !r) {
        return Unexpected{r.error()};
    }
    auto levels = frame_levels(mono, grid, params);
    if (!levels) {
        return Unexpected{levels.error()};
    }
    ActivityTrack out;
    out.grid = grid;
    out.level = std::move(*levels);
    const FloorEstimate est = estimate_floor(out.level, params);
    if (!est.found) {
        out.noise_floor = params.absolute_floor;
        out.activity.assign(out.level.size(), FrameActivity::silent);
        return out;
    }
    out.noise_floor = Decibels{est.floor_db};
    const double hop = grid.hop.value();
    if (est.range_top_db - est.floor_db < params.min_range.value()) {
        // No clear silence in the file: everything above digital silence is active.
        out.activity =
            hysteresis(out.level, -std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(), params.absolute_floor.value());
        return out;
    }
    out.activity =
        hysteresis(out.level, est.floor_db + params.on_margin.value(),
                   est.floor_db + params.off_margin.value(), params.absolute_floor.value());
    rewrite_short_runs(out.activity, FrameActivity::active, FrameActivity::silent,
                       params.min_active.value(), hop, false);
    rewrite_short_runs(out.activity, FrameActivity::silent, FrameActivity::active,
                       params.max_dip.value(), hop, true);
    return out;
}

Result<PitchTrack> gate_pitch(const PitchTrack& track, const ActivityTrack& activity) {
    const auto n = static_cast<std::size_t>(track.grid.n_frames);
    if (track.grid != activity.grid || activity.activity.size() != n || !validate(track)) {
        return fail(ErrorCode::invalid_argument,
                    "gate_pitch: the activity track and the pitch track must share one valid grid");
    }
    PitchTrack out = track;
    const auto nan = Cents{std::numeric_limits<double>::quiet_NaN()};
    for (std::size_t i = 0; i < n; ++i) {
        if (is_active(activity, i)) {
            continue;
        }
        out.f0[i] = nan;
        out.voicing_prob[i] = 0.0;
        out.confidence_prob[i] = 1.0;
        for (auto& alt : out.alternates) {
            alt[i] = nan;
        }
    }
    return out;
}

}  // namespace anno
