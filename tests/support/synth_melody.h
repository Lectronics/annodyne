#pragma once

#include "core/types/tracks.h"

#include <cmath>
#include <functional>
#include <vector>

// Synthetic pitch tracks with known notes, for segmentation tests: exact f0 per frame (no estimator
// in the loop), voicing 1 inside notes and 0 outside.

namespace anno::test {

struct MelodyNote {
    double start_s = 0.0;
    double end_s = 0.0;
    double midi = 69.0;  // pitch (A4 = 69)
    std::function<double(double)> bend_cents{};  // optional deviation (cents) at time t since the note start
};

inline PitchTrack make_track(const std::vector<MelodyNote>& notes, double duration_s,
                             double hop_s = 0.005) {
    PitchTrack track;
    const auto n = static_cast<std::int64_t>(std::floor(duration_s / hop_s)) + 1;
    track.grid = FrameGrid{.hop = Seconds{hop_s}, .start = SourceTime{}, .n_frames = n};
    const auto count = static_cast<std::size_t>(n);
    track.f0.assign(count, Cents{std::nan("")});
    track.confidence_prob.assign(count, 0.0);
    track.voicing_prob.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) * hop_s;
        for (const MelodyNote& note : notes) {
            if (t >= note.start_s && t < note.end_s) {
                const double bend = note.bend_cents ? note.bend_cents(t - note.start_s) : 0.0;
                track.f0[i] = Cents{note.midi * 100.0 + bend};
                track.confidence_prob[i] = 1.0;
                track.voicing_prob[i] = 1.0;
            }
        }
    }
    return track;
}

inline OnsetList make_onsets(const std::vector<double>& times_s,
                             OnsetKind kind = OnsetKind::attack) {
    OnsetList list;
    for (const double t : times_s) {
        list.times.push_back(SourceTime{} + Seconds{t});
        list.strength.push_back(3.0);
        list.kind.push_back(kind);
    }
    return list;
}

}  // namespace anno::test
