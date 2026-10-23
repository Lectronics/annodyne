#pragma once

#include "core/types/tracks.h"

#include <cstdint>
#include <initializer_list>

namespace anno {

/// A set of frame classes: canonical (no order, no duplicates), so equal sets give equal cache
/// keys.
class FrameClassSet {
public:
    constexpr FrameClassSet() noexcept = default;
    constexpr FrameClassSet(std::initializer_list<FrameClass> classes) noexcept {
        for (const FrameClass c : classes) {
            bits_ |= std::uint32_t{1} << static_cast<unsigned>(c);
        }
    }
    [[nodiscard]] constexpr bool contains(FrameClass c) const noexcept {
        return (bits_ >> static_cast<unsigned>(c) & 1U) != 0U;
    }
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return bits_; }
    friend constexpr bool operator==(FrameClassSet, FrameClassSet) noexcept = default;

private:
    std::uint32_t bits_ = 0;
};

}  // namespace anno
