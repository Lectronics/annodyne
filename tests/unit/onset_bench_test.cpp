#include "core/analysis/onsets/detail/superflux.h"
#include "core/analysis/onsets/onsets.h"
#include "core/io/wav.h"
#include "support/onset_bench.h"
#include "support/onset_score.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace anno;
using namespace anno::test;
using namespace anno::test::onset_bench;

namespace {

double seconds_of(const Family& f) {
    return static_cast<double>(f.samples.size()) / k_rate;
}

float peak_of(const Family& f) {
    float p = 0.0F;
    for (const float v : f.samples) {
        p = std::max(p, std::abs(v));
    }
    return p;
}

/// FNV-1a over the bytes of the values: a bit-exact fingerprint.
std::uint64_t fingerprint(const OnsetList& list) {
    std::uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](const void* data, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ b[i]) * 1099511628211ULL;
        }
    };
    for (std::size_t i = 0; i < list.times.size(); ++i) {
        const double t = (list.times[i] - SourceTime{}).value();
        mix(&t, sizeof t);
        mix(&list.strength[i], sizeof(double));
        const auto k = static_cast<int>(list.kind[i]);
        mix(&k, sizeof k);
    }
    return h;
}

std::uint64_t fingerprint(const Activation& a) {
    std::uint64_t h = 1469598103934665603ULL;
    const double hop = a.hop.value();
    const auto mix = [&h](const void* data, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ b[i]) * 1099511628211ULL;
        }
    };
    mix(&hop, sizeof hop);
    mix(a.values.data(), a.values.size() * sizeof(double));
    return h;
}

/// The signals of the bit-exact gate: every family, the level sweep of family A and a real
/// recording.
struct GateSignal {
    std::string name;
    std::vector<float> samples;
    double rate;
};

/// The activation as detect_onsets computes it. Phase 3 form; the stage refactor swaps this body
/// for band_spectrogram -> log_compress -> superflux and the gate pins that they agree.
Activation onset_activation(const std::vector<float>& samples, Hz rate, const OnsetParams& params) {
    return superflux_activation(samples, rate, params).value();
}

std::vector<GateSignal> gate_signals() {
    std::vector<GateSignal> out;
    for (const Family& f : {family_a(), family_a0(), family_b(), family_c(), family_d(), family_e(),
                            family_f(), family_g()}) {
        out.push_back({f.name, f.samples, k_rate});
    }
    out.push_back({"A_minus30dB", attenuated(family_a().samples, -30.0), k_rate});
#ifdef ANNO_GOLDEN_DIR
    const auto wav = std::filesystem::path{ANNO_GOLDEN_DIR} / "Audio" / "vocadito_2.wav";
    if (auto audio = io::read_wav(wav)) {
        const auto channel = audio->channel(0);
        const std::size_t n = std::min<std::size_t>(channel.size(), 12 * 44100);
        out.push_back(
            {"vocadito_2_first_12s",
             std::vector<float>(channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(n)),
             audio->sample_rate().value()});
    }
#endif
    return out;
}

/// Parameter sets of the gate: the defaults, an even filter width (accepted today), plain flux.
std::vector<std::pair<std::string, OnsetParams>> gate_params() {
    OnsetParams even;
    even.max_filter_bins = 4;
    OnsetParams plain;
    plain.max_filter_bins = 1;
    OnsetParams other;
    other.log_lambda = 1.0;
    other.lag = Seconds{0.010};
    return {
        {"default", OnsetParams{}}, {"even4", even}, {"plain", plain}, {"lambda1_lag10", other}};
}

}  // namespace

TEST_CASE("the_bench_families_have_the_lengths_truth_counts_and_peak_of_gen_py", "[onset_bench]") {
    // gen.py prints (duration s, onset count) per signal.
    const struct {
        Family f;
        double seconds;
        std::size_t onsets;
    } cases[] = {{family_a(), 5.3, 8}, {family_a0(), 5.3, 8}, {family_b(), 5.3, 8},
                 {family_c(), 4.5, 8}, {family_d(), 4.5, 1},  {family_e(), 6.3, 8},
                 {family_f(), 5.3, 8}, {family_g(), 2.5, 16}};
    for (const auto& c : cases) {
        INFO(c.f.name);
        CHECK(seconds_of(c.f) == Catch::Approx(c.seconds).margin(0.01));
        CHECK(c.f.truth.size() == c.onsets);
        CHECK(peak_of(c.f) == Catch::Approx(0.5F).margin(1e-6));
    }
    CHECK(family_e().consonant_truth.size() == 8);
}

