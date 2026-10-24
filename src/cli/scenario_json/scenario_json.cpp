#include "cli/scenario_json/scenario_json.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace anno::cli {

namespace {

using Json = nlohmann::json;

std::string at(const std::string& path, std::string_view key) {
    std::string out = path;
    out += '.';
    out += key;
    return out;
}

Unexpected bad(const std::string& path, const std::string& what) {
    return fail(ErrorCode::invalid_argument, path + ": " + what);
}

Result<std::string> string_field(const Json& obj, const char* key, const std::string& path,
                                 bool required) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        if (required) {
            return bad(at(path, key), "is required");
        }
        return std::string{};
    }
    if (!it->is_string()) {
        return bad(at(path, key), "must be a string");
    }
    return it->get<std::string>();
}

Result<double> number(const Json& v, const std::string& path) {
    if (!v.is_number()) {
        return bad(path, "must be a number");
    }
    return v.get<double>();
}

Result<ScenarioNote> parse_note(const Json& j, const std::string& path) {
    if (!j.is_object()) {
        return bad(path, "must be an object");
    }
    for (const auto& [key, value] : j.items()) {
        if (key != "id" && key != "start_s" && key != "end_s") {
            return bad(at(path, key), "unknown key");
        }
    }
    ScenarioNote note;
    auto id = string_field(j, "id", path, true);
    if (!id) {
        return Unexpected{id.error()};
    }
    note.id = *id;
    for (const char* key : {"start_s", "end_s"}) {
        const auto it = j.find(key);
        if (it == j.end()) {
            return bad(at(path, key), "is required");
        }
        auto v = number(*it, at(path, key));
        if (!v) {
            return Unexpected{v.error()};
        }
        (std::string{key} == "start_s" ? note.start_s : note.end_s) = *v;
    }
    return note;
}

Result<EditValue> parse_value(const Json& j, ValueKind kind, const std::string& path) {
    const auto it = j.find("value");
    if (it == j.end()) {
        return bad(at(path, "value"), "is required");
    }
    const std::string vpath = at(path, "value");
    if (kind == ValueKind::fade_shape) {
        if (!it->is_string()) {
            return bad(vpath, "must be a string");
        }
        const auto shape = parse_fade_shape(it->get<std::string>());
        if (!shape) {
            return bad(vpath, "unknown fade shape");
        }
        return EditValue{*shape};
    }
    if (kind == ValueKind::time_pair) {
        if (!it->is_object() || !it->contains("source_s") || !it->contains("output_s")) {
            return bad(vpath, R"(must be {"source_s": number, "output_s": number})");
        }
        auto s = number((*it)["source_s"], at(vpath, "source_s"));
        auto o = number((*it)["output_s"], at(vpath, "output_s"));
        if (!s) {
            return Unexpected{s.error()};
        }
        if (!o) {
            return Unexpected{o.error()};
        }
        return EditValue{
            TimePair{.source = SourceTime{} + Seconds{*s}, .output = OutputTime{} + Seconds{*o}}};
    }
    auto v = number(*it, vpath);
    if (!v) {
        return Unexpected{v.error()};
    }
    switch (kind) {
    case ValueKind::cents: return EditValue{Cents{*v}};
    case ValueKind::ratio: return EditValue{Ratio{*v}};
    case ValueKind::decibels: return EditValue{Decibels{*v}};
    case ValueKind::seconds: return EditValue{Seconds{*v}};
    case ValueKind::fade_shape:
    case ValueKind::time_pair: break;
    }
    return bad(vpath, "unsupported value kind");
}

/// {"gap": {"before": id | null, "after": id | null}}: the gap between two notes (null: the
/// recording's edge).
Result<SegmentId> parse_gap_target(const Json& gap, const std::string& path) {
    if (!gap.is_object()) {
        return bad(path, R"(must be {"before": id | null, "after": id | null})");
    }
    for (const auto& [key, value] : gap.items()) {
        if (key != "before" && key != "after") {
            return bad(at(path, key), "unknown key");
        }
    }
    std::array<std::optional<NoteId>, 2> sides;
    const std::array<const char*, 2> names{"before", "after"};
    for (std::size_t i = 0; i < 2; ++i) {
        const auto it = gap.find(names[i]);
        if (it == gap.end()) {
            return bad(path, R"(needs both "before" and "after" (null at the recording's edge))");
        }
        if (it->is_string()) {
            sides[i] = NoteId{it->get<std::string>()};
        } else if (!it->is_null()) {
            return bad(at(path, names[i]), "must be a note id or null");
        }
    }
    return gap_segment_id(sides[0], sides[1]);
}

