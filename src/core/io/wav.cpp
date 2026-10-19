#include "core/io/wav.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// RIFF/WAVE layout: Microsoft "Multimedia Programming Interface and Data Specifications 1.0"
// (1991) and "WAVEFORMATEXTENSIBLE" (KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT GUIDs).

namespace anno::io {

namespace {

constexpr std::uint16_t k_format_pcm = 1;
constexpr std::uint16_t k_format_ieee_float = 3;
constexpr std::uint16_t k_format_extensible = 0xFFFE;
// Bytes 2..15 of the KSDATAFORMAT_SUBTYPE GUIDs; bytes 0..1 hold the format code.
constexpr std::array<std::uint8_t, 14> k_ksdataformat_guid_tail = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

constexpr std::size_t k_riff_header_size = 12;
constexpr std::size_t k_chunk_header_size = 8;
constexpr std::size_t k_min_fmt_size = 16;
constexpr std::size_t k_extensible_fmt_size = 40;
constexpr std::size_t k_decode_block_frames = 4096;
constexpr std::uint64_t k_riff_size_limit = 0xFFFFFFFFULL;

using Bytes = std::span<const std::uint8_t>;

std::uint16_t read_u16(Bytes p) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(p[0]) |
                                      (static_cast<unsigned>(p[1]) << 8U));
}
std::uint32_t read_u32(Bytes p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}
bool tag_is(Bytes p, const char* tag) {
    return std::equal(p.begin(), p.begin() + 4, tag,
                      [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); });
}

bool read_bytes(std::ifstream& in, std::span<std::uint8_t> dst) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): istream reads bytes as char
    in.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(dst.size()));
    return std::cmp_equal(in.gcount(), dst.size());
}
bool seek_to(std::ifstream& in, std::uint64_t offset) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(in);
}

/// The decoded "fmt " chunk.
struct Format {
    WavSampleFormat sample_format = WavSampleFormat::pcm16;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t block_align = 0;
};

/// Where the samples are and how they are laid out.
struct DataLocation {
    WavInfo info;
    std::uint64_t data_offset = 0;  // byte offset of the first sample in the file
    std::size_t bytes_per_sample = 0;
};

Result<WavSampleFormat> parse_sample_format(std::uint16_t tag, std::uint16_t bits) {
    if (tag == k_format_pcm) {
        switch (bits) {
        case 16: return WavSampleFormat::pcm16;
        case 24: return WavSampleFormat::pcm24;
        case 32: return WavSampleFormat::pcm32;
        default: break;
        }
        return fail(ErrorCode::unsupported_format,
                    "PCM with " + std::to_string(bits) + " bits per sample");
    }
    if (tag == k_format_ieee_float) {
        if (bits == 32) {
            return WavSampleFormat::float32;
        }
        return fail(ErrorCode::unsupported_format,
                    "IEEE float with " + std::to_string(bits) + " bits");
    }
    return fail(ErrorCode::unsupported_format,
                "WAV format tag " + std::to_string(tag) + " (compressed)");
}

/// Decodes a fmt chunk body (`chunk` holds up to the first 40 bytes; `size` is the full size).
Result<Format> parse_fmt_chunk(Bytes chunk, std::uint64_t size) {
    if (size < k_min_fmt_size) {
        return fail(ErrorCode::corrupt_data, "fmt chunk too small");
    }
    std::uint16_t tag = read_u16(chunk);
    const std::uint16_t bits = read_u16(chunk.subspan(14));
    if (tag == k_format_extensible) {
        if (size < k_extensible_fmt_size) {
            return fail(ErrorCode::corrupt_data, "WAVE_FORMAT_EXTENSIBLE fmt chunk too small");
        }
        if (!std::ranges::equal(k_ksdataformat_guid_tail, chunk.subspan(26, 14))) {
            return fail(ErrorCode::unsupported_format, "unknown WAVE_FORMAT_EXTENSIBLE sub-format");
        }
        tag = read_u16(chunk.subspan(24));
    }
    auto sample_format = parse_sample_format(tag, bits);
    if (!sample_format) {
        return Unexpected{std::move(sample_format).error()};
    }
    Format format{.sample_format = *sample_format,
                  .channels = read_u16(chunk.subspan(2)),
                  .sample_rate = read_u32(chunk.subspan(4)),
                  .block_align = read_u16(chunk.subspan(12))};
    if (format.channels == 0 || format.sample_rate == 0) {
        return fail(ErrorCode::corrupt_data, "fmt chunk has zero channels or sample rate");
    }
    if (format.block_align != format.channels * (bits / 8)) {
        return fail(ErrorCode::corrupt_data, "fmt block_align does not match channels x bits");
    }
    return format;
}

