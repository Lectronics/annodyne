#pragma once

#include "core/synthesis/grain/period_curve.h"
#include "core/synthesis/stretch/detail/geometry.h"
#include "core/synthesis/stretch/stretch.h"

#include <cstdint>
#include <vector>

namespace anno::stretch_detail {

/// One voiced run: output samples [begin, end) of the span, as absolute output sample indices.
struct Run {
    std::int64_t begin = 0;
    std::int64_t end = 0;
};

/// Per output sample of the span (index i = sample first + i): 1 where the note frame at the source
/// time has a class in params.voiced_classes (an empty frame_class counts as voiced) and a finite
/// pitch, else 0. Outside every active note: 0. Frames beyond the first/last hold the edge frame.
/// Errors: invalid_argument if a note's frame_class is neither empty nor n_frames long.
[[nodiscard]] Result<std::vector<std::uint8_t>> voiced_indicator(const AnalysisResult& analysis,
                                                                 const SpanGeometry& geometry,
                                                                 const StretchParams& params);

/// Maximal runs of 1 in `indicator`, as absolute output sample indices (`first` = the span's
/// first).
[[nodiscard]] std::vector<Run> voiced_runs(const std::vector<std::uint8_t>& indicator,
                                           std::int64_t first);

/// (source sample position, absolute cents) of every finite frame of the active notes whose time
/// range meets [from, to] (source samples). Sorted by position. Empty if there is none.
[[nodiscard]] std::vector<grain::CentsPoint> pitch_points(const AnalysisResult& analysis,
                                                          double rate_hz, double from, double to);

}  // namespace anno::stretch_detail
