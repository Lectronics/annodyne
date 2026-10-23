#include "core/analysis/gaps/gap_evidence.h"

namespace anno {

namespace {
constexpr double k_far_below_db = -1000.0;  // below any margin a caller can set
}

GapEvidence neutral_gap_evidence(const FrameGrid& grid) {
    const auto n = static_cast<std::size_t>(std::max<std::int64_t>(0, grid.n_frames));
    return GapEvidence{.grid = grid,
                       .frame_class = std::vector<FrameClass>(n, FrameClass::silence),
                       .excess_db = std::vector<Decibels>(n, Decibels{k_far_below_db})};
}

}  // namespace anno
