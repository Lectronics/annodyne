#pragma once

#include "core/analysis/segmentation/segmentation.h"
#include "core/dsp/mono.h"
#include "core/types/frame_grid.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Per-note loudness (architecture spec §4.2 measure_loudness; implementation guide B13; ADR-0010).
// Design: docs/design/phase3-notes.md §3.

namespace anno {

struct LoudnessParams {
    Seconds amp_window{
        0.020};  // short-term RMS window of the amp curve, centred on each grid frame
    Decibels absolute_gate{-70.0};  // BS.1770 absolute gate (LUFS)
    Decibels amp_floor{-120.0};     // amp curve floor (dBFS)
    Seconds block{0.400};           // longest block the gate is applied to
};

struct NoteLoudness {
    LoudnessUnits loudness;     // K-weighted LUFS over [attack_end, source_end]
    std::vector<Decibels> amp;  // dBFS RMS of the UNWEIGHTED signal per grid frame
};

/// `plain` is the unweighted mix (`read_mono`), `k_weighted` its `k_weight()` output (computed once
/// per source): same rate and length. loudness is integrated over [attack_end, source_end] of the
/// note: -0.691 + 10 log10(mean square of k_weighted), with the BS.1770 absolute gate applied per
/// block of at most `block` (one block for a shorter span): blocks below the gate are ignored, and
/// if every block is gated out the value is the gate itself (-70 LUFS, "silent"). There is no
/// relative gate and no step at 400 ms: the note is the programme. Per implementation guide B13,
/// sibilant and breath exclusion needs the noise track (Phase 4); until then the span is used
/// whole. amp[i] = 10 log10(mean square of `plain` over amp_window centred on frame i of `grid`),
/// floored at amp_floor. Errors: invalid_argument (signals differ in rate or length, bad params,
/// note times outside the signal). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<NoteLoudness> measure_loudness(const MonoAudio& plain,
                                                    const MonoAudio& k_weighted,
                                                    const NoteSpan& span, const FrameGrid& grid,
                                                    const LoudnessParams& params);

}  // namespace anno
