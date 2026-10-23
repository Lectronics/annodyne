#include "core/tuning/chromatic_grid.h"

#include <cmath>

namespace anno {

namespace {
constexpr double k_semitone_cents = 100.0;
}  // namespace

Result<Cents> target_pitch(Cents pitch, const ChromaticGrid& grid) {
    if (!std::isfinite(pitch.value())) {
        return fail(ErrorCode::invalid_argument, "target_pitch: the pitch is not finite");
    }
    if (!std::isfinite(grid.a4_reference.value()) || grid.a4_reference.value() <= 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "target_pitch: a4_reference must be finite and > 0");
    }
    const double a4 = to_cents(grid.a4_reference, k_a4_standard_hz).value();
    const double steps = std::floor((pitch.value() - a4) / k_semitone_cents + 0.5);
    return Cents{a4 + steps * k_semitone_cents};
}

}  // namespace anno
