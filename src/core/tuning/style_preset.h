#pragma once

#include "core/types/units.h"

#include <cstdint>

// Style presets: the defaults that differ between pop, classical, jazz/blues and gamaka-based
// singing (design: docs/design/note-pitch-target.md §11). Plain data; correction policy lives here
// (layer 1) so tools may read it without linking analysis.

namespace anno {

enum class StylePreset : std::uint8_t { pop, classical, jazz_blues, gamaka };

enum class OrnamentHandling : std::uint8_t {
    detect_and_keep,  // trills / mordents are detected and corrected as a whole
    leave_alone,      // the movement is the note: detected as ornament, never corrected
};

struct StyleDefaults {
    Cents tolerance_low{-10.0};  // notes within [low, high] of the grid are left alone
    Cents tolerance_high{15.0};
    double glissando_g = 0.32;  // semitone-seconds of the glissando threshold (0.16 isolated tones)
    bool skip_consistent_off_grid = false;  // blue notes are left alone
    OrnamentHandling ornaments = OrnamentHandling::detect_and_keep;
    bool detect_gamaka = false;  // dense turning points without a level part are an ornament
    bool quantise_turning_points_only = false;
    bool provisional = true;  // values taken from the cited studies, not yet checked on data
};

/// The defaults of a preset. Only `pop` is checked against recordings in Phase 3 (`provisional`
/// false); the others carry the values of the studies named in the design note and are marked
/// provisional until the style-presets design note and listening sessions confirm them. Pure.
[[nodiscard]] StyleDefaults style_defaults(StylePreset preset) noexcept;

}  // namespace anno
