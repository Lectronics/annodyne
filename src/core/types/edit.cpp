#include "core/types/edit.h"

#include "core/types/effective_note.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

namespace anno {

namespace {

template <class E, std::size_t N>
constexpr std::string_view name_of(const std::array<std::pair<E, std::string_view>, N>& table,
                                   E value) noexcept {
    for (const auto& [e, name] : table) {
        if (e == value) {
            return name;
        }
    }
    return "unknown";
}

template <class E, std::size_t N>
constexpr std::optional<E> parse_name(const std::array<std::pair<E, std::string_view>, N>& table,
                                      std::string_view name) noexcept {
    for (const auto& [e, n] : table) {
        if (n == name) {
            return e;
        }
    }
    return std::nullopt;
}

using enum EditCategory;
constexpr std::array<std::pair<EditCategory, std::string_view>, 18> k_category_names{
    {{pitch, "pitch"},
     {pitch_mod, "pitch_mod"},
     {pitch_drift, "pitch_drift"},
     {transition, "transition"},
     {formant, "formant"},
     {amplitude, "amplitude"},
     {fade, "fade"},
     {sibilant, "sibilant"},
     {time, "time"},
     {attack, "attack"},
     {separation, "separation"},
     {assignment, "assignment"},
     {sound, "sound"},
     {gap, "gap"},
     {layout, "layout"},
     {breath, "breath"},
     {breath_chain, "breath_chain"},
     {breath_assignment, "breath_assignment"}}};

constexpr std::array<std::pair<EditOp, std::string_view>, 4> k_op_names{{{EditOp::set, "set"},
                                                                         {EditOp::add, "add"},
                                                                         {EditOp::scale, "scale"},
                                                                         {EditOp::reset, "reset"}}};

constexpr std::array<std::pair<FadeShape, std::string_view>, 3> k_shape_names{
    {{FadeShape::equal_power, "equal_power"},
     {FadeShape::linear, "linear"},
     {FadeShape::log, "log"}}};

constexpr std::array<std::pair<EditParam, std::string_view>, k_edit_param_count> k_param_names{
    {{EditParam::center_offset, "center_offset"},
     {EditParam::mod_scale, "mod_scale"},
     {EditParam::drift_scale, "drift_scale"},
     {EditParam::transition_in_scale, "transition_in_scale"},
     {EditParam::transition_out_scale, "transition_out_scale"},
     {EditParam::formant_offset, "formant_offset"},
     {EditParam::gain, "gain"},
     {EditParam::within_note_flatten, "within_note_flatten"},
     {EditParam::fade_in, "fade_in"},
     {EditParam::fade_out, "fade_out"},
     {EditParam::fade_shape, "fade_shape"},
     {EditParam::noise_gain_voiced, "noise_gain_voiced"},
     {EditParam::noise_gain_sibilant, "noise_gain_sibilant"},
     {EditParam::noise_gain_breath, "noise_gain_breath"},
     {EditParam::noise_gain_plosive, "noise_gain_plosive"},
     {EditParam::onset_shift, "onset_shift"},
     {EditParam::length_scale, "length_scale"},
     {EditParam::time_handle, "time_handle"},
     {EditParam::attack_time_scale, "attack_time_scale"}}};

}  // namespace

std::string_view to_string(EditCategory c) noexcept {
    return name_of(k_category_names, c);
}
std::string_view to_string(EditOp op) noexcept {
    return name_of(k_op_names, op);
}
std::string_view to_string(EditParam p) noexcept {
    return name_of(k_param_names, p);
}
std::string_view to_string(FadeShape s) noexcept {
    return name_of(k_shape_names, s);
}
std::optional<EditCategory> parse_edit_category(std::string_view n) noexcept {
    return parse_name(k_category_names, n);
}
std::optional<EditOp> parse_edit_op(std::string_view n) noexcept {
    return parse_name(k_op_names, n);
}
std::optional<EditParam> parse_edit_param(std::string_view n) noexcept {
    return parse_name(k_param_names, n);
}
std::optional<FadeShape> parse_fade_shape(std::string_view n) noexcept {
    return parse_name(k_shape_names, n);
}

namespace {

constexpr double k_inf = std::numeric_limits<double>::infinity();

constexpr ParamInfo offset_row(EditParam p, EditCategory c, ValueKind k, double min = -k_inf) {
    return {.param = p,
            .category = c,
            .kind = k,
            .allows_add = true,
            .allows_scale = false,
            .neutral = 0.0,
            .set_min = min,
            .set_max = k_inf,
            .scale_min = -k_inf};
}
constexpr ParamInfo multiplier_row(EditParam p, EditCategory c, double min, double max,
                                   double scale_min) {
    return {.param = p,
            .category = c,
            .kind = ValueKind::ratio,
            .allows_add = false,
            .allows_scale = true,
            .neutral = 1.0,
            .set_min = min,
            .set_max = max,
            .scale_min = scale_min};
}
/// An amount in [min, max] with neutral 0: set only (scaling a neutral 0 means nothing).
constexpr ParamInfo amount_row(EditParam p, EditCategory c, double min, double max) {
    return {.param = p,
            .category = c,
            .kind = ValueKind::ratio,
            .allows_add = false,
            .allows_scale = false,
            .neutral = 0.0,
            .set_min = min,
            .set_max = max,
            .scale_min = -k_inf};
}
constexpr ParamInfo keyed_row(EditParam p, EditCategory c, ValueKind k) {
    return {.param = p,
            .category = c,
            .kind = k,
            .allows_add = false,
            .allows_scale = false,
            .neutral = 0.0,
            .set_min = -k_inf,
            .set_max = k_inf,
            .scale_min = -k_inf};
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
    amount_row(EditParam::within_note_flatten, EditCategory::amplitude, 0.0, 1.0),
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
    if (auto v = number_of<Cents>(value)) {
        return v;
    }
    if (auto v = number_of<Ratio>(value)) {
        return v;
    }
    if (auto v = number_of<Decibels>(value)) {
        return v;
    }
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
    if (spec.target.segment_ids.empty() && spec.target.orphaned_segment_ids.empty()) {
        return invalid("edit targets no segments");
    }
    std::set<SegmentId> ids;
    for (const auto* list : {&spec.target.segment_ids, &spec.target.orphaned_segment_ids}) {
        for (const SegmentId& id : *list) {
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
    if (spec.target.segment_ids.empty() || !spec.target.orphaned_segment_ids.empty()) {
        return invalid("a new edit must target live segments only");
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
