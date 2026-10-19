#include "core/types/note_span.h"

#include <algorithm>
#include <cmath>

namespace anno {

FrameGrid note_grid(const NoteSpan& span, const FrameGrid& pitch_grid) {
    constexpr double k_eps = 1e-9;
    const double hop = pitch_grid.hop.value();
    const double start = (span.source_start - pitch_grid.start).value() / hop;
    const double end = (span.source_end - pitch_grid.start).value() / hop;
    const auto first =
        std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil(start - k_eps)));
    const auto stop = std::min<std::int64_t>(pitch_grid.n_frames,
                                             static_cast<std::int64_t>(std::ceil(end - k_eps)));
    return FrameGrid{
        .hop = pitch_grid.hop,
        .start = frame_time(pitch_grid,
                            std::min(first, std::max<std::int64_t>(0, pitch_grid.n_frames - 1))),
        .n_frames = std::max<std::int64_t>(0, stop - first)};
}

}  // namespace anno
