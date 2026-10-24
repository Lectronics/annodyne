#include "core/edits/dirty_regions.h"

#include "core/edits/effective_gap.h"
#include "core/edits/effective_state.h"

#include <algorithm>
#include <set>

namespace anno {

namespace {

bool timing_differs(const EffectiveNote& a, const EffectiveNote& b) {
    return a.onset_shift != b.onset_shift || a.length_scale != b.length_scale ||
           a.handles != b.handles || a.attack_time_scale != b.attack_time_scale;
}

struct Span {
    SourceTime start;
    SourceTime end;
};

SourceTime clamp_at_origin(SourceTime t) {
    return std::max(t, SourceTime{});
}

/// Sorts and merges overlapping or touching spans into ranges.
Result<std::vector<SourceRange>> merged(std::vector<Span> spans) {
    std::ranges::sort(spans, {}, &Span::start);
    std::vector<SourceRange> out;
    for (std::size_t i = 0; i < spans.size();) {
        Span run = spans[i];
        for (++i; i < spans.size() && spans[i].start <= run.end; ++i) {
            run.end = std::max(run.end, spans[i].end);
        }
        auto range = SourceRange::make(run.start, run.end);
        if (!range) {
            return Unexpected{range.error()};
        }
        out.push_back(*range);
    }
    return out;
}

Result<void> check_inputs(std::span<const Note> notes, const EditList& a, const EditList& b) {
    if (!(a.track_id() == b.track_id())) {
        return fail(ErrorCode::invalid_argument,
                    "dirty_regions: edit lists are for different tracks");
    }
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!(notes[i].track_id == a.track_id())) {
            return fail(ErrorCode::invalid_argument, "dirty_regions: note is on another track");
        }
        if (i > 0 && notes[i].source_start < notes[i - 1].source_start) {
            return fail(ErrorCode::invalid_argument, "dirty_regions: notes must be sorted");
        }
    }
    return {};
}

/// The gap ids that live gap-category edits of either list target.
std::set<SegmentId> targeted_gaps(const EditList& a, const EditList& b) {
    std::set<SegmentId> ids;
    for (const EditList* list : {&a, &b}) {
        for (const Edit& edit : list->active()) {
            if (edit.spec.category != EditCategory::gap) {
                continue;
            }
            for (const SegmentId& id : edit.spec.target.segment_ids) {
                if (id.kind == SegmentKind::gap) {
                    ids.insert(id);
                }
            }
        }
    }
    return ids;
}

/// The ranges of the gaps whose gain differs between the lists.
Result<std::vector<Span>> changed_gaps(const EditList& old_edits, const EditList& new_edits,
                                       const SegmentTimeline* timeline) {
    std::vector<Span> spans;
    const std::set<SegmentId> targeted = targeted_gaps(old_edits, new_edits);
    for (const SegmentId& id : targeted) {
        const bool known =
            timeline != nullptr &&
            std::ranges::any_of(timeline->gaps(), [&](const Gap& g) { return g.id == id; });
        if (!known) {
            return fail(ErrorCode::invalid_argument,
                        "dirty_regions: gap edit targets '" + id.value +
                            (timeline != nullptr ? "', which is not a gap of the segment timeline"
                                                 : "', but no segment timeline was given"));
        }
    }
    if (timeline == nullptr) {
        return spans;
    }
    for (const Gap& gap : timeline->gaps()) {
        if (targeted.contains(gap.id) &&
            effective_gap(gap.id, old_edits) != effective_gap(gap.id, new_edits)) {
            spans.push_back({.start = gap.start, .end = gap.end});
        }
    }
    return spans;
}

}  // namespace

Result<std::vector<SourceRange>> dirty_regions(std::span<const Note> notes,
                                               const EditList& old_edits, const EditList& new_edits,
                                               const DirtyParams& params) {
    if (auto r = check_inputs(notes, old_edits, new_edits); !r) {
        return Unexpected{r.error()};
    }
    std::vector<Span> spans;
    SourceTime last_end = params.source_end;  // timing changes reach the end of the recording
    for (const Note& note : notes) {
        if (note.is_active) {
            last_end = std::max(last_end, note.source_end);
        }
    }
    for (const Note& note : notes) {
        if (!note.is_active) {
            continue;
        }
        const EffectiveNote before = effective_state(note, old_edits);
        const EffectiveNote after = effective_state(note, new_edits);
        if (before != after) {
            spans.push_back({.start = clamp_at_origin(note.source_start - params.crossfade_margin),
                             .end = note.source_end + params.crossfade_margin});
        }
        if (timing_differs(before, after)) {
            spans.push_back({.start = SourceTime{}, .end = last_end});
        }
    }
    auto gaps = changed_gaps(old_edits, new_edits, params.timeline);
    if (!gaps) {
        return Unexpected{gaps.error()};
    }
    spans.insert(spans.end(), gaps->begin(), gaps->end());
    return merged(std::move(spans));
}

}  // namespace anno
