#include "core/types/audio_buffer.h"

#include "core/types/contract.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace melo {

Result<AudioBuffer> AudioBuffer::create(int channels, Samples frames, Hz sample_rate) {
    if (channels < 1) {
        return fail(ErrorCode::invalid_argument, "audio buffer needs at least one channel");
    }
    if (frames.value() < 0) {
        return fail(ErrorCode::invalid_argument, "audio buffer frame count is negative");
    }
    if (!std::isfinite(sample_rate.value()) || sample_rate.value() <= 0.0) {
        return fail(ErrorCode::invalid_argument, "audio buffer sample rate must be finite and > 0");
    }
    // channels * frames must fit a std::vector<float>; checked by division so it cannot overflow.
    const auto max_frames = std::vector<float>{}.max_size() / static_cast<std::size_t>(channels);
    if (std::cmp_greater(frames.value(), max_frames)) {
        return fail(ErrorCode::out_of_range, "audio buffer of " + std::to_string(frames.value()) +
                                                 " frames x " + std::to_string(channels) +
                                                 " channels is too large");
    }
    return AudioBuffer{channels, frames, sample_rate};
}

AudioBuffer::AudioBuffer(int channels, Samples frames, Hz sample_rate)
    : samples_(static_cast<std::size_t>(channels) * static_cast<std::size_t>(frames.value()), 0.0F),
      channel_count_{channels}, frame_count_{frames}, sample_rate_{sample_rate} {}

Seconds AudioBuffer::duration() const noexcept {
    return to_seconds(frame_count_, sample_rate_);
}

std::span<const float> AudioBuffer::channel(int index) const noexcept {
    MELO_EXPECTS(index >= 0 && index < channel_count_);
    const auto frames = static_cast<std::size_t>(frame_count_.value());
    return std::span<const float>{samples_}.subspan(static_cast<std::size_t>(index) * frames,
                                                    frames);
}

std::span<float> AudioBuffer::channel(int index) noexcept {
    MELO_EXPECTS(index >= 0 && index < channel_count_);
    const auto frames = static_cast<std::size_t>(frame_count_.value());
    return std::span<float>{samples_}.subspan(static_cast<std::size_t>(index) * frames, frames);
}

}  // namespace melo
