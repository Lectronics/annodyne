#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/units.h"

#include <cstdint>
#include <optional>
#include <vector>

// The objective audio measurements of docs/AUDIO_VERIFICATION.md §1 that `anno-cli audition`
// reports and the audio tests assert. Thresholds are parameters with the values pinned by
// tests/tolerances.h (a unit test checks they agree).

namespace anno::cli {

/// Click detector settings. Initial values, pending human review (AUDIO_VERIFICATION §1 names the
/// method but gives no number).
struct ClickParams {
    /// A frame is a click candidate if its first difference exceeds this multiple of the RMS of the
    /// first difference in the surrounding window. A 440 Hz sine peaks at ~1.4x its own RMS.
    double ratio = 6.0;
    int window = 128;     // frames each side for the local RMS (the frame itself excluded)
    double floor = 1e-3;  // absolute floor on the first difference (full scale = 1)
};

/// True if shape and every sample bit pattern are equal. Pure; thread-safe.
[[nodiscard]] bool bit_identical(const AudioBuffer& a, const AudioBuffer& b);

/// First frame at which the buffers differ in any channel; nullopt if identical. A length
/// difference counts as a difference at the shorter length. Pure; thread-safe.
[[nodiscard]] std::optional<std::int64_t> first_difference(const AudioBuffer& a,
                                                           const AudioBuffer& b);

/// Frames (sorted, unique, over all channels) where the first difference is a click candidate.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] std::vector<std::int64_t> find_clicks(const AudioBuffer& audio,
                                                    const ClickParams& params = {});

struct SanityReport {
    std::int64_t nan_inf_samples = 0;
    Decibels worst_dc_dbfs{-200.0};  // largest channel mean, dBFS
    float peak = 0.0F;               // largest |sample| (non-finite samples excluded)
};

/// Pure; thread-safe; not real-time safe.
[[nodiscard]] SanityReport sanity(const AudioBuffer& audio);

/// FNV-1a over shape and sample bit patterns. Equal only on the same build and platform; never
/// store across platforms. Pure; thread-safe.
[[nodiscard]] std::uint64_t hash_audio(const AudioBuffer& audio);

/// RMS over all channels and frames (linear); 0 for an empty buffer. Pure; thread-safe.
[[nodiscard]] double rms(const AudioBuffer& audio);

}  // namespace anno::cli
