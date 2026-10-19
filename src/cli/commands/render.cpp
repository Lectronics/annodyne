#include "cli/commands/render.h"

#include "cli/scenario_json/scenario_json.h"
#include "core/io/wav.h"
#include "core/synthesis/passthrough/passthrough_engine.h"

namespace anno::cli {

Result<EngineChoice> parse_engine(std::string_view name) {
    if (name == "passthrough") {
        return EngineChoice::passthrough;
    }
    return fail(ErrorCode::invalid_argument,
                "unknown engine '" + std::string{name} + "' (available: passthrough)");
}

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
    auto analysis = make_scenario_analysis(*scenario, source);
    if (!analysis) {
        return Unexpected{analysis.error()};
    }
    EditList edits{scenario_track()};
    for (const EditSpec& spec : scenario->edits) {
        auto next = edits.add(std::span{&spec, 1}, WallTime{});
        if (!next) {
            return Unexpected{next.error()};
        }
        edits = std::move(*next);
    }
    return LoadedScenario{.source = source,
                          .analysis = std::make_shared<AnalysisResult>(std::move(*analysis)),
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
    const PassthroughEngine passthrough;
    (void)engine;  // passthrough is the only engine (EngineChoice)
    const SynthesisEngine& chosen = passthrough;
    const OutputRange whole = OutputRange::from_start(
        OutputTime{}, to_seconds(run->source.frame_count(), run->source.sample_rate()));
    auto rendered = render_region(
        RenderInputs{.source = run->source, .analysis = run->analysis, .edits = run->edits}, whole,
        chosen, RenderParams{});
    if (!rendered) {
        return Unexpected{rendered.error()};
    }
    return io::write_wav(out_wav, (*rendered)->buffer.audio);
}

}  // namespace anno::cli
