#include "core/edits/edit_list.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <string>

namespace anno {

namespace {

Unexpected corrupt(std::string message) {
    return fail(ErrorCode::corrupt_data, "edit list: " + std::move(message));
}

/// Checks seq order and the group-run rule of one partition; seqs seen are added to `seen`.
Result<void> check_partition(const std::vector<Edit>& edits, std::set<std::uint64_t>& seen) {
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const Edit& e = edits[i];
        if (i > 0 && !(e.seq > edits[i - 1].seq)) {
            return corrupt("seq is not strictly increasing");
        }
        if (!seen.insert(e.seq.value).second) {
            return corrupt("seq appears in both edits and discarded");
        }
        const bool starts_group = e.group.value == e.seq.value;
        const bool continues = i > 0 && e.group == edits[i - 1].group;
        if (!starts_group && !continues) {
            return corrupt("group id must equal the seq of its first edit");
        }
        if (continues && e.seq.value != edits[i - 1].seq.value + 1) {
            return corrupt("edits of one group must have consecutive seqs");
        }
        if (auto r = validate(e.spec); !r) {
            return corrupt("invalid edit: " + r.error().message);
        }
    }
    return {};
}

std::size_t group_start(const std::vector<Edit>& edits, std::size_t end) {
    const GroupId group = edits[end - 1].group;
    while (end > 0 && edits[end - 1].group == group) {
        --end;
    }
    return end;
}

/// Note-kind targets follow `mapping` (a note with no continuation becomes orphaned); gap targets
/// pass through unchanged (a gap id map arrives with gap analysis, Phase 5).
std::vector<SegmentId> remapped_ids(const std::vector<SegmentId>& ids, const NoteIdMap& mapping,
                                    std::vector<SegmentId>& orphaned) {
    std::vector<SegmentId> out;
    const auto push_unique = [](std::vector<SegmentId>& v, const SegmentId& id) {
        if (std::ranges::find(v, id) == v.end()) {
            v.push_back(id);
        }
    };
    for (const SegmentId& id : ids) {
        const std::optional<NoteId> note = as_note_id(id);
        if (!note) {
            push_unique(out, id);
            continue;
        }
        const auto it = mapping.find(*note);
        if (it == mapping.end() || it->second.empty()) {
            push_unique(orphaned, id);
        } else {
            for (const NoteId& next : it->second) {
                push_unique(out, segment_id(next));
            }
        }
    }
    return out;
}

void retarget_all(std::vector<Edit>& edits, const NoteIdMap& mapping) {
    for (Edit& e : edits) {
        std::vector<SegmentId> orphaned = e.spec.target.orphaned_segment_ids;
        e.spec.target.segment_ids = remapped_ids(e.spec.target.segment_ids, mapping, orphaned);
        // An id that is live again (a later re-analysis reused it) is no longer orphaned: the id is
        // the identity, and a target must not sit in both lists.
        std::erase_if(orphaned, [&](const SegmentId& id) {
            return std::ranges::find(e.spec.target.segment_ids, id) !=
                   e.spec.target.segment_ids.end();
        });
        e.spec.target.orphaned_segment_ids = std::move(orphaned);
    }
}

}  // namespace

EditList::EditList(TrackId track) : track_{std::move(track)} {}

EditList::EditList(TrackId track, std::vector<Edit> edits, std::size_t cursor,
                   std::vector<Edit> discarded, EditSeq next_seq)
    : track_{std::move(track)}, edits_{std::move(edits)}, cursor_{cursor},
      discarded_{std::move(discarded)}, next_seq_{next_seq} {
    ANNO_ASSERT(cursor_ <= edits_.size());
}

