#pragma once

#include "core/render/render.h"
#include "core/types/result.h"

#include <filesystem>
#include <string_view>

// `anno-cli render <scenario.json> --out out.wav [--engine passthrough]`.
// Design: docs/design/phase2-render-skeleton.md.

namespace anno::cli {

/// The engines the CLI can select (the test gain engine is deliberately not among them).
/// Errors: invalid_argument for an unknown name. Pure.
enum class EngineChoice { passthrough };
[[nodiscard]] Result<EngineChoice> parse_engine(std::string_view name);

/// Everything a render or audition run needs, loaded from a scenario file.
struct LoadedScenario {
    AudioSource source;
    std::shared_ptr<const AnalysisResult> analysis;
    std::shared_ptr<const EditList> edits;
    std::string name;
    std::string algorithm;
    std::string edits_json;   // the scenario's "edits" array as written
    std::string expect_json;  // the scenario's "expect" object as written, or empty
};

/// Reads the scenario and its source WAV, builds the synthetic analysis and the edit list (one
/// group per scenario edit). Errors: those of load_scenario, io::read_wav,
/// make_scenario_analysis, EditList::add. Not real-time safe.
[[nodiscard]] Result<LoadedScenario> load_run(const std::filesystem::path& scenario_path,
                                              const std::filesystem::path& repo_root);

/// Renders the whole recording through the chosen engine and writes it as float32 WAV.
/// Errors: as load_run and render_region, plus io::write_wav. Not real-time safe.
[[nodiscard]] Result<void> run_render(const std::filesystem::path& scenario_path,
                                      const std::filesystem::path& out_wav, EngineChoice engine,
                                      const std::filesystem::path& repo_root);

}  // namespace anno::cli
