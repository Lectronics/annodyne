#pragma once

#include <cstddef>

// Index of a non-negative int (vectors indexed by lag, bin or frame). One definition for the
// analysis code, so the cast is written once.

namespace anno {

[[nodiscard]] constexpr std::size_t idx(int i) noexcept {
    return static_cast<std::size_t>(i);
}

}  // namespace anno
