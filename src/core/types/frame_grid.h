#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

#include <cstdint>
#include <vector>

// FrameGrid and FrameMatrix: the shared analysis grid of the frame-based tracks (spec §3).
// Design: docs/design/phase1-data-model.md.

namespace anno {

/// Regular grid of analysis frames on the source axis. Frame i is at start + i * hop.
struct FrameGrid {
    Seconds hop{0.005};         // > 0
    SourceTime start;           // source time of frame 0
    std::int64_t n_frames = 0;  // >= 0

    friend bool operator==(const FrameGrid&, const FrameGrid&) = default;
};

/// Source time of frame `index`. Pre: 0 <= index < grid.n_frames. Pure; real-time safe.
[[nodiscard]] SourceTime frame_time(const FrameGrid& grid, std::int64_t index) noexcept;

/// Errors: invalid_argument if hop is not finite and > 0, or n_frames < 0. Pure; thread-safe.
[[nodiscard]] Result<void> validate(const FrameGrid& grid);

/// Row-major frames x bins matrix of floats. The unit of the values is given by the field that
/// holds the matrix (e.g. HarmonicTrack::freq_hz).
struct FrameMatrix {
    std::int64_t rows = 0;  // frames
    std::int64_t cols = 0;  // bins
    std::vector<float> data;

    friend bool operator==(const FrameMatrix&, const FrameMatrix&) = default;
};

/// Errors: invalid_argument if rows or cols < 0 or data.size() != rows * cols.
[[nodiscard]] Result<void> validate(const FrameMatrix& matrix);

}  // namespace anno
