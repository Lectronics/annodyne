#include "core/analysis/onsets/detail/superflux.h"
#include "core/analysis/onsets/onset_stages.h"
#include "core/analysis/onsets/onsets.h"
#include "core/io/wav.h"
#include "support/onset_bench.h"
#include "support/onset_score.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>

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

/// FNV-1a over the values rounded to 1e-9 (absolute). Doubles are not hashed bit for bit: libm and
/// FMA contraction differ in the last bits between macOS, Linux and Windows, and a bit hash
/// recorded on one failed on the others although every detection was identical.
class Fingerprint {
public:
    void add(double value) { add_bytes(static_cast<std::int64_t>(std::llround(value * 1e9))); }
    void add(int value) { add_bytes(value); }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    template <class T>
    void add_bytes(const T& value) {
        for (const auto byte : std::bit_cast<std::array<unsigned char, sizeof(T)>>(value)) {
            hash_ = (hash_ ^ byte) * 1099511628211ULL;
        }
    }
    std::uint64_t hash_ = 1469598103934665603ULL;
};

std::uint64_t fingerprint(const OnsetList& list) {
    Fingerprint h;
    for (std::size_t i = 0; i < list.times.size(); ++i) {
        h.add((list.times[i] - SourceTime{}).value());
        h.add(list.strength[i]);
        h.add(static_cast<int>(list.kind[i]));
    }
    return h.value();
}

std::uint64_t fingerprint(const Activation& a) {
    Fingerprint h;
    h.add(a.hop.value());
    for (const double v : a.values) {
        h.add(v);
    }
    return h.value();
}

std::vector<double> times_of(const OnsetList& list) {
    std::vector<double> t;
    t.reserve(list.times.size());
    for (const SourceTime s : list.times) {
        t.push_back((s - SourceTime{}).value());
    }
    return t;
}

/// The signals of the bit-exact gate: every family, family A at -30 dB and a real recording.
struct GateSignal {
    std::string name;
    std::vector<float> samples;
    double rate = k_rate;
};

std::vector<GateSignal> gate_signals() {
    std::vector<GateSignal> out;
    for (const Family& f : {family_a(), family_a0(), family_b(), family_c(), family_d(), family_e(),
                            family_f(), family_g()}) {
        out.push_back({.name = f.name, .samples = f.samples, .rate = k_rate});
    }
    out.push_back(
        {.name = "A_minus30dB", .samples = attenuated(family_a().samples, -30.0), .rate = k_rate});
#ifdef ANNO_GOLDEN_DIR
    const auto wav = std::filesystem::path{ANNO_GOLDEN_DIR} / "Audio" / "vocadito_2.wav";
    if (auto audio = io::read_wav(wav)) {
        const auto channel = audio->channel(0);
        const std::size_t n = std::min<std::size_t>(channel.size(), std::size_t{12} * 44100);
        out.push_back({.name = "vocadito_2_first_12s",
                       .samples = std::vector<float>(
                           channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(n)),
                       .rate = audio->sample_rate().value()});
    }
#endif
    return out;
}

/// Parameter sets of the gate: the defaults, an even filter width (accepted today), plain flux and
/// a second parameter set.
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

struct GateRow {
    const char* signal;
    const char* params;
    std::uint64_t onsets;      // fingerprint of detect_onsets' OnsetList
    std::uint64_t activation;  // fingerprint of the SuperFlux activation
    std::size_t count;
};

