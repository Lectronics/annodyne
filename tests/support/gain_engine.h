#pragma once

#include "core/synthesis/engine/synthesis_engine.h"

#include <cmath>

// Test-only engine (ADR-0006): multiplies the zone by the note's gain edit. Lives under tests/ so
// no product code can select it; removed in Phase 4.

namespace anno::test {

class GainTestEngine final : public SynthesisEngine {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "gain_test"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 1; }
    [[nodiscard]] std::string settings_key() const override { return {}; }

    [[nodiscard]] Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                  const EffectiveNote& eff, const TimeMap& time_map,
                                                  OutputRange out_range) const override {
        if (!time_map.is_identity() || !only_gain_edited(eff, note)) {
            return fail(ErrorCode::not_implemented, "GainTestEngine realises gain edits only");
        }
        const Hz rate = src.sample_rate();
        const Samples first = to_sample_index(out_range.start(), rate, Rounding::nearest);
        const Samples last = to_sample_index(out_range.end(), rate, Rounding::nearest);
        auto audio = src.read(first, last - first);
        if (!audio) {
            return Unexpected{audio.error()};
        }
        const float gain = static_cast<float>(to_linear_gain(eff.gain).value());
        for (int c = 0; c < audio->channel_count(); ++c) {
            for (float& v : audio->channel(c)) {
                v *= gain;
            }
        }
        return OutputAudio{.audio = std::move(*audio),
                           .start = to_time_point<OutputAxis>(first, rate)};
    }

private:
    static bool only_gain_edited(const EffectiveNote& e, const Note& n) {
        return e.center == n.pitch_center && e.mod_scale == Ratio{1.0} &&
               e.drift_scale == Ratio{1.0} && e.transition_in_scale == Ratio{1.0} &&
               e.transition_out_scale == Ratio{1.0} && e.formant_offset == Cents{} &&
               e.within_note_flatten == Ratio{} && e.fade_in == Seconds{} &&
               e.fade_out == Seconds{} && e.noise_gain == NoiseGains{} &&
               e.onset_shift == Seconds{} && e.length_scale == Ratio{1.0} && e.handles.empty() &&
               e.attack_time_scale == Ratio{1.0};
    }
};

}  // namespace anno::test
