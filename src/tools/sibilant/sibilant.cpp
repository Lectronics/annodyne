#include "tools/sibilant/sibilant.h"

#include <cmath>

namespace anno {

namespace {

EditSpec spec_of(const NoteId& note, EditParam param, Decibels gain) {
    return EditSpec{.origin = "sibilant_tool",
                    .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                    .category = EditCategory::sibilant,
                    .param = param,
                    .op = EditOp::set,
                    .value = gain};
}

}  // namespace

Result<std::vector<EditSpec>> sibilant_edits(const AnalysisResult& analysis,
                                             const SibilantGains& gains) {
    const Note* note = find_note(analysis, gains.note);
    if (note == nullptr || !note->is_active) {
        return fail(ErrorCode::invalid_argument, "sibilant_edits: unknown or inactive note");
    }
    for (const Decibels g : {gains.sibilant, gains.breath, gains.plosive}) {
        if (!std::isfinite(g.value())) {
            return fail(ErrorCode::invalid_argument, "sibilant_edits: a gain is not finite");
        }
    }
    std::vector<EditSpec> specs{spec_of(gains.note, EditParam::noise_gain_sibilant, gains.sibilant),
                                spec_of(gains.note, EditParam::noise_gain_breath, gains.breath),
                                spec_of(gains.note, EditParam::noise_gain_plosive, gains.plosive)};
    for (const EditSpec& spec : specs) {
        if (auto ok = validate_for_add(spec); !ok) {
            return Unexpected{ok.error()};
        }
    }
    return specs;
}

}  // namespace anno
