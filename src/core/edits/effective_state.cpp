#include "core/edits/effective_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace anno {

namespace {

/// Mutable accumulators of the fold, one double per scalar parameter.
struct Fold {
    std::array<double, k_edit_param_count> value{};
    FadeShape fade_shape = FadeShape::linear;
    std::vector<TimePair> handles;

    Fold() {
        for (int i = 0; i < k_edit_param_count; ++i) {
            value[static_cast<std::size_t>(i)] = param_info(static_cast<EditParam>(i)).neutral;
        }
    }

    double& at(EditParam p) { return value[static_cast<std::size_t>(p)]; }
};

bool same_key(const TimePair& a, const TimePair& b) {
    return std::abs((a.source - b.source).value()) < k_handle_key.value();
}

void apply_handle(Fold& fold, EditOp op, const TimePair& pair) {
    const auto it =
        std::ranges::find_if(fold.handles, [&](const TimePair& h) { return same_key(h, pair); });
    if (op == EditOp::set) {
        if (it != fold.handles.end()) {
            *it = pair;
        } else {
            fold.handles.push_back(pair);
        }
    } else if (it != fold.handles.end()) {  // reset of one handle
        fold.handles.erase(it);
    }
}

void reset_param(Fold& fold, EditParam p) {
    switch (p) {
    case EditParam::fade_shape: fold.fade_shape = FadeShape::linear; break;
    case EditParam::time_handle: fold.handles.clear(); break;
    default: fold.at(p) = param_info(p).neutral; break;
    }
}

void apply_reset(Fold& fold, const EditSpec& spec) {
    if (spec.param) {
        if (const auto* pair = std::get_if<TimePair>(&spec.value)) {
            apply_handle(fold, EditOp::reset, *pair);
        } else {
            reset_param(fold, *spec.param);
        }
        return;
    }
    for (int i = 0; i < k_edit_param_count; ++i) {
        const auto p = static_cast<EditParam>(i);
        if (param_info(p).category == spec.category) {
            reset_param(fold, p);
        }
    }
}

double scalar(const EditValue& v) {
    return std::visit(
        [](const auto& x) -> double {
            if constexpr (requires { x.value(); }) {
                return x.value();
            } else {
                return 0.0;
            }
        },
        v);
}

void apply(Fold& fold, const EditSpec& spec) {
    if (spec.op == EditOp::reset) {
        apply_reset(fold, spec);
        return;
    }
    const EditParam p = *spec.param;
    if (p == EditParam::time_handle) {
        apply_handle(fold, spec.op, std::get<TimePair>(spec.value));
    } else if (p == EditParam::fade_shape) {
        fold.fade_shape = std::get<FadeShape>(spec.value);
    } else if (spec.op == EditOp::set) {
        fold.at(p) = scalar(spec.value);
    } else if (spec.op == EditOp::add) {
        fold.at(p) += scalar(spec.value);
    } else {
        fold.at(p) *= scalar(spec.value);
    }
}

double snap_offset(double v) {
    return std::abs(v) <= k_neutral_snap_offset ? 0.0 : v;
}
double snap_ratio(double v) {
    return std::abs(v - 1.0) <= k_neutral_snap_ratio ? 1.0 : v;
}

bool targets(const EditSpec& spec, const NoteId& id) {
    return std::ranges::find(spec.target.segment_ids, segment_id(id)) !=
           spec.target.segment_ids.end();
}

/// Clamps, then snaps float dust, in the order the design note fixes.
void finish(Fold& fold) {
    for (int i = 0; i < k_edit_param_count; ++i) {
        const auto p = static_cast<EditParam>(i);
        const ParamInfo& info = param_info(p);
        if (info.kind == ValueKind::fade_shape || info.kind == ValueKind::time_pair) {
            continue;
        }
        double& v = fold.at(p);
        v = info.allows_scale ? snap_ratio(v) : snap_offset(v);
    }
    auto clamp_to = [&](EditParam p, double lo, double hi) {
        double& v = fold.at(p);
        v = std::clamp(v, lo, hi);
    };
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

bool any_not_neutral(const Fold& fold) {
    for (int i = 0; i < k_edit_param_count; ++i) {
        const auto p = static_cast<EditParam>(i);
        const ParamInfo& info = param_info(p);
        if (info.kind == ValueKind::fade_shape || info.kind == ValueKind::time_pair) {
            continue;
        }
        if (fold.value[static_cast<std::size_t>(i)] != info.neutral) {
            return true;
        }
    }
    return !fold.handles.empty() || fold.fade_shape != FadeShape::linear;
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
