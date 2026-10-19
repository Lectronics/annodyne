#pragma once

#include "core/io/wav.h"
#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <filesystem>
#include <string>
#include <vector>

// `melo-cli info <file>`. Computing the report and formatting it are separate so the report can be
// tested without capturing stdout (coding-standards §5, split-function #4).

namespace melo::cli {

struct InfoReport {
    io::WavInfo wav;
    Seconds duration;
    SourceId source_id;
    std::vector<Decibels> peak_dbfs;  // per channel; k_silence_floor_db for a silent channel
};

/// Reads the WAV at `path` and summarises it. Errors: those of io::read_wav.
/// Not real-time safe.
[[nodiscard]] Result<InfoReport> make_info_report(const std::filesystem::path& path);

/// Human-readable multi-line text for a report, ending in a newline.
[[nodiscard]] std::string format_info(const InfoReport& report);

}  // namespace melo::cli
