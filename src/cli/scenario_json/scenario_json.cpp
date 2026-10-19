#include "cli/scenario_json/scenario_json.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

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

Result<std::vector<NoteId>> parse_target(const Json& j, const std::vector<ScenarioNote>& notes,
                                         const std::string& path) {
    const auto it = j.find("target");
    if (it == j.end() || !it->is_object()) {
        return bad(at(path, "target"), "is required and must be an object");
    }
    const std::string tpath = at(path, "target");
    if (it->contains("note_id") == it->contains("note_index")) {
        return bad(tpath, "needs exactly one of note_id, note_index");
    }
    if (it->contains("note_id")) {
        if (!(*it)["note_id"].is_string()) {
            return bad(at(tpath, "note_id"), "must be a string");
        }
        return std::vector<NoteId>{NoteId{(*it)["note_id"].get<std::string>()}};
    }
    const Json& index = (*it)["note_index"];
    if (!index.is_number_integer() || index.get<long long>() < 0 ||
        index.get<long long>() >= static_cast<long long>(notes.size())) {
        return bad(at(tpath, "note_index"), "must be an index into notes");
    }
    return std::vector<NoteId>{NoteId{notes[static_cast<std::size_t>(index.get<long long>())].id}};
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
    spec.target.note_ids = std::move(*target);
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

/// Fills scenario.edits and edits_json from the "edits" array (an absent array means no edits).
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
    for (std::size_t i = 0; i < it->size(); ++i) {
        auto edit = parse_edit((*it)[i], scenario.notes, "edits[" + std::to_string(i) + "]");
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
    static const std::set<std::string> keys{"name",  "source", "algorithm",
                                            "notes", "edits",  "expect"};
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
