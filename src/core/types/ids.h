#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
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

/// What a segment of a track's source time is (ADR-0008). Notes exist from Phase 1; gaps arrive
/// with gap analysis in Phase 5.
enum class SegmentKind { note, gap };

/// Identity of a segment: a note or a gap. The kind is part of the identity, so a gap id never
/// equals a note id with the same text. Compare for equality/order only; the text format is not
/// part of the contract.
struct SegmentId {
    SegmentKind kind = SegmentKind::note;
    std::string value;

    friend auto operator<=>(const SegmentId&, const SegmentId&) = default;
};

/// The segment id of a note.
[[nodiscard]] inline SegmentId segment_id(const NoteId& id) {
    return SegmentId{.kind = SegmentKind::note, .value = id.value};
}

/// The note id of a note-kind segment id; empty for a gap.
[[nodiscard]] inline std::optional<NoteId> as_note_id(const SegmentId& id) {
    if (id.kind != SegmentKind::note) {
        return std::nullopt;
    }
    return NoteId{id.value};
}

/// Stable identity of a recording's content. Compare for equality only; the text format is not
/// part of the contract.
struct SourceId {
    std::string value;

    friend auto operator<=>(const SourceId&, const SourceId&) = default;
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

/// Re-analysis lineage of gaps (ADR-0024): old gap id -> the one new gap that continues it. A gap
/// absent from the map has no continuation (its edits become orphans). Built by gap_id_map.
using GapIdMap = std::map<SegmentId, SegmentId>;

}  // namespace anno
