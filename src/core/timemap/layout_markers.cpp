#include "core/edits/effective_state.h"
#include "core/timemap/detail/anchors.h"
#include "core/timemap/layout.h"

namespace anno {

namespace {

using timemap_detail::Anchor;
using timemap_detail::k_no_note;
using timemap_detail::raw;

/// The kind of a merged anchor: by where it sits in its note.
MarkerKind kind_of(const Anchor& a, const Note& note) {
    if (a.s == raw(note.source_start)) {
        return MarkerKind::note_start;
    }
    if (a.s == raw(note.source_end)) {
        return MarkerKind::note_end;
    }
    if (a.s == raw(note.attack_end)) {
        return MarkerKind::attack_end;
    }
    return MarkerKind::handle;
}

LayoutMarker marker_of(const Anchor& a, std::span<const Note> notes) {
    LayoutMarker m;
    m.source = SourceTime::unchecked_from_origin(Seconds{a.s});
    m.output = OutputTime::unchecked_from_origin(Seconds{a.t});
    if (a.note == k_no_note) {
        m.kind = a.s == 0.0 ? MarkerKind::origin : MarkerKind::end;
        return m;
    }
    m.note = static_cast<std::size_t>(a.note);
    m.kind = kind_of(a, notes[m.note]);
    return m;
}

}  // namespace

Result<LayoutMarkers> layout_markers(std::span<const Note> notes, const EditList& edits,
                                     SourceTime source_end) {
    if (!(raw(source_end) > 0.0)) {
        return fail(ErrorCode::invalid_argument, "layout_markers: source_end must be > 0");
    }
    if (auto r = timemap_detail::check_notes(notes, edits, source_end); !r) {
        return Unexpected{r.error()};
    }
    LayoutMarkers out;
    out.repaired.assign(notes.size(), false);
    std::vector<Anchor> done{{.s = 0.0, .t = 0.0, .note = k_no_note}};
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!notes[i].is_active) {
            continue;
        }
        const EffectiveNote eff = effective_state(notes[i], edits);
        if (timemap_detail::has_timing_edit(eff)) {
            timemap_detail::append(
                done, timemap_detail::desired_anchors(static_cast<int>(i), notes[i], eff),
                out.repaired);
        }
    }
    if (raw(source_end) > done.back().s) {
        done.push_back({.s = raw(source_end), .t = raw(source_end), .note = k_no_note});
    }
    out.markers.reserve(done.size());
    for (const Anchor& a : done) {
        out.markers.push_back(marker_of(a, notes));
    }
    return out;
}

}  // namespace anno
