#include "core/analysis/onsets/onsets.h"

#include "core/analysis/onsets/detail/energy_step.h"
#include "core/analysis/onsets/detail/peak_picking.h"
#include "core/analysis/onsets/detail/pitch_change.h"
#include "core/analysis/onsets/detail/superflux.h"
#include "core/analysis/onsets/onset_stages.h"
#include "core/dsp/mono.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>

namespace anno {

namespace {

constexpr double k_min_rise_db = 6.0;  // a smaller energy rise is not refined
constexpr double k_falling_db =
    3.0;  // louder before than after by this much: an offset, not an onset
constexpr double k_energy_window_s = 0.002;  // short-window energy for the refinement
constexpr double k_voicing_probe_s = 0.020;

struct RawOnset {
    SourceTime time;
    double strength = 0.0;
    OnsetKind kind = OnsetKind::attack;
    bool spectral = true;
    bool energy_rise = false;  // spectral onsets: the short-window energy really rose (not legato)
};

Result<std::vector<RawOnset>> spectral_onsets(const MonoAudio& mono, const OnsetParams& params) {
    auto activation = superflux_activation(mono.samples, mono.sample_rate, params);
    if (!activation) {
        return Unexpected{activation.error()};
    }
    PeakList peaks{.hop = activation->hop,
                   .peaks =
                       pick_peaks(activation->values, {.hop = activation->hop,
                                                       .threshold_window = params.threshold_window,
                                                       .delta = params.delta,
                                                       .floor = params.min_activation,
                                                       .min_gap = params.min_gap})};
    auto localised = localise(mono.samples, mono.sample_rate, peaks,
                              {.search = params.refine_window,
                               .energy_window = Seconds{k_energy_window_s},
                               .min_rise = Decibels{k_min_rise_db},
                               .end_drop = Decibels{k_falling_db}});
    if (!localised) {
        return Unexpected{localised.error()};
    }
    std::vector<RawOnset> out;
    for (const LocalisedOnset& o : *localised) {
        out.push_back({.time = o.time,
                       .strength = o.strength,
                       .kind = OnsetKind::attack,
                       .spectral = true,
                       .energy_rise = o.outcome == LocaliseOutcome::rose});
    }
    return out;
}

/// Merges all onsets: within min_gap the stronger is kept. A spectral onset without an energy rise
/// (legato: the timbre or pitch changed under a steady level) that meets a pitch-change onset is
/// that pitch change: the pitch-change entry survives with the larger strength. A spectral onset
/// with an energy rise (a new attack) wins against a pitch change; on equal strength the spectral
/// one wins.
std::vector<RawOnset> merge(std::vector<RawOnset> all, Seconds min_gap) {
    std::ranges::stable_sort(all, {}, [](const RawOnset& o) { return o.time; });
    std::vector<RawOnset> kept;
    for (const RawOnset& o : all) {
        if (kept.empty() || (o.time - kept.back().time) >= min_gap) {
            kept.push_back(o);
            continue;
        }
        RawOnset& last = kept.back();
        const bool one_legato_spectral = (last.spectral && !last.energy_rise && !o.spectral) ||
                                         (o.spectral && !o.energy_rise && !last.spectral);
        if (one_legato_spectral) {
            RawOnset change = o.spectral ? last : o;
            change.strength = std::max(o.strength, last.strength);
            last = change;
        } else if (o.strength > last.strength ||
                   (o.strength == last.strength && o.spectral && !last.spectral)) {
            last = o;
        }
    }
    return kept;
}

/// Voicing probability of the pitch frame nearest to `t`.
double voicing_at(const PitchTrack& pitch, SourceTime t) {
    const double pos = (t - pitch.grid.start).value() / pitch.grid.hop.value();
    const auto i = std::clamp<std::int64_t>(std::llround(pos), 0, pitch.grid.n_frames - 1);
    return pitch.voicing_prob[static_cast<std::size_t>(i)];
}

OnsetList to_list(const std::vector<RawOnset>& onsets) {
    OnsetList list;
    for (const RawOnset& o : onsets) {
        list.times.push_back(o.time);
        list.strength.push_back(o.strength);
        list.kind.push_back(o.kind);
    }
    return list;
}

Result<std::vector<RawOnset>> read_and_detect(const AudioSource& src, const OnsetParams& params) {
    auto mono = read_mono(src);
    if (!mono) {
        return Unexpected{mono.error()};
    }
    auto onsets = spectral_onsets(*mono, params);
    if (!onsets) {
        return onsets;
    }
    for (const EnergyStep& s : find_energy_steps(
             mono->samples, mono->sample_rate,
             {.window = params.energy_step_window, .min_rise = params.energy_step_db})) {
        // Within min_gap merge() keeps the stronger of the two; further out, up to the window of
        // the step, the rise is already an onset and the step is dropped.
        const auto near_spectral = std::ranges::any_of(*onsets, [&](const RawOnset& o) {
            const double d = std::abs((o.time - s.time).value());
            return d > params.min_gap.value() && d <= params.energy_step_window.value();
        });
        if (near_spectral) {
            continue;
        }
        onsets->push_back({.time = s.time,
                           .strength = s.rise_db / params.energy_step_strength_db.value(),
                           .kind = OnsetKind::attack,
                           .spectral = false,
                           .energy_rise = true});
    }
    return onsets;
}

}  // namespace

Result<OnsetList> detect_onsets(const AudioSource& src, const OnsetParams& params) {
    auto onsets = read_and_detect(src, params);
    if (!onsets) {
        return Unexpected{onsets.error()};
    }
    return to_list(*onsets);
}

Result<OnsetList> detect_onsets(const AudioSource& src, const PitchTrack& pitch,
                                const OnsetParams& params) {
    if (auto r = validate(pitch); !r) {
        return fail(ErrorCode::invalid_argument,
                    "onsets: invalid pitch track: " + r.error().message);
    }
    auto onsets = read_and_detect(src, params);
    if (!onsets) {
        return Unexpected{onsets.error()};
    }
    std::vector<RawOnset> all = std::move(*onsets);
    if (pitch.grid.n_frames > 0) {
        for (const PitchChange& c :
             find_pitch_changes(pitch, params.pitch_change_threshold, params.pitch_stable)) {
            all.push_back({.time = c.time,
                           .strength = c.strength,
                           .kind = OnsetKind::pitch_change,
                           .spectral = false});
        }
    }
    std::vector<RawOnset> merged = merge(std::move(all), params.min_gap);
    if (pitch.grid.n_frames > 0) {
        for (RawOnset& o : merged) {
            if (o.spectral) {
                const bool unvoiced =
                    voicing_at(pitch, o.time + Seconds{k_voicing_probe_s}) < k_voiced_probability;
                o.kind = unvoiced ? OnsetKind::consonant : OnsetKind::attack;
            }
        }
    }
    return to_list(merged);
}

}  // namespace anno
