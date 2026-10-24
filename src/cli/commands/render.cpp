#include "cli/commands/render.h"

#include "cli/scenario_json/scenario_json.h"
#include "core/analysis/activity/activity.h"
#include "core/analysis/gaps/analyse_gaps.h"
#include "core/analysis/orchestrator/analyse.h"
#include "core/dsp/mono.h"
#include "core/io/wav.h"
#include "core/synthesis/harmonic_noise/harmonic_noise_engine.h"
#include "core/synthesis/passthrough/passthrough_engine.h"
#include "core/synthesis/psola/psola_engine.h"
#include "core/synthesis/rubberband_pitch/rubberband_pitch_engine.h"
#include "core/tuning/chromatic_grid.h"
#include "core/tuning/scale_grid.h"
#include "macros/correct_pitch/correct_pitch.h"
#include "macros/leveling/leveling.h"
#include "tools/formant/formant.h"
#include "tools/main_tool/main_tool.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace anno::cli {

namespace {

/// True if any edit targets a gap or moves time: both need the segment timeline (the gate, and
/// the layout of timing edits, ADR-0024 and ADR-0025).
bool needs_timeline(const std::vector<EditSpec>& edits) {
    return std::ranges::any_of(edits, [](const EditSpec& e) {
        return e.category == EditCategory::time || e.category == EditCategory::attack ||
               std::ranges::any_of(e.target.segment_ids,
                                   [](const SegmentId& id) { return id.kind == SegmentKind::gap; });
    });
}

constexpr double k_scenario_hop_s = 0.005;  // the activity grid of a scenario's gap analysis

/// The noise floor and the segment timeline of a hand-given analysis, built from the audio the way
/// analyse() builds them, so a scenario can address the gaps between its notes (ADR-0024).
Result<void> add_scenario_gaps(const AudioSource& source, AnalysisResult& analysis) {
    auto mono = read_mono(source);
    if (!mono) {
        return Unexpected{mono.error()};
    }
    FrameGrid grid;
    grid.hop = Seconds{k_scenario_hop_s};
    grid.start = SourceTime{};
    grid.n_frames =
        static_cast<std::int64_t>(std::floor(
            to_seconds(source.frame_count(), source.sample_rate()).value() / k_scenario_hop_s)) +
        1;
    auto activity = detect_activity(*mono, grid, ActivityParams{});
    if (!activity) {
        return Unexpected{activity.error()};
    }
    auto gaps = analyse_gaps(source, *mono, analysis.notes, *activity, GapAnalysisParams{});
    if (!gaps) {
        return Unexpected{gaps.error()};
    }
    analysis.noise_floor = std::move(gaps->noise_floor);
    analysis.timeline = std::move(gaps->timeline);
    return {};
}

}  // namespace

Result<EngineChoice> parse_engine(std::string_view name) {
    for (const EngineChoice choice : {EngineChoice::passthrough, EngineChoice::harmonic_noise,
                                      EngineChoice::psola, EngineChoice::rubberband}) {
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
    case EngineChoice::rubberband: return "rubberband";
    }
    return "passthrough";
}

