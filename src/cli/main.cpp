// melo-cli: command dispatch only. Commands live in cli/commands so they can be tested.
// Exit codes: 0 success, 1 error (message on stderr), 2 usage.

#include "cli/commands/info.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <span>
#include <string_view>

namespace {

constexpr int k_exit_ok = 0;
constexpr int k_exit_error = 1;
constexpr int k_exit_usage = 2;

void print_usage() {
    std::fputs("usage: melo-cli info <file.wav>\n", stderr);
}

int run_info(std::string_view path) {
    const auto report = melo::cli::make_info_report(path);
    if (!report) {
        const auto code = melo::to_string(report.error().code);
        std::fprintf(stderr, "melo-cli: error: %.*s: %s\n", static_cast<int>(code.size()),
                     code.data(), report.error().message.c_str());
        return k_exit_error;
    }
    std::fputs(melo::cli::format_info(*report).c_str(), stdout);
    return k_exit_ok;
}

}  // namespace

int main(int argc, char** argv) {
    // Melo code does not throw; this only turns std::bad_alloc into a clean error exit.
    try {
        const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (args.size() == 3 && std::string_view{args[1]} == "info") {
            return run_info(args[2]);
        }
        print_usage();
        return k_exit_usage;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "melo-cli: fatal: %s\n", e.what());
        return k_exit_error;
    }
}
