// Test helper: writes a short stereo float32 WAV to argv[1] for the melo-cli process tests.

#include "core/io/wav.h"

#include <cstddef>
#include <cstdio>
#include <span>

// NOLINTNEXTLINE(bugprone-exception-escape): test helper; an escaping exception fails the test
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: write_test_wav <path>\n", stderr);
        return 2;
    }
    auto buf = melo::AudioBuffer::create(2, melo::Samples{4800}, melo::Hz{48000.0});
    if (!buf) {
        return 1;
    }
    buf->channel(0)[10] = 0.25F;
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    const auto written = melo::io::write_wav(args[1], *buf);
    if (!written) {
        std::fprintf(stderr, "write_test_wav: %s\n", written.error().message.c_str());
        return 1;
    }
    return 0;
}
