#pragma once

#include "core/synthesis/stretch/detail/geometry.h"
#include "core/synthesis/stretch/stretch.h"

#include <cstdint>
#include <utility>

namespace anno::stretch_detail {

/// Output samples [wanted.first, wanted.second) (absolute indices inside the span) of the warp
/// described by `geometry`, made by Rubber Band R3 (offline, one thread, no pitch change): the
/// span's source samples [round(src_start), round(source_at(last))) (with 100 ms of the recording
/// on each side at slope 1, so the stretcher's own start-up ramp lies outside the span) are
/// stretched to exactly `geometry.last - geometry.first` samples, all channels. Over `edge_zone` at
/// each end the result is blended (equal power) into an exact slope-1 copy of the source that
/// continues the neighbouring audio, so the span meets its neighbours on the source's own samples.
/// A span of fewer than `k_min_rubberband_input` source samples is resampled linearly instead. The
/// whole span is rendered whatever `wanted` is (the samples are then independent of the range asked
/// for). Errors: whatever reading the source returns; internal if Rubber Band reports no output.
/// Pure; thread-safe (one stretcher per call); not real-time safe.
[[nodiscard]] Result<OutputAudio> rubberband_span(const AudioSource& src,
                                                  const SpanGeometry& geometry,
                                                  std::pair<std::int64_t, std::int64_t> wanted,
                                                  OutputTime start);

inline constexpr std::int64_t k_min_rubberband_input = 2048;  // below this, a linear resample
inline constexpr double k_rubberband_edge_zone_s = 0.030;     // blend into the source copy

inline constexpr double k_rubberband_context_s = 0.100;  // source fed beyond each end, at slope 1

}  // namespace anno::stretch_detail
