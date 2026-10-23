#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/ids.h"
#include "core/types/time_range.h"

#include <string>
#include <vector>

// RenderResult (architecture spec §4.6, ADR-0002, ADR-0006).

namespace anno {

struct RenderResult {
    OutputAudio buffer;                    // the request snapped to whole samples at both ends
    std::string cache_id;                  // short hex of the cache key, for reports; not unique
    std::vector<NoteId> notes_rendered;    // edited notes whose blend zone meets the region, before
                                           // any bit-exact shortcut, in source order
    std::vector<SegmentId> gaps_rendered;  // edited gaps (gap_gain) whose silence regions meet the
                                           // region, in timeline order (ADR-0024)
    bool passthrough = false;  // exactly notes_rendered.empty() && gaps_rendered.empty();
                               // with `stretched` it does not mean the audio equals the source
    bool stretched = false;    // a slope != 1 warp of the map meets the region and was
                               // rendered by stretch_span (ADR-0023)
    int repaired_notes = 0;    // notes whose timing request build_time_map could not honour
                               // exactly (only when the map was built from the edits)
};

}  // namespace anno
