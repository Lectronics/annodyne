// Helper for the precondition-abort tests. Each case breaks one precondition and must end in
// melo::detail::contract_violation (prints "contract violation", then aborts). Driven by
// expect_contract_violation.cmake, because a SIGABRT would fail a normal CTest test.

#include "core/types/audio_buffer.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <cstddef>
#include <cstdio>
#include <limits>
#include <span>
#include <string_view>

namespace {

int run(std::string_view name) {
    using namespace melo;
    constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double k_inf = std::numeric_limits<double>::infinity();

    if (name == "ok") {
        return 0;  // positive control: the helper itself works
    }
    if (name == "to_samples_nan") {
        return static_cast<int>(to_samples(Seconds{k_nan}, Hz{48000.0}, Rounding::nearest).value());
    }
    if (name == "to_samples_zero_rate") {
        return static_cast<int>(to_samples(Seconds{1.0}, Hz{0.0}, Rounding::nearest).value());
    }
    if (name == "to_samples_too_large") {
        return static_cast<int>(to_samples(Seconds{1e300}, Hz{48000.0}, Rounding::nearest).value());
    }
    if (name == "to_cents_zero_hz") {
        return static_cast<int>(to_cents(Hz{0.0}, k_a4_standard_hz).value());
    }
    if (name == "to_hz_nan") {
        return static_cast<int>(to_hz(Cents{k_nan}, k_a4_standard_hz).value());
    }
    if (name == "to_decibels_inf") {
        return static_cast<int>(to_decibels(LinearGain{k_inf}).value());
    }
    if (name == "result_value_on_error") {
        const Result<int> r = fail(ErrorCode::internal, "x");
        return r.value();
    }
    if (name == "result_error_on_value") {
        const Result<int> r = 1;
        return static_cast<int>(r.error().code);
    }
    if (name == "time_range_negative_duration") {
        return static_cast<int>(
            SourceRange::from_start(SourceTime{}, Seconds{-1.0}).duration().value());
    }
    if (name == "audio_buffer_bad_channel") {
        const auto buf = AudioBuffer::create(1, Samples{1}, Hz{48000.0}).value();
        return static_cast<int>(buf.channel(1).size());
    }
    std::fprintf(stderr, "unknown case '%.*s'\n", static_cast<int>(name.size()), name.data());
    return 2;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): test helper; an escaping exception fails the test
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: contract_violation_cases <case>\n", stderr);
        return 2;
    }
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    return run(args[1]);
}
