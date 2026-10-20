#pragma once

#include "core/analysis/pitch_target/pitch_target.h"

#include <vector>

// Per-frame reliability of a note's pitch curve. Internal to core/analysis/pitch_target.

namespace anno {

struct Reliability {
    std::vector<double> weight;  // per note frame, 0..1
    bool creak = false;          // some stretch sits at half / double the note's median f0
    double median_voicing = 0.0;
};

/// r(t) = voicing probability x consonant factor x creak factor x disagreement factor, per frame of
/// `note.grid`. The consonant factor ramps 0 -> 1 over `consonant_zone` after a consonant onset.
/// Creak: a stretch of at least `creak_min_stretch` whose f0 lies within `creak_tolerance` of half
/// or double the median curve gets factor 0. `curve_cents` is the note's curve (one per frame).
/// Pure; thread-safe.
[[nodiscard]] Reliability frame_reliability(const Note& note, const NoteEvidence& evidence,
                                            const std::vector<double>& curve_cents,
                                            const ReliabilityParams& params);

}  // namespace anno