TEST_CASE("the_bench_is_deterministic_and_the_truth_is_sorted_and_inside_the_signal",
          "[onset_bench]") {
    for (const Family& f : {family_a(), family_c(), family_e(), family_f(), family_g()}) {
        INFO(f.name);
        CHECK(std::ranges::is_sorted(f.truth));
        CHECK(f.truth.front() > 0.0);
        CHECK(f.truth.back() < seconds_of(f));
    }
    CHECK(family_f().samples == family_f().samples);
    CHECK(family_e().samples == family_e().samples);
}

TEST_CASE("an_onset_appears_in_the_signal_at_its_truth_time", "[onset_bench]") {
    for (const Family& f : {family_a(), family_g()}) {
        INFO(f.name);
        for (const double t : f.truth) {
            const auto at = static_cast<std::size_t>(t * k_rate);
            double before = 0.0;
            double after = 0.0;
            for (std::size_t i = 0; i < 441; ++i) {  // 10 ms
                before += std::abs(static_cast<double>(f.samples[at - 441 + i]));
                after += std::abs(static_cast<double>(f.samples[at + 441 + i]));
            }
            CHECK(after > 20.0 * (before + 1e-9));
        }
    }
}

TEST_CASE("the_scorer_matches_greedily_with_an_inclusive_tolerance_like_run_all_py",
          "[onset_bench]") {
    // Exact tolerance counts as a match.
    auto s = score_onsets({1.025}, {1.0}, 0.025);
    CHECK(s.tp == 1);
    CHECK(s.offsets_s.front() == Catch::Approx(0.025));
    // One detection cannot match two truths; the first truth takes it.
    s = score_onsets({1.01}, {1.0, 1.02}, 0.025);
    CHECK(s.tp == 1);
    CHECK(s.fn == 1);
    CHECK(s.fp == 0);
    // The nearest of two candidates is taken; the other is a false positive.
    s = score_onsets({0.99, 1.003}, {1.0}, 0.025);
    CHECK(s.tp == 1);
    CHECK(s.fp == 1);
    CHECK(s.offsets_s.front() == Catch::Approx(0.003));
    // Greedy by truth order, not optimal: truth 1.0 takes 1.02 (nearest unused) leaving 1.03's
    // truth with nothing within 25 ms... here 1.04 is the only candidate for 1.045.
    s = score_onsets({1.02, 1.04}, {1.0, 1.045}, 0.025);
    CHECK(s.tp == 2);
    // Median offset and pooling.
    s = score_onsets({1.01, 2.03, 3.0}, {1.0, 2.0, 3.0}, 0.05);
    CHECK(s.median_offset_s() == Catch::Approx(0.01));
    OnsetScore total;
    accumulate(total, s);
    accumulate(total, s);
    CHECK(total.tp == 6);
    CHECK(s.f_measure() == Catch::Approx(1.0));
    CHECK(score_onsets({}, {1.0}, 0.025).fn == 1);
    CHECK(score_onsets({1.0}, {}, 0.025).fp == 1);
}

// Prints the baseline of detect_onsets on the research's families and level sweep. Hidden; run
// with "[onset_baseline]" to refresh the pinned bounds in the pipeline tests.
TEST_CASE("onset_baseline_print", "[onset_baseline][.]") {
    const auto run = [](const std::vector<float>& x) {
        auto src = AudioSource::from_buffer(to_buffer(x, k_rate));
        const auto list = detect_onsets(src, OnsetParams{}).value();
        std::vector<double> t;
        for (const SourceTime s : list.times) {
            t.push_back((s - SourceTime{}).value());
        }
        return t;
    };
    for (const Family& f : {family_a(), family_a0(), family_b(), family_c(), family_d(), family_e(),
                            family_f(), family_g()}) {
        const auto s = score_onsets(run(f.samples), f.truth, 0.025);
        std::fprintf(stderr, "BASE %-20s TP %d FP %d FN %d median %+.1f ms\n", f.name.c_str(), s.tp,
                     s.fp, s.fn, 1000.0 * s.median_offset_s());
    }
    for (const double db : {0.0, -10.0, -20.0, -30.0, -40.0}) {
        OnsetScore total;
        for (const Family& f : pooled_families()) {
            accumulate(total, score_onsets(run(attenuated(f.samples, db)), f.truth, 0.025));
        }
        std::fprintf(stderr, "LEVEL %+4.0f dB pooled TP %d FP %d FN %d F %.3f median %+.1f ms\n",
                     db, total.tp, total.fp, total.fn, total.f_measure(),
                     1000.0 * total.median_offset_s());
    }
}