Result<std::vector<SegmentId>> parse_target(const Json& j, const std::vector<ScenarioNote>& notes,
                                            const std::string& path) {
    const auto it = j.find("target");
    if (it == j.end() || !it->is_object()) {
        return bad(at(path, "target"), "is required and must be an object");
    }
    const std::string tpath = at(path, "target");
    const int named = static_cast<int>(it->contains("note_id")) +
                      static_cast<int>(it->contains("note_index")) +
                      static_cast<int>(it->contains("gap"));
    if (named != 1) {
        return bad(tpath, "needs exactly one of note_id, note_index, gap");
    }
    if (it->contains("gap")) {
        auto gap = parse_gap_target((*it)["gap"], at(tpath, "gap"));
        if (!gap) {
            return Unexpected{gap.error()};
        }
        return std::vector<SegmentId>{*gap};
    }
    if (it->contains("note_id")) {
        if (!(*it)["note_id"].is_string()) {
            return bad(at(tpath, "note_id"), "must be a string");
        }
        return std::vector<SegmentId>{segment_id(NoteId{(*it)["note_id"].get<std::string>()})};
    }
    const Json& index = (*it)["note_index"];
    if (!index.is_number_integer() || index.get<long long>() < 0 ||
        index.get<long long>() >= static_cast<long long>(notes.size())) {
        return bad(at(tpath, "note_index"), "must be an index into notes");
    }
    return std::vector<SegmentId>{
        segment_id(NoteId{notes[static_cast<std::size_t>(index.get<long long>())].id})};
}

/// category, op and param of an edit.
Result<void> parse_edit_names(const Json& j, const std::string& path, EditSpec& spec) {
    auto category = string_field(j, "category", path, true);
    auto op = string_field(j, "op", path, true);
    auto param = string_field(j, "param", path, false);
    if (!category) {
        return Unexpected{category.error()};
    }
    if (!op) {
        return Unexpected{op.error()};
    }
    if (!param) {
        return Unexpected{param.error()};
    }
    const auto parsed_category = parse_edit_category(*category);
    const auto parsed_op = parse_edit_op(*op);
    if (!parsed_category) {
        return bad(at(path, "category"), "unknown category '" + *category + "'");
    }
    if (!parsed_op) {
        return bad(at(path, "op"), "unknown op '" + *op + "'");
    }
    spec.category = *parsed_category;
    spec.op = *parsed_op;
    if (!param->empty()) {
        spec.param = parse_edit_param(*param);
        if (!spec.param) {
            return bad(at(path, "param"), "unknown parameter '" + *param + "'");
        }
    }
    return {};
}

/// The value of an edit; a reset carries none (except a time handle, which names the handle).
Result<void> parse_edit_value(const Json& j, const std::string& path, EditSpec& spec) {
    const bool has_value = j.contains("value");
    if (spec.op == EditOp::reset) {
        if (has_value && spec.param != EditParam::time_handle) {
            return bad(at(path, "value"), "reset takes no value");
        }
        if (!has_value) {
            return {};
        }
        auto handle = parse_value(j, ValueKind::time_pair, path);
        if (!handle) {
            return Unexpected{handle.error()};
        }
        spec.value = *handle;
        return {};
    }
    if (!spec.param) {
        return bad(at(path, "param"), "is required unless op is reset");
    }
    auto value = parse_value(j, param_info(*spec.param).kind, path);
    if (!value) {
        return Unexpected{value.error()};
    }
    spec.value = *value;
    return {};
}

