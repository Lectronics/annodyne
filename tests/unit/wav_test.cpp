#include "core/io/wav.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace anno;
using namespace anno::io;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
}
void put_u32(Bytes& b, std::uint32_t v) {
    put_u16(b, v & 0xFFFFU);
    put_u16(b, v >> 16U);
}
void put_tag(Bytes& b, const char* tag) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<std::uint8_t>(tag[i]));
    }
}
void put_chunk(Bytes& b, const char* tag, const Bytes& body) {
    put_tag(b, tag);
    put_u32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() % 2 != 0) {
        b.push_back(0);  // RIFF pad byte
    }
}

struct FmtSpec {
    std::uint32_t format_tag = 1;  // 1 PCM, 3 float, 0xFFFE extensible
    std::uint32_t channels = 1;
    std::uint32_t sample_rate = 48000;
    std::uint32_t bits = 16;
    std::uint32_t sub_format = 1;  // for extensible
};

Bytes fmt_body(const FmtSpec& f) {
    Bytes body;
    const std::uint32_t block_align = f.channels * f.bits / 8;
    put_u16(body, f.format_tag);
    put_u16(body, f.channels);
    put_u32(body, f.sample_rate);
    put_u32(body, f.sample_rate * block_align);
    put_u16(body, block_align);
    put_u16(body, f.bits);
    if (f.format_tag == 0xFFFE) {
        put_u16(body, 22);  // cbSize
        put_u16(body, f.bits);
        put_u32(body, 0);  // channel mask
        put_u16(body, f.sub_format);
        const std::array<std::uint8_t, 14> guid_tail = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                                        0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        body.insert(body.end(), guid_tail.begin(), guid_tail.end());
    }
    return body;
}

Bytes riff(const std::vector<std::pair<const char*, Bytes>>& chunks) {
    Bytes content;
    put_tag(content, "WAVE");
    for (const auto& [tag, body] : chunks) {
        put_chunk(content, tag, body);
    }
    Bytes file;
    put_tag(file, "RIFF");
    put_u32(file, static_cast<std::uint32_t>(content.size()));
    file.insert(file.end(), content.begin(), content.end());
    return file;
}

std::filesystem::path temp_path(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / "anno_wav_test";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::filesystem::path write_bytes(const std::string& name, const Bytes& bytes) {
    const auto path = temp_path(name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): ostream writes bytes as char
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
}

}  // namespace

TEST_CASE("float32_write_then_read_is_bit_exact", "[wav]") {
    auto buf = AudioBuffer::create(2, Samples{5}, Hz{44100.0}).value();
    const std::array<float, 5> values = {0.0F, -1.0F, 1.0F, 1e-30F, 0.123456789F};
    for (int ch = 0; ch < 2; ++ch) {
        for (std::size_t i = 0; i < 5; ++i) {
            buf.channel(ch)[i] = ch == 0 ? values[i] : -values[i] * 0.5F;
        }
    }
    const auto path = temp_path("roundtrip.wav");
    REQUIRE(write_wav(path, buf).has_value());

    const auto info = read_wav_info(path);
    REQUIRE(info.has_value());
    CHECK(info->format == WavSampleFormat::float32);
    CHECK(info->channel_count == 2);
    CHECK(info->frame_count == Samples{5});
    CHECK(info->sample_rate == Hz{44100.0});

    const auto back = read_wav(path);
    REQUIRE(back.has_value());
    for (int ch = 0; ch < 2; ++ch) {
        for (std::size_t i = 0; i < 5; ++i) {
            CHECK(std::bit_cast<std::uint32_t>(back->channel(ch)[i]) ==
                  std::bit_cast<std::uint32_t>(buf.channel(ch)[i]));
        }
    }
}

TEST_CASE("pcm16_decodes_exactly", "[wav]") {
    Bytes data;
    put_u16(data, 0x0000);  // 0
    put_u16(data, 0x4000);  // 16384 -> 0.5
    put_u16(data, 0x8000);  // -32768 -> -1.0
    put_u16(data, 0x7FFF);  // 32767
    const auto path = write_bytes("pcm16.wav", riff({{"fmt ", fmt_body({})}, {"data", data}}));
    const auto buf = read_wav(path);
    REQUIRE(buf.has_value());
    REQUIRE(buf->frame_count() == Samples{4});
    CHECK(buf->channel(0)[0] == 0.0F);
    CHECK(buf->channel(0)[1] == 0.5F);
    CHECK(buf->channel(0)[2] == -1.0F);
    CHECK(buf->channel(0)[3] == 32767.0F / 32768.0F);
}

TEST_CASE("pcm24_stereo_decodes_and_deinterleaves", "[wav]") {
    Bytes data;
    // frame 0: L = 0x400000 (0.5), R = 0xC00000 (-0.5); frame 1: L = 1, R = -1 (0xFFFFFF)
    const std::array<std::uint8_t, 12> raw = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0,
                                              0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF};
    data.insert(data.end(), raw.begin(), raw.end());
    FmtSpec f;
    f.channels = 2;
    f.bits = 24;
    const auto path = write_bytes("pcm24.wav", riff({{"fmt ", fmt_body(f)}, {"data", data}}));
    const auto buf = read_wav(path);
    REQUIRE(buf.has_value());
    REQUIRE(buf->channel_count() == 2);
    REQUIRE(buf->frame_count() == Samples{2});
    CHECK(buf->channel(0)[0] == 0.5F);
    CHECK(buf->channel(1)[0] == -0.5F);
    CHECK(buf->channel(0)[1] == 1.0F / 8388608.0F);
    CHECK(buf->channel(1)[1] == -1.0F / 8388608.0F);
}

