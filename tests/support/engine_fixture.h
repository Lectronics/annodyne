#pragma once

#include "core/analysis/orchestrator/analyse.h"
#include "core/analysis/partials/partials.h"
#include "core/synthesis/engine/synthesis_engine.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// A synthetic voice, its Phase 4 analysis and a render/re-measure harness shared by the pitch
// engine tests (harmonic_noise, psola).

namespace anno::test::engine {

inline constexpr double k_rate = 44100.0;
inline constexpr double k_total_s = 1.6;

/// 0.2 .. 1.4 s of a harmonic voice with amplitudes `amps` and pitch f0_hz(t); silence elsewhere.
template <class F0Hz>
inline std::vector<float> voice(const F0Hz& f0_hz, const std::vector<double>& amps) {
    auto x = make_harmonic(f0_hz, k_total_s, k_rate, amps);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double t = static_cast<double>(i) / k_rate;
        const double fade = std::clamp(std::min(t - 0.2, 1.4 - t) / 0.02, 0.0, 1.0);
        x[i] *= static_cast<float>(fade);
    }
    return x;
}

struct Fixture {
    std::vector<float> mono;
    AudioSource src;
    std::shared_ptr<const AnalysisResult> analysis;
    Note note;
    TimeMap map;

    explicit Fixture(std::vector<float> samples)
        : mono(std::move(samples)), src(AudioSource::from_buffer(to_buffer(mono, k_rate))),
          analysis(analyse(src, AnalyseParams{}).value()),
          map(TimeMap::identity(Seconds{k_total_s})) {
        REQUIRE_FALSE(analysis->notes.empty());
        note = *std::max_element(
            analysis->notes.begin(), analysis->notes.end(),
            [](const Note& a, const Note& b) { return a.grid.n_frames < b.grid.n_frames; });
    }

    [[nodiscard]] EffectiveNote effective(double shift_cents) const {
        EffectiveNote e;
        e.note_id = note.id;
        e.center = note.pitch_center + Cents{shift_cents};
        e.is_edited = shift_cents != 0.0;
        return e;
    }

    /// The whole recording rendered with the note shifted (identity map: output axis = source
    /// axis).
    [[nodiscard]] Result<OutputAudio> render(const SynthesisEngine& engine,
                                             const EffectiveNote& eff) const {
        return engine.render_note(src, note, eff, map,
                                  OutputRange::from_start(OutputTime{}, Seconds{k_total_s}));
    }
};

/// f1..fK of the rendered note re-measured with the shifted pitch curves.
inline HarmonicTrack remeasure(const Fixture& fx, const OutputAudio& out, double shift_cents) {
    std::vector<float> mono(out.audio.channel(0).begin(), out.audio.channel(0).end());
    const NoteCurves curves{.grid = fx.note.grid,
                            .pitch_center = fx.note.pitch_center + Cents{shift_cents},
                            .pitch_drift = fx.note.pitch_drift,
                            .pitch_mod = fx.note.pitch_mod};
    return analyse_partials(mono, Hz{k_rate}, curves, {}).value();
}

inline double f1_cents(const HarmonicTrack& h, std::size_t frame) {
    const auto cols = static_cast<std::size_t>(h.freq_hz.cols);
    return to_cents(Hz{static_cast<double>(h.freq_hz.data[frame * cols])}, k_a4_standard_hz)
        .value();
}

}  // namespace anno::test::engine
