#pragma once

#include <source_location>

// Contract checks. These two macros are the only macros in the project (coding-standards §1).
//   MELO_EXPECTS(cond)  always on: cheap preconditions of public functions.
//   MELO_ASSERT(cond)   debug builds only: internal invariants.
// A failed check prints "contract violation" with the expression and location to stderr and aborts.
// Design: docs/design/phase0-foundations.md §5.

namespace melo::detail {

/// Reports a broken precondition or invariant and terminates the process. Never returns, never
/// throws. Not real-time safe (writes to stderr), but only ever called on a bug.
[[noreturn]] void contract_violation(const char* expression, std::source_location where) noexcept;

}  // namespace melo::detail

#define MELO_EXPECTS(cond)                                                                         \
    ((cond) ? static_cast<void>(0)                                                                 \
            : ::melo::detail::contract_violation(#cond, std::source_location::current()))

#ifdef NDEBUG
#define MELO_ASSERT(cond) static_cast<void>(sizeof((cond) ? 1 : 0))
#else
#define MELO_ASSERT(cond) MELO_EXPECTS(cond)
#endif