Result<void> check_riff_header(std::ifstream& in, std::uint64_t file_size) {
    std::array<std::uint8_t, k_riff_header_size> riff{};
    if (!read_bytes(in, riff)) {
        if (file_size >= 4 && tag_is(riff, "RIFF")) {
            return fail(ErrorCode::corrupt_data, "truncated RIFF header");
        }
        return fail(ErrorCode::unsupported_format, "not a RIFF/WAVE file");
    }
    if (tag_is(riff, "RF64")) {
        return fail(ErrorCode::unsupported_format, "RF64 (WAV over 4 GiB) is not supported");
    }
    if (!tag_is(riff, "RIFF") || !tag_is(Bytes{riff}.subspan(8), "WAVE")) {
        return fail(ErrorCode::unsupported_format, "not a RIFF/WAVE file");
    }
    return {};
}

/// Walks the chunk list after the RIFF header until both "fmt " and "data" are found.
Result<DataLocation> locate_data(std::ifstream& in, std::uint64_t file_size) {
    if (auto header = check_riff_header(in, file_size); !header) {
        return Unexpected{std::move(header).error()};
    }
    std::optional<Format> format;
    std::optional<std::uint64_t> data_offset;
    std::uint64_t data_size = 0;

    for (std::uint64_t pos = k_riff_header_size;
         pos + k_chunk_header_size <= file_size && (!format || !data_offset);) {
        std::array<std::uint8_t, k_chunk_header_size> header{};
        if (!seek_to(in, pos) || !read_bytes(in, header)) {
            return fail(ErrorCode::io_error, "failed reading a chunk header");
        }
        const std::uint64_t size = read_u32(Bytes{header}.subspan(4));
        const std::uint64_t body = pos + k_chunk_header_size;
        if (body + size > file_size) {
            return fail(ErrorCode::corrupt_data, "chunk extends past the end of the file");
        }
        if (tag_is(header, "fmt ")) {
            std::array<std::uint8_t, k_extensible_fmt_size> chunk{};
            const auto to_read =
                static_cast<std::size_t>(std::min<std::uint64_t>(size, chunk.size()));
            if (!read_bytes(in, std::span{chunk}.first(to_read))) {
                return fail(ErrorCode::io_error, "failed reading the fmt chunk");
            }
            auto parsed = parse_fmt_chunk(chunk, size);
            if (!parsed) {
                return Unexpected{std::move(parsed).error()};
            }
            format = *parsed;
        } else if (tag_is(header, "data")) {
            data_offset = body;
            data_size = size;
        }
        pos = body + size + (size % 2);  // chunks are padded to even sizes
    }

    if (!format) {
        return fail(ErrorCode::corrupt_data, "no fmt chunk");
    }
    if (!data_offset) {
        return fail(ErrorCode::corrupt_data, "no data chunk");
    }
    if (data_size % format->block_align != 0) {
        return fail(ErrorCode::corrupt_data, "data chunk is not a whole number of frames");
    }
    const WavInfo info{.sample_rate = Hz{static_cast<double>(format->sample_rate)},
                       .channel_count = format->channels,
                       .frame_count =
                           Samples{static_cast<std::int64_t>(data_size / format->block_align)},
                       .format = format->sample_format};
    return DataLocation{.info = info,
                        .data_offset = *data_offset,
                        .bytes_per_sample = std::size_t{format->block_align} / format->channels};
}

Result<DataLocation> open_and_locate(const std::filesystem::path& path, std::ifstream& in) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return fail(ErrorCode::not_found, "no such file: " + path.string());
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return fail(ErrorCode::io_error, "cannot stat " + path.string() + ": " + ec.message());
    }
    in.open(path, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::io_error, "cannot open " + path.string());
    }
    return locate_data(in, size);
}

float decode_sample(Bytes p, WavSampleFormat format) {
    // Divisions by 2^(bits-1) are exact powers of two (design note §7).
    switch (format) {
    case WavSampleFormat::pcm16:
        return static_cast<float>(static_cast<std::int16_t>(read_u16(p))) / 32768.0F;
    case WavSampleFormat::pcm24: {
        const std::uint32_t raw = static_cast<std::uint32_t>(p[0]) |
                                  (static_cast<std::uint32_t>(p[1]) << 8U) |
                                  (static_cast<std::uint32_t>(p[2]) << 16U);
        const auto value = static_cast<std::int32_t>(raw << 8U) / 256;  // sign-extend 24 -> 32
        return static_cast<float>(value) / 8388608.0F;
    }
    case WavSampleFormat::pcm32:
        return static_cast<float>(static_cast<double>(static_cast<std::int32_t>(read_u32(p))) /
                                  2147483648.0);
    case WavSampleFormat::float32: return std::bit_cast<float>(read_u32(p));
    }
    return 0.0F;
}

}  // namespace

