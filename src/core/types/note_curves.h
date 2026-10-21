#pragma once

#include "core/types/frame_grid.h"
#include "core/types/units.h"

#include <span>

// NoteCurves: what the Phase 4 analysis stages (partials, harmonic/noise, envelope) read of a note.
// Design: docs/design/phase4-pitch-edit.md.

namespace anno {

/// What the analysis stages read of a note (ADR-0010 style: no half-built Note). The three curves
/// are the ones Phase 3 stores in Note: f0(i) = pitch_center + pitch_drift[i] + pitch_mod[i], all
/// in absolute cents; a non-finite sum marks an unvoiced frame. The spans must outlive the call.
struct NoteCurves {
    FrameGrid grid;  // source axis
    Cents pitch_center;
    std::span<const Cents> pitch_drift;  // grid.n_frames long
    std::span<const Cents> pitch_mod;    // grid.n_frames long
};

}  // namespace anno
