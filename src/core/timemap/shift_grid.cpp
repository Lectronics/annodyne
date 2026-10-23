#include "core/timemap/shift_grid.h"

#include <algorithm>
#include <cmath>

namespace anno {

Result<Samples> shift_step_samples(Hz sample_rate, const ShiftGridParams& grid) {
    if (!std::isfinite(sample_rate.value()) || sample_rate.value() <= 0.0 ||
        !std::isfinite(grid.tempo_bpm) || grid.tempo_bpm <= 0.0 || grid.note_division <= 0) {
        return fail(ErrorCode::invalid_argument,
                    "shift_step_samples: rate, tempo and note division must be positive");
    }
    constexpr double k_beats_per_whole_note = 4.0;
    const double step_s = 60.0 / grid.tempo_bpm * k_beats_per_whole_note / grid.note_division;
    const double samples = std::round(step_s * sample_rate.value());
    return Samples{std::max<std::int64_t>(1, static_cast<std::int64_t>(samples))};
}

}  // namespace anno
