#pragma once

#include "core/types/edit.h"

#include <cstdint>
#include <span>

// Crossfade kernel (architecture spec §4.1, ADR-0006). Design:
// docs/design/phase2-render-skeleton.md.

namespace anno {

struct CrossfadeGains {
    float outgoing;
    float incoming;
};

/// Gains at position t in [0,1] of a fade (0 = all outgoing, 1 = all incoming). Computed in double,
/// cast once.
///  - equal_power: outgoing = cos(t pi/2), incoming = sin(t pi/2); the squares sum to 1.
///  - linear: outgoing = 1 - t, incoming = t; the gains sum to 1.
///  - log: incoming = 10^(-60 (1 - t) / 20), outgoing = 10^(-60 t / 20): linear in dB from -60 dB
///  to
///    0 dB. Never reaches zero (a step of up to 0.001 remains at the far end).
/// Pre: 0 <= t <= 1 (asserted in debug, clamped in release). Pure; real-time safe.
[[nodiscard]] CrossfadeGains crossfade_gains(FadeShape shape, double t) noexcept;

/// Fades a slice of a longer ramp: out[i] = a[i] * outgoing + b[i] * incoming with
/// t_i = (ramp_offset + i + 0.5) / ramp_length. The half-sample offset keeps t strictly inside
/// (0,1). The whole-span fade is ramp_offset = 0, ramp_length = size.
/// Pre: a, b and out have equal size; ramp_length >= 1; ramp_offset >= 0;
/// ramp_offset + size <= ramp_length. `out` may alias `a` or `b`. An empty span is a no-op.
/// Pure; thread-safe; real-time safe (no allocation).
void crossfade(std::span<const float> a, std::span<const float> b, std::span<float> out,
               FadeShape shape, std::int64_t ramp_offset, std::int64_t ramp_length) noexcept;

}  // namespace anno
