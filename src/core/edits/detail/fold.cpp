#include "core/edits/detail/fold.h"

#include "core/types/effective_note.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace anno::edits_detail {

Fold::Fold() {
    for (int i = 0; i < k_edit_param_count; ++i) {
        value[static_cast<std::size_t>(i)] = param_info(static_cast<EditParam>(i)).neutral;
    }
}

namespace {

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

double snap_offset(double v) {
    return std::abs(v) <= k_neutral_snap_offset ? 0.0 : v;
}
double snap_ratio(double v) {
    return std::abs(v - 1.0) <= k_neutral_snap_ratio ? 1.0 : v;
}

}  // namespace

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

void snap_to_neutral(Fold& fold) {
    for (int i = 0; i < k_edit_param_count; ++i) {
        const auto p = static_cast<EditParam>(i);
        const ParamInfo& info = param_info(p);
        if (info.kind == ValueKind::fade_shape || info.kind == ValueKind::time_pair) {
            continue;
        }
        double& v = fold.at(p);
        v = info.allows_scale ? snap_ratio(v) : snap_offset(v);
    }
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

}  // namespace anno::edits_detail
