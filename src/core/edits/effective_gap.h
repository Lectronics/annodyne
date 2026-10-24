#pragma once

#include "core/edits/edit_list.h"
#include "core/types/effective_gap.h"

// effective_gap: the fold of a gap's edits (ADR-0024), as effective_state is for a note.
// Design: docs/design/phase5-gaps-core.md.

namespace anno {

/// Folds every active edit of `edits` that targets `gap` (in seq order) onto a neutral 0 dB, with
/// the same rules as effective_state (set replaces, add sums, reset returns to neutral; the
/// shared fold in edits/detail/fold). The result is clamped once, after the whole fold, to
/// [k_gap_mute_db, +24 dB], float dust within k_neutral_snap_offset of 0 dB is snapped to exactly
/// 0, and is_edited is true iff the final gain is not 0 dB. So set -120 dB then add +6 dB gives
/// -114 dB: lifted, not muted. A gain at or below k_gap_mute_db means exactly silent.
/// Orphaned target ids and the redo tail are ignored.
/// Precondition: gap.kind == SegmentKind::gap (ANNO_EXPECTS). Pure; thread-safe; not real-time
/// safe. Cost: O(active edits x targets per edit).
[[nodiscard]] EffectiveGap effective_gap(const SegmentId& gap, const EditList& edits);

}  // namespace anno
