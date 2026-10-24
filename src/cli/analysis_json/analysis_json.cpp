#include "cli/analysis_json/analysis_json.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

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
        Json run = {{"class", to_text(classes[i])}, {"start_s", frame_s(i)}, {"end_s", frame_s(j)}};
        if (n.noise.max_voiced_hz.size() == classes.size()) {
            std::vector<double> values;
            for (std::size_t k = i; k < j; ++k) {
                values.push_back(n.noise.max_voiced_hz[k].value());
            }
            const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
            std::ranges::nth_element(values, mid);
            run["mvf_hz"] = *mid;
        }
        runs.push_back(std::move(run));
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

const char* to_text(GapRegionKind kind) {
    switch (kind) {
    case GapRegionKind::silence: return "silence";
    case GapRegionKind::tail: return "tail";
    case GapRegionKind::protected_preroll: return "protected_preroll";
    case GapRegionKind::audible: return "audible";
    case GapRegionKind::sibilant: return "sibilant";
    case GapRegionKind::breath: return "breath";
    case GapRegionKind::plosive: return "plosive";
    }
    return "silence";
}

Json note_id_or_null(const std::optional<NoteId>& id) {
    return id ? Json(id->value) : Json(nullptr);
}

/// The gaps of the segment timeline: id, neighbours, range, floor and regions.
Json gaps_json(const SegmentTimeline& timeline) {
    Json gaps = Json::array();
    for (const Gap& g : timeline.gaps()) {
        Json regions = Json::array();
        for (const GapRegion& r : g.regions) {
            regions.push_back({{"kind", to_text(r.kind)},
                               {"start_s", seconds(r.start)},
                               {"end_s", seconds(r.end)}});
        }
        gaps.push_back({{"id", g.id.value},
                        {"before", note_id_or_null(g.before)},
                        {"after", note_id_or_null(g.after)},
                        {"start_s", seconds(g.start)},
                        {"end_s", seconds(g.end)},
                        {"floor_dbfs", g.floor_dbfs.value()},
                        {"regions", regions}});
    }
    return gaps;
}

/// The noise floor: its grid and band spectrum; the per-frame curve only on request (large).
Json noise_floor_json(const NoiseFloor& floor, bool include_curve) {
    Json centres = Json::array();
    for (const Hz c : floor.band_centre) {
        centres.push_back(c.value());
    }
    Json bands = Json::array();
    for (const Decibels b : floor.band_dbfs) {
        bands.push_back(b.value());
    }
    Json out = {{"hop_s", floor.grid.hop.value()},
                {"start_s", seconds(floor.grid.start)},
                {"n_frames", floor.grid.n_frames},
                {"band_centre_hz", centres},
                {"band_dbfs", bands}};
    if (include_curve) {
        Json curve = Json::array();
        for (const Decibels d : floor.curve_dbfs) {
            curve.push_back(d.value());
        }
        out["curve_dbfs"] = curve;
    }
    return out;
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
    if (analysis.timeline) {
        root["gaps"] = gaps_json(*analysis.timeline);
    }
    if (analysis.noise_floor) {
        root["noise_floor"] = noise_floor_json(*analysis.noise_floor, options.include_pitch_track);
    }
    if (options.include_pitch_track && analysis.pitch) {
        root["pitch"] = pitch_json(*analysis.pitch);
    }
    return root.dump(2) + "\n";
}

}  // namespace anno::cli
