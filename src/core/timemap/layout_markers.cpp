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

/// An unedited note's start and end at their own times, minus a start that shares its source time
/// with the marker before it (the earlier marker keeps the boundary).
void append_pins(std::vector<Anchor>& done, std::vector<bool>& pinned, int index,
                 const Note& note) {
    for (const double s : {raw(note.source_start), raw(note.source_end)}) {
        if (s > done.back().s) {
            done.push_back({.s = s, .t = s, .note = index});
            pinned.resize(done.size(), false);
            pinned.back() = true;
        }
    }
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
    std::vector<bool> pinned{false};
    std::vector<EffectiveNote> states;
    states.reserve(notes.size());
    bool any_timing_edit = false;
    for (const Note& n : notes) {
        states.push_back(effective_state(n, edits));
        any_timing_edit =
            any_timing_edit || (n.is_active && timemap_detail::has_timing_edit(states.back()));
    }
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!notes[i].is_active) {
            continue;
        }
        const auto index = static_cast<int>(i);
        if (!timemap_detail::has_timing_edit(states[i])) {
            if (any_timing_edit) {
                append_pins(done, pinned, index, notes[i]);
            }
            continue;
        }
        const auto wanted = timemap_detail::desired_anchors(index, notes[i], states[i]);
        if (pinned.back() && done.back().s == wanted.front().s) {
            done.pop_back();  // the edited note owns the shared boundary
            pinned.pop_back();
        }
        timemap_detail::append(done, wanted, out.repaired);
        pinned.resize(done.size(), false);
    }
    if (raw(source_end) > done.back().s) {
        done.push_back({.s = raw(source_end), .t = raw(source_end), .note = k_no_note});
        pinned.resize(done.size(), false);
    }
    out.markers.reserve(done.size());
    for (std::size_t k = 0; k < done.size(); ++k) {
        out.markers.push_back(marker_of(done[k], notes));
        out.markers.back().pinned = pinned[k];
    }
    return out;
}

}  // namespace anno