std::string_view to_string(WavSampleFormat format) noexcept {
    switch (format) {
    case WavSampleFormat::pcm16: return "pcm16";
    case WavSampleFormat::pcm24: return "pcm24";
    case WavSampleFormat::pcm32: return "pcm32";
    case WavSampleFormat::float32: return "float32";
    }
    return "unknown";
}

Result<WavInfo> read_wav_info(const std::filesystem::path& path) {
    std::ifstream in;
    return open_and_locate(path, in).transform([](const DataLocation& loc) { return loc.info; });
}

Result<AudioBuffer> read_wav(const std::filesystem::path& path) {
    std::ifstream in;
    auto located = open_and_locate(path, in);
    if (!located) {
        return Unexpected{std::move(located).error()};
    }
    const DataLocation& loc = *located;
    auto created =
        AudioBuffer::create(loc.info.channel_count, loc.info.frame_count, loc.info.sample_rate);
    if (!created) {
        return created;
    }
    AudioBuffer audio = std::move(created).value();
    if (!seek_to(in, loc.data_offset)) {
        return fail(ErrorCode::io_error, "cannot seek to the data chunk");
    }

    const auto channels = static_cast<std::size_t>(loc.info.channel_count);
    const std::size_t frame_bytes = channels * loc.bytes_per_sample;
    const auto total_frames = static_cast<std::size_t>(loc.info.frame_count.value());
    std::vector<std::uint8_t> block(k_decode_block_frames * frame_bytes);

    for (std::size_t done = 0; done < total_frames;) {
        const std::size_t frames = std::min(k_decode_block_frames, total_frames - done);
        const auto bytes = std::span{block}.first(frames * frame_bytes);
        if (!read_bytes(in, bytes)) {
            return fail(ErrorCode::io_error, "failed reading sample data");
        }
        for (std::size_t ch = 0; ch < channels; ++ch) {
            auto out = audio.channel(static_cast<int>(ch)).subspan(done, frames);
            for (std::size_t i = 0; i < frames; ++i) {
                const auto at = i * frame_bytes + ch * loc.bytes_per_sample;
                out[i] =
                    decode_sample(Bytes{bytes}.subspan(at, loc.bytes_per_sample), loc.info.format);
            }
        }
        done += frames;
    }
    return audio;
}

Result<void> write_wav(const std::filesystem::path& path, const AudioBuffer& audio) {
    const double rate = audio.sample_rate().value();
    if (rate != std::floor(rate) || rate >= 4294967296.0) {
        return fail(ErrorCode::invalid_argument, "WAV needs a whole sample rate below 2^32 Hz");
    }
    if (audio.channel_count() > std::numeric_limits<std::uint16_t>::max()) {
        return fail(ErrorCode::invalid_argument, "WAV supports at most 65535 channels");
    }
    const auto channels = static_cast<std::uint32_t>(audio.channel_count());
    const auto frames = static_cast<std::uint64_t>(audio.frame_count().value());
    const std::uint64_t data_size = frames * channels * 4U;
    // RIFF size = 4 ("WAVE") + fmt chunk (8 + 18) + fact chunk (8 + 4) + data chunk (8 + data).
    const std::uint64_t riff_size = 4 + 26 + 12 + 8 + data_size;
    if (riff_size > k_riff_size_limit) {
        return fail(ErrorCode::invalid_argument, "audio exceeds the 4 GiB WAV limit");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(riff_size + 8));
    auto u16 = [&bytes](std::uint32_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    };
    auto u32 = [&u16](std::uint32_t v) {
        u16(v & 0xFFFFU);
        u16(v >> 16U);
    };
    auto tag = [&bytes](std::string_view t) { bytes.insert(bytes.end(), t.begin(), t.end()); };

    const auto sample_rate = static_cast<std::uint32_t>(rate);
    tag("RIFF");
    u32(static_cast<std::uint32_t>(riff_size));
    tag("WAVE");
    tag("fmt ");
    u32(18);
    u16(k_format_ieee_float);
    u16(channels);
    u32(sample_rate);
    u32(sample_rate * channels * 4U);
    u16(channels * 4U);
    u16(32);
    u16(0);       // cbSize
    tag("fact");  // required for non-PCM formats
    u32(4);
    u32(static_cast<std::uint32_t>(frames));
    tag("data");
    u32(static_cast<std::uint32_t>(data_size));
    for (std::size_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < audio.channel_count(); ++ch) {
            u32(std::bit_cast<std::uint32_t>(audio.channel(ch)[i]));
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return fail(ErrorCode::io_error, "cannot open " + path.string() + " for writing");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): ostream writes bytes as char
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        return fail(ErrorCode::io_error, "failed writing " + path.string());
    }
    return {};
}

}  // namespace anno::io
