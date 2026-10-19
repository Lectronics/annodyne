// Compile-time contracts of Result<T>: no narrowing, no ambiguity, nodiscard.

#include "core/types/result.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <type_traits>

using namespace melo;

// Values convert implicitly when the conversion is implicit and non-narrowing.
static_assert(std::is_convertible_v<int, Result<int>>);
static_assert(std::is_convertible_v<const char*, Result<std::string>>);
static_assert(std::is_convertible_v<Unexpected, Result<int>>);
static_assert(std::is_convertible_v<Unexpected, Result<void>>);

// Narrowing and surprising conversions are rejected.
static_assert(!std::is_constructible_v<Result<int>, double>);
static_assert(!std::is_constructible_v<Result<float>, double>);
static_assert(!std::is_constructible_v<Result<bool>, int*>);
static_assert(!std::is_constructible_v<Result<int>, long long>);

// A bare Error is not a Result: failures are spelled with fail()/Unexpected.
static_assert(!std::is_constructible_v<Result<int>, Error>);

// Move-only payloads work; copyability follows T.
static_assert(std::is_move_constructible_v<Result<std::unique_ptr<int>>>);
static_assert(!std::is_copy_constructible_v<Result<std::unique_ptr<int>>>);

// value() on an rvalue returns by value (no dangling reference).
static_assert(std::is_same_v<decltype(std::declval<Result<std::string>>().value()), std::string>);
static_assert(std::is_same_v<decltype(std::declval<Result<std::string>&>().value()), std::string&>);
static_assert(std::is_same_v<decltype(std::declval<Result<int>>().error()), Error>);

TEST_CASE("result_contracts_are_checked_at_compile_time", "[result][contract]") {
    SUCCEED("static_asserts in this file passed");
}
