#include "core/types/edit.h"

#include "core/types/effective_note.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace anno {

namespace {

constexpr double k_inf = std::numeric_limits<double>::infinity();

constexpr ParamInfo offset_row(EditParam p, EditCategory c, ValueKind k, double min = -k_inf) {
    return {p, c, k, true, false, 0.0, min, k_inf, -k_inf};
}
constexpr ParamInfo multiplier_row(EditParam p, EditCategory c, double min, double max,
                                   double scale_min) {
    return {p, c, ValueKind::ratio, false, true, 1.0, min, max, scale_min};
}
constexpr ParamInfo keyed_row(EditParam p, EditCategory c, ValueKind k) {
    return {p, c, k, false, false, 0.0, -k_inf, k_inf, -k_inf};
}

constexpr double k_min_scale = 1e-3;  // k_min_time_scale.value(); constexpr-friendly copy
static_assert(k_min_time_scale.value() == k_min_scale);

// Indexed by EditParam; edit_types_test checks every row.
constexpr std::array<ParamInfo, k_edit_param_count> k_table{{
    offset_row(EditParam::center_offset, EditCategory::pitch, ValueKind::cents),
    multiplier_row(EditParam::mod_scale, EditCategory::pitch_mod, -k_inf, k_inf, -k_inf),
    multiplier_row(EditParam::drift_scale, EditCategory::pitch_drift, -k_inf, k_inf, -k_inf),
    multiplier_row(EditParam::transition_in_scale, EditCategory::transition, 0.0, k_inf, -k_inf),
    multiplier_row(EditParam::transition_out_scale, EditCategory::transition, 0.0, k_inf, -k_inf),
    offset_row(EditParam::formant_offset, EditCategory::formant, ValueKind::cents),
    offset_row(EditParam::gain, EditCategory::amplitude, ValueKind::decibels),
    multiplier_row(EditParam::within_note_flatten, EditCategory::amplitude, 0.0, 1.0, 0.0),
    offset_row(EditParam::fade_in, EditCategory::fade, ValueKind::seconds, 0.0),
    offset_row(EditParam::fade_out, EditCategory::fade, ValueKind::seconds, 0.0),
    keyed_row(EditParam::fade_shape, EditCategory::fade, ValueKind::fade_shape),
    offset_row(EditParam::noise_gain_voiced, EditCategory::sibilant, ValueKind::decibels),
    offset_row(EditParam::noise_gain_sibilant, EditCategory::sibilant, ValueKind::decibels),
    offset_row(EditParam::noise_gain_breath, EditCategory::sibilant, ValueKind::decibels),
    offset_row(EditParam::noise_gain_plosive, EditCategory::sibilant, ValueKind::decibels),
    offset_row(EditParam::onset_shift, EditCategory::time, ValueKind::seconds),
    multiplier_row(EditParam::length_scale, EditCategory::time, k_min_scale, k_inf, 0.0),
    keyed_row(EditParam::time_handle, EditCategory::time, ValueKind::time_pair),
    multiplier_row(EditParam::attack_time_scale, EditCategory::attack, k_min_scale, k_inf, 0.0),
}};

Unexpected invalid(std::string message) {
    return fail(ErrorCode::invalid_argument, std::move(message));
}

template <class Q>
std::optional<double> number_of(const EditValue& value) {
    if (const auto* q = std::get_if<Q>(&value)) {
        return q->value();
    }
    return std::nullopt;
}

bool kind_matches(ValueKind kind, const EditValue& value) {
    switch (kind) {
        case ValueKind::cents: return std::holds_alternative<Cents>(value);
        case ValueKind::ratio: return std::holds_alternative<Ratio>(value);
        case ValueKind::decibels: return std::holds_alternative<Decibels>(value);
        case ValueKind::seconds: return std::holds_alternative<Seconds>(value);
        case ValueKind::fade_shape: return std::holds_alternative<FadeShape>(value);
        case ValueKind::time_pair: return std::holds_alternative<TimePair>(value);
    }
    return false;
}

std::optional<double> scalar_of(const EditValue& value) {
    if (auto v = number_of<Cents>(value)) return v;
    if (auto v = number_of<Ratio>(value)) return v;
    if (auto v = number_of<Decibels>(value)) return v;
    return number_of<Seconds>(value);
}

Result<void> validate_value(const ParamInfo& info, EditOp op, const EditValue& value) {
    if (!kind_matches(info.kind, value)) {
        return invalid("edit value has the wrong type for its parameter");
    }
    if (const auto* pair = std::get_if<TimePair>(&value)) {
        const bool finite = std::isfinite(pair->source.unchecked_since_origin().value()) &&
                            std::isfinite(pair->output.unchecked_since_origin().value());
        return finite ? Result<void>{} : Result<void>{invalid("time handle is not finite")};
    }
    const auto scalar = scalar_of(value);
    if (!scalar) {
        return {};  // fade_shape
    }
    if (!std::isfinite(*scalar)) {
        return invalid("edit value is not finite");
    }
    if (op == EditOp::set && (*scalar < info.set_min || *scalar > info.set_max)) {
        return invalid("edit value is outside the legal range of its parameter");
    }
    if (op == EditOp::scale && !(*scalar > info.scale_min)) {
        return invalid("scale factor is outside the legal range of its parameter");
    }
    return {};
}

Result<void> validate_reset(const EditSpec& spec) {
    if (!spec.param) {
        return std::holds_alternative<std::monostate>(spec.value)
                   ? Result<void>{}
                   : Result<void>{invalid("a category reset carries no value")};
    }
    if (std::holds_alternative<std::monostate>(spec.value)) {
        return {};
    }
    if (*spec.param == EditParam::time_handle && std::holds_alternative<TimePair>(spec.value)) {
        return {};
    }
    return invalid("reset carries no value (except the handle to remove for time_handle)");
}

}  // namespace

