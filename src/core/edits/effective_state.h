#pragma once

#include "core/edits/edit_list.h"
#include "core/types/effective_note.h"
#include "core/types/note.h"

// effective_state: the fold of a note's edits onto its analysed values (architecture spec §5.3).
// Design: docs/design/phase1-data-model.md.

namespace anno {

/// Folds every active edit of `edits` that targets `note` (in seq order) onto neutral values and
/// the note's analysed pitch_center.
///  - set replaces the parameter's accumulator; add (offsets) sums; scale (multipliers) multiplies;
///    reset returns one parameter (or, with no param, the whole category) to neutral;
///  - time_handle is keyed by source time within k_handle_key;
///  - results are clamped to their legal ranges, float dust within k_neutral_snap_* of neutral is
///    snapped to exactly neutral, and is_edited is true iff any final value is not neutral.
/// Orphaned target ids and the redo tail are ignored. Precondition: note.track_id ==
/// edits.track_id() (ANNO_EXPECTS). Pure; thread-safe; not real-time safe (allocates).
/// Cost: O(active edits x targets per edit).
[[nodiscard]] EffectiveNote effective_state(const Note& note, const EditList& edits);

}  // namespace anno
