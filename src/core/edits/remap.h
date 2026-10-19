#pragma once

#include "core/edits/edit_list.h"
#include "core/types/analysis_result.h"
#include "core/types/ids.h"
#include "core/types/units.h"

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
    Ratio keep_overlap{0.8};     // overlap / max(len old, len new) to call it the same note; also the
                                 // share of the old note the children of a split must cover
    Ratio child_coverage{0.5};   // share of a new note (split child) or old note (merge) overlapped
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

struct RemapResult {
    EditList edits;                  // same history, targets rewritten; nothing dropped
    std::vector<RemapEntry> report;  // one entry per old note id that any edit targets, by id
};

/// match_notes + EditList::retargeted + a report. Ids that edits target but old_analysis does not
/// contain are reported as orphaned. Errors: invalid_argument if the analyses have different
/// source_ids. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<RemapResult> remap_after_reanalysis(const EditList& edits,
                                                         const AnalysisResult& old_analysis,
                                                         const AnalysisResult& new_analysis,
                                                         const MatchParams& params);

}  // namespace anno
