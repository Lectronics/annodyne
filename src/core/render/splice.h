#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/audio_source.h"
#include "core/types/edit.h"
#include "core/types/result.h"
#include "core/types/segments.h"
#include "core/types/time_map.h"
#include "core/types/units.h"

// The joins of a layout's cuts and inserts (ADR-0025; design: docs/design/phase5-layout.md,
// "Rendering").

namespace anno {

struct SpliceParams {
    Seconds splice_fade{0.005};  // crossfade at a cut, fade to and from zero at an insert; under
                                 // half a sample keeps the hard joins
    FadeShape shape = FadeShape::equal_power;
};

/// Smooths the joins of `map`'s cut and insert spans in `block`, the output samples
/// [begin, begin + block.frame_count()) as read_through_map and stretch_span left them.
///  - Cut: over the `splice_fade` samples after the join the output moves from the source continued
///    into the cut (still silence: the solver keeps a cut a margin away from every region edge) to
///    the audio after the cut. A cut shorter than the fade fades over its own length (at least one
///    sample).
///  - Insert (digital zeros) inside a gap: the audio on each side fades to and from zero over
///    `splice_fade` samples, at most as far as the silence region reaches. Inserts without a gap
///    (a note at the start of the recording pushed later) have no join to smooth.
/// Only samples within `splice_fade` of a join change, and a sample is a function of the map and
/// the source alone, so any block split gives the same samples. `timeline` may be null (inserts
/// then keep their hard edges). Errors: out_of_range if a continuation would read outside the
/// recording; internal on a channel or rate mismatch. Pure; thread-safe; not real-time safe
/// (allocates).
[[nodiscard]] Result<void> apply_splices(const AudioSource& source, const TimeMap& map,
                                         const SegmentTimeline* timeline, std::int64_t begin,
                                         AudioBuffer& block, const SpliceParams& params);

}  // namespace anno