Result<EditList> EditList::from_parts(EditListParts parts) {
    if (parts.cursor > parts.edits.size()) {
        return corrupt("cursor is past the end");
    }
    std::set<std::uint64_t> seen;
    if (auto r = check_partition(parts.edits, seen); !r) {
        return Unexpected{r.error()};
    }
    if (auto r = check_partition(parts.discarded, seen); !r) {
        return Unexpected{r.error()};
    }
    if (!seen.empty() && !(*seen.rbegin() < parts.next_seq.value)) {
        return corrupt("next_seq must exceed every seq");
    }
    if (parts.cursor > 0 && parts.cursor < parts.edits.size() &&
        parts.edits[parts.cursor].group == parts.edits[parts.cursor - 1].group) {
        return corrupt("cursor splits a group");
    }
    return EditList{std::move(parts.track), std::move(parts.edits), parts.cursor,
                    std::move(parts.discarded), parts.next_seq};
}

EditListParts EditList::to_parts() const {
    return EditListParts{.track = track_,
                         .edits = edits_,
                         .cursor = cursor_,
                         .discarded = discarded_,
                         .next_seq = next_seq_};
}

std::span<const Edit> EditList::active() const noexcept {
    return std::span<const Edit>{edits_}.first(cursor_);
}

std::span<const Edit> EditList::redo_tail() const noexcept {
    return std::span<const Edit>{edits_}.subspan(cursor_);
}

Result<EditList> EditList::add(std::span<const EditSpec> group, WallTime created_at) const {
    if (group.empty()) {
        return fail(ErrorCode::invalid_argument, "an edit group must contain at least one edit");
    }
    for (const EditSpec& spec : group) {
        if (auto r = validate_for_add(spec); !r) {
            return Unexpected{r.error()};
        }
    }
    std::vector<Edit> edits(edits_.begin(), edits_.begin() + static_cast<std::ptrdiff_t>(cursor_));
    std::vector<Edit> discarded = discarded_;
    discarded.insert(discarded.end(), edits_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                     edits_.end());
    // The tail can hold older seqs than earlier branches (undo past an old branch point), and
    // groups occupy consecutive seqs, so sorting keeps both the order and the group runs.
    std::ranges::sort(discarded, {}, &Edit::seq);
    std::uint64_t seq = next_seq_.value;
    const GroupId group_id{seq};
    for (const EditSpec& spec : group) {
        edits.push_back(
            Edit{.seq = EditSeq{seq++}, .group = group_id, .created_at = created_at, .spec = spec});
    }
    const std::size_t cursor = edits.size();
    return EditList{track_, std::move(edits), cursor, std::move(discarded), EditSeq{seq}};
}

Result<EditList> EditList::reset(std::span<const NoteId> note_ids,
                                 std::span<const EditCategory> categories,
                                 WallTime created_at) const {
    if (note_ids.empty() || categories.empty()) {
        return fail(ErrorCode::invalid_argument, "reset needs at least one note and one category");
    }
    std::vector<EditSpec> specs;
    for (const EditCategory category : categories) {
        EditSpec spec;
        spec.origin = "reset";
        std::ranges::transform(note_ids, std::back_inserter(spec.target.segment_ids),
                               [](const NoteId& id) { return segment_id(id); });
        spec.category = category;
        spec.op = EditOp::reset;
        specs.push_back(std::move(spec));
    }
    return add(specs, created_at);
}

Result<EditList> EditList::undo() const {
    if (!can_undo()) {
        return fail(ErrorCode::out_of_range, "nothing to undo");
    }
    return EditList{track_, edits_, group_start(edits_, cursor_), discarded_, next_seq_};
}

Result<EditList> EditList::redo() const {
    if (!can_redo()) {
        return fail(ErrorCode::out_of_range, "nothing to redo");
    }
    const GroupId group = edits_[cursor_].group;
    std::size_t end = cursor_;
    while (end < edits_.size() && edits_[end].group == group) {
        ++end;
    }
    return EditList{track_, edits_, end, discarded_, next_seq_};
}

EditList EditList::retargeted(const NoteIdMap& mapping) const {
    std::vector<Edit> edits = edits_;
    std::vector<Edit> discarded = discarded_;
    retarget_all(edits, mapping);
    retarget_all(discarded, mapping);
    return EditList{track_, std::move(edits), cursor_, std::move(discarded), next_seq_};
}

}  // namespace anno
