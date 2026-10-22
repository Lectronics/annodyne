#include "core/analysis/onsets/onsets.h"

#include "core/analysis/onsets/detail/energy_step.h"
#include "core/analysis/onsets/detail/merge_onsets.h"
#include "core/analysis/onsets/detail/pitch_change.h"
#include "core/analysis/onsets/detail/spectral_onsets.h"
#include "core/dsp/mono.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>

namespace anno {

namespace {

constexpr double k_voicing_probe_s = 0.020;

/// Voicing probability of the pitch frame nearest to `t`.
double voicing_at(const PitchTrack& pitch, SourceTime t) {
    const double pos = (t - pitch.grid.start).value() / pitch.grid.hop.value();
    const auto i = std::clamp<std::int64_t>(std::llround(pos), 0, pitch.grid.n_frames - 1);
    return pitch.voicing_prob[static_cast<std::size_t>(i)];
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
    return to_onset_list(*onsets);
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
    std::vector<RawOnset> merged = merge_onsets(std::move(all), params.min_gap);
    if (pitch.grid.n_frames > 0) {
        for (RawOnset& o : merged) {
            if (o.spectral) {
                const bool unvoiced =
                    voicing_at(pitch, o.time + Seconds{k_voicing_probe_s}) < k_voiced_probability;
                o.kind = unvoiced ? OnsetKind::consonant : OnsetKind::attack;
            }
        }
    }
    return to_onset_list(merged);
}

}  // namespace anno
