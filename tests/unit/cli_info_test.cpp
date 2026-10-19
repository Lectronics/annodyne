#include "cli/commands/info.h"
#include "core/io/wav.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace anno;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

// Each test case passes its own file name: ctest runs cases in parallel processes.
std::filesystem::path write_test_wav(const std::string& name) {
    auto buf = AudioBuffer::create(2, Samples{24000}, Hz{48000.0}).value();
    buf.channel(0)[100] = 0.5F;   // -6.02 dBFS
    buf.channel(1)[200] = -1.0F;  // 0 dBFS
    const auto dir = std::filesystem::temp_directory_path() / "anno_cli_info_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / name;
    REQUIRE(io::write_wav(path, buf).has_value());
    return path;
}

}  // namespace

TEST_CASE("info_report_summarises_a_wav", "[cli]") {
    const auto report = cli::make_info_report(write_test_wav("summarises.wav"));
    REQUIRE(report.has_value());
    CHECK(report->wav.channel_count == 2);
    CHECK(report->wav.frame_count == Samples{24000});
    CHECK(report->wav.format == io::WavSampleFormat::float32);
    CHECK_THAT(report->duration.value(),
               WithinAbs(0.5, tolerance::k_seconds_conversion_abs.value()));
    CHECK_FALSE(report->source_id.value.empty());
    REQUIRE(report->peak_dbfs.size() == 2);
    CHECK_THAT(report->peak_dbfs[0].value(), WithinAbs(-6.020599913279624, 1e-9));
    CHECK_THAT(report->peak_dbfs[1].value(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("info_report_propagates_read_errors", "[cli]") {
    const auto report = cli::make_info_report("/definitely/not/here.wav");
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().code == ErrorCode::not_found);
}

TEST_CASE("format_info_lists_every_field", "[cli]") {
    const auto report = cli::make_info_report(write_test_wav("format.wav")).value();
    const std::string text = cli::format_info(report);
    CHECK_THAT(text, ContainsSubstring("float32"));
    CHECK_THAT(text, ContainsSubstring("48000 Hz"));
    CHECK_THAT(text, ContainsSubstring("channels:    2"));
    CHECK_THAT(text, ContainsSubstring("24000"));
    CHECK_THAT(text, ContainsSubstring("0.500000 s"));
    CHECK_THAT(text, ContainsSubstring(report.source_id.value));
    CHECK_THAT(text, ContainsSubstring("-6.02 dBFS"));
    CHECK_THAT(text, ContainsSubstring("0.00 dBFS"));
}
