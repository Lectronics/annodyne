#pragma once

#include "core/types/edit.h"

#include <array>
#include <cstddef>
#include <vector>

// The fold shared by the note and gap consumers (effective_state, effective_gap): one double per
// scalar parameter, applied in edit order. Internal to core/edits (ADR-0024, gap slice 3b).

namespace anno::edits_detail {

/// Mutable accumulators of the fold, one double per scalar parameter. A consumer reads only the
/// parameters of its own categories; the others stay neutral.
struct Fold {
    std::array<double, k_edit_param_count> value{};
    FadeShape fade_shape = FadeShape::linear;
    std::vector<TimePair> handles;

    Fold();

    double& at(EditParam p) { return value[static_cast<std::size_t>(p)]; }
    [[nodiscard]] double at(EditParam p) const { return value[static_cast<std::size_t>(p)]; }
};

/// Applies one recorded edit (set, add, scale or reset by the parameter's op rules). The spec must
/// have passed validate(EditSpec). Pure on `fold`; thread-safe on distinct folds.
void apply(Fold& fold, const EditSpec& spec);

/// Snaps float dust to the neutral value: offsets within k_neutral_snap_offset of 0, multipliers
/// within k_neutral_snap_ratio of 1. fade_shape and time_handle are untouched.
void snap_to_neutral(Fold& fold);

/// True if any scalar differs from its neutral value, a handle exists or the fade shape is not
/// linear. Call after snap_to_neutral.
[[nodiscard]] bool any_not_neutral(const Fold& fold);

}  // namespace anno::edits_detail
