#include "core/edits/effective_gap.h"

#include "core/edits/detail/fold.h"
#include "core/types/effective_note.h"

#include <algorithm>

namespace anno {

namespace {

using edits_detail::Fold;

bool targets(const EditSpec& spec, const SegmentId& id) {
    return std::ranges::find(spec.target.segment_ids, id) != spec.target.segment_ids.end();
}

}  // namespace

EffectiveGap effective_gap(const SegmentId& gap, const EditList& edits) {
    ANNO_EXPECTS(gap.kind == SegmentKind::gap);
    Fold fold;
    for (const Edit& edit : edits.active()) {
        if (targets(edit.spec, gap)) {
            edits_detail::apply(fold, edit.spec);
        }
    }
    edits_detail::snap_to_neutral(fold);
    double& db = fold.at(EditParam::gap_gain);
    const ParamInfo& info = param_info(EditParam::gap_gain);  // [k_gap_mute_db, +24 dB]
    db = std::clamp(db, info.set_min, info.set_max);
    return EffectiveGap{
        .gap = gap, .gain = Decibels{db}, .is_edited = edits_detail::any_not_neutral(fold)};
}

}  // namespace anno
