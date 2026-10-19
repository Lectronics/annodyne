#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/note.h"

#include <string>
#include <utility>
#include <vector>

// Builders for notes and edit specs used by unit and contract tests.

namespace anno::test {

inline SourceTime src(double seconds) {
    return SourceTime::unchecked_from_origin(Seconds{seconds});
}
inline OutputTime out(double seconds) {
    return OutputTime::unchecked_from_origin(Seconds{seconds});
}

/// A valid note with no frame data: start..end seconds, attack ends 10% in, release starts at 90%.
inline Note make_note(const std::string& id, double start_s, double end_s,
                      double pitch_center_cents = 6900.0) {
    Note note;
    note.id = NoteId{id};
    note.track_id = TrackId{"t1"};
    const double len = end_s - start_s;
    note.source_start = src(start_s);
    note.attack_end = src(start_s + 0.1 * len);
    note.release_start = src(start_s + 0.9 * len);
    note.source_end = src(end_s);
    note.perceived_pitch = Cents{pitch_center_cents};
    note.pitch_center = Cents{pitch_center_cents};
    return note;
}

/// Segment ids of notes, for comparing against EditTarget.
inline std::vector<SegmentId> segments_of(const std::vector<NoteId>& notes) {
    std::vector<SegmentId> out;
    out.reserve(notes.size());
    for (const NoteId& id : notes) {
        out.push_back(segment_id(id));
    }
    return out;
}

inline EditSpec make_spec(const std::string& note_id, EditCategory category,
                          std::optional<EditParam> param, EditOp op, EditValue value) {
    EditSpec spec;
    spec.origin = "test";
    spec.target.segment_ids = {segment_id(NoteId{note_id})};
    spec.category = category;
    spec.param = param;
    spec.op = op;
    spec.value = value;
    return spec;
}

inline EditSpec pitch_add(const std::string& note_id, double cents) {
    return make_spec(note_id, EditCategory::pitch, EditParam::center_offset, EditOp::add,
                     Cents{cents});
}
inline EditSpec pitch_set(const std::string& note_id, double cents) {
    return make_spec(note_id, EditCategory::pitch, EditParam::center_offset, EditOp::set,
                     Cents{cents});
}

}  // namespace anno::test
