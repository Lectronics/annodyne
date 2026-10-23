#pragma once

#include "core/render/render.h"
#include "core/synthesis/engine/synthesis_engine.h"
#include "core/types/result.h"

#include <filesystem>
#include <memory>
#include <string_view>

// `anno-cli render <scenario.json> --out out.wav [--engine passthrough]`.
// Design: docs/design/phase2-render-skeleton.md.

namespace anno::cli {

/// The engines the CLI can select (the test gain engine is deliberately not among them).
enum class EngineChoice { passthrough, harmonic_noise, psola };

/// Errors: invalid_argument for an unknown name. Pure.
[[nodiscard]] Result<EngineChoice> parse_engine(std::string_view name);
/// The engine's name as parse_engine reads it. Pure.
[[nodiscard]] std::string_view to_string(EngineChoice engine) noexcept;
/// A default-parameter engine of that kind; never null. Not real-time safe (allocates).
[[nodiscard]] std::unique_ptr<const SynthesisEngine> make_engine(EngineChoice engine);

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

/// Reads the scenario and its source WAV, builds the analysis (the synthetic one for the given
/// notes, or analyse() of the source for `"analysis": "analyse"`) and the edit list (one group per
/// scenario edit; a hand-given scenario with a gap target also gets the noise floor and segment
/// timeline built from its audio; pitch drags are resolved against the analysed notes with
/// pitch_drag_edit and added after the raw edits). Errors: those of load_scenario, io::read_wav,
/// make_scenario_analysis, analyse, pitch_drag_edit, target_pitch, EditList::add; invalid_argument
/// for a drag whose note_index is past the analysed notes. Not real-time safe.
[[nodiscard]] Result<LoadedScenario> load_run(const std::filesystem::path& scenario_path,
                                              const std::filesystem::path& repo_root);

/// Renders the whole recording through the chosen engine and writes it as float32 WAV.
/// Errors: as load_run and render_region (so not_implemented when the engine cannot realise an
/// edit, e.g. psola above its shift limit), plus io::write_wav. Not real-time safe.
[[nodiscard]] Result<void> run_render(const std::filesystem::path& scenario_path,
                                      const std::filesystem::path& out_wav, EngineChoice engine,
                                      const std::filesystem::path& repo_root);

}  // namespace anno::cli
