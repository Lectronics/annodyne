#include "core/types/audio_source.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace anno {

namespace {

// FNV-1a 64-bit (Fowler/Noll/Vo). Not cryptographic; a content id for caching (design note §6).
constexpr std::uint64_t k_fnv_offset_basis = 14695981039346656037ULL;
constexpr std::uint64_t k_fnv_prime = 1099511628211ULL;

class Fnv1a64 {
public:
    void add_byte(std::uint8_t byte) noexcept {
        hash_ ^= byte;
        hash_ *= k_fnv_prime;
    }
    // Fixed little-endian byte order so ids are identical on every platform.
    void add_u32(std::uint32_t v) noexcept {
        for (int shift = 0; shift < 32; shift += 8) {
            add_byte(static_cast<std::uint8_t>((v >> static_cast<unsigned>(shift)) & 0xFFU));
        }
    }
    void add_u64(std::uint64_t v) noexcept {
        add_u32(static_cast<std::uint32_t>(v & 0xFFFFFFFFU));
        add_u32(static_cast<std::uint32_t>(v >> 32U));
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = k_fnv_offset_basis;
};

SourceId content_id(const AudioBuffer& audio) {
    Fnv1a64 hash;
    hash.add_u64(std::bit_cast<std::uint64_t>(audio.sample_rate().value()));
    hash.add_u32(static_cast<std::uint32_t>(audio.channel_count()));
    hash.add_u64(static_cast<std::uint64_t>(audio.frame_count().value()));
    for (int ch = 0; ch < audio.channel_count(); ++ch) {
        for (const float sample : audio.channel(ch)) {
            hash.add_u32(std::bit_cast<std::uint32_t>(sample));
        }
    }
    constexpr std::array<char, 16> k_hex = {'0', '1', '2', '3', '4', '5', '6', '7',
                                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string text(16, '0');
    unsigned shift = 64;
    for (char& digit : text) {
        shift -= 4;
        digit = k_hex[static_cast<std::size_t>((hash.value() >> shift) & 0xFU)];
    }
    return SourceId{std::move(text)};
}

}  // namespace

struct AudioSource::Impl {
    AudioBuffer audio;
    SourceId id;
};

AudioSource::AudioSource(std::shared_ptr<const Impl> impl) noexcept : impl_{std::move(impl)} {}

AudioSource AudioSource::from_buffer(AudioBuffer audio) {
    SourceId id = content_id(audio);
    return AudioSource{
        std::make_shared<const Impl>(Impl{.audio = std::move(audio), .id = std::move(id)})};
}

const SourceId& AudioSource::id() const noexcept {
    return impl_->id;
}
Hz AudioSource::sample_rate() const noexcept {
    return impl_->audio.sample_rate();
}
int AudioSource::channel_count() const noexcept {
    return impl_->audio.channel_count();
}
Samples AudioSource::frame_count() const noexcept {
    return impl_->audio.frame_count();
}

Result<AudioBuffer> AudioSource::read(Samples start, Samples count) const {
    const Samples length = frame_count();
    // Written so that no intermediate sum can overflow int64.
    if (start.value() < 0 || count.value() < 0 || start > length || count > length - start) {
        return fail(ErrorCode::out_of_range, "read outside the source");
    }
    auto created = AudioBuffer::create(channel_count(), count, sample_rate());
    if (!created) {
        return created;
    }
    AudioBuffer out = std::move(created).value();
    const auto offset = static_cast<std::size_t>(start.value());
    for (int ch = 0; ch < channel_count(); ++ch) {
        const auto from = impl_->audio.channel(ch).subspan(offset, out.channel(ch).size());
        std::ranges::copy(from, out.channel(ch).begin());
    }
    return out;
}

}  // namespace anno
