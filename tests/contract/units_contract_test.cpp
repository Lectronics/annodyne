// Compile-time contracts of units.h (CLAUDE.md rules 3-4): what must NOT compile.
// Each check is a static_assert, so a regression fails the build, not just a test.

#include "core/types/units.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using namespace melo;

namespace {

template <class A, class B>
concept addable = requires(A a, B b) { a + b; };
template <class A, class B>
concept subtractable = requires(A a, B b) { a - b; };
template <class A, class B>
concept multipliable = requires(A a, B b) { a * b; };

}  // namespace

// No implicit conversion from or to raw numbers.
static_assert(!std::is_convertible_v<double, Seconds>);
static_assert(!std::is_convertible_v<Seconds, double>);
static_assert(!std::is_convertible_v<double, Cents>);
static_assert(!std::is_convertible_v<std::int64_t, Samples>);
static_assert(std::is_constructible_v<Seconds, double>);  // explicit construction is allowed

// No conversion between units or between axes.
static_assert(!std::is_convertible_v<Seconds, SourceTime>);
static_assert(!std::is_constructible_v<SourceTime, Seconds>);
static_assert(!std::is_convertible_v<SourceTime, OutputTime>);
static_assert(!std::is_constructible_v<OutputTime, SourceTime>);
static_assert(!std::is_convertible_v<Cents, Hz>);
static_assert(!std::is_convertible_v<Ratio, double>);

// Axis algebra.
static_assert(!subtractable<SourceTime, OutputTime>);
static_assert(!addable<SourceTime, SourceTime>);
static_assert(!addable<SourceTime, OutputTime>);
static_assert(std::is_same_v<decltype(SourceTime{} - SourceTime{}), Seconds>);
static_assert(std::is_same_v<decltype(SourceTime{} + Seconds{}), SourceTime>);

// Unit algebra.
static_assert(!addable<Cents, Hz>);
static_assert(!addable<Ratio, Seconds>);
static_assert(!addable<Seconds, Samples>);
static_assert(!addable<Seconds, double>);
static_assert(!multipliable<Samples, Ratio>);  // goes through Seconds with an explicit Rounding
static_assert(!multipliable<Seconds, Seconds>);
static_assert(std::is_same_v<decltype(Samples{} / Samples{1}), Ratio>);
static_assert(std::is_same_v<decltype(Cents{} * Ratio{}), Cents>);
static_assert(std::is_same_v<decltype(Ratio{} * Ratio{}), Ratio>);
static_assert(std::is_same_v<decltype(Ratio{} / Ratio{1.0}), Ratio>);

// Representation: zero overhead.
static_assert(std::is_trivially_copyable_v<Seconds>);
static_assert(std::is_trivially_copyable_v<SourceTime>);
static_assert(sizeof(Seconds) == sizeof(double));
static_assert(sizeof(Samples) == sizeof(std::int64_t));
static_assert(sizeof(SourceTime) == sizeof(double));

TEST_CASE("units_contracts_are_checked_at_compile_time", "[units][contract]") {
    SUCCEED("static_asserts in this file passed");
}
