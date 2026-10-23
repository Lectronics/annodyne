#include "core/analysis/gaps/classify_gap_frames.h"

#include <cmath>

namespace anno {

namespace {

constexpr double k_min_rate_hz = 8000.0;  // measure_frames rejects lower rates

Unexpected invalid(const char* what) {
    return fail(ErrorCode::invalid_argument, std::string{"classify_gap_frames: "} + what);
}

Result<void> check_inputs(const MonoAudio& mono, const ActivityTrack& activity,
                          const NoiseFloor& floor) {
    if (mono.samples.empty()) {
        return invalid("the recording is empty");
    }
    if (!(mono.sample_rate.value() >= k_min_rate_hz)) {
        return invalid("the sample rate is below 8 kHz");
    }
    if (!(activity.grid == floor.grid)) {
        return invalid("the floor and the activity track are on different grids");
    }
    if (auto r = validate(activity.grid); !r) {
        return Unexpected{r.error()};
    }
    const auto n = static_cast<std::size_t>(activity.grid.n_frames);
    if (activity.level.size() != n || floor.curve_dbfs.size() != n) {
        return invalid("the activity levels and the floor curve must match their grid");
    }
    return {};
}

/// What measure_frames reads of a note, for a recording that is judged as unvoiced everywhere.
struct UnvoicedInputs {
    std::vector<Cents> zero_curve;  // pitch_drift and pitch_mod: read on every frame
    std::vector<double> voicing;    // all 0
    HarmonicTrack harmonics;        // rows = frames, no harmonics
};

UnvoicedInputs unvoiced_inputs(const FrameGrid& grid) {
    const auto n = static_cast<std::size_t>(grid.n_frames);
    UnvoicedInputs in;
    in.zero_curve.assign(n, Cents{0.0});
    in.voicing.assign(n, 0.0);
    in.harmonics.grid = grid;
    in.harmonics.freq_hz = {.rows = grid.n_frames, .cols = 0, .data = {}};
    in.harmonics.amp_linear = {.rows = grid.n_frames, .cols = 0, .data = {}};
    return in;
}

}  // namespace

Result<GapEvidence> classify_gap_frames(const MonoAudio& mono, const ActivityTrack& activity,
                                        const NoiseFloor& floor, const GapEvidenceParams& params) {
    if (auto r = check_inputs(mono, activity, floor); !r) {
        return Unexpected{r.error()};
    }
    GapEvidence out;
    out.grid = activity.grid;
    const auto n = static_cast<std::size_t>(activity.grid.n_frames);
    if (n == 0) {
        return out;
    }
    const UnvoicedInputs in = unvoiced_inputs(activity.grid);
    const NoteCurves curves{.grid = activity.grid,
                            .pitch_center = Cents{0.0},
                            .pitch_drift = in.zero_curve,
                            .pitch_mod = in.zero_curve};
    HarmonicNoiseParams features = params.features;
    features.silence_level = k_level_floor_db;  // the floor, not an absolute level, decides
    const PitchEvidence evidence{.voicing_prob = in.voicing, .alternate_f0 = {}};
    auto metrics =
        measure_frames(mono.samples, mono.sample_rate, curves, in.harmonics, evidence, features);
    if (!metrics) {
        return Unexpected{metrics.error()};
    }
    auto classes = classify_frames(*metrics, in.voicing, activity.grid.hop, features);
    if (!classes) {
        return Unexpected{classes.error()};
    }
    out.frame_class = std::move(*classes);
    out.excess_db.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.excess_db.emplace_back(activity.level[i].value() - floor.curve_dbfs[i].value());
    }
    return out;
}

}  // namespace anno
