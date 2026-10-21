#include "cli/commands/render.h"

#include "cli/scenario_json/scenario_json.h"
#include "core/analysis/orchestrator/analyse.h"
#include "core/io/wav.h"
#include "core/synthesis/harmonic_noise/harmonic_noise_engine.h"
#include "core/synthesis/passthrough/passthrough_engine.h"
#include "core/synthesis/psola/psola_engine.h"
#include "core/tuning/chromatic_grid.h"
#include "tools/main_tool/main_tool.h"

#include <memory>

namespace anno::cli {

Result<EngineChoice> parse_engine(std::string_view name) {
    for (const EngineChoice choice :
         {EngineChoice::passthrough, EngineChoice::harmonic_noise, EngineChoice::psola}) {
        if (name == to_string(choice)) {
            return choice;
        }
    }
    return fail(ErrorCode::invalid_argument,
                "unknown engine '" + std::string{name} +
                    "' (available: passthrough, harmonic_noise, psola)");
}

std::string_view to_string(EngineChoice engine) noexcept {
    switch (engine) {
    case EngineChoice::passthrough: return "passthrough";
    case EngineChoice::harmonic_noise: return "harmonic_noise";
    case EngineChoice::psola: return "psola";
    }
    return "passthrough";
}

std::unique_ptr<const SynthesisEngine> make_engine(EngineChoice engine) {
    switch (engine) {
    case EngineChoice::passthrough: return std::make_unique<PassthroughEngine>();
    case EngineChoice::harmonic_noise: return std::make_unique<HarmonicNoiseEngine>();
    case EngineChoice::psola: return std::make_unique<PsolaEngine>();
    }
    return std::make_unique<PassthroughEngine>();
}

namespace {

/// The edit of one scenario pitch drag against the analysed notes.
Result<EditSpec> resolve_drag(const AnalysisResult& analysis, const ScenarioPitchDrag& drag) {
    if (drag.note_index >= analysis.notes.size()) {
        return fail(ErrorCode::invalid_argument, "pitch_drag: note_index " +
                                                     std::to_string(drag.note_index) +
                                                     " is past the analysed notes (" +
                                                     std::to_string(analysis.notes.size()) + ")");
    }
    if (drag.offset_cents.has_value() == drag.target_cents.has_value()) {
        return fail(ErrorCode::invalid_argument,
                    "pitch_drag: exactly one of offset_cents, target_cents is needed");
    }
    const Note& note = analysis.notes[drag.note_index];
    // The relative form is measured from the same baseline pitch_drag_edit subtracts.
    Cents target = drag.target_cents ? Cents{*drag.target_cents}
                                     : note.pitch_target.target + Cents{*drag.offset_cents};
    if (drag.snap_chromatic) {
        auto snapped = target_pitch(target, ChromaticGrid{});
        if (!snapped) {
            return Unexpected{snapped.error()};
        }
        target = *snapped;
    }
    return pitch_drag_edit(analysis, {.note = note.id, .target = target});
}

}  // namespace

Result<LoadedScenario> load_run(const std::filesystem::path& scenario_path,
                                const std::filesystem::path& repo_root) {
    auto scenario = load_scenario(scenario_path, repo_root);
    if (!scenario) {
        return Unexpected{scenario.error()};
    }
    auto audio = io::read_wav(scenario->source);
    if (!audio) {
        return Unexpected{audio.error()};
    }
    AudioSource source = AudioSource::from_buffer(std::move(*audio));
    std::shared_ptr<const AnalysisResult> analysis;
    if (scenario->analysis == ScenarioAnalysis::analyse) {
        AnalyseParams params;
        params.track = scenario_track();
        auto analysed = anno::analyse(source, params);
        if (!analysed) {
            return Unexpected{analysed.error()};
        }
        analysis = *analysed;
    } else {
        auto synthetic = make_scenario_analysis(*scenario, source);
        if (!synthetic) {
            return Unexpected{synthetic.error()};
        }
        analysis = std::make_shared<AnalysisResult>(std::move(*synthetic));
    }
    std::vector<EditSpec> specs = scenario->edits;
    for (const ScenarioPitchDrag& drag : scenario->drags) {
        auto spec = resolve_drag(*analysis, drag);
        if (!spec) {
            return Unexpected{spec.error()};
        }
        specs.push_back(std::move(*spec));
    }
    EditList edits{scenario_track()};
    for (const EditSpec& spec : specs) {
        auto next = edits.add(std::span{&spec, 1}, WallTime{});
        if (!next) {
            return Unexpected{next.error()};
        }
        edits = std::move(*next);
    }
    return LoadedScenario{.source = source,
                          .analysis = std::move(analysis),
                          .edits = std::make_shared<EditList>(std::move(edits)),
                          .name = scenario->name,
                          .algorithm = scenario->algorithm,
                          .edits_json = scenario->edits_json,
                          .expect_json = scenario->expect_json};
}

Result<void> run_render(const std::filesystem::path& scenario_path,
                        const std::filesystem::path& out_wav, EngineChoice engine,
                        const std::filesystem::path& repo_root) {
    auto run = load_run(scenario_path, repo_root);
    if (!run) {
        return Unexpected{run.error()};
    }
    const std::unique_ptr<const SynthesisEngine> chosen_engine = make_engine(engine);
    const SynthesisEngine& chosen = *chosen_engine;
    const OutputRange whole = OutputRange::from_start(
        OutputTime{}, to_seconds(run->source.frame_count(), run->source.sample_rate()));
    auto rendered = render_region(RenderInputs{.source = run->source,
                                               .analysis = run->analysis,
                                               .edits = run->edits,
                                               .time_map = nullptr},
                                  whole, chosen, RenderParams{});
    if (!rendered) {
        return Unexpected{rendered.error()};
    }
    return io::write_wav(out_wav, (*rendered)->buffer.audio);
}

}  // namespace anno::cli
