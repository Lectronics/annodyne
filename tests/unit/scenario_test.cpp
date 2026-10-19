#include "cli/checks/audio_checks.h"
#include "cli/scenario_json/scenario_json.h"
#include "core/io/wav.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::cli;

namespace {

constexpr const char* k_good = R"({
  "name": "gain_up",
  "source": "tone.wav",
  "notes": [{"id": "n1", "start_s": 0.5, "end_s": 1.0}, {"id": "n2", "start_s": 1.5, "end_s": 2.0}],
  "edits": [
    {"category": "amplitude", "op": "add", "param": "gain", "value": 6.0, "target": {"note_id": "n1"}},
    {"category": "pitch", "op": "add", "param": "center_offset", "value": 200,
     "target": {"note_index": 1}},
    {"category": "fade", "op": "set", "param": "fade_shape", "value": "linear",
     "target": {"note_id": "n2"}},
    {"category": "time", "op": "set", "param": "time_handle",
     "value": {"source_s": 1.75, "output_s": 1.8}, "target": {"note_id": "n2"}},
    {"category": "pitch", "op": "reset", "target": {"note_id": "n1"}}
  ],
  "expect": {"pitch_cents_delta": 200}
})";

Result<Scenario> parse(const std::string& text) {
    return parse_scenario(text, "/scenarios", "/repo");
}

}  // namespace

TEST_CASE("a_complete_scenario_parses_into_typed_edits", "[scenario]") {
    const auto s = parse(k_good);
    REQUIRE(s.has_value());
    CHECK(s->name == "gain_up");
    CHECK(s->algorithm == "melodic");
    REQUIRE(s->notes.size() == 2);
    REQUIRE(s->edits.size() == 5);
    CHECK(s->edits[0].param == EditParam::gain);
    CHECK(std::get<Decibels>(s->edits[0].value) == Decibels{6.0});
    CHECK(s->edits[1].target.note_ids[0].value == "n2");  // note_index 1
    CHECK(std::get<FadeShape>(s->edits[2].value) == FadeShape::linear);
    CHECK(std::holds_alternative<TimePair>(s->edits[3].value));
    CHECK(s->edits[4].op == EditOp::reset);
    CHECK_FALSE(s->edits[4].param.has_value());
    CHECK(s->expect_json.find("pitch_cents_delta") != std::string::npos);
}

TEST_CASE("a_relative_source_falls_back_to_the_repo_root", "[scenario]") {
    const auto s = parse(k_good);
    REQUIRE(s.has_value());
    CHECK(s->source == std::filesystem::path{"/repo/tone.wav"});  // /scenarios/tone.wav absent
}

TEST_CASE("errors_name_the_json_path", "[scenario]") {
    const auto message = [](const std::string& text) { return parse(text).error().message; };
    CHECK(parse("{not json").error().code == ErrorCode::corrupt_data);
    CHECK(message(R"({"name":"x"})").find("$.source") != std::string::npos);
    CHECK(message(R"({"name":"x","source":"a","extra":1})").find("$.extra") != std::string::npos);
    const std::string bad_value =
        R"({"name":"x","source":"a","notes":[{"id":"n","start_s":0,"end_s":1}],
      "edits":[{"category":"amplitude","op":"add","param":"gain","value":"loud",
                "target":{"note_id":"n"}}]})";
    CHECK(message(bad_value).find("edits[0].value") != std::string::npos);
    const std::string bad_index = R"({"name":"x","source":"a","notes":[],
      "edits":[{"category":"pitch","op":"add","param":"center_offset","value":1,
                "target":{"note_index":3}}]})";
    CHECK(message(bad_index).find("edits[0].target.note_index") != std::string::npos);
    const std::string wrong_category =
        R"({"name":"x","source":"a","notes":[{"id":"n","start_s":0,"end_s":1}],
      "edits":[{"category":"pitch","op":"add","param":"gain","value":1,"target":{"note_id":"n"}}]})";
    CHECK(parse(wrong_category).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("a_missing_scenario_file_is_not_found", "[scenario]") {
    CHECK(load_scenario("/no/such/scenario.json", "/repo").error().code == ErrorCode::not_found);
}

TEST_CASE("the_synthetic_analysis_has_valid_sorted_notes", "[scenario]") {
    const AudioSource source = AudioSource::from_buffer(
        AudioBuffer::create(1, Samples{std::int64_t{48000} * 3}, Hz{48000.0}).value());
    const auto s = parse(k_good).value();
    const auto analysis = make_scenario_analysis(s, source);
    REQUIRE(analysis.has_value());
    CHECK(analysis->notes.size() == 2);
    CHECK(analysis->source_id == source.id());

    Scenario overlapping = s;
    overlapping.notes = {{.id = "a", .start_s = 0.0, .end_s = 1.0},
                         {.id = "b", .start_s = 0.5, .end_s = 1.5}};
    CHECK_FALSE(make_scenario_analysis(overlapping, source).has_value());
    Scenario past_end = s;
    past_end.notes = {{.id = "a", .start_s = 2.0, .end_s = 4.0}};
    CHECK_FALSE(make_scenario_analysis(past_end, source).has_value());
    Scenario duplicate = s;
    duplicate.notes = {{.id = "a", .start_s = 0.0, .end_s = 0.1},
                       {.id = "a", .start_s = 0.2, .end_s = 0.3}};
    CHECK_FALSE(make_scenario_analysis(duplicate, source).has_value());
}

TEST_CASE("the_click_detector_defaults_match_the_pinned_tolerances", "[scenario]") {
    const ClickParams defaults;
    CHECK(defaults.ratio == tolerance::k_click_ratio);
    CHECK(defaults.window == tolerance::k_click_window);
    CHECK(defaults.floor == tolerance::k_click_floor);
}
