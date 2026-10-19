#pragma once

#include "core/types/result.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// AudioBuffer and PositionedAudio (architecture spec §3, ADR-0002).
// Design: docs/design/phase0-foundations.md §6.

namespace melo {

/// Planar float32 audio. Carries no time position (ADR-0002); see PositionedAudio.
/// Invariant: channel_count() >= 1, sample_rate() finite and > 0, every channel holds
/// frame_count() samples. Value type: copying copies the samples.
class AudioBuffer {
public:
    /// Zero-filled buffer.
    /// Errors: invalid_argument if channels < 1, frames < 0, or sample_rate not finite and > 0;
    /// out_of_range if channels * frames overflows or exceeds what a std::vector can hold.
    /// Thread-safe; not real-time safe (allocates). std::bad_alloc is not caught.
    [[nodiscard]] static Result<AudioBuffer> create(int channels, Samples frames, Hz sample_rate);

    [[nodiscard]] int channel_count() const noexcept { return channel_count_; }
    [[nodiscard]] Samples frame_count() const noexcept { return frame_count_; }
    [[nodiscard]] Hz sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] Seconds duration() const noexcept;

    /// Samples of one channel. Pre: 0 <= index < channel_count(). Real-time safe.
    [[nodiscard]] std::span<const float> channel(int index) const noexcept;
    [[nodiscard]] std::span<float> channel(int index) noexcept;

private:
    AudioBuffer(int channels, Samples frames, Hz sample_rate);

    std::vector<float> samples_;  // channel-major: samples_[ch * frames + i]
    int channel_count_;
    Samples frame_count_;
    Hz sample_rate_;
};

/// Audio placed on one time axis: frame 0 of `audio` is at `start` (ADR-0002).
template <class Axis>
struct PositionedAudio {
    AudioBuffer audio;
    TimePoint<Axis> start;
};

using SourceAudio = PositionedAudio<SourceAxis>;
using OutputAudio = PositionedAudio<OutputAxis>;

/// The output-time range an OutputAudio covers (replaces RenderResult.out_range, ADR-0002).
[[nodiscard]] inline OutputRange output_range(const OutputAudio& audio) noexcept {
    return OutputRange::from_start(audio.start, audio.audio.duration());
}

/// The source-time range a SourceAudio covers.
[[nodiscard]] inline SourceRange source_range(const SourceAudio& audio) noexcept {
    return SourceRange::from_start(audio.start, audio.audio.duration());
}

}  // namespace melo
