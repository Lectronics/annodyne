#pragma once

#include "core/analysis/segmentation/segmentation.h"

#include <cstddef>
#include <vector>

// The note HMM (Mauch & Dixon 2014 note tracking, with onset fusion): Viterbi over {attack(b),
// stable(b)} per pitch bin plus one silence state. Internal to core/analysis/segmentation.
// Design: docs/design/phase3-notes.md §1.

namespace anno {

struct NoteHmmInput {
    std::vector<double> smoothed_cents;  // vibrato-smoothed f0; NaN where unvoiced
    std::vector<double> voicing_prob;    // 0..1 per frame
    std::vector<double> new_note_prob;   // per frame: P(a new note starts from a stable state)
    double min_cents = 0.0;              // centre of pitch bin 0
    double bin_cents = 100.0 / 3.0;
    int bin_count = 1;
};

/// One decoded note: frames [first, last] inclusive, in pitch bin `bin`.
struct NoteRun {
    std::size_t first = 0;
    std::size_t last = 0;
    int bin = 0;
};

/// Viterbi decoding; a note starts where the path enters an attack state from anything but the same
/// attack state and lasts through its attack and stable states. Pre: the three per-frame arrays
/// have equal, non-zero length. Pure; thread-safe. Memory: frames x (2 bins + 1) ints.
[[nodiscard]] std::vector<NoteRun> decode_note_runs(const NoteHmmInput& input,
                                                    const SegmentParams& params);

}  // namespace anno
