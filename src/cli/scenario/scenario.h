#pragma once

#include "core/types/analysis_result.h"
#include "core/types/audio_source.h"
#include "core/types/edit.h"
#include "core/types/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Scenario data (docs/AUDIO_VERIFICATION.md §2). JSON-free: the parser is cli/scenario_json.
// Design: docs/design/phase2-render-skeleton.md.

namespace anno::cli {

/// A note given by hand until Phase 3 analyses real audio.
struct ScenarioNote {
    std::string id;
    double start_s = 0.0;  // source time
    double end_s = 0.0;
};

/// How the scenario's notes come about.
enum class ScenarioAnalysis {
    given,  // `notes` lists them by hand (Phase 2); the analysis is synthetic and has no frame data
    analyse,  // analyse() runs on the source; target.note_index indexes the analysed notes (Phase
              // 4)
};

/// A Main Tool pitch drag written in a scenario ({"tool": "pitch_drag", ...}); resolved at load
/// time, after the analysis, by pitch_drag_edit.
struct ScenarioPitchDrag {
    std::size_t note_index = 0;          // into the analysed notes
    std::optional<double> offset_cents;  // target = the note's analysed target + this, or
    std::optional<double> target_cents;  // the absolute target (A4 = 6900); exactly one is set
    bool snap_chromatic = false;         // pass the target through target_pitch (440 Hz grid)
};

/// A macro written in a scenario ("macros": [...]); resolved after the analysis. correct_pitch
/// reads scale, root, center_amount, drift_amount, min_deviation_cents; leveling reads
/// between_notes, within_notes; formant (every active note) reads offset_cents.
struct ScenarioMacro {
    std::string name;  // "correct_pitch" or "leveling"
    std::string scale = "major";
    int root = 0;
    double center_amount = 1.0;
    double drift_amount = 0.0;
    double min_deviation_cents = 0.0;
    double between_notes = 0.5;
    double within_notes = 0.0;
    double offset_cents = 0.0;  // formant: the envelope shift for every active note
};

struct Scenario {
    std::string name;
    ScenarioAnalysis analysis = ScenarioAnalysis::given;
    std::filesystem::path source;  // WAV file, already resolved to a usable path
    std::string algorithm;         // echoed in reports; "melodic" if absent
    std::vector<ScenarioNote> notes;
    std::vector<EditSpec> edits;  // each one is its own EditList::add group, in order
    std::vector<ScenarioPitchDrag>
        drags;  // the edits of an analyse-mode scenario (never mixed with `edits`)
    std::vector<ScenarioMacro> macros;  // the "macros" array (analyse-mode scenarios only)
    // "rubberband": {"glide_ms": n, "formant_path": "offline"|"live", "scope":
    // "full_band"|"below_mvf"} (A/B of the pitch engine)
    double rubberband_glide_ms = -1.0;  // < 0: the engine default
    bool rubberband_formant_live = false;
    bool rubberband_below_mvf = false;    // "scope": "below_mvf" (ADR-0033)
    bool stretch_psola = false;           // "stretch_engine": "psola" (the default is Rubber Band)
    bool stretch_vibrato_retime = false;  // "stretch_vibrato": "retime" (A/B against the default)
    std::string edits_json;               // the "edits" array as written, for the audition package
    std::string expect_json;              // the "expect" object as written ("" if absent)
};

/// The track all scenarios use.
[[nodiscard]] TrackId scenario_track();

/// Flat synthetic analysis for `scenario.notes`: valid notes with no frame data, attack ending 10%
/// and release starting 90% into each note, pitch A4, version 1, `source`'s id.
/// Errors: invalid_argument if a note has an empty or duplicate id, end < start, or lies outside
/// the recording, or the notes overlap. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<AnalysisResult> make_scenario_analysis(const Scenario& scenario,
                                                            const AudioSource& source);

}  // namespace anno::cli