std::unique_ptr<const SynthesisEngine> make_engine(EngineChoice engine,
                                                   const RubberBandPitchParams& rubberband) {
    switch (engine) {
    case EngineChoice::passthrough: return std::make_unique<PassthroughEngine>();
    case EngineChoice::harmonic_noise: return std::make_unique<HarmonicNoiseEngine>();
    case EngineChoice::psola: return std::make_unique<PsolaEngine>();
    case EngineChoice::rubberband: return std::make_unique<RubberBandPitchEngine>(rubberband);
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

/// The scale type named in a scenario.
Result<ScaleType> parse_scale(const std::string& name) {
    for (const auto& [text, type] :
         {std::pair{"chromatic", ScaleType::chromatic}, std::pair{"major", ScaleType::major},
          std::pair{"natural_minor", ScaleType::natural_minor},
          std::pair{"harmonic_minor", ScaleType::harmonic_minor},
          std::pair{"major_pentatonic", ScaleType::major_pentatonic},
          std::pair{"minor_pentatonic", ScaleType::minor_pentatonic},
          std::pair{"dorian", ScaleType::dorian}, std::pair{"mixolydian", ScaleType::mixolydian}}) {
        if (name == text) {
            return type;
        }
    }
    return fail(ErrorCode::invalid_argument, "macro: unknown scale '" + name + "'");
}

/// The edits of one scenario macro against the analysed notes.
Result<std::vector<EditSpec>> resolve_macro(const AnalysisResult& analysis,
                                            const ScenarioMacro& macro) {
    if (macro.name == "formant") {
        std::vector<EditSpec> specs;
        for (const Note& note : analysis.notes) {
            if (!note.is_active) {
                continue;
            }
            auto spec = formant_edit(analysis, note.id, Cents{macro.offset_cents});
            if (!spec) {
                return Unexpected{spec.error()};
            }
            specs.push_back(std::move(*spec));
        }
        return specs;
    }
    if (macro.name == "leveling") {
        LevelingParams params;
        params.between_notes = Ratio{macro.between_notes};
        params.within_notes = Ratio{macro.within_notes};
        return leveling_edits(analysis, params);
    }
    auto type = parse_scale(macro.scale);
    if (!type) {
        return Unexpected{type.error()};
    }
    auto grid = make_scale_grid(*type, macro.root);
    if (!grid) {
        return Unexpected{grid.error()};
    }
    CorrectPitchParams params;
    params.grid = std::move(*grid);
    params.center_amount = Ratio{macro.center_amount};
    params.drift_amount = Ratio{macro.drift_amount};
    params.min_deviation = Cents{macro.min_deviation_cents};
    return correct_pitch_edits(analysis, params);
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
        if (needs_timeline(scenario->edits)) {
            if (auto r = add_scenario_gaps(source, *synthetic); !r) {
                return Unexpected{r.error()};
            }
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
    for (const ScenarioMacro& macro : scenario->macros) {
        auto produced = resolve_macro(*analysis, macro);
        if (!produced) {
            return Unexpected{produced.error()};
        }
        specs.insert(specs.end(), produced->begin(), produced->end());
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
                          .stretch_vibrato_retime = scenario->stretch_vibrato_retime,
                          .rubberband_glide_ms = scenario->rubberband_glide_ms,
                          .rubberband_formant_live = scenario->rubberband_formant_live,
                          .stretch_psola = scenario->stretch_psola,
                          .edits_json = scenario->edits_json,
                          .expect_json = scenario->expect_json};
}

RenderParams render_params_of(const LoadedScenario& scenario) {
    RenderParams params;
    if (scenario.stretch_psola) {
        params.stretch.engine = StretchEngine::psola;
    }
    if (scenario.stretch_vibrato_retime) {
        params.stretch.vibrato = VibratoMode::retime;
    }
    return params;
}

RubberBandPitchParams rubberband_params_of(const LoadedScenario& scenario) {
    RubberBandPitchParams params;
    if (scenario.rubberband_glide_ms >= 0.0) {
        params.glide_time = Seconds{scenario.rubberband_glide_ms / 1000.0};
    }
    if (scenario.rubberband_formant_live) {
        params.formant_path = FormantPath::live;
    }
    return params;
}

Result<void> run_render(const std::filesystem::path& scenario_path,
                        const std::filesystem::path& out_wav, EngineChoice engine,
                        const std::filesystem::path& repo_root) {
    auto run = load_run(scenario_path, repo_root);
    if (!run) {
        return Unexpected{run.error()};
    }
    const std::unique_ptr<const SynthesisEngine> chosen_engine =
        make_engine(engine, rubberband_params_of(*run));
    const SynthesisEngine& chosen = *chosen_engine;
    const OutputRange whole = OutputRange::from_start(
        OutputTime{}, to_seconds(run->source.frame_count(), run->source.sample_rate()));
    auto rendered = render_region(RenderInputs{.source = run->source,
                                               .analysis = run->analysis,
                                               .edits = run->edits,
                                               .time_map = nullptr},
                                  whole, chosen, render_params_of(*run));
    if (!rendered) {
        return Unexpected{rendered.error()};
    }
    return io::write_wav(out_wav, (*rendered)->buffer.audio);
}

}  // namespace anno::cli
