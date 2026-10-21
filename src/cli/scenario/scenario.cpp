#include "cli/scenario/scenario.h"

#include <algorithm>
#include <set>

namespace anno::cli {

TrackId scenario_track() {
    return TrackId{"main"};
}

Result<AnalysisResult> make_scenario_analysis(const Scenario& scenario, const AudioSource& source) {
    const double duration_s = to_seconds(source.frame_count(), source.sample_rate()).value();
    std::vector<ScenarioNote> sorted = scenario.notes;
    std::ranges::sort(sorted, {}, &ScenarioNote::start_s);
    AnalysisResult analysis;
    analysis.version = 1;
    analysis.source_id = source.id();
    std::set<std::string> ids;
    for (const ScenarioNote& n : sorted) {
        if (n.id.empty() || !ids.insert(n.id).second) {
            return fail(ErrorCode::invalid_argument,
                        "scenario note id is empty or duplicated: '" + n.id + "'");
        }
        if (!(n.start_s >= 0.0) || !(n.end_s >= n.start_s) || n.end_s > duration_s) {
            return fail(ErrorCode::invalid_argument,
                        "scenario note '" + n.id + "' must satisfy 0 <= start <= end <= duration");
        }
        const double len = n.end_s - n.start_s;
        Note note;
        note.id = NoteId{n.id};
        note.track_id = scenario_track();
        note.source_start = SourceTime{} + Seconds{n.start_s};
        note.attack_end = SourceTime{} + Seconds{n.start_s + 0.1 * len};
        note.release_start = SourceTime{} + Seconds{n.start_s + 0.9 * len};
        note.source_end = SourceTime{} + Seconds{n.end_s};
        note.pitch_target = make_whole_target(k_a4_cents);
        note.pitch_center = k_a4_cents;
        analysis.notes.push_back(std::move(note));
    }
    for (std::size_t i = 1; i < analysis.notes.size(); ++i) {
        if (analysis.notes[i].source_start < analysis.notes[i - 1].source_end) {
            return fail(ErrorCode::invalid_argument,
                        "scenario notes overlap (polyphony is Phase 11)");
        }
    }
    if (auto r = validate(analysis); !r) {
        return Unexpected{r.error()};
    }
    return analysis;
}

}  // namespace anno::cli
