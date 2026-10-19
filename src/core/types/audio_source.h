#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <compare>
#include <memory>
#include <string>

// AudioSource: read-only access to an original recording (architecture spec §1, §3).
// Design: docs/design/phase0-foundations.md §6.

namespace anno {

/// Read-only handle to an original recording (CLAUDE.md rule 1: the source is never modified).
/// Cheap to copy: all copies share one immutable backend. Phase 9 adds an ARA backend behind the
/// same API.
/// Invariant: the audio never changes after construction, and every AudioSource refers to a backend
/// (there is no empty state). Only copy operations are declared, so "moving" copies the shared
/// pointer and a moved-from AudioSource stays valid.
class AudioSource {
public:
    AudioSource(const AudioSource&) = default;
    AudioSource& operator=(const AudioSource&) = default;
    ~AudioSource() = default;

    /// In-memory backend owning `audio`. The id is a hash of the sample rate, channel count and
    /// samples, so equal content gives an equal id. Not real-time safe.
    [[nodiscard]] static AudioSource from_buffer(AudioBuffer audio);

    [[nodiscard]] const SourceId& id() const noexcept;
    [[nodiscard]] Hz sample_rate() const noexcept;
    [[nodiscard]] int channel_count() const noexcept;
    [[nodiscard]] Samples frame_count() const noexcept;

    /// Copy of frames [start, start + count). `start` is a source-axis sample index, e.g. from
    /// to_sample_index(SourceTime, sample_rate(), rounding).
    /// Errors: out_of_range if start < 0, count < 0, or start + count > frame_count().
    /// Pure; thread-safe; not real-time safe (allocates).
    [[nodiscard]] Result<AudioBuffer> read(Samples start, Samples count) const;

private:
    struct Impl;
    explicit AudioSource(std::shared_ptr<const Impl> impl) noexcept;

    std::shared_ptr<const Impl> impl_;
};

}  // namespace anno
