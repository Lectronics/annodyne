#include "cli/analysis_json/analysis_json.h"

#include <array>
#include <cmath>
#include <nlohmann/json.hpp>
#include <utility>

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

const char* to_text(PitchTargetKind k) {
    switch (k) {
    case PitchTargetKind::whole: return "whole";
    case PitchTargetKind::settled: return "settled";
    case PitchTargetKind::glide: return "glide";
    case PitchTargetKind::multi: return "multi";
    case PitchTargetKind::ornament: return "ornament";
    case PitchTargetKind::unpitched: return "unpitched";
    }
    return "unpitched";
}

const char* to_text(ApproachKind k) {
    switch (k) {
    case ApproachKind::none: return "none";
    case ApproachKind::legato: return "legato";
    case ApproachKind::scoop: return "scoop";
    case ApproachKind::overshoot: return "overshoot";
    }
    return "none";
}

const char* to_text(ReleaseKind k) {
    switch (k) {
    case ReleaseKind::none: return "none";
    case ReleaseKind::fall: return "fall";
    case ReleaseKind::rise: return "rise";
    case ReleaseKind::preparation: return "preparation";
    }
    return "none";
}

const char* to_text(FrameClass c) {
    switch (c) {
    case FrameClass::voiced: return "voiced";
    case FrameClass::sibilant: return "sibilant";
    case FrameClass::breath: return "breath";
    case FrameClass::plosive: return "plosive";
    case FrameClass::silence: return "silence";
    case FrameClass::breathy: return "breathy";
    case FrameClass::rough: return "rough";
    }
    return "voiced";
}

/// The note's frame classes as runs: {class, start_s, end_s}.
Json sound_classes_json(const Note& n) {
    Json runs = Json::array();
    const auto& classes = n.noise.frame_class;
    for (std::size_t i = 0; i < classes.size();) {
        std::size_t j = i;
        while (j < classes.size() && classes[j] == classes[i]) {
            ++j;
        }
        const auto frame_s = [&](std::size_t k) {
            return seconds(n.grid.start) + static_cast<double>(k) * n.grid.hop.value();
        };
        runs.push_back(
            {{"class", to_text(classes[i])}, {"start_s", frame_s(i)}, {"end_s", frame_s(j)}});
        i = j;
    }
    return runs;
}

Json pitch_target_json(const PitchTarget& t) {
    Json flags = Json::array();
    const std::array<std::pair<PitchFlag, const char*>, 7> names{
        {{PitchFlag::short_note, "short_note"},
         {PitchFlag::run, "run"},
         {PitchFlag::creak, "creak"},
         {PitchFlag::register_break, "register_break"},
         {PitchFlag::consistent_off_grid, "consistent_off_grid"},
         {PitchFlag::expressive_peak, "expressive_peak"},
         {PitchFlag::ambiguous_intent, "ambiguous_intent"}}};
    for (const auto& [flag, name] : names) {
        if (t.flags.has(flag)) {
            flags.push_back(name);
        }
    }
    Json out{{"kind", to_text(t.kind)},
             {"target_cents", t.target.value()},
             {"secondary_cents", t.secondary ? Json(t.secondary->value()) : Json(nullptr)},
             {"core_start_s", seconds(t.core.start())},
             {"core_end_s", seconds(t.core.end())},
             {"approach", to_text(t.approach)},
             {"release", to_text(t.release)},
             {"flags", flags},
             {"confidence", t.confidence_prob}};
    return out;
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
        notes.push_back({{"id", n.id.value},
                         {"start_s", seconds(n.source_start)},
                         {"attack_end_s", seconds(n.attack_end)},
                         {"release_start_s", seconds(n.release_start)},
                         {"end_s", seconds(n.source_end)},
                         {"pitch_target", pitch_target_json(n.pitch_target)},
                         {"pitch_center_cents", n.pitch_center.value()},
                         {"loudness_lufs", n.loudness.value()},
                         {"confidence", n.confidence_prob},
                         {"separation_in", n.separation_in == SeparationIn::soft ? "soft" : "hard"},
                         {"harmonic_count", n.harmonics.freq_hz.cols},
                         {"sound_classes", sound_classes_json(n)}});
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
