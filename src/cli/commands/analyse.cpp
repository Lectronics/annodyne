#include "cli/commands/analyse.h"

#include "cli/analysis_json/analysis_json.h"
#include "cli/plot/plot.h"
#include "core/analysis/orchestrator/analyse.h"
#include "core/io/wav.h"
#include "core/types/audio_source.h"

#include <format>
#include <fstream>
#include <utility>

namespace anno::cli {

Result<std::string> run_analyse(const AnalyseArgs& args) {
    auto audio = io::read_wav(args.wav);
    if (!audio) {
        return Unexpected{std::move(audio).error()};
    }
    const double duration = audio->duration().value();
    const AudioSource source = AudioSource::from_buffer(std::move(audio).value());
    AnalyseParams params;
    params.track = args.track;
    auto analysis = analyse(source, params);
    if (!analysis) {
        return Unexpected{std::move(analysis).error()};
    }
    if (args.json) {
        std::ofstream out{*args.json, std::ios::binary};
        out << analysis_to_json(**analysis, {.include_pitch_track = args.pitch_track_in_json,
                                             .source_duration_s = duration});
        if (!out) {
            return fail(ErrorCode::io_error, "cannot write " + args.json->string());
        }
    }
    if (args.plot_dir) {
        if (auto r = write_plots(**analysis, *args.plot_dir); !r) {
            return Unexpected{r.error()};
        }
    }
    return std::format("analysed {}: {} notes, {:.2f} s\n", args.wav.filename().string(),
                       (*analysis)->notes.size(), duration);
}

}  // namespace anno::cli
