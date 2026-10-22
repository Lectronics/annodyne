// anno-cli: command dispatch only. Commands live in cli/commands so they can be tested.
// Exit codes: 0 success, 1 error (message on stderr), 2 usage.

#include "cli/commands/analyse.h"
#include "cli/commands/audition.h"
#include "cli/commands/info.h"
#include "cli/commands/render.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <span>
#include <string_view>

namespace {

constexpr int k_exit_ok = 0;
constexpr int k_exit_error = 1;
constexpr int k_exit_usage = 2;

void print_usage() {
    std::fputs("usage: anno-cli info <file.wav>\n"
               "       anno-cli render <scenario.json> --out <out.wav> [--engine "
               "passthrough|harmonic_noise|psola]\n"
               "       anno-cli audition <scenario.json> --out <dir> [--engine "
               "passthrough|harmonic_noise|psola]\n"
               "       anno-cli analyse <file.wav> [--json <out.json>] [--plot-dir <dir>] "
               "[--pitch-track]\n"
               "                        [--track-id <id>] [--onsets adaptive|pipeline]\n",
               stderr);
}

int report_error(const anno::Error& error) {
    const auto code = anno::to_string(error.code);
    std::fprintf(stderr, "anno-cli: error: %.*s: %s\n", static_cast<int>(code.size()), code.data(),
                 error.message.c_str());
    return k_exit_error;
}

/// Parses `<scenario> --out <path> [--engine <name>]`; false on a usage error.
struct RunArgs {
    std::filesystem::path scenario;
    std::filesystem::path out;
    anno::cli::EngineChoice engine = anno::cli::EngineChoice::passthrough;
};

bool parse_run_args(std::span<char*> args, RunArgs& parsed, int& exit_code) {
    if (args.size() < 5 || std::string_view{args[3]} != "--out") {
        return false;
    }
    parsed.scenario = args[2];
    parsed.out = args[4];
    if (args.size() == 7 && std::string_view{args[5]} == "--engine") {
        auto engine = anno::cli::parse_engine(args[6]);
        if (!engine) {
            exit_code = report_error(engine.error());
            return true;
        }
        parsed.engine = *engine;
        return true;
    }
    return args.size() == 5;
}

/// Parses `analyse <wav> [--json f] [--plot-dir d] [--pitch-track] [--track-id id] [--onsets
/// adaptive|pipeline]`; false on a usage error.
bool parse_analyse_args(std::span<char*> args, anno::cli::AnalyseArgs& parsed) {
    if (args.size() < 3) {
        return false;
    }
    parsed.wav = args[2];
    for (std::size_t i = 3; i < args.size(); ++i) {
        const std::string_view flag{args[i]};
        const bool has_value = i + 1 < args.size();
        if (flag == "--pitch-track") {
            parsed.pitch_track_in_json = true;
        } else if (flag == "--json" && has_value) {
            parsed.json = std::filesystem::path{args[++i]};
        } else if (flag == "--plot-dir" && has_value) {
            parsed.plot_dir = std::filesystem::path{args[++i]};
        } else if (flag == "--track-id" && has_value) {
            parsed.track = anno::TrackId{args[++i]};
        } else if (flag == "--onsets" && has_value) {
            const std::string_view method{args[++i]};
            if (method == "adaptive") {
                parsed.onset_method = anno::SpectralMethod::adaptive_threshold;
            } else if (method == "pipeline") {
                parsed.onset_method = anno::SpectralMethod::windowed_peaks;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

int do_analyse(const anno::cli::AnalyseArgs& a) {
    const auto summary = anno::cli::run_analyse(a);
    if (!summary) {
        return report_error(summary.error());
    }
    std::fputs(summary->c_str(), stdout);
    return k_exit_ok;
}

/// The repository root a scenario's relative "source" may fall back to: the working directory.
std::filesystem::path repo_root() {
    return std::filesystem::current_path();
}

int run_render(const RunArgs& a) {
    const auto result = anno::cli::run_render(a.scenario, a.out, a.engine, repo_root());
    return result ? k_exit_ok : report_error(result.error());
}

int run_audition(const RunArgs& a) {
    const auto result = anno::cli::run_audition(a.scenario, a.out, a.engine, repo_root());
    if (!result) {
        return report_error(result.error());
    }
    std::fputs(result->report.c_str(), stdout);
    return result->all_checks_passed ? k_exit_ok : k_exit_error;
}

int run_info(std::string_view path) {
    const auto report = anno::cli::make_info_report(path);
    if (!report) {
        const auto code = anno::to_string(report.error().code);
        std::fprintf(stderr, "anno-cli: error: %.*s: %s\n", static_cast<int>(code.size()),
                     code.data(), report.error().message.c_str());
        return k_exit_error;
    }
    std::fputs(anno::cli::format_info(*report).c_str(), stdout);
    return k_exit_ok;
}

}  // namespace

int main(int argc, char** argv) {
    // Annodyne code does not throw; this only turns std::bad_alloc into a clean error exit.
    try {
        const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (args.size() == 3 && std::string_view{args[1]} == "info") {
            return run_info(args[2]);
        }
        if (args.size() >= 2 && std::string_view{args[1]} == "analyse") {
            anno::cli::AnalyseArgs parsed;
            if (!parse_analyse_args(args, parsed)) {
                print_usage();
                return k_exit_usage;
            }
            return do_analyse(parsed);
        }
        if (args.size() >= 2 &&
            (std::string_view{args[1]} == "render" || std::string_view{args[1]} == "audition")) {
            RunArgs parsed;
            int exit_code = k_exit_ok;
            if (!parse_run_args(args, parsed, exit_code)) {
                print_usage();
                return k_exit_usage;
            }
            if (exit_code != k_exit_ok) {
                return exit_code;
            }
            return std::string_view{args[1]} == "render" ? run_render(parsed)
                                                         : run_audition(parsed);
        }
        print_usage();
        return k_exit_usage;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "anno-cli: fatal: %s\n", e.what());
        return k_exit_error;
    }
}
