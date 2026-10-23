#include "macros/correct_pitch/correct_pitch.h"

#include <cmath>

namespace anno {

namespace {

bool valid_amount(Ratio r) {
    return std::isfinite(r.value()) && r.value() >= 0.0 && r.value() <= 1.0;
}

EditSpec spec_of(const NoteId& note, EditCategory category, EditParam param, EditValue value) {
    return EditSpec{.origin = "correct_pitch",
                    .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                    .category = category,
                    .param = param,
                    .op = EditOp::set,
                    .value = value};
}

}  // namespace

Result<std::vector<EditSpec>> correct_pitch_edits(const AnalysisResult& analysis,
                                                  const CorrectPitchParams& params) {
    if (!valid_amount(params.center_amount) || !valid_amount(params.drift_amount)) {
        return fail(ErrorCode::invalid_argument,
                    "correct_pitch_edits: the amounts must be finite and in 0..1");
    }
    if (!std::isfinite(params.min_deviation.value()) || params.min_deviation.value() < 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "correct_pitch_edits: min_deviation must be finite and >= 0");
    }
    // The grid is checked by target_pitch on the first note; check it here as well so a bad grid is
    // an error even for an empty analysis.
    if (auto ok = target_pitch(k_a4_cents, params.grid); !ok) {
        return Unexpected{ok.error()};
    }
    std::vector<EditSpec> specs;
    for (const Note& note : analysis.notes) {
        const PitchTargetKind kind = note.pitch_target.kind;
        if (!note.is_active || kind == PitchTargetKind::multi ||
            kind == PitchTargetKind::unpitched) {
            continue;
        }
        auto grid_pitch = target_pitch(note.pitch_target.target, params.grid);
        if (!grid_pitch) {
            return Unexpected{grid_pitch.error()};
        }
        const Cents distance = *grid_pitch - note.pitch_target.target;
        if (std::abs(distance.value()) >= params.min_deviation.value() && distance.value() != 0.0 &&
            params.center_amount.value() > 0.0) {
            const Cents offset{distance.value() * params.center_amount.value()};
            if (std::abs(offset.value()) > k_max_pitch_offset.value()) {
                return fail(ErrorCode::out_of_range,
                            "correct_pitch_edits: the correction is larger than "
                            "k_max_pitch_offset");
            }
            specs.push_back(
                spec_of(note.id, EditCategory::pitch, EditParam::center_offset, offset));
        }
        if (params.drift_amount.value() > 0.0) {
            specs.push_back(spec_of(note.id, EditCategory::pitch_drift, EditParam::drift_scale,
                                    Ratio{1.0 - params.drift_amount.value()}));
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
