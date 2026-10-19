#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/ids.h"
#include "core/types/time_range.h"

#include <string>
#include <vector>

// RenderResult (architecture spec §4.6, ADR-0002, ADR-0006).

namespace anno {

struct RenderResult {
    OutputAudio buffer;        // the request snapped to whole samples at both ends
    SourceRange source_range;  // with an identity TimeMap: the same samples on the source axis
    std::string cache_id;      // short hex of the cache key, for reports; not unique
    std::vector<NoteId> notes_rendered;  // edited notes whose blend zone meets the region, before
                                         // any bit-exact shortcut, in source order
    bool passthrough = false;            // exactly notes_rendered.empty()
};

}  // namespace anno
