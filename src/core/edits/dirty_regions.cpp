#include "core/edits/dirty_regions.h"

#include "core/edits/effective_state.h"

#include <algorithm>

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

}  // namespace

Result<std::vector<SourceRange>> dirty_regions(std::span<const Note> notes,
                                               const EditList& old_edits, const EditList& new_edits,
                                               const DirtyParams& params) {
    if (auto r = check_inputs(notes, old_edits, new_edits); !r) {
        return Unexpected{r.error()};
    }
    std::vector<Span> spans;
    SourceTime previous_end;  // end of the previous active note; the origin for the first
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
            spans.push_back({.start = clamp_at_origin(previous_end - params.crossfade_margin),
                             .end = std::max(params.source_end, note.source_end)});
        }
        previous_end = note.source_end;
    }
    return merged(std::move(spans));
}

}  // namespace anno
