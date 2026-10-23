#pragma once

#include "core/edits/edit_list.h"
#include "core/types/analysis_result.h"
#include "core/types/ids.h"
#include "core/types/segments.h"
#include "core/types/units.h"

#include <optional>
#include <span>
#include <vector>

// Re-attaching edits after re-analysis (architecture spec §5.7, §4.4).
// Design: docs/design/phase1-data-model.md.

namespace anno {

enum class RemapOutcome {
    kept,         // one new note continues the old one
    split,        // several new notes continue it; all get its edits
    merged_kept,  // merged into a new note, and this was the longest parent: its edits carry over
    merged_away,  // merged into a new note, but another parent was longer: its edits are orphaned
    orphaned      // nothing matches
};

struct RemapEntry {
    NoteId old_id;
    RemapOutcome outcome = RemapOutcome::orphaned;
    std::vector<NoteId> new_ids;  // empty for merged_away and orphaned
};

/// Thresholds on source-span overlap. Untuned guesses until Phase 7 has real re-analysis data.
struct MatchParams {
    Ratio keep_overlap{0.8};  // overlap / max(len old, len new) to call it the same note; also the
                              // share of the old note the children of a split must cover
    Ratio child_coverage{0.5};  // share of a new note (split child) or old note (merge) overlapped
};

/// One entry per active old note, in `old_notes` order.
struct NoteMatching {
    std::vector<RemapEntry> entries;

    /// old id -> new ids, for EditList::retargeted.
    [[nodiscard]] NoteIdMap id_map() const;
};

/// Geometry only: matches notes by overlap of their source spans, in this order of precedence
/// per old note: keep, split, merge, orphan (see design note). If several old notes would map to
/// the same single new note, the longest keeps it (ties: earliest) and the others are merged_away.
/// Inactive notes are ignored on both sides. Pure; thread-safe; not real-time safe.
[[nodiscard]] NoteMatching match_notes(std::span<const Note> old_notes,
                                       std::span<const Note> new_notes, const MatchParams& params);

/// What became of a gap that an edit targets.
struct GapRemapEntry {
    SegmentId old_gap;
    std::optional<SegmentId> new_gap;  // empty: no continuation, the edits are orphaned
};

struct RemapResult {
    EditList edits;                  // same history, targets rewritten; nothing dropped
    std::vector<RemapEntry> report;  // one entry per old note id that any edit targets, by id
    std::vector<GapRemapEntry> gap_report;  // one entry per old gap id that any edit targets
};

/// The gap lineage (ADR-0024): an old gap follows the new gap whose neighbours are the images of
/// its own: the last child of its `before` note, the first child of its `after` note (an absent
/// side stays absent: a leading or trailing gap stays at the recording's edge). A gap is left out
/// (orphaned) when a present neighbour has no image in `notes`, or the new timeline has no gap
/// between the two images (they were merged, or another note lies between them). The children of a
/// note are in time order, as NoteMatching::id_map gives them. Pure; thread-safe; not real-time
/// safe.
[[nodiscard]] GapIdMap gap_id_map(const SegmentTimeline& old_timeline,
                                  const SegmentTimeline& new_timeline, const NoteIdMap& notes);

/// match_notes + gap_id_map + EditList::retargeted + a report. Gap edits follow the gaps when both
/// analyses have a timeline; if either has none, every gap edit is orphaned (never dropped). Ids
/// that edits target but old_analysis does not contain, or contains only as an inactive note, are
/// reported as orphaned. Errors: invalid_argument if the analyses have different source_ids or the
/// old notes are on another track than the edit list. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<RemapResult> remap_after_reanalysis(const EditList& edits,
                                                         const AnalysisResult& old_analysis,
                                                         const AnalysisResult& new_analysis,
                                                         const MatchParams& params);

}  // namespace anno
