#include "core/edits/remap.h"

#include <algorithm>
#include <map>
#include <set>

namespace anno {

namespace {

Seconds length(const Note& n) {
    return n.source_end - n.source_start;
}

Seconds overlap(const Note& a, const Note& b) {
    const Seconds d =
        std::min(a.source_end, b.source_end) - std::max(a.source_start, b.source_start);
    return std::max(d, Seconds{0.0});
}

struct Candidate {
    RemapOutcome outcome = RemapOutcome::orphaned;
    std::vector<std::size_t> targets;  // indices into the active new notes
};

std::vector<const Note*> active_only(std::span<const Note> notes) {
    std::vector<const Note*> out;
    for (const Note& n : notes) {
        if (n.is_active) {
            out.push_back(&n);
        }
    }
    return out;
}

/// keep: one new note overlapping nearly as much as the longer of the two.
std::optional<std::size_t> find_keep(const Note& old_note, const std::vector<const Note*>& news,
                                     const MatchParams& p) {
    std::optional<std::size_t> best;
    double best_ov = 0.0;
    for (std::size_t i = 0; i < news.size(); ++i) {
        const double ov = overlap(old_note, *news[i]).value();
        const double longer = std::max(length(old_note), length(*news[i])).value();
        if (longer > 0.0 && ov >= p.keep_overlap.value() * longer && ov > best_ov) {
            best = i;
            best_ov = ov;
        }
    }
    return best;
}

/// split: two or more new notes mostly inside the old one that together cover most of it.
std::vector<std::size_t> find_split(const Note& old_note, const std::vector<const Note*>& news,
                                    const MatchParams& p) {
    std::vector<std::size_t> children;
    double covered = 0.0;
    for (std::size_t i = 0; i < news.size(); ++i) {
        const double ov = overlap(old_note, *news[i]).value();
        if (length(*news[i]).value() > 0.0 &&
            ov >= p.child_coverage.value() * length(*news[i]).value()) {
            children.push_back(i);
            covered += ov;
        }
    }
    const bool covers = covered >= p.keep_overlap.value() * length(old_note).value();
    return (children.size() >= 2 && covers) ? children : std::vector<std::size_t>{};
}

/// merge candidate: the new note that holds at least child_coverage of the old one.
std::optional<std::size_t> find_merge(const Note& old_note, const std::vector<const Note*>& news,
                                      const MatchParams& p) {
    std::optional<std::size_t> best;
    double best_ov = 0.0;
    for (std::size_t i = 0; i < news.size(); ++i) {
        const double ov = overlap(old_note, *news[i]).value();
        if (length(old_note).value() > 0.0 &&
            ov >= p.child_coverage.value() * length(old_note).value() && ov > best_ov) {
            best = i;
            best_ov = ov;
        }
    }
    return best;
}

Candidate classify(const Note& old_note, const std::vector<const Note*>& news,
                   const MatchParams& p) {
    if (const auto keep = find_keep(old_note, news, p)) {
        return {.outcome = RemapOutcome::kept, .targets = {*keep}};
    }
    if (auto children = find_split(old_note, news, p); !children.empty()) {
        return {.outcome = RemapOutcome::split, .targets = std::move(children)};
    }
    if (const auto merge = find_merge(old_note, news, p)) {
        return {.outcome = RemapOutcome::kept, .targets = {*merge}};
    }
    return {};
}

}  // namespace

NoteIdMap NoteMatching::id_map() const {
    NoteIdMap map;
    for (const RemapEntry& e : entries) {
        map[e.old_id] = e.new_ids;
    }
    return map;
}

NoteMatching match_notes(std::span<const Note> old_notes, std::span<const Note> new_notes,
                         const MatchParams& params) {
    const auto olds = active_only(old_notes);
    const auto news = active_only(new_notes);

    std::vector<Candidate> candidates;
    candidates.reserve(olds.size());
    for (const Note* o : olds) {
        candidates.push_back(classify(*o, news, params));
    }
    // Several old notes pointing at the same single new note: the longest keeps it.
    std::map<std::size_t, std::vector<std::size_t>> claimed;  // new index -> old indices
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].outcome == RemapOutcome::kept) {
            claimed[candidates[i].targets.front()].push_back(i);
        }
    }
    for (auto& [new_index, old_indices] : claimed) {
        if (old_indices.size() < 2) {
            continue;
        }
        std::size_t winner = old_indices.front();
        for (const std::size_t i : old_indices) {
            if (length(*olds[i]) > length(*olds[winner])) {
                winner = i;  // ties: earliest stays
            }
        }
        for (const std::size_t i : old_indices) {
            candidates[i].outcome =
                i == winner ? RemapOutcome::merged_kept : RemapOutcome::merged_away;
            if (i != winner) {
                candidates[i].targets.clear();
            }
        }
        (void)new_index;
    }

    NoteMatching matching;
    for (std::size_t i = 0; i < olds.size(); ++i) {
        RemapEntry entry{.old_id = olds[i]->id, .outcome = candidates[i].outcome, .new_ids = {}};
        for (const std::size_t t : candidates[i].targets) {
            entry.new_ids.push_back(news[t]->id);
        }
        matching.entries.push_back(std::move(entry));
    }
    return matching;
}