const ParamInfo& param_info(EditParam param) noexcept {
    const auto index = static_cast<std::size_t>(param);
    ANNO_EXPECTS(index < k_table.size());
    return k_table[index];
}

Result<void> validate(const EditSpec& spec) {
    if (spec.origin.empty()) {
        return invalid("edit needs an origin");
    }
    if (spec.target.note_ids.empty() && spec.target.orphaned_note_ids.empty()) {
        return invalid("edit targets no notes");
    }
    std::set<NoteId> ids;
    for (const auto* list : {&spec.target.note_ids, &spec.target.orphaned_note_ids}) {
        for (const NoteId& id : *list) {
            if (id.value.empty() || !ids.insert(id).second) {
                return invalid("edit target ids must be non-empty and unique");
            }
        }
    }
    if (spec.op == EditOp::reset) {
        if (spec.param && param_info(*spec.param).category != spec.category) {
            return invalid("parameter does not belong to the edit's category");
        }
        return validate_reset(spec);
    }
    if (!spec.param) {
        return invalid("only a reset may omit the parameter");
    }
    const ParamInfo& info = param_info(*spec.param);
    if (info.category != spec.category) {
        return invalid("parameter does not belong to the edit's category");
    }
    const bool op_allowed = spec.op == EditOp::set || (spec.op == EditOp::add && info.allows_add) ||
                            (spec.op == EditOp::scale && info.allows_scale);
    if (!op_allowed) {
        return invalid("operation is not allowed for this parameter");
    }
    return validate_value(info, spec.op, spec.value);
}

Result<void> validate_for_add(const EditSpec& spec) {
    if (spec.target.note_ids.empty() || !spec.target.orphaned_note_ids.empty()) {
        return invalid("a new edit must target live notes only");
    }
    return validate(spec);
}

Decibels noise_gain(const NoiseGains& gains, FrameClass frame_class) noexcept {
    switch (frame_class) {
        case FrameClass::voiced: return gains.voiced;
        case FrameClass::sibilant: return gains.sibilant;
        case FrameClass::breath: return gains.breath;
        case FrameClass::plosive: return gains.plosive;
        case FrameClass::silence: break;
    }
    return Decibels{0.0};
}

}  // namespace anno
