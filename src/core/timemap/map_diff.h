#pragma once

#include "core/types/result.h"
#include "core/types/time_map.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <vector>

// Local invalidation for timing edits (ADR-0023 amendment 1, ADR-0025 decision 8).

namespace anno {

/// The source ranges whose mapping to the output differs between two maps of the same recording:
/// where a span was added, removed, cut or moved by more than 1e-9 s or has another slope. Under
/// the layout a marker move changes only the two intervals next to it, so a timing edit dirties
/// those and nothing else (the Flex builder's maps differ from the first changed marker to the
/// end, as before). Each range is widened by `margin` (the blend and splice reach), clamped to the
/// recording, and the result is sorted and merged. Inserts have no source length: a change in one
/// shows as the shifted spans around it. Identical maps give an empty list.
/// Combine with dirty_regions for the pitch, level and formant changes of notes.
/// Errors: invalid_argument if the source lengths differ by more than 1e-9 s or `margin` is
/// negative. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<SourceRange>>
changed_source_ranges(const TimeMap& old_map, const TimeMap& new_map, Seconds margin);

}  // namespace anno
