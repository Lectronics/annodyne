#include "tools/amplitude/amplitude.h"

#include <cmath>

namespace anno {

namespace {

Result<void> check_note(const AnalysisResult& analysis, const NoteId& note, const char* tool) {
    const Note* found = find_note(analysis, note);
    if (found == nullptr || !found->is_active) {
        return fail(ErrorCode::invalid_argument, std::string{tool} + ": unknown or inactive note");
    }
    return {};
}

Result<EditSpec> make(const NoteId& note, EditCategory category, EditParam param, EditValue value) {
    EditSpec spec{.origin = "amplitude_tool",
                  .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                  .category = category,
                  .param = param,
                  .op = EditOp::set,
                  .value = value};
    if (auto ok = validate_for_add(spec); !ok) {
        return Unexpected{ok.error()};
    }
    return spec;
}

}  // namespace

Result<EditSpec> gain_edit(const AnalysisResult& analysis, const NoteId& note, Decibels gain) {
    if (auto ok = check_note(analysis, note, "gain_edit"); !ok) {
        return Unexpected{ok.error()};
    }
    if (!std::isfinite(gain.value())) {
        return fail(ErrorCode::invalid_argument, "gain_edit: the gain must be finite");
    }
    return make(note, EditCategory::amplitude, EditParam::gain, gain);
}

Result<EditSpec> mute_edit(const AnalysisResult& analysis, const NoteId& note) {
    return gain_edit(analysis, note, Decibels{-120.0});
}

Result<EditSpec> flatten_edit(const AnalysisResult& analysis, const NoteId& note, Ratio amount) {
    if (auto ok = check_note(analysis, note, "flatten_edit"); !ok) {
        return Unexpected{ok.error()};
    }
    if (!std::isfinite(amount.value())) {
        return fail(ErrorCode::invalid_argument, "flatten_edit: the amount must be finite");
    }
    return make(note, EditCategory::amplitude, EditParam::within_note_flatten, amount);
}

}  // namespace anno