// Captured from detect_onsets / superflux_activation BEFORE the stage refactor (2026-10-22;
// bit-exact then, re-recorded as 1e-9-rounded hashes the same day so the gate also holds on Linux
// and Windows, with every onset count unchanged). The refactor must reproduce them
// (docs/design/phase5-transient-pipeline.md).
constexpr std::array<GateRow, 40> k_gate = {{
    GateRow{.signal = "A_sharp",
            .params = "default",
            .onsets = 0xeab80e1dd7120a70ULL,
            .activation = 0xfec8f9f726208f95ULL,
            .count = 8},
    GateRow{.signal = "A_sharp",
            .params = "even4",
            .onsets = 0xeab80e1dd7120a70ULL,
            .activation = 0xfec8f9f726208f95ULL,
            .count = 8},
    GateRow{.signal = "A_sharp",
            .params = "plain",
            .onsets = 0x6d015367b8b7c436ULL,
            .activation = 0x2c368373af0ac353ULL,
            .count = 11},
    GateRow{.signal = "A_sharp",
            .params = "lambda1_lag10",
            .onsets = 0xaee500d5e14e4bccULL,
            .activation = 0x5cffd3586d5c996dULL,
            .count = 8},
    GateRow{.signal = "A0_sharp_novib",
            .params = "default",
            .onsets = 0xabaaa0f437b85eaeULL,
            .activation = 0x7eadf694e419459bULL,
            .count = 8},
    GateRow{.signal = "A0_sharp_novib",
            .params = "even4",
            .onsets = 0xabaaa0f437b85eaeULL,
            .activation = 0x7eadf694e419459bULL,
            .count = 8},
    GateRow{.signal = "A0_sharp_novib",
            .params = "plain",
            .onsets = 0xb11a040e65c81e6aULL,
            .activation = 0x2b021bbf0e34021dULL,
            .count = 8},
    GateRow{.signal = "A0_sharp_novib",
            .params = "lambda1_lag10",
            .onsets = 0x0ae86883ee30a813ULL,
            .activation = 0x6231536bfc336ef2ULL,
            .count = 8},
    GateRow{.signal = "B_soft80ms",
            .params = "default",
            .onsets = 0x6de48657ccbd0e1eULL,
            .activation = 0x373029c305476bdfULL,
            .count = 8},
    GateRow{.signal = "B_soft80ms",
            .params = "even4",
            .onsets = 0x6de48657ccbd0e1eULL,
            .activation = 0x373029c305476bdfULL,
            .count = 8},
    GateRow{.signal = "B_soft80ms",
            .params = "plain",
            .onsets = 0xa20f5545aa29963dULL,
            .activation = 0x69aab6706f3dae70ULL,
            .count = 10},
    GateRow{.signal = "B_soft80ms",
            .params = "lambda1_lag10",
            .onsets = 0x14650fb0739d0383ULL,
            .activation = 0x4c0c96112798fd95ULL,
            .count = 0},
    GateRow{.signal = "C_legato",
            .params = "default",
            .onsets = 0xa63e3cf6a18ed4cbULL,
            .activation = 0x1a5e2cf3c0346e3dULL,
            .count = 11},
    GateRow{.signal = "C_legato",
            .params = "even4",
            .onsets = 0xa63e3cf6a18ed4cbULL,
            .activation = 0x1a5e2cf3c0346e3dULL,
            .count = 11},
    GateRow{.signal = "C_legato",
            .params = "plain",
            .onsets = 0x48156814982822f7ULL,
            .activation = 0x69e4d586804cca45ULL,
            .count = 16},
    GateRow{.signal = "C_legato",
            .params = "lambda1_lag10",
            .onsets = 0xbf753a2202c74444ULL,
            .activation = 0x6fcd7977f97d68b3ULL,
            .count = 1},
    GateRow{.signal = "D_vib_trem",
            .params = "default",
            .onsets = 0x0f9b8f8a0875a9feULL,
            .activation = 0xd8008059dba43268ULL,
            .count = 24},
    GateRow{.signal = "D_vib_trem",
            .params = "even4",
            .onsets = 0x0f9b8f8a0875a9feULL,
            .activation = 0xd8008059dba43268ULL,
            .count = 24},
    GateRow{.signal = "D_vib_trem",
            .params = "plain",
            .onsets = 0x8cd5ac99f9fa0ef8ULL,
            .activation = 0xd66c83d9b379b0c4ULL,
            .count = 24},
    GateRow{.signal = "D_vib_trem",
            .params = "lambda1_lag10",
            .onsets = 0x5b0ead0b1d9dfa45ULL,
            .activation = 0x31bed4b452ab6133ULL,
            .count = 24},
    GateRow{.signal = "E_consonants",
            .params = "default",
            .onsets = 0x4fb8ad55b947e987ULL,
            .activation = 0xe6b3a581c17c270aULL,
            .count = 16},
    GateRow{.signal = "E_consonants",
            .params = "even4",
            .onsets = 0x4fb8ad55b947e987ULL,
            .activation = 0xe6b3a581c17c270aULL,
            .count = 16},
    GateRow{.signal = "E_consonants",
            .params = "plain",
            .onsets = 0x60f52853ffe9b1ddULL,
            .activation = 0x21d09717a9368e35ULL,
            .count = 21},
    GateRow{.signal = "E_consonants",
            .params = "lambda1_lag10",
            .onsets = 0x75b0ee50f66d007dULL,
            .activation = 0x6373e9d2a0c6b3c2ULL,
            .count = 13},
    GateRow{.signal = "F_sharp_noise30dB",
            .params = "default",
            .onsets = 0xdf4aabdb64df4602ULL,
            .activation = 0xb2f57e9775a8214eULL,
            .count = 16},
    GateRow{.signal = "F_sharp_noise30dB",
            .params = "even4",
            .onsets = 0xdf4aabdb64df4602ULL,
            .activation = 0xb2f57e9775a8214eULL,
            .count = 16},
    GateRow{.signal = "F_sharp_noise30dB",
            .params = "plain",
            .onsets = 0xac2be220fc45f9cdULL,
            .activation = 0x01375e5b224f8871ULL,
            .count = 18},
    GateRow{.signal = "F_sharp_noise30dB",
            .params = "lambda1_lag10",
            .onsets = 0xf0dc638147c67e77ULL,
            .activation = 0xfb9770e0dbe68b45ULL,
            .count = 16},
    GateRow{.signal = "G_fast_8perSec",
            .params = "default",
            .onsets = 0xe8a4cb1855bcde8cULL,
            .activation = 0x5e4ebeb1a25fed9cULL,
            .count = 30},
    GateRow{.signal = "G_fast_8perSec",
            .params = "even4",
            .onsets = 0xe8a4cb1855bcde8cULL,
            .activation = 0x5e4ebeb1a25fed9cULL,
            .count = 30},
    GateRow{.signal = "G_fast_8perSec",
            .params = "plain",
            .onsets = 0x511ad573f8bf767fULL,
            .activation = 0x24da3074804fbfccULL,
            .count = 29},
    GateRow{.signal = "G_fast_8perSec",
            .params = "lambda1_lag10",
            .onsets = 0xd493516caf472bb9ULL,
            .activation = 0x0178c907850d6281ULL,
            .count = 31},
    GateRow{.signal = "A_minus30dB",
            .params = "default",
            .onsets = 0x8b76c3d94206357dULL,
            .activation = 0x467117674a26740bULL,
            .count = 8},
    GateRow{.signal = "A_minus30dB",
            .params = "even4",
            .onsets = 0x8b76c3d94206357dULL,
            .activation = 0x467117674a26740bULL,
            .count = 8},
    GateRow{.signal = "A_minus30dB",
            .params = "plain",
            .onsets = 0x07914a6e840baeb5ULL,
            .activation = 0x3b193ae246cf4bf5ULL,
            .count = 10},
    GateRow{.signal = "A_minus30dB",
            .params = "lambda1_lag10",
            .onsets = 0x14650fb0739d0383ULL,
            .activation = 0xc64056c6218d6887ULL,
            .count = 0},
    GateRow{.signal = "vocadito_2_first_12s",
            .params = "default",
            .onsets = 0x911047cde1a7cc52ULL,
            .activation = 0x9643cc22b7efa126ULL,
            .count = 29},
    GateRow{.signal = "vocadito_2_first_12s",
            .params = "even4",
            .onsets = 0x911047cde1a7cc52ULL,
            .activation = 0x9643cc22b7efa126ULL,
            .count = 29},
    GateRow{.signal = "vocadito_2_first_12s",
            .params = "plain",
            .onsets = 0x5c3fc0a16e2580a3ULL,
            .activation = 0x8d57da24f6ecab68ULL,
            .count = 47},
    GateRow{.signal = "vocadito_2_first_12s",
            .params = "lambda1_lag10",
            .onsets = 0x3dcb1ee827a33b58ULL,
            .activation = 0xd6c13a2799f105e4ULL,
            .count = 22},
}};