// Prints the fingerprints that the bit-exact gate pins. Hidden.
TEST_CASE("onset_gate_print", "[onset_baseline][.]") {
    for (const auto& signal : gate_signals()) {
        const auto src = AudioSource::from_buffer(to_buffer(signal.samples, signal.rate));
        for (const auto& [name, params] : gate_params()) {
            const auto list = detect_onsets(src, params).value();
            const auto act = onset_activation(signal.samples, Hz{signal.rate}, params);
            std::fprintf(stderr, "GATE {\"%s\", \"%s\", 0x%016llxULL, 0x%016llxULL, %zu},\n",
                         signal.name.c_str(), name.c_str(),
                         static_cast<unsigned long long>(fingerprint(list)),
                         static_cast<unsigned long long>(fingerprint(act)), list.times.size());
        }
    }
}

namespace {

struct GateRow {
    const char* signal;
    const char* params;
    std::uint64_t onsets;      // fingerprint of detect_onsets' OnsetList
    std::uint64_t activation;  // fingerprint of the SuperFlux activation
    std::size_t count;
};

// Captured from detect_onsets / superflux_activation BEFORE the stage refactor (2026-10-22). The
// refactor must reproduce them bit for bit (docs/design/phase5-transient-pipeline.md).
constexpr GateRow k_gate[] = {
    {"A_sharp", "default", 0x062c04b7f256bee0ULL, 0xcf753fcdc49bcbdeULL, 8},
    {"A_sharp", "even4", 0x062c04b7f256bee0ULL, 0xcf753fcdc49bcbdeULL, 8},
    {"A_sharp", "plain", 0x19a8a63b54210db0ULL, 0xd126be09b96e3089ULL, 11},
    {"A_sharp", "lambda1_lag10", 0x36c91e8d6642916cULL, 0xe235c52b84008fadULL, 8},
    {"A0_sharp_novib", "default", 0x2fc9399ee8a8ac49ULL, 0x68a232ac3c627b41ULL, 8},
    {"A0_sharp_novib", "even4", 0x2fc9399ee8a8ac49ULL, 0x68a232ac3c627b41ULL, 8},
    {"A0_sharp_novib", "plain", 0xd0d5eff988d20c34ULL, 0xcf532c75c2f266e9ULL, 8},
    {"A0_sharp_novib", "lambda1_lag10", 0xd0a1ca200c174d66ULL, 0x3c5f61716b695ccbULL, 8},
    {"B_soft80ms", "default", 0x0375575697be81f9ULL, 0x79c06e3d506ad702ULL, 8},
    {"B_soft80ms", "even4", 0x0375575697be81f9ULL, 0x79c06e3d506ad702ULL, 8},
    {"B_soft80ms", "plain", 0x9620e767896ab49aULL, 0x0eab557270197869ULL, 10},
    {"B_soft80ms", "lambda1_lag10", 0x14650fb0739d0383ULL, 0xeb2c55f2071e7f39ULL, 0},
    {"C_legato", "default", 0x90f3e24d988b1c71ULL, 0x5f5c648c2e609462ULL, 11},
    {"C_legato", "even4", 0x90f3e24d988b1c71ULL, 0x5f5c648c2e609462ULL, 11},
    {"C_legato", "plain", 0x3388260d0c2c1bc9ULL, 0x59d7269785ed84c6ULL, 16},
    {"C_legato", "lambda1_lag10", 0x440ae1076fd2b78cULL, 0x673d6adb6613100aULL, 1},
    {"D_vib_trem", "default", 0x307659d99e2b986cULL, 0x0c32545406736262ULL, 24},
    {"D_vib_trem", "even4", 0x307659d99e2b986cULL, 0x0c32545406736262ULL, 24},
    {"D_vib_trem", "plain", 0x894dd48d0e01ddc0ULL, 0xcb4bf6be987cb5dcULL, 24},
    {"D_vib_trem", "lambda1_lag10", 0x201c446dc2382dddULL, 0xe1a02a9ec1485263ULL, 24},
    {"E_consonants", "default", 0x092abf456f099c91ULL, 0x56d1dd57f4bd3489ULL, 16},
    {"E_consonants", "even4", 0x092abf456f099c91ULL, 0x56d1dd57f4bd3489ULL, 16},
    {"E_consonants", "plain", 0x9b16cc49774f46c4ULL, 0x4b692d68499fb83aULL, 21},
    {"E_consonants", "lambda1_lag10", 0x4265f53e204ef8a0ULL, 0xfc87729b35a8f59dULL, 13},
    {"F_sharp_noise30dB", "default", 0x003db198a74672b5ULL, 0x366b0e2ebaacde3cULL, 16},
    {"F_sharp_noise30dB", "even4", 0x003db198a74672b5ULL, 0x366b0e2ebaacde3cULL, 16},
    {"F_sharp_noise30dB", "plain", 0xdd79d5165f893bb9ULL, 0x6619228ddef46fb0ULL, 18},
    {"F_sharp_noise30dB", "lambda1_lag10", 0x73c511c10f1a6b90ULL, 0x59e86534fb4d36feULL, 16},
    {"G_fast_8perSec", "default", 0xc24ddd9bb55c2850ULL, 0xc27c3c6b46b0777cULL, 30},
    {"G_fast_8perSec", "even4", 0xc24ddd9bb55c2850ULL, 0xc27c3c6b46b0777cULL, 30},
    {"G_fast_8perSec", "plain", 0x117e9961af715ca9ULL, 0xf4ba37ef33fae86dULL, 29},
    {"G_fast_8perSec", "lambda1_lag10", 0x5145c572edf6ca5bULL, 0x7cfa663b16516e4fULL, 31},
    {"A_minus30dB", "default", 0xb5a935cc1fe5837bULL, 0x941370465261b8c7ULL, 8},
    {"A_minus30dB", "even4", 0xb5a935cc1fe5837bULL, 0x941370465261b8c7ULL, 8},
    {"A_minus30dB", "plain", 0x4b49c475e91e221aULL, 0xc24be7e6a4980267ULL, 10},
    {"A_minus30dB", "lambda1_lag10", 0x14650fb0739d0383ULL, 0x8c5e6e59becc0fa9ULL, 0},
    {"vocadito_2_first_12s", "default", 0xcfcc46ad03590b41ULL, 0xfd9154154f9b9f36ULL, 29},
    {"vocadito_2_first_12s", "even4", 0xcfcc46ad03590b41ULL, 0xfd9154154f9b9f36ULL, 29},
    {"vocadito_2_first_12s", "plain", 0x9a03fd05fea6afc1ULL, 0x7874ce9f0e3b2ef1ULL, 47},
    {"vocadito_2_first_12s", "lambda1_lag10", 0x6da6b67465937783ULL, 0x66ade3ed3cad3bd4ULL, 22},
};

}  // namespace

TEST_CASE("detect_onsets_and_its_activation_are_bit_identical_to_the_phase_3_values",
          "[onset_gate]") {
    const auto signals = gate_signals();
    const auto params = gate_params();
    for (const GateRow& row : k_gate) {
        const auto signal = std::ranges::find(signals, std::string{row.signal}, &GateSignal::name);
        const auto set = std::ranges::find(params, std::string{row.params},
                                           &std::pair<std::string, OnsetParams>::first);
        if (signal == signals.end() || set == params.end()) {
            continue;  // the real recording is not part of every checkout
        }
        INFO(row.signal << " / " << row.params);
        const auto src = AudioSource::from_buffer(to_buffer(signal->samples, signal->rate));
        const auto list = detect_onsets(src, set->second).value();
        CHECK(list.times.size() == row.count);
        CHECK(fingerprint(list) == row.onsets);
        CHECK(fingerprint(onset_activation(signal->samples, Hz{signal->rate}, set->second)) ==
              row.activation);
    }
}