TEST_CASE("pcm32_decodes_with_float_rounding", "[wav]") {
    Bytes data;
    put_u32(data, 0x40000000U);  // 0.5 exactly
    put_u32(data, 0x80000000U);  // -1.0 exactly
    FmtSpec f;
    f.bits = 32;
    const auto path = write_bytes("pcm32.wav", riff({{"fmt ", fmt_body(f)}, {"data", data}}));
    const auto buf = read_wav(path);
    REQUIRE(buf.has_value());
    CHECK(buf->channel(0)[0] == 0.5F);
    CHECK(buf->channel(0)[1] == -1.0F);
    const auto info = read_wav_info(path);
    CHECK(info->format == WavSampleFormat::pcm32);
}

TEST_CASE("extensible_header_and_unknown_chunks_are_handled", "[wav]") {
    FmtSpec f;
    f.format_tag = 0xFFFE;
    f.bits = 32;
    f.sub_format = 3;  // IEEE float
    Bytes data;
    put_u32(data, std::bit_cast<std::uint32_t>(0.25F));
    const Bytes odd_chunk = {1, 2, 3};  // odd size: exercises the pad byte
    const auto path = write_bytes(
        "extensible.wav",
        riff({{"LIST", odd_chunk}, {"fmt ", fmt_body(f)}, {"junk", Bytes(10)}, {"data", data}}));
    const auto buf = read_wav(path);
    REQUIRE(buf.has_value());
    CHECK(buf->channel(0)[0] == 0.25F);
    CHECK(read_wav_info(path)->format == WavSampleFormat::float32);
}

TEST_CASE("missing_file_is_not_found", "[wav]") {
    const auto r = read_wav(temp_path("does_not_exist.wav"));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::not_found);
}

TEST_CASE("non_riff_files_are_unsupported", "[wav]") {
    const Bytes text = {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!', '!', '!'};
    CHECK(read_wav(write_bytes("text.wav", text)).error().code == ErrorCode::unsupported_format);

    Bytes rf64 = riff({{"fmt ", fmt_body({})}, {"data", Bytes(2)}});
    rf64[0] = 'R';
    rf64[1] = 'F';
    rf64[2] = '6';
    rf64[3] = '4';
    CHECK(read_wav(write_bytes("rf64.wav", rf64)).error().code == ErrorCode::unsupported_format);
}

TEST_CASE("unsupported_encodings_are_rejected", "[wav]") {
    FmtSpec adpcm;
    adpcm.format_tag = 2;
    adpcm.bits = 4;
    CHECK(read_wav(write_bytes("adpcm.wav", riff({{"fmt ", fmt_body(adpcm)}, {"data", Bytes(4)}})))
              .error()
              .code == ErrorCode::unsupported_format);

    FmtSpec pcm8;
    pcm8.bits = 8;
    CHECK(read_wav(write_bytes("pcm8.wav", riff({{"fmt ", fmt_body(pcm8)}, {"data", Bytes(4)}})))
              .error()
              .code == ErrorCode::unsupported_format);

    FmtSpec float64;
    float64.format_tag = 3;
    float64.bits = 64;
    CHECK(read_wav(
              write_bytes("float64.wav", riff({{"fmt ", fmt_body(float64)}, {"data", Bytes(8)}})))
              .error()
              .code == ErrorCode::unsupported_format);
}

TEST_CASE("truncated_and_inconsistent_files_are_corrupt", "[wav]") {
    Bytes good = riff({{"fmt ", fmt_body({})}, {"data", Bytes(8)}});

    Bytes truncated(good.begin(), good.end() - 4);  // data chunk claims more than the file holds
    CHECK(read_wav(write_bytes("truncated.wav", truncated)).error().code ==
          ErrorCode::corrupt_data);

    const Bytes header_only(good.begin(), good.begin() + 20);
    CHECK(read_wav(write_bytes("header_only.wav", header_only)).error().code ==
          ErrorCode::corrupt_data);

    CHECK(read_wav(write_bytes("no_data.wav", riff({{"fmt ", fmt_body({})}}))).error().code ==
          ErrorCode::corrupt_data);
    CHECK(read_wav(write_bytes("no_fmt.wav", riff({{"data", Bytes(8)}}))).error().code ==
          ErrorCode::corrupt_data);

    // Data size not a whole number of frames.
    CHECK(read_wav(
              write_bytes("partial_frame.wav", riff({{"fmt ", fmt_body({})}, {"data", Bytes(3)}})))
              .error()
              .code == ErrorCode::corrupt_data);

    // block_align inconsistent with channels * bits.
    Bytes bad_fmt = fmt_body({});
    bad_fmt[12] = 7;
    CHECK(read_wav(write_bytes("bad_align.wav", riff({{"fmt ", bad_fmt}, {"data", Bytes(8)}})))
              .error()
              .code == ErrorCode::corrupt_data);
}

TEST_CASE("write_rejects_fractional_sample_rates", "[wav]") {
    const auto buf = AudioBuffer::create(1, Samples{1}, Hz{44100.5}).value();
    CHECK(write_wav(temp_path("fractional.wav"), buf).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("write_to_an_unwritable_path_is_an_io_error", "[wav]") {
    const auto buf = AudioBuffer::create(1, Samples{1}, Hz{48000.0}).value();
    const auto path = temp_path("no_such_dir") / "sub" / "x.wav";
    CHECK(write_wav(path, buf).error().code == ErrorCode::io_error);
}

TEST_CASE("sample_format_names_are_stable", "[wav]") {
    CHECK(to_string(WavSampleFormat::pcm16) == "pcm16");
    CHECK(to_string(WavSampleFormat::pcm24) == "pcm24");
    CHECK(to_string(WavSampleFormat::pcm32) == "pcm32");
    CHECK(to_string(WavSampleFormat::float32) == "float32");
}
