// Runs PYinEstimator with different threshold priors on a WAV and prints the voiced share in a window.
// Usage: beta_probe <wav> <from_s> <to_s>
#include "core/analysis/pitch/pyin.h"
#include "core/io/wav.h"
#include "core/types/audio_source.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    auto buf = anno::io::read_wav(argv[1]);
    if (!buf) return 3;
    const auto src = anno::AudioSource::from_buffer(*buf);
    const double from = std::atof(argv[2]);
    const double to = std::atof(argv[3]);
    for (double mean : {0.10, 0.15, 0.20, 0.30}) {
        anno::PYinParams yp;
        yp.beta_mean = mean;
        auto track = anno::PYinEstimator{yp}.estimate(src, anno::PitchParams{});
        if (!track) return 4;
        const double hop = track->grid.hop.value();
        int voiced = 0, total = 0;
        for (auto i = static_cast<std::size_t>(from / hop); i < static_cast<std::size_t>(to / hop); ++i) {
            voiced += anno::is_voiced(*track, i) ? 1 : 0;
            ++total;
        }
        std::printf("  beta mean %.2f: voiced %3.0f%%\n", mean, 100.0 * voiced / total);
    }
    return 0;
}
