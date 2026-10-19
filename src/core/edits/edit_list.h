#pragma once

#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"

#include <cstddef>
#include <span>
#include <vector>

// EditList: the append-only record of one track's edits (architecture spec §4.4, §5.1; ADR-0005).
// Design: docs/design/phase1-data-model.md.

namespace anno {

/// The pieces of an EditList, for persistence (Phase 7) and tests. See EditList::from_parts.
struct EditListParts {
    TrackId track;
    std::vector<Edit> edits;  // active prefix [0, cursor) plus the redo tail
    std::size_t cursor = 0;
    std::vector<Edit> discarded;  // superseded branches
    EditSeq next_seq{1};
};

/// Immutable value type: every operation returns a new list. Nothing is ever deleted.
///
/// Invariants (checked by from_parts, maintained by every method):
///  - `edits` and `discarded` are each strictly increasing in seq and share no seq;
///  - next_seq is greater than every seq in either;
///  - groups are contiguous runs; an edit either starts a group (group == seq) or continues the
///    previous edit's group; `cursor` lies on a group boundary and is <= edits.size();
///  - every edit passed validate(EditSpec).
/// The full history is edits + discarded ordered by seq. Folding uses active() only.
///
/// Thread safety: concurrent const access is safe. Not real-time safe (allocates).
class EditList {
public:
    explicit EditList(TrackId track);

    /// Errors: corrupt_data if an invariant above is violated.
    [[nodiscard]] static Result<EditList> from_parts(EditListParts parts);

    /// The parts this list is made of (inverse of from_parts).
    [[nodiscard]] EditListParts to_parts() const;

    [[nodiscard]] const TrackId& track_id() const noexcept { return track_; }
    /// Edits that are folded, in seq order: [0, cursor).
    [[nodiscard]] std::span<const Edit> active() const noexcept;
    /// Undone edits that redo() can bring back: [cursor, end).
    [[nodiscard]] std::span<const Edit> redo_tail() const noexcept;
    /// Edits superseded by a later add after an undo. Kept as history, never redoable.
    [[nodiscard]] std::span<const Edit> discarded() const noexcept { return discarded_; }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    [[nodiscard]] bool can_undo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool can_redo() const noexcept { return cursor_ < edits_.size(); }

    /// Appends `group` as one group (= one undo step). seq numbers come from next_seq; the group id
    /// is the first seq. If the cursor is not at the end, the redo tail moves to discarded() first.
    /// `created_at` is stored on every edit and never read by the fold.
    /// Errors: invalid_argument if `group` is empty or any spec fails validate_for_add.
    [[nodiscard]] Result<EditList> add(std::span<const EditSpec> group, WallTime created_at) const;

    /// Resets `categories` for `note_ids`: one reset edit per category (origin "reset"), one group.
    /// History is kept. Errors: invalid_argument if either list is empty or has duplicates.
    [[nodiscard]] Result<EditList> reset(std::span<const NoteId> note_ids,
                                         std::span<const EditCategory> categories,
                                         WallTime created_at) const;

    /// Moves the cursor back over the last whole group. Errors: out_of_range if !can_undo().
    [[nodiscard]] Result<EditList> undo() const;

    /// Moves the cursor forward over the next whole group. Errors: out_of_range if !can_redo().
    [[nodiscard]] Result<EditList> redo() const;

    /// Rewrites target note ids through `mapping` in every edit (active, tail, discarded). An id
    /// mapped to a non-empty vector is replaced by those ids; an id absent from the map, or mapped
    /// to nothing, moves to orphaned_note_ids; if a later mapping makes that id live again it
    /// leaves orphaned_note_ids (a target is never in both lists). Duplicates are removed; seqs,
    /// groups, cursor and next_seq are unchanged. A pure bookkeeping step: nothing is dropped.
    [[nodiscard]] EditList retargeted(const NoteIdMap& mapping) const;

private:
    EditList(TrackId track, std::vector<Edit> edits, std::size_t cursor,
             std::vector<Edit> discarded, EditSeq next_seq);

    TrackId track_;
    std::vector<Edit> edits_;  // [0, cursor_) active, [cursor_, end) redo tail
    std::size_t cursor_ = 0;
    std::vector<Edit> discarded_;
    EditSeq next_seq_{1};
};

}  // namespace anno
