#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Identifiers (architecture spec §3, §5.1; ADR-0005).
// Design: docs/design/phase1-data-model.md.

namespace anno {

/// Stable identity of a note across re-analysis where possible. Compare for equality/order only;
/// the text format is not part of the contract.
struct NoteId {
    std::string value;

    friend auto operator<=>(const NoteId&, const NoteId&) = default;
};

/// Identity of a track. One EditList and one TimeMap exist per track.
struct TrackId {
    std::string value;

    friend auto operator<=>(const TrackId&, const TrackId&) = default;
};

/// Sequence number of a recorded edit: unique within an EditList's history, never reused, strictly
/// increasing in the order edits were added. It is the edit's identity (ADR-0005 §1).
struct EditSeq {
    std::uint64_t value = 0;

    friend auto operator<=>(const EditSeq&, const EditSeq&) = default;
};

/// One user gesture or macro run = one group = one undo step. Equal to the EditSeq of the group's
/// first edit.
struct GroupId {
    std::uint64_t value = 0;

    friend auto operator<=>(const GroupId&, const GroupId&) = default;
};

/// Re-analysis lineage: old note id -> the new note ids that continue it. An empty vector means
/// the old note has no continuation (its edits become orphans). Ordered, so iteration is
/// deterministic.
using NoteIdMap = std::map<NoteId, std::vector<NoteId>>;

}  // namespace anno