GapIdMap gap_id_map(const SegmentTimeline& old_timeline, const SegmentTimeline& new_timeline,
                    const NoteIdMap& notes) {
    GapIdMap out;
    for (const Gap& gap : old_timeline.gaps()) {
        std::optional<NoteId> before;
        std::optional<NoteId> after;
        if (gap.before) {
            const auto it = notes.find(*gap.before);
            if (it == notes.end() || it->second.empty()) {
                continue;
            }
            before = it->second.back();
        }
        if (gap.after) {
            const auto it = notes.find(*gap.after);
            if (it == notes.end() || it->second.empty()) {
                continue;
            }
            after = it->second.front();
        }
        const SegmentId id = gap_segment_id(before, after);
        if (std::ranges::find(new_timeline.gaps(), id, &Gap::id) != new_timeline.gaps().end()) {
            out.emplace(gap.id, id);
        }
    }
    return out;
}

Result<RemapResult> remap_after_reanalysis(const EditList& edits,
                                           const AnalysisResult& old_analysis,
                                           const AnalysisResult& new_analysis,
                                           const MatchParams& params) {
    if (!(old_analysis.source_id == new_analysis.source_id)) {
        return fail(ErrorCode::invalid_argument, "re-analysis must be of the same source");
    }
    for (const Note& n : old_analysis.notes) {
        if (!(n.track_id == edits.track_id())) {
            return fail(ErrorCode::invalid_argument,
                        "remap_after_reanalysis: the edit list belongs to another track");
        }
    }
    const NoteMatching matching = match_notes(old_analysis.notes, new_analysis.notes, params);
    const NoteIdMap map = matching.id_map();

    GapIdMap gaps;  // empty without both timelines: every gap edit is orphaned
    if (old_analysis.timeline && new_analysis.timeline) {
        gaps = gap_id_map(*old_analysis.timeline, *new_analysis.timeline, map);
    }

    std::set<NoteId> targeted;
    std::set<SegmentId> targeted_gaps;
    const auto collect = [&](std::span<const Edit> list) {
        for (const Edit& e : list) {
            for (const SegmentId& id : e.spec.target.segment_ids) {
                if (const auto note = as_note_id(id)) {
                    targeted.insert(*note);
                } else if (id.kind == SegmentKind::gap) {
                    targeted_gaps.insert(id);
                }
            }
        }
    };
    const EditListParts parts = edits.to_parts();
    collect(parts.edits);
    collect(parts.discarded);

    RemapResult result{.edits = edits.retargeted(map, gaps), .report = {}, .gap_report = {}};
    for (const NoteId& id : targeted) {
        const auto it = std::ranges::find(matching.entries, id, &RemapEntry::old_id);
        result.report.push_back(
            it != matching.entries.end()
                ? *it
                : RemapEntry{.old_id = id, .outcome = RemapOutcome::orphaned, .new_ids = {}});
    }
    for (const SegmentId& id : targeted_gaps) {
        const auto it = gaps.find(id);
        result.gap_report.push_back(GapRemapEntry{
            .old_gap = id,
            .new_gap = it != gaps.end() ? std::optional<SegmentId>{it->second} : std::nullopt});
    }
    return result;
}

}  // namespace anno
