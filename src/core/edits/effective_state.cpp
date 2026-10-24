#include "core/edits/effective_state.h"

#include "core/edits/detail/fold.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace anno {

namespace {

using edits_detail::any_not_neutral;
using edits_detail::apply;
using edits_detail::Fold;
using edits_detail::snap_to_neutral;

bool targets(const EditSpec& spec, const NoteId& id) {
    return std::ranges::find(spec.target.segment_ids, segment_id(id)) !=
           spec.target.segment_ids.end();
}

/// Snaps float dust, then clamps, in the order the design note fixes.
void finish(Fold& fold) {
    snap_to_neutral(fold);
    auto clamp_to = [&](EditParam p, double lo, double hi) {
        double& v = fold.at(p);
        v = std::clamp(v, lo, hi);
    };
    clamp_to(EditParam::center_offset, -k_max_pitch_offset.value(), k_max_pitch_offset.value());
    clamp_to(EditParam::fade_in, 0.0, std::numeric_limits<double>::infinity());
    clamp_to(EditParam::fade_out, 0.0, std::numeric_limits<double>::infinity());
    clamp_to(EditParam::within_note_flatten, 0.0, 1.0);
    clamp_to(EditParam::length_scale, k_min_time_scale.value(),
             std::numeric_limits<double>::infinity());
    clamp_to(EditParam::attack_time_scale, k_min_time_scale.value(),
             std::numeric_limits<double>::infinity());
    if (fold.at(EditParam::fade_in) == 0.0 && fold.at(EditParam::fade_out) == 0.0) {
        fold.fade_shape = FadeShape::linear;
    }
    std::ranges::sort(fold.handles, {}, &TimePair::source);
}

}  // namespace

EffectiveNote effective_state(const Note& note, const EditList& edits) {
    ANNO_EXPECTS(note.track_id == edits.track_id());
    Fold fold;
    for (const Edit& edit : edits.active()) {
        if (targets(edit.spec, note.id)) {
            apply(fold, edit.spec);
        }
    }
    finish(fold);

    EffectiveNote eff;
    eff.is_edited = any_not_neutral(fold);
    eff.note_id = note.id;
    eff.center = note.pitch_center + Cents{fold.at(EditParam::center_offset)};
    eff.mod_scale = Ratio{fold.at(EditParam::mod_scale)};
    eff.drift_scale = Ratio{fold.at(EditParam::drift_scale)};
    eff.transition_in_scale = Ratio{fold.at(EditParam::transition_in_scale)};
    eff.transition_out_scale = Ratio{fold.at(EditParam::transition_out_scale)};
    eff.formant_offset = Cents{fold.at(EditParam::formant_offset)};
    eff.gain = Decibels{fold.at(EditParam::gain)};
    eff.within_note_flatten = Ratio{fold.at(EditParam::within_note_flatten)};
    eff.fade_in = Seconds{fold.at(EditParam::fade_in)};
    eff.fade_out = Seconds{fold.at(EditParam::fade_out)};
    eff.fade_shape = fold.fade_shape;
    eff.noise_gain = NoiseGains{.voiced = Decibels{fold.at(EditParam::noise_gain_voiced)},
                                .sibilant = Decibels{fold.at(EditParam::noise_gain_sibilant)},
                                .breath = Decibels{fold.at(EditParam::noise_gain_breath)},
                                .plosive = Decibels{fold.at(EditParam::noise_gain_plosive)}};
    eff.onset_shift = Seconds{fold.at(EditParam::onset_shift)};
    eff.length_scale = Ratio{fold.at(EditParam::length_scale)};
    eff.handles = std::move(fold.handles);
    eff.attack_time_scale = Ratio{fold.at(EditParam::attack_time_scale)};
    return eff;
}

}  // namespace anno