Result<EditSpec> parse_edit(const Json& j, const std::vector<ScenarioNote>& notes,
                            const std::string& path) {
    if (!j.is_object()) {
        return bad(path, "must be an object");
    }
    for (const auto& [key, value] : j.items()) {
        if (key != "category" && key != "op" && key != "param" && key != "value" &&
            key != "target") {
            return bad(at(path, key), "unknown key");
        }
    }
    EditSpec spec;
    spec.origin = "scenario";
    if (auto r = parse_edit_names(j, path, spec); !r) {
        return Unexpected{r.error()};
    }
    if (auto r = parse_edit_value(j, path, spec); !r) {
        return Unexpected{r.error()};
    }
    auto target = parse_target(j, notes, path);
    if (!target) {
        return Unexpected{target.error()};
    }
    spec.target.segment_ids = std::move(*target);
    if (auto r = validate_for_add(spec); !r) {
        return bad(path, r.error().message);
    }
    return spec;
}

std::filesystem::path resolve_source(const std::string& text, const std::filesystem::path& base_dir,
                                     const std::filesystem::path& repo_root) {
    if (const std::filesystem::path given{text}; given.is_absolute()) {
        return given;
    }
    std::filesystem::path near = base_dir / text;
    std::error_code ec;
    return std::filesystem::exists(near, ec) ? near : repo_root / text;
}

Result<std::vector<ScenarioNote>> parse_notes(const Json& root) {
    std::vector<ScenarioNote> notes;
    const auto it = root.find("notes");
    if (it == root.end()) {
        return notes;
    }
    if (!it->is_array()) {
        return bad("$.notes", "must be an array");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        auto note = parse_note((*it)[i], "notes[" + std::to_string(i) + "]");
        if (!note) {
            return Unexpected{note.error()};
        }
        notes.push_back(std::move(*note));
    }
    return notes;
}

/// {"tool": "pitch_drag", "target": {"note_index": i}, "offset_cents" | "target_cents": n,
/// "snap": "chromatic"?}.
Result<ScenarioPitchDrag> parse_pitch_drag(const Json& j, const std::string& path) {
    for (const auto& [key, value] : j.items()) {
        if (key != "tool" && key != "target" && key != "offset_cents" && key != "target_cents" &&
            key != "snap") {
            return bad(at(path, key), "unknown key");
        }
    }
    if (j.at("tool") != "pitch_drag") {  // the caller checked that "tool" is present
        return bad(at(path, "tool"), "unknown tool (available: pitch_drag)");
    }
    ScenarioPitchDrag drag;
    const auto target = j.find("target");
    if (target == j.end() || !target->is_object() || target->size() != 1 ||
        !target->contains("note_index") || !(*target)["note_index"].is_number_integer() ||
        (*target)["note_index"].get<long long>() < 0) {
        return bad(at(path, "target"), R"(must be {"note_index": a non-negative integer})");
    }
    drag.note_index = static_cast<std::size_t>((*target)["note_index"].get<long long>());
    if (j.contains("offset_cents") == j.contains("target_cents")) {
        return bad(path, "needs exactly one of offset_cents, target_cents");
    }
    for (const char* key : {"offset_cents", "target_cents"}) {
        if (!j.contains(key)) {
            continue;
        }
        auto v = number(j[key], at(path, key));
        if (!v) {
            return Unexpected{v.error()};
        }
        (std::string{key} == "offset_cents" ? drag.offset_cents : drag.target_cents) = *v;
    }
    if (const auto snap = j.find("snap"); snap != j.end()) {
        if (!snap->is_string() || snap->get<std::string>() != "chromatic") {
            return bad(at(path, "snap"), R"(must be "chromatic")");
        }
        drag.snap_chromatic = true;
    }
    return drag;
}

