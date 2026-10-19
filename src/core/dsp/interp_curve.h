#pragma once

#include "core/types/frame_grid.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// Frame-curve interpolation (design: docs/design/phase3-dsp.md).

namespace anno {

enum class InterpKind { linear, cubic, hold };

/// Value of a frame curve at arbitrary source times. The unit of the output is the unit of the
/// input (the function is unit-agnostic: cents in, cents out). `values` has grid.n_frames entries;
/// frame i is at grid.start + i * grid.hop. Outside the grid the nearest frame value is held.
/// linear interpolates between the two neighbouring frames, cubic is the 4-point Lagrange
/// interpolation (exact for polynomials up to degree 3; at the ends it uses the nearest interior
/// four frames), hold is the value of the last frame at or before the time. A NaN value gives NaN
/// wherever it contributes (the caller fills unvoiced gaps first). An empty curve gives NaN
/// everywhere. Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<double> interp_curve(std::span<const double> values,
                                               const FrameGrid& grid,
                                               std::span<const SourceTime> times, InterpKind kind);

}  // namespace anno
