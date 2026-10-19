#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/time_map.h"
#include "core/types/units.h"

// read_through_map: what "the original" is on the output axis when time has moved (ADR-0008).
// Design: docs/design/phase2-addendum-timemap-spans.md §3.

namespace anno {

/// Output samples [begin, begin + count) of `source` as played through `map` (output axis, at the
/// source's sample rate):
///  - a slope-1 warp gives source samples, shifted by a whole number of samples, copied bit for
///  bit;
///  - an insert gives zeros (digital silence);
///  - a cut has no output and contributes nothing.
/// Span boundaries are snapped to samples with Rounding::nearest on the output axis. A warp's shift
/// d = (output_start - source_start) * rate must be a whole number of samples (within 1e-6 sample)
/// and its output length must equal its source length (within 1e-9 s): output sample i reads source
/// sample i - d. Only spans meeting the range are examined.
/// Errors: not_implemented (a span meeting the range is a warp with slope != 1 or a fractional
/// shift; Phase 5's layout solver must produce whole-sample shifts, stretching is Phase 6);
/// out_of_range (the range leaves [0, output frames], or a warp would read outside the recording);
/// invalid_argument (map.source_length() differs from the source's duration by more than half a
/// sample, or begin/count is negative).
/// Pure; thread-safe; not real-time safe (allocates the buffer).
[[nodiscard]] Result<AudioBuffer> read_through_map(const AudioSource& source, const TimeMap& map,
                                                   Samples begin, Samples count);

}  // namespace anno