/// {"macro": "correct_pitch" | "leveling", ...numbers...}.
Result<ScenarioMacro> parse_macro(const Json& j, const std::string& path) {
    if (!j.is_object() || !j.contains("macro") || !j["macro"].is_string()) {
        return bad(path, R"(must be an object with a "macro" name)");
    }
    ScenarioMacro macro;
    macro.name = j["macro"].get<std::string>();
    if (macro.name != "correct_pitch" && macro.name != "leveling" && macro.name != "formant") {
        return bad(at(path, "macro"),
                   "unknown macro (available: correct_pitch, leveling, formant)");
    }
    for (const auto& [key, value] : j.items()) {
        if (key == "macro") {
            continue;
        }
        if (key == "scale") {
            if (!value.is_string()) {
                return bad(at(path, key), "must be a string");
            }
            macro.scale = value.get<std::string>();
            continue;
        }
        auto v = number(value, at(path, key));
        if (!v) {
            return Unexpected{v.error()};
        }
        if (key == "root") {
            macro.root = static_cast<int>(*v);
        } else if (key == "center_amount") {
            macro.center_amount = *v;
        } else if (key == "drift_amount") {
            macro.drift_amount = *v;
        } else if (key == "min_deviation_cents") {
            macro.min_deviation_cents = *v;
        } else if (key == "between_notes") {
            macro.between_notes = *v;
        } else if (key == "within_notes") {
            macro.within_notes = *v;
        } else if (key == "offset_cents") {
            macro.offset_cents = *v;
        } else {
            return bad(at(path, key), "unknown key");
        }
    }
    return macro;
}

/// Fills the Rubber Band engine settings from the "rubberband" object (absent: defaults).
Result<void> parse_rubberband(const Json& root, Scenario& scenario) {
    const auto rb = root.find("rubberband");
    if (rb == root.end()) {
        return {};
    }
    if (!rb->is_object()) {
        return bad("$.rubberband", "must be an object");
    }
    for (const auto& [key, value] : rb->items()) {
        if (key == "glide_ms") {
            auto v = number(value, "$.rubberband.glide_ms");
            if (!v) {
                return Unexpected{v.error()};
            }
            scenario.rubberband_glide_ms = *v;
        } else if (key == "formant_path") {
            if (!value.is_string() ||
                (value.get<std::string>() != "offline" && value.get<std::string>() != "live")) {
                return bad("$.rubberband.formant_path", R"(must be "offline" or "live")");
            }
            scenario.rubberband_formant_live = value.get<std::string>() == "live";
        } else {
            return bad("$.rubberband." + key, "unknown key");
        }
    }
    return {};
}

/// Fills scenario.macros from the "macros" array (absent: none). Needs "analysis": "analyse".
Result<void> parse_macros(const Json& root, Scenario& scenario) {
    const auto macros = root.find("macros");
    if (macros == root.end()) {
        return {};
    }
    if (!macros->is_array()) {
        return bad("$.macros", "must be an array");
    }
    if (scenario.analysis != ScenarioAnalysis::analyse) {
        return bad("$.macros", R"(need "analysis": "analyse")");
    }
    for (std::size_t i = 0; i < macros->size(); ++i) {
        auto macro = parse_macro((*macros)[i], "$.macros[" + std::to_string(i) + "]");
        if (!macro) {
            return Unexpected{macro.error()};
        }
        scenario.macros.push_back(std::move(*macro));
    }
    return {};
}

/// Fills scenario.edits, scenario.drags and edits_json from the "edits" array (an absent array
/// means no edits).
Result<void> parse_edits(const Json& root, Scenario& scenario) {
    const auto it = root.find("edits");
    if (it == root.end()) {
        scenario.edits_json = "[]";
        return {};
    }
    if (!it->is_array()) {
        return bad("$.edits", "must be an array");
    }
    scenario.edits_json = it->dump(2);
    const auto tool_edits = std::ranges::count_if(
        *it, [](const Json& e) { return e.is_object() && e.contains("tool"); });
    if (tool_edits != 0 && std::cmp_not_equal(tool_edits, it->size())) {
        // Drags are resolved after the analysis and added after raw edits, so a mix would apply in
        // a different order than written.
        return bad("$.edits", R"(must be all "tool" edits or all raw edits, not a mix)");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string path = "edits[" + std::to_string(i) + "]";
        if ((*it)[i].is_object() && (*it)[i].contains("tool")) {
            if (scenario.analysis != ScenarioAnalysis::analyse) {
                return bad(path, R"("tool" edits need "analysis": "analyse")");
            }
            auto drag = parse_pitch_drag((*it)[i], path);
            if (!drag) {
                return Unexpected{drag.error()};
            }
            scenario.drags.push_back(*drag);
            continue;
        }
        if (scenario.analysis == ScenarioAnalysis::analyse && (*it)[i].is_object() &&
            (*it)[i].contains("target") && (*it)[i]["target"].is_object() &&
            (*it)[i]["target"].contains("note_index")) {
            return bad(
                path,
                R"(raw edits cannot use note_index with "analysis": "analyse"; use a "tool" edit or note_id)");
        }
        auto edit = parse_edit((*it)[i], scenario.notes, path);
        if (!edit) {
            return Unexpected{edit.error()};
        }
        scenario.edits.push_back(std::move(*edit));
    }
    return {};
}

}  // namespace

