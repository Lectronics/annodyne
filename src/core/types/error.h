#pragma once

#include <string>
#include <string_view>

namespace anno {

/// Closed set of failure kinds. Add a code through a design note, not ad hoc.
enum class ErrorCode {
    invalid_argument,    // caller passed a value outside the documented range
    out_of_range,        // index or size beyond what the object holds or can hold
    io_error,            // the OS failed to open, read or write
    unsupported_format,  // valid input in a format Annodyne does not handle
    corrupt_data,        // input that claims a format but breaks it (truncated, inconsistent)
    not_found,           // a named file or object does not exist
    cancelled,           // the operation was cancelled through its cancel token
    not_implemented,     // valid request this build or engine cannot do yet (ADR-0006); not a file
                         // format
    internal,            // a bug in Annodyne; always worth a report
};

/// Stable lower-case name of a code, e.g. "corrupt_data". Pure; thread-safe; real-time safe.
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

/// A failure: what kind, and a human-readable detail. `message` allocates, so an Error is never
/// built on the audio thread.
struct Error {
    ErrorCode code;
    std::string message;
};

/// Marks an Error as the failure branch when constructing a Result (like std::unexpected).
struct Unexpected {
    Error error;
};

/// Shorthand for `return fail(ErrorCode::io_error, "...");` in functions returning Result<T>.
/// Not real-time safe (allocates).
[[nodiscard]] Unexpected fail(ErrorCode code, std::string message);

}  // namespace anno
