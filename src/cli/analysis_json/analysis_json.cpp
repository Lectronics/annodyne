#include "cli/analysis_json/analysis_json.h"

#include <cmath>
#include <nlohmann/json.hpp>

namespace anno::cli {

namespace {

using Json = nlohmann::json;

double seconds(SourceTime t) {
    return (t - SourceTime{}).value();
}

/// NaN becomes null.
Json number_or_null(double v) {
    return std::isnan(v) ? Json(nullptr) : Json(v);
}

const char* to_text(OnsetKind k) {
    switch (k) {
    case OnsetKind::attack: return "attack";
    case OnsetKind::consonant: return "consonant";
    case OnsetKind::pitch_change: return "pitch_change";
    case OnsetKind::phoneme: return "phoneme";
    }
    return "attack";
}

const char* to_text(Algorithm a) {
    switch (a) {
    case Algorithm::melodic: return "melodic";
    case Algorithm::percussive: return "percussive";
    case Algorithm::percussive_pitched: return "percussive_pitched";
    case Algorithm::poly_sustain: return "poly_sustain";
    case Algorithm::poly_decay: return "poly_decay";
    case Algorithm::universal: return "universal";
    }
    return "melodic";
}

Json notes_json(const AnalysisResult& a) {
    Json notes = Json::array();
    for (const Note& n : a.notes) {
        notes.push_back(
            {{"id", n.id.value},
             {"start_s", seconds(n.source_start)},
             {"attack_end_s", seconds(n.attack_end)},
             {"release_start_s", seconds(n.release_start)},
             {"end_s", seconds(n.source_end)},
             {"perceived_pitch_cents", n.perceived_pitch.value()},
             {"pitch_center_cents", n.pitch_center.value()},
             {"loudness_lufs", n.loudness.value()},
             {"confidence", n.confidence_prob},
             {"separation_in", n.separation_in == SeparationIn::soft ? "soft" : "hard"}});
    }
    return notes;
}

Json onsets_json(const OnsetList& o) {
    Json onsets = Json::array();
    for (std::size_t i = 0; i < o.times.size(); ++i) {
        onsets.push_back({{"time_s", seconds(o.times[i])},
                          {"strength", o.strength[i]},
                          {"kind", to_text(o.kind[i])}});
    }
    return onsets;
}

Json pitch_json(const PitchTrack& p) {
    Json f0 = Json::array();
    for (const Cents c : p.f0) {
        f0.push_back(number_or_null(c.value()));
    }
    return {{"hop_s", p.grid.hop.value()}, {"start_s", seconds(p.grid.start)},
            {"n_frames", p.grid.n_frames}, {"f0_cents", f0},
            {"voicing", p.voicing_prob},   {"confidence", p.confidence_prob}};
}

}  // namespace

std::string analysis_to_json(const AnalysisResult& analysis, const AnalysisJsonOptions& options) {
    Json root;
    root["source"] = {{"id", analysis.source_id.value}};
    if (options.source_duration_s > 0.0) {
        root["source"]["duration_s"] = options.source_duration_s;
    }
    root["version"] = analysis.version;
    root["algorithm"] = to_text(analysis.algorithm);
    root["notes"] = notes_json(analysis);
    root["onsets"] = onsets_json(analysis.onsets);
    if (options.include_pitch_track && analysis.pitch) {
        root["pitch"] = pitch_json(*analysis.pitch);
    }
    return root.dump(2) + "\n";
}

}  // namespace anno::cli
