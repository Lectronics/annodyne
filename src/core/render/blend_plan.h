#pragma once

#include <cstdint>
#include <span>
#include <vector>

// Blend plan: where the edited audio fades in and out of the original (design note phase 2, step 4;
// ADR-0006). Sample coordinates only; no audio is touched here.

namespace anno {

/// Half-open sample range [begin, end) of an edited note on the output axis.
struct SampleSpan {
    std::int64_t begin = 0;
    std::int64_t end = 0;
};

inline constexpr int k_pristine = -1;  // "the original audio" in a Ramp

/// A fade from one signal to the next over [begin, end). `from` and `to` are indices into the spans
/// passed to plan_blend, or k_pristine. begin == end is a hard switch. Coordinates are not clamped
/// to the recording; the renderer truncates them.
struct Ramp {
    std::int64_t begin = 0;
    std::int64_t end = 0;
    int from = k_pristine;
    int to = k_pristine;

    friend bool operator==(const Ramp&, const Ramp&) = default;
};

/// The ramps for edited notes `spans` with crossfade length `margin_samples` (m), in signal order:
/// the signal is pristine before the first ramp, `to` of each ramp until the next, pristine after
/// the last. Nominal ramps: pristine -> first note over [a - m, a); last note -> pristine over
/// [b, b + m); between neighbours with gap g = a' - b: if g <= m one engine-to-engine ramp over
/// [a' - m, b + m), else a fall [b, b + m) and a rise [a' - m, a'). Where a ramp's end passes the
/// next ramp's start, both meet at the floor of the middle of the overlap (never before the earlier
/// ramp's begin). Shrunk ramps stay inside both notes' zones [a - m, b + m).
/// Pre: m >= 0; each span has begin <= end; spans sorted and non-overlapping (end <= next begin).
/// Result invariants (checked by the contract test): begin <= end per ramp, end <= next begin, and
/// the from/to chain is consistent. Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<Ramp> plan_blend(std::span<const SampleSpan> spans,
                                           std::int64_t margin_samples);

}  // namespace anno
