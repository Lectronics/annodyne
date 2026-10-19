#pragma once

#include "core/types/result.h"
#include "core/types/time_pair.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// TimeMap: the monotone piecewise-linear link between source time and output time (architecture
// spec §2). The pure mapping lives here so synthesis (layer 2) can use it; building one from edits
// is build_time_map in core/timemap (ADR-0005 §11).
// Design: docs/design/phase1-data-model.md.

namespace anno {

/// tau = M(s). Invariant: at least one anchor; anchors strictly increasing in both source and
/// output; slope 1 before the first and after the last anchor; linear in between. is_identity()
/// means M(s) == s exactly. Value type; immutable.
class TimeMap {
public:
    /// M(s) = s.
    [[nodiscard]] static TimeMap identity();

    /// Errors: invalid_argument if `anchors` is empty, a time is not finite, or the anchors are not
    /// strictly increasing in both source and output. Not real-time safe (allocates).
    [[nodiscard]] static Result<TimeMap> from_anchors(std::vector<TimePair> anchors);

    /// M(s). Pre: s finite (not enforced; garbage in, garbage out). Identity maps return their
    /// input exactly. Real-time safe.
    [[nodiscard]] OutputTime to_output(SourceTime s) const noexcept;

    /// M^-1(tau). Pre: tau finite. M^-1(M(s)) == s within 1e-9 s. Real-time safe.
    [[nodiscard]] SourceTime to_source(OutputTime tau) const noexcept;

    /// dM/ds: the stretch ratio (2 = twice as long). At an anchor, the slope of the segment that
    /// starts there. Real-time safe.
    [[nodiscard]] Ratio stretch_ratio(SourceTime s) const noexcept;

    [[nodiscard]] std::span<const TimePair> anchors() const noexcept { return anchors_; }
    [[nodiscard]] bool is_identity() const noexcept { return identity_; }

private:
    TimeMap(std::vector<TimePair> anchors, bool identity) noexcept;

    std::vector<TimePair> anchors_;
    bool identity_;
};

}  // namespace anno
