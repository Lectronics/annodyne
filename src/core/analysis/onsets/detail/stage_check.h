#pragma once

#include "core/types/error.h"

#include <cmath>
#include <string>

// Argument checks shared by the onset stages. Internal to core/analysis/onsets.

namespace anno::detail {

/// An invalid_argument failure naming the stage.
[[nodiscard]] inline Unexpected bad_stage_argument(const char* what) {
    return fail(ErrorCode::invalid_argument, std::string{"onset stages: "} + what);
}

[[nodiscard]] inline bool positive(double v) {
    return std::isfinite(v) && v > 0.0;
}

}  // namespace anno::detail