Result<Scenario> parse_scenario(std::string_view json_text, const std::filesystem::path& base_dir,
                                const std::filesystem::path& repo_root) {
    const Json root = Json::parse(json_text, nullptr, false);
    if (root.is_discarded()) {
        return fail(ErrorCode::corrupt_data, "scenario is not valid JSON");
    }
    if (!root.is_object()) {
        return bad("$", "must be an object");
    }
    static const std::set<std::string> keys{
        "name",   "source",          "algorithm",      "analysis", "notes",     "edits",
        "expect", "stretch_vibrato", "stretch_engine", "macros",   "rubberband"};
    for (const auto& [key, value] : root.items()) {
        if (!keys.contains(key)) {
            return bad("$." + key, "unknown key");
        }
    }
    Scenario scenario;
    auto name = string_field(root, "name", "$", true);
    auto source = string_field(root, "source", "$", true);
    auto algorithm = string_field(root, "algorithm", "$", false);
    if (!name) {
        return Unexpected{name.error()};
    }
    if (!source) {
        return Unexpected{source.error()};
    }
    if (!algorithm) {
        return Unexpected{algorithm.error()};
    }
    scenario.name = *name;
    scenario.source = resolve_source(*source, base_dir, repo_root);
    scenario.algorithm = algorithm->empty() ? "melodic" : *algorithm;
    auto vibrato = string_field(root, "stretch_vibrato", "$", false);
    if (!vibrato) {
        return Unexpected{vibrato.error()};
    }
    if (!vibrato->empty() && *vibrato != "retime" && *vibrato != "preserve") {
        return bad("$.stretch_vibrato", R"(must be "preserve" or "retime")");
    }
    scenario.stretch_vibrato_retime = *vibrato == "retime";
    auto engine = string_field(root, "stretch_engine", "$", false);
    if (!engine) {
        return Unexpected{engine.error()};
    }
    if (!engine->empty() && *engine != "psola" && *engine != "rubberband") {
        return bad("$.stretch_engine", R"(must be "psola" or "rubberband")");
    }
    scenario.stretch_psola = *engine == "psola";
    auto analysis = string_field(root, "analysis", "$", false);
    if (!analysis) {
        return Unexpected{analysis.error()};
    }
    if (*analysis == "analyse") {
        scenario.analysis = ScenarioAnalysis::analyse;
    } else if (!analysis->empty() && *analysis != "given") {
        return bad("$.analysis", R"(must be "given" or "analyse")");
    }
    if (auto parsed = parse_rubberband(root, scenario); !parsed) {
        return Unexpected{parsed.error()};
    }
    if (auto parsed = parse_macros(root, scenario); !parsed) {
        return Unexpected{parsed.error()};
    }
    if (scenario.analysis == ScenarioAnalysis::analyse && root.contains("notes")) {
        return bad("$.notes", R"(not allowed with "analysis": "analyse")");
    }
    auto notes = parse_notes(root);
    if (!notes) {
        return Unexpected{notes.error()};
    }
    scenario.notes = std::move(*notes);
    if (auto r = parse_edits(root, scenario); !r) {
        return Unexpected{r.error()};
    }
    if (const auto it = root.find("expect"); it != root.end()) {
        scenario.expect_json = it->dump(2);
    }
    return scenario;
}

Result<Scenario> load_scenario(const std::filesystem::path& path,
                               const std::filesystem::path& repo_root) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return fail(ErrorCode::not_found, "no such scenario file: " + path.string());
    }
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return fail(ErrorCode::io_error, "cannot open scenario file: " + path.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse_scenario(text.str(), path.parent_path(), repo_root);
}

}  // namespace anno::cli