OnsetScore pooled_detect_onsets(double db) {
    OnsetScore total;
    for (const Family& f : pooled_families()) {
        const auto src = AudioSource::from_buffer(to_buffer(attenuated(f.samples, db), k_rate));
        accumulate(total, score_onsets(times_of(detect_onsets(src, OnsetParams{}).value()), f.truth,
                                       0.025));
    }
    return total;
}

}  // namespace

TEST_CASE("the_bench_families_have_the_lengths_truth_counts_and_peak_of_gen_py", "[onset_bench]") {
    // gen.py prints (duration s, onset count) per signal.
    struct Expected {
        Family family;
        double seconds = 0.0;
        std::size_t onsets = 0;
    };
    const std::vector<Expected> cases = {{.family = family_a(), .seconds = 5.3, .onsets = 8},
                                         {.family = family_a0(), .seconds = 5.3, .onsets = 8},
                                         {.family = family_b(), .seconds = 5.3, .onsets = 8},
                                         {.family = family_c(), .seconds = 4.5, .onsets = 8},
                                         {.family = family_d(), .seconds = 4.5, .onsets = 1},
                                         {.family = family_e(), .seconds = 6.3, .onsets = 8},
                                         {.family = family_f(), .seconds = 5.3, .onsets = 8},
                                         {.family = family_g(), .seconds = 2.5, .onsets = 16}};
    for (const auto& c : cases) {
        INFO(c.family.name);
        CHECK(seconds_of(c.family) == Catch::Approx(c.seconds).margin(0.01));
        CHECK(c.family.truth.size() == c.onsets);
        CHECK(peak_of(c.family) == Catch::Approx(0.5F).margin(1e-6));
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
    // Greedy by truth order: truth 1.0 takes 1.02, truth 1.045 takes 1.04.
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
        CHECK(fingerprint(
                  superflux_activation(signal->samples, Hz{signal->rate}, set->second).value()) ==
              row.activation);
    }
}

