#pragma once

#include "core/types/ids.h"
#include "core/types/units.h"

// The folded gap edits of one gap (ADR-0024), as EffectiveNote is for a note. The fold is
// core/edits::effective_gap (slice 3).

namespace anno {

/// A gap_gain at or below this means exactly silent (mute is a gain, not a flag).
inline constexpr Decibels k_gap_mute_db{-120.0};

struct EffectiveGap {
    SegmentId gap;
    Decibels gain{};  // folded gap_gain, in [k_gap_mute_db, +24 dB]
    bool is_edited = false;
};

}  // namespace anno
