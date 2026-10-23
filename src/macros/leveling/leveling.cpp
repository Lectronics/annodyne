#include "macros/leveling/leveling.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

bool valid_amount(Ratio r) {
    return std::isfinite(r.value()) && r.value() >= 0.0 && r.value() <= 1.0;
}

EditSpec spec_of(const NoteId& note, EditParam param, EditValue value) {
    return EditSpec{.origin = "leveling",
                    .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                    .category = EditCategory::amplitude,
                    .param = param,
                    .op = EditOp::set,
                    .value = value};
}

}  // namespace

Result<std::vector<EditSpec>> leveling_edits(const AnalysisResult& analysis,
                                             const LevelingParams& params) {
    if (!valid_amount(params.between_notes) || !valid_amount(params.within_notes)) {
        return fail(ErrorCode::invalid_argument,
                    "leveling_edits: the amounts must be finite and in 0..1");
    }
    if (!std::isfinite(params.max_gain.value()) || !(params.max_gain.value() > 0.0) ||
        !std::isfinite(params.min_gain.value()) || params.min_gain.value() < 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "leveling_edits: max_gain must be > 0 and min_gain >= 0 (finite)");
    }
    if (params.target && !std::isfinite(params.target->value())) {
        return fail(ErrorCode::invalid_argument, "leveling_edits: the target is not finite");
    }
    double target = params.target ? params.target->value() : 0.0;
    if (!params.target) {
        double sum = 0.0;
        std::size_t count = 0;
        for (const Note& note : analysis.notes) {
            if (note.is_active && std::isfinite(note.loudness.value())) {
                sum += note.loudness.value();
                ++count;
            }
        }
        if (count == 0) {
            return std::vector<EditSpec>{};
        }
        target = sum / static_cast<double>(count);
    }
    std::vector<EditSpec> specs;
    for (const Note& note : analysis.notes) {
        if (!note.is_active) {
            continue;
        }
        if (params.between_notes.value() > 0.0 && std::isfinite(note.loudness.value())) {
            const double wanted = (target - note.loudness.value()) * params.between_notes.value();
            const double gain =
                std::clamp(wanted, -params.max_gain.value(), params.max_gain.value());
            if (std::abs(gain) >= params.min_gain.value() && gain != 0.0) {
                specs.push_back(spec_of(note.id, EditParam::gain, Decibels{gain}));
            }
        }
        if (params.within_notes.value() > 0.0) {
            specs.push_back(spec_of(note.id, EditParam::within_note_flatten, params.within_notes));
        }
    }
    for (const EditSpec& spec : specs) {
        if (auto ok = validate_for_add(spec); !ok) {
            return Unexpected{ok.error()};
        }
    }
    return specs;
}

}  // namespace anno
