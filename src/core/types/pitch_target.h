#pragma once

#include "core/types/result.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <cstdint>
#include <optional>
#include <vector>

// The pitch a note is judged by (ADR-0013; design: docs/design/note-pitch-target.md). Plain data,
// no behaviour except the flag helpers. All times are SourceTime; pitch is absolute cents (A4 =
// 6900).

namespace anno {

enum class PitchTargetKind : std::uint8_t {
    whole,      // one level segment: weighted average of the voiced note
    settled,    // approach and/or release around a level core: the core
    glide,      // moving only: end target in `target`, start in `secondary`
    multi,      // two or more plateaus: left alone, a split is suggested
    ornament,   // trill, mordent, turn, gamaka: main note in `target`, other pitch in `secondary`
    unpitched,  // no usable pitch: `target` is the note's pitch_center and means "none"
};

enum class TonalShape : std::uint8_t { level, moving };
enum class ApproachKind : std::uint8_t { none, legato, scoop, overshoot };
enum class ReleaseKind : std::uint8_t { none, fall, rise, preparation };

enum class PitchFlag : std::uint16_t {
    short_note = 1,
    run = 2,
    creak = 4,
    register_break = 8,
    consistent_off_grid = 16,
    expressive_peak = 32,
    ambiguous_intent = 64,
};

/// Set of PitchFlag in 16 bits (no allocation in a Note).
struct PitchFlags {
    std::uint16_t bits = 0;

    [[nodiscard]] constexpr bool has(PitchFlag f) const noexcept {
        return (static_cast<unsigned>(bits) & static_cast<unsigned>(f)) != 0U;
    }
    constexpr void set(PitchFlag f) noexcept {
        bits = static_cast<std::uint16_t>(static_cast<unsigned>(bits) | static_cast<unsigned>(f));
    }
    constexpr void clear(PitchFlag f) noexcept {
        bits = static_cast<std::uint16_t>(static_cast<unsigned>(bits) & ~static_cast<unsigned>(f));
    }
    friend constexpr bool operator==(PitchFlags, PitchFlags) noexcept = default;
};

/// One stretch of the smoothed pitch curve: level (one pitch) or moving (start and end differ).
struct TonalSegment {
    SourceRange range;
    TonalShape shape = TonalShape::level;
    Cents start_pitch;
    Cents end_pitch;
};

struct PitchTarget {
    PitchTargetKind kind = PitchTargetKind::unpitched;
    Cents target;                    // always finite (see PitchTargetKind)
    std::optional<Cents> secondary;  // glide start, or an ornament's other pitch
    SourceRange core;                // frames used for the target; empty = not set
    ApproachKind approach = ApproachKind::none;
    ReleaseKind release = ReleaseKind::none;
    std::vector<TonalSegment> segments;  // sorted, non-overlapping, inside the note
    PitchFlags flags;
    double confidence_prob = 0.0;  // 0..1
    // The targets each override mode would give, so a tool can switch mode without re-analysis.
    std::optional<Cents> target_whole;
    std::optional<Cents> target_settled;
    std::optional<Cents> target_end;
};

/// A whole-note target at `pitch` with full confidence, for hand-built notes (fixtures, scenarios).
[[nodiscard]] inline PitchTarget make_whole_target(Cents pitch) {
    PitchTarget t;
    t.kind = PitchTargetKind::whole;
    t.target = pitch;
    t.confidence_prob = 1.0;
    return t;
}

}  // namespace anno
