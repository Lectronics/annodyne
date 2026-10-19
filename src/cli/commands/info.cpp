#include "cli/commands/info.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace melo::cli {

Result<InfoReport> make_info_report(const std::filesystem::path& path) {
    auto audio = io::read_wav(path);
    if (!audio) {
        return Unexpected{std::move(audio).error()};
    }
    // The header is re-read for the on-disk format; samples are already decoded to float.
    auto wav = io::read_wav_info(path);
    if (!wav) {
        return Unexpected{std::move(wav).error()};
    }

    std::vector<Decibels> peaks;
    for (int ch = 0; ch < audio->channel_count(); ++ch) {
        float peak = 0.0F;
        for (const float s : audio->channel(ch)) {
            peak = std::max(peak, std::fabs(s));
        }
        peaks.push_back(to_decibels(LinearGain{static_cast<double>(peak)}));
    }
    const Seconds duration = audio->duration();
    const AudioSource source = AudioSource::from_buffer(std::move(audio).value());
    return InfoReport{
        .wav = *wav, .duration = duration, .source_id = source.id(), .peak_dbfs = std::move(peaks)};
}

std::string format_info(const InfoReport& report) {
    std::string text;
    text += std::format("format:      {}\n", io::to_string(report.wav.format));
    text += std::format("sample rate: {} Hz\n", report.wav.sample_rate.value());
    text += std::format("channels:    {}\n", report.wav.channel_count);
    text += std::format("frames:      {}\n", report.wav.frame_count.value());
    text += std::format("duration:    {:.6f} s\n", report.duration.value());
    text += std::format("source id:   {}\n", report.source_id.value);
    for (std::size_t ch = 0; ch < report.peak_dbfs.size(); ++ch) {
        text += std::format("peak ch{}:    {:.2f} dBFS\n", ch, report.peak_dbfs[ch].value());
    }
    return text;
}

}  // namespace melo::cli
