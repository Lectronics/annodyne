#pragma once

#include "core/types/effective_note.h"
#include "core/types/result.h"
#include "core/types/time_map.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Pieces the pitch engines (harmonic_noise, psola) share. Design: docs/design/phase4-pitch-edit.md.

namespace anno {

/// True when every effective value except `center` and `onset_shift` is neutral (the only edits the
/// Phase 4 engines realise).
[[nodiscard]] bool only_pitch_edited(const EffectiveNote& e);

/// Whole samples d such that output sample n plays source sample n - d over the whole zone.
/// Errors: not_implemented (messages start with `engine`) when the zone is not inside one slope-1
/// warp span of the map with a whole-sample shift. Pure; thread-safe.
[[nodiscard]] Result<std::int64_t> zone_shift(const TimeMap& map, Hz rate, OutputRange zone,
                                              std::string_view engine);

/// Lower-case hex of a 64-bit pattern, 16 digits (for locale-free cache keys).
[[nodiscard]] std::string hex64(std::uint64_t v);

/// An analysis frame as the weight builder sees it: its centre on the zone's sample axis and
/// whether its class is shifted.
struct RunFrame {
    double position = 0.0;
    bool shifted = false;
};

/// Weight of the edit, 0..1, for the zone samples [first, first + count): 1 on runs of shifted
/// frames, 0 elsewhere, with raised-cosine edges of `fade_samples` at the run boundaries. A run
/// ends half a hop beyond its last frame. The first and last entries of `frames` carry no edge on
/// their outer side (they are the held copies beyond the zone; the renderer fades the note edge).
/// Pre: frames is not empty, positions ascend, fade_samples >= 1, hop_samples > 0.
[[nodiscard]] std::vector<double> run_weights(std::span<const RunFrame> frames, double hop_samples,
                                              double fade_samples, std::int64_t first,
                                              std::int64_t count);

}  // namespace anno