TEST_CASE("detect_onsets_on_the_bench_is_level_independent_and_weaker_than_the_new_pipeline",
          "[onset_bench]") {
    // Recorded 2026-10-22 on the research's pooled set (A, A0, B, D, F, G): TP 39 FP 55 FN 10 at
    // every level from 0 to -30 dB (F 0.545): the adaptive threshold makes the Phase 3 detector
    // level independent, and it raises many false positives inside sustained notes (family D: 23).
    // Pinned bounds with a margin.
    const auto base = pooled_detect_onsets(0.0);
    CHECK(base.f_measure() > 0.50);
    CHECK(base.f_measure() < 0.60);
    CHECK(base.fp >= 50);
    for (const double db : {-10.0, -20.0, -30.0, -40.0}) {
        CHECK(std::abs(pooled_detect_onsets(db).f_measure() - base.f_measure()) < 0.03);
    }
}

// Writes the families, our activations and our onsets for scripts/phase5_superflux_check.py when
// ANNO_ONSET_DUMP names a directory. Hidden; a no-op otherwise.
TEST_CASE("onset_dump_for_the_reference_check", "[onset_dump][.]") {
    const char* dir = std::getenv("ANNO_ONSET_DUMP");
    if (dir == nullptr) {
        SUCCEED("ANNO_ONSET_DUMP is not set");
        return;
    }
    for (const Family& f : {family_a(), family_a0(), family_b(), family_c(), family_d(), family_e(),
                            family_f(), family_g()}) {
        const std::filesystem::path base = std::filesystem::path{dir} / f.name;
        std::ofstream raw(base.string() + ".f32", std::ios::binary);
        for (const float v : f.samples) {
            const auto bytes = std::bit_cast<std::array<char, sizeof(float)>>(v);
            raw.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        // The reference's settings: lag 10 ms (its window-shape rule gives 2 frames), and
        // log(1 + x), so the SHAPE of the activation is comparable (its scale is a sum over bands).
        TransientParams p;
        p.flux.lag = Seconds{0.010};
        const auto bands = band_spectrogram(f.samples, Hz{k_rate}, p.bands).value();
        const auto act = superflux(log_compress(bands, p.log).value(), p.flux).value();
        std::ofstream act_out(base.string() + ".ours_act.txt");
        act_out << std::setprecision(12) << act.hop.value() << '\n';
        for (const double v : act.values) {
            act_out << v << '\n';
        }
        const auto src = AudioSource::from_buffer(to_buffer(f.samples, k_rate));
        std::ofstream onsets_out(base.string() + ".ours_onsets.txt");
        for (const double t : times_of(detect_transients(src, p).value())) {
            onsets_out << std::fixed << std::setprecision(5) << t << '\n';
        }
        std::ofstream truth_out(base.string() + ".truth.txt");
        for (const double t : f.truth) {
            truth_out << std::fixed << std::setprecision(5) << t << '\n';
        }
    }
}
