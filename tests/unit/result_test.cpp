#include "core/types/result.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>

using namespace anno;

namespace {

Result<int> parse_positive(int v) {
    if (v <= 0) {
        return fail(ErrorCode::invalid_argument, "not positive");
    }
    return v;
}

Result<void> check_even(int v) {
    if (v % 2 != 0) {
        return fail(ErrorCode::invalid_argument, "odd");
    }
    return {};
}

}  // namespace

TEST_CASE("result_holds_a_value", "[result]") {
    const Result<int> r = parse_positive(3);
    REQUIRE(r.has_value());
    CHECK(static_cast<bool>(r));
    CHECK(r.value() == 3);
    CHECK(*r == 3);
    CHECK(r.value_or(7) == 3);
}

TEST_CASE("result_holds_an_error", "[result]") {
    const Result<int> r = parse_positive(-1);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::invalid_argument);
    CHECK(r.error().message == "not positive");
    CHECK(r.value_or(7) == 7);
}

TEST_CASE("and_then_chains_and_passes_errors_through", "[result]") {
    const auto doubled = parse_positive(4).and_then([](int v) { return Result<int>{v * 2}; });
    REQUIRE(doubled.has_value());
    CHECK(*doubled == 8);

    const auto failed = parse_positive(0).and_then([](int v) { return Result<int>{v * 2}; });
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message == "not positive");

    const auto to_void = parse_positive(3).and_then(check_even);
    REQUIRE_FALSE(to_void.has_value());
    CHECK(to_void.error().message == "odd");
}

TEST_CASE("transform_maps_values_including_to_void", "[result]") {
    const Result<std::string> text =
        parse_positive(5).transform([](int v) { return std::to_string(v); });
    REQUIRE(text.has_value());
    CHECK(*text == "5");

    int seen = 0;
    const Result<void> done = parse_positive(5).transform([&seen](int v) { seen = v; });
    CHECK(done.has_value());
    CHECK(seen == 5);

    const Result<void> skipped = parse_positive(-5).transform([&seen](int v) { seen = v * 100; });
    CHECK_FALSE(skipped.has_value());
    CHECK(seen == 5);
}

TEST_CASE("result_void_success_and_failure", "[result]") {
    CHECK(check_even(2).has_value());
    const Result<void> odd = check_even(3);
    REQUIRE_FALSE(odd.has_value());
    CHECK(odd.error().code == ErrorCode::invalid_argument);

    const auto chained = check_even(2).and_then([] { return parse_positive(9); });
    REQUIRE(chained.has_value());
    CHECK(*chained == 9);

    const auto mapped = check_even(2).transform([] { return 1.5; });
    REQUIRE(mapped.has_value());
    CHECK(*mapped == 1.5);
}

TEST_CASE("result_supports_move_only_values_through_every_member", "[result]") {
    auto make = [](bool ok) -> Result<std::unique_ptr<int>> {
        if (!ok) {
            return fail(ErrorCode::not_found, "none");
        }
        return std::make_unique<int>(42);
    };

    Result<std::unique_ptr<int>> r = make(true);
    REQUIRE(r.has_value());
    CHECK(**r == 42);
    CHECK(*r.value() == 42);
    CHECK(*r->get() == 42);

    std::unique_ptr<int> taken = std::move(r).value();
    CHECK(*taken == 42);

    CHECK(*make(true).value_or(std::make_unique<int>(1)) == 42);
    CHECK(*make(false).value_or(std::make_unique<int>(1)) == 1);

    const auto chained = make(true).and_then([](std::unique_ptr<int> p) {
        return Result<std::unique_ptr<int>>{std::make_unique<int>(*p + 1)};
    });
    CHECK(**chained == 43);

    const auto mapped = make(true).transform([](std::unique_ptr<int> p) { return *p * 2; });
    CHECK(*mapped == 84);

    const auto missing = make(false).transform([](std::unique_ptr<int> p) { return *p; });
    CHECK(missing.error().code == ErrorCode::not_found);

    Error moved_error = make(false).error();
    CHECK(moved_error.message == "none");
}

TEST_CASE("error_codes_have_stable_names", "[result]") {
    CHECK(to_string(ErrorCode::invalid_argument) == "invalid_argument");
    CHECK(to_string(ErrorCode::out_of_range) == "out_of_range");
    CHECK(to_string(ErrorCode::io_error) == "io_error");
    CHECK(to_string(ErrorCode::unsupported_format) == "unsupported_format");
    CHECK(to_string(ErrorCode::corrupt_data) == "corrupt_data");
    CHECK(to_string(ErrorCode::not_found) == "not_found");
    CHECK(to_string(ErrorCode::cancelled) == "cancelled");
    CHECK(to_string(ErrorCode::internal) == "internal");
}
