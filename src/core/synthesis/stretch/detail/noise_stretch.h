#pragma once

#include "core/synthesis/grain/signal.h"
#include "core/synthesis/stretch/detail/geometry.h"
#include "core/synthesis/stretch/stretch.h"

#include <cstdint>
#include <vector>

namespace anno::stretch_detail {

/// Sine-window grains (power complementary at 50 % overlap) of the source read at the source time
/// of each grain centre plus a seeded jitter, summed over the output samples [begin, end). The
/// grain centres lie on the lattice out_start + k * hop for every integer k, and the jitter of
/// grain k depends only on (seed, span first sample, k), so the samples are the same whatever range
/// is asked for. A silent source gives exact zeros. Pure; thread-safe; not real-time safe.
[[nodiscard]] std::vector<double> synthesise_noise(const grain::Signal& x,
                                                   const SpanGeometry& geometry, std::int64_t begin,
                                                   std::int64_t end, const StretchParams& params);

}  // namespace anno::stretch_detail
