#include "tools/time/time_tool.h"

#include <cmath>

namespace anno {

namespace {

constexpr const char* k_origin = "time_tool";

Result<const Note*> active_note(const AnalysisResult& analysis, const NoteId& id,
                                const char* what) {
    const Note* note = find_note(analysis, id);
    if (note == nullptr || !note->is_active) {
        return fail(ErrorCode::invalid_argument, std::string{what} + ": unknown or inactive note");
    }
    return note;
}

double raw(SourceTime t) {
    return (t - SourceTime{}).value();
}

Result<EditSpec> checked(EditSpec spec) {
    if (auto ok = validate_for_add(spec); !ok) {
        return Unexpected{ok.error()};
    }
    return spec;
}

EditSpec time_spec(const NoteId& note, EditParam param, EditOp op, EditValue value) {
    return {.origin = k_origin,
            .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
            .category = EditCategory::time,
            .param = param,
            .op = op,
            .value = value};
}

}  // namespace

Result<EditSpec> note_move_edit(const AnalysisResult& analysis, const NoteMove& move) {
    if (auto note = active_note(analysis, move.note, "note_move_edit"); !note) {
        return Unexpected{note.error()};
    }
    if (!std::isfinite(move.shift.value())) {
        return fail(ErrorCode::invalid_argument, "note_move_edit: the shift is not finite");
    }
    return checked(time_spec(move.note, EditParam::onset_shift, EditOp::set, move.shift));
}

Result<EditSpec> note_length_edit(const AnalysisResult& analysis, const NoteLength& length) {
    auto note = active_note(analysis, length.note, "note_length_edit");
    if (!note) {
        return Unexpected{note.error()};
    }
    if (!std::isfinite(length.body_length.value()) || length.body_length.value() <= 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "note_length_edit: the body length must be finite and > 0");
    }
    const double body = raw((*note)->source_end) - raw((*note)->attack_end);
    if (!((*note)->attack_end < (*note)->source_end)) {
        return fail(ErrorCode::invalid_argument, "note_length_edit: the note has no body");
    }
    const Ratio scale{length.body_length.value() / body};
    if (scale < k_min_time_scale) {
        return fail(ErrorCode::out_of_range,
                    "note_length_edit: the body would shrink below the smallest time scale");
    }
    return checked(time_spec(length.note, EditParam::length_scale, EditOp::set, scale));
}

Result<EditSpec> note_handle_edit(const AnalysisResult& analysis, const NoteHandle& handle) {
    auto note = active_note(analysis, handle.note, "note_handle_edit");
    if (!note) {
        return Unexpected{note.error()};
    }
    const double source = raw(handle.handle.source);
    const double output = (handle.handle.output - OutputTime{}).value();
    if (!std::isfinite(source) || !std::isfinite(output) || output < 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "note_handle_edit: the handle must be finite with an output time >= 0");
    }
    const double key = k_handle_key.value();
    if (source <= raw((*note)->attack_end) + key || source >= raw((*note)->source_end) - key) {
        return fail(ErrorCode::invalid_argument,
                    "note_handle_edit: the handle's source time must lie inside the note's body");
    }
    return checked(time_spec(handle.note, EditParam::time_handle, EditOp::set, handle.handle));
}

Result<EditSpec> note_handle_remove_edit(const AnalysisResult& analysis,
                                         const NoteHandleRemoval& removal) {
    if (auto note = active_note(analysis, removal.note, "note_handle_remove_edit"); !note) {
        return Unexpected{note.error()};
    }
    if (!std::isfinite(raw(removal.source))) {
        return fail(ErrorCode::invalid_argument,
                    "note_handle_remove_edit: the source time is not finite");
    }
    return checked(time_spec(removal.note, EditParam::time_handle, EditOp::reset,
                             TimePair{.source = removal.source, .output = OutputTime{}}));
}

}  // namespace anno
