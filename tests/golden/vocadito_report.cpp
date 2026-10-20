// Tuning report over a Vocadito directory (not a ctest; never run in CI):
//   vocadito_report <dir> [first-last] [-v]     parameter overrides: ANNO_TUNE="beta=0.3,lp=1.5"
// Prints per-track lines with -v and always the corpus totals.

#include "core/analysis/orchestrator/analyse.h"
#include "core/io/wav.h"
#include "support/vocadito_eval.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <map>
#include <span>
#include <sstream>
#include <string>

using namespace anno;
using namespace anno::test;

namespace {

std::map<std::string, double> overrides() {
    std::map<std::string, double> out;
    const char* env = std::getenv("ANNO_TUNE");
    if (env == nullptr) {
        return out;
    }
    std::istringstream in{env};
    std::string item;
    while (std::getline(in, item, ',')) {
        const auto eq = item.find('=');
        if (eq != std::string::npos) {
            out[item.substr(0, eq)] = std::strtod(item.c_str() + eq + 1, nullptr);
        }
    }
    return out;
}

AnalyseParams params_from(const std::map<std::string, double>& o) {
    AnalyseParams p;
    const auto get = [&](const char* k, double& v) {
        if (const auto it = o.find(k); it != o.end()) {
            v = it->second;
        }
    };
    const auto set_ratio = [&](const char* k, std::optional<double>& r) {
        if (const auto it = o.find(k); it != o.end()) {
            r = it->second > 0.0 ? std::optional<double>{it->second} : std::nullopt;
        }
    };
    get("beta", p.pyin.beta_mean);
    get("v2u", p.pyin.voiced_to_unvoiced_prob);
    get("u2v", p.pyin.unvoiced_to_voiced_prob);
    set_ratio("hp", p.pyin.highpass_min_f0_ratio);
    set_ratio("lp", p.pyin.lowpass_max_f0_ratio);
    double v = 0.0;
    const auto sec = [&](const char* k, Seconds& s) {
        v = s.value();
        get(k, v);
        s = Seconds{v};
    };
    const auto db = [&](const char* k, Decibels& d) {
        v = d.value();
        get(k, v);
        d = Decibels{v};
    };
    sec("window", p.activity.window);
    sec("minact", p.activity.min_active);
    sec("dip", p.activity.max_dip);
    db("on", p.activity.on_margin);
    db("off", p.activity.off_margin);
    db("range", p.activity.min_range);
    get("floormass", p.activity.floor_mass_prob);
    sec("min_note", p.segmentation.min_note);
    sec("vib", p.segmentation.vibrato_smooth);
    sec("preroll", p.segmentation.preroll_max);
    sec("stab_min", p.segmentation.stable_min);
    get("sigma", p.segmentation.pitch_sigma_cents);
    get("stay", p.segmentation.stay_stable);
    get("s2s", p.segmentation.stable_to_silence);
    get("newp", p.segmentation.new_note_prob);
    get("onnew", p.segmentation.onset_new_note_prob);
    get("delta", p.onsets.delta);
    get("mact", p.onsets.min_activation);
    sec("mingap", p.onsets.min_gap);
    sec("lag", p.onsets.lag);
    sec("thrwin", p.onsets.threshold_window);
    double pc = p.onsets.pitch_change_threshold.value();
    get("pcthr", pc);
    p.onsets.pitch_change_threshold = Cents{pc};
    return p;
}

std::string line(const std::string& label, const VocaditoScores& t, int ref_notes_a1,
                 int ref_notes_a2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << std::left << std::setw(9) << label << std::right
        << " notes " << t.notes << " (A1 " << ref_notes_a1 << " A2 " << ref_notes_a2
        << ") | voicing recall " << t.voicing_recall() << " RPA50 " << t.pitch.rpa() << " FA "
        << t.pitch.false_alarm_rate() << " oct " << t.octave_rate() << " | onset F20 A1 "
        << t.onset20[0].f_measure() << " A2 " << t.onset20[1].f_measure() << " F50 A1 "
        << t.onset50[0].f_measure() << " A2 " << t.onset50[1].f_measure() << " | note pitch ok "
        << t.note_pitch_ok();
    return out.str();
}

/// The tracks kept out of tuning and committed as the gate subset (singer, range, language and
/// annotator agreement vary). "--tune" analyses the others, "--holdout" only these.
constexpr std::array<int, 5> k_holdout{2, 4, 13, 28, 37};

int to_int(const std::string& text) {
    return static_cast<int>(std::strtol(text.c_str(), nullptr, 10));
}

}  // namespace

// A tuning tool, not library code: an escaping exception ends it with the usual message.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    if (argc < 2) {
        std::fprintf(stderr, "usage: vocadito_report <dir> [first-last] [--tune|--holdout] [-v]\n");
        return 2;
    }
    int first = 1;
    int last = 40;
    bool verbose = false;
    enum class Subset { all, tune, holdout } subset = Subset::all;
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string a = args[i];
        if (a == "-v") {
            verbose = true;
        } else if (a == "--tune") {
            subset = Subset::tune;
        } else if (a == "--holdout") {
            subset = Subset::holdout;
        } else if (const auto dash = a.find('-'); dash != std::string::npos) {
            first = to_int(a.substr(0, dash));
            last = to_int(a.substr(dash + 1));
        } else {
            first = last = to_int(a);
        }
    }
    const AnalyseParams params = params_from(overrides());
    struct Job {
        VocaditoTrack track;
        std::future<VocaditoScores> scores;
    };
    std::vector<Job> jobs;
    for (int id = first; id <= last; ++id) {
        const bool held = std::ranges::find(k_holdout, id) != k_holdout.end();
        if ((subset == Subset::tune && held) || (subset == Subset::holdout && !held)) {
            continue;
        }
        auto track = load_vocadito(args[1], id);
        if (!track) {
            std::fprintf(stderr, "track %d: files missing or malformed\n", id);
            continue;
        }
        Job job;
        job.track = *track;
        // NOLINTNEXTLINE(bugprone-exception-escape): a tuning tool; an escaping exception ends it.
        job.scores = std::async(std::launch::async, [tr = std::move(*track), params] {
            auto audio = io::read_wav(tr.wav);
            if (!audio) {
                std::fprintf(stderr, "track %d: cannot read the wav\n", tr.id);
                return VocaditoScores{};
            }
            const auto result = analyse(AudioSource::from_buffer(std::move(audio).value()), params);
            if (!result) {
                std::fprintf(stderr, "track %d: %s\n", tr.id, result.error().message.c_str());
                return VocaditoScores{};
            }
            return evaluate(tr, **result);
        });
        jobs.push_back(std::move(job));
    }
    VocaditoScores sum;
    int ref_a1 = 0;
    int ref_a2 = 0;
    for (Job& job : jobs) {
        const VocaditoScores s = job.scores.get();
        if (verbose) {
            const std::string label = "track " + std::to_string(job.track.id);
            std::printf("%s\n", line(label, s, static_cast<int>(job.track.notes_a1.size()),
                                     static_cast<int>(job.track.notes_a2.size()))
                                    .c_str());
        }
        accumulate(sum, s);
        ref_a1 += static_cast<int>(job.track.notes_a1.size());
        ref_a2 += static_cast<int>(job.track.notes_a2.size());
    }
    std::printf("%s\n", line("TOTAL", sum, ref_a1, ref_a2).c_str());
    return 0;
}
