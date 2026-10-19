#pragma once

#include "core/types/contract.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <cmath>
#include <string>

// Typed time ranges and the persistence form of time points (ADR-0003).
// Design: docs/design/phase0-foundations.md §4.

namespace anno {

/// Half-open range [start, end) on one time axis. Invariant: start() <= end().
template <class Axis>
class TimeRange {
public:
    /// The empty range at the axis origin.
    constexpr TimeRange() noexcept = default;

    /// For ranges built from untrusted values. Errors: invalid_argument if end < start.
    /// Not real-time safe on the error path (allocates the message).
    [[nodiscard]] static Result<TimeRange> make(TimePoint<Axis> start, TimePoint<Axis> end) {
        if (end < start) {
            return fail(ErrorCode::invalid_argument, "time range ends before it starts");
        }
        return TimeRange{start, end};
    }

    /// For ranges that cannot be invalid, e.g. a start plus a buffer length.
    /// Pre: duration >= 0. Real-time safe.
    [[nodiscard]] static TimeRange from_start(TimePoint<Axis> start, Seconds duration) noexcept {
        ANNO_EXPECTS(duration >= Seconds{0.0});
        return TimeRange{start, start + duration};
    }

    [[nodiscard]] constexpr TimePoint<Axis> start() const noexcept { return start_; }
    [[nodiscard]] constexpr TimePoint<Axis> end() const noexcept { return end_; }
    [[nodiscard]] constexpr Seconds duration() const noexcept { return end_ - start_; }

    friend constexpr bool operator==(TimeRange, TimeRange) noexcept = default;

private:
    constexpr TimeRange(TimePoint<Axis> start, TimePoint<Axis> end) noexcept
        : start_{start}, end_{end} {}

    TimePoint<Axis> start_{};
    TimePoint<Axis> end_{};
};

using SourceRange = TimeRange<SourceAxis>;
using OutputRange = TimeRange<OutputAxis>;

/// Seconds from the axis origin, for writing edit logs and scenario files. Allowed only in
/// core/types, project and cli (enforced by scripts/check_includes.py). Lossless with
/// from_serialized_seconds. Pure; real-time safe.
template <class Axis>
[[nodiscard]] double to_serialized_seconds(TimePoint<Axis> t) noexcept {
    return t.unchecked_since_origin().value();
}

/// Inverse of to_serialized_seconds. The value comes from a file, so bad input is an error, not a
/// precondition: corrupt_data if s is NaN or infinite. Same usage restriction.
template <class Axis>
[[nodiscard]] Result<TimePoint<Axis>> from_serialized_seconds(double s) {
    if (!std::isfinite(s)) {
        return fail(ErrorCode::corrupt_data, "serialized time is not a finite number");
    }
    return TimePoint<Axis>::unchecked_from_origin(Seconds{s});
}

}  // namespace anno
