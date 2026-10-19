#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <filesystem>
#include <string_view>

// WAV file I/O (ADR-0001). Only layers 6-7 and tests may link core/io.
// Design: docs/design/phase0-foundations.md §7.

namespace melo::io {

/// Sample encodings Melo reads. Anything else is ErrorCode::unsupported_format.
enum class WavSampleFormat { pcm16, pcm24, pcm32, float32 };

/// Stable lower-case name, e.g. "pcm24". Pure; real-time safe.
[[nodiscard]] std::string_view to_string(WavSampleFormat format) noexcept;

/// What a WAV file holds, without decoding the samples.
struct WavInfo {
    Hz sample_rate{};
    int channel_count = 0;
    Samples frame_count{};
    WavSampleFormat format = WavSampleFormat::pcm16;
};

/// Reads the header of a WAV file (RIFF/WAVE, PCM or IEEE float, WAVE_FORMAT_EXTENSIBLE allowed;
/// unknown chunks skipped).
/// Errors: not_found (no such file); io_error (cannot open or read); unsupported_format (not
/// RIFF/WAVE, RF64, compressed formats, PCM8, float64, other bit depths); corrupt_data (truncated
/// or inconsistent chunks, missing fmt or data chunk).
/// Thread-safe for distinct paths; not real-time safe.
[[nodiscard]] Result<WavInfo> read_wav_info(const std::filesystem::path& path);

/// Reads a whole WAV file into float32. PCM16/24 convert exactly (divide by 2^(bits-1)); PCM32 is
/// rounded to float32 precision; float32 is copied bit for bit. Same errors as read_wav_info.
/// Thread-safe for distinct paths; not real-time safe.
[[nodiscard]] Result<AudioBuffer> read_wav(const std::filesystem::path& path);

/// Writes `audio` as a 32-bit IEEE float WAV, overwriting `path`. Reading it back with read_wav
/// gives bit-identical samples.
/// Errors: invalid_argument if the sample rate is not a whole number of Hz below 2^32, the channel
/// count exceeds 65535, or the data would exceed the 4 GiB RIFF limit; io_error if the file cannot
/// be written.
/// Thread-safe for distinct paths; not real-time safe.
[[nodiscard]] Result<void> write_wav(const std::filesystem::path& path, const AudioBuffer& audio);

}  // namespace melo::io
