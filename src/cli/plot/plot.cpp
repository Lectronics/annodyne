#include "cli/plot/plot.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

namespace anno::cli {

namespace {

constexpr double k_px_per_second = 150.0;
constexpr double k_px_per_semitone = 14.0;
constexpr double k_margin_left = 48.0;
constexpr double k_margin = 16.0;
constexpr double k_min_width = 400.0;
constexpr double k_note_half_height_semitones = 0.4;
constexpr double k_padding_semitones = 3.0;
constexpr int k_semitones_per_octave = 12;

double seconds(SourceTime t) {
    return (t - SourceTime{}).value();
}

/// "C4" for the MIDI note number 60 and so on (A4 = 69).
std::string midi_name(int midi) {
    static constexpr std::array<const char*, 12> k_names{"C",  "C#", "D",  "D#", "E",  "F",
                                                         "F#", "G",  "G#", "A",  "A#", "B"};
    const int octave = midi / k_semitones_per_octave - 1;
    return std::format("{}{}", k_names[static_cast<std::size_t>(((midi % 12) + 12) % 12)], octave);
}

struct Frame {
    double duration_s = 0.0;
    double min_semitone = 55.0;
    double max_semitone = 76.0;
};

Frame frame_of(const AnalysisResult& a) {
    Frame f;
    double lo = 1e9;
    double hi = -1e9;
    for (const Note& n : a.notes) {
        f.duration_s = std::max(f.duration_s, seconds(n.source_end));
        lo = std::min(lo, n.perceived_pitch.value() / 100.0);
        hi = std::max(hi, n.perceived_pitch.value() / 100.0);
    }
    if (a.pitch) {
        f.duration_s = std::max(f.duration_s, static_cast<double>(a.pitch->grid.n_frames) *
                                                  a.pitch->grid.hop.value());
        for (const Cents c : a.pitch->f0) {
            if (!std::isnan(c.value())) {
                lo = std::min(lo, c.value() / 100.0);
                hi = std::max(hi, c.value() / 100.0);
            }
        }
    }
    if (hi >= lo) {
        f.min_semitone = std::floor(lo - k_padding_semitones);
        f.max_semitone = std::ceil(hi + k_padding_semitones);
    }
    return f;
}

}  // namespace

std::string pitch_csv(const AnalysisResult& analysis) {
    std::string out = "time_s,f0_cents,voicing,confidence\n";
    if (!analysis.pitch) {
        return out;
    }
    const PitchTrack& p = *analysis.pitch;
    for (std::size_t i = 0; i < p.f0.size(); ++i) {
        const double t = seconds(p.grid.start) + static_cast<double>(i) * p.grid.hop.value();
        const double f0 = p.f0[i].value();
        out += std::isnan(f0) ? std::format("{:.4f},,{:.3f},{:.3f}\n", t, p.voicing_prob[i],
                                            p.confidence_prob[i])
                              : std::format("{:.4f},{:.2f},{:.3f},{:.3f}\n", t, f0,
                                            p.voicing_prob[i], p.confidence_prob[i]);
    }
    return out;
}

std::string notes_csv(const AnalysisResult& analysis) {
    std::string out = "id,start_s,attack_end_s,release_start_s,end_s,perceived_pitch_cents,pitch_"
                      "center_cents,loudness_lufs,confidence,separation_in\n";
    for (const Note& n : analysis.notes) {
        out +=
            std::format("{},{:.4f},{:.4f},{:.4f},{:.4f},{:.2f},{:.2f},{:.2f},{:.3f},{}\n",
                        n.id.value, seconds(n.source_start), seconds(n.attack_end),
                        seconds(n.release_start), seconds(n.source_end), n.perceived_pitch.value(),
                        n.pitch_center.value(), n.loudness.value(), n.confidence_prob,
                        n.separation_in == SeparationIn::soft ? "soft" : "hard");
    }
    return out;
}

std::string plot_svg(const AnalysisResult& analysis) {
    const Frame f = frame_of(analysis);
    const double width =
        std::max(k_min_width, k_margin_left + f.duration_s * k_px_per_second + k_margin);
    const double plot_height = (f.max_semitone - f.min_semitone) * k_px_per_semitone;
    const double height = plot_height + 2.0 * k_margin;
    const auto x_of = [&](double t) { return k_margin_left + t * k_px_per_second; };
    const auto y_of = [&](double semitone) {
        return k_margin + (f.max_semitone - semitone) * k_px_per_semitone;
    };

    std::string svg =
        std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{:.0f}\" height=\"{:.0f}\" "
                    "viewBox=\"0 0 {:.0f} {:.0f}\">\n"
                    "<rect x=\"0\" y=\"0\" width=\"{:.0f}\" height=\"{:.0f}\" fill=\"#ffffff\"/>\n",
                    width, height, width, height, width, height);
    for (int m = static_cast<int>(f.min_semitone); m <= static_cast<int>(f.max_semitone); ++m) {
        const bool is_c = ((m % 12) + 12) % 12 == 0;
        svg += std::format("<line x1=\"{:.1f}\" y1=\"{:.1f}\" x2=\"{:.1f}\" y2=\"{:.1f}\" "
                           "stroke=\"{}\" stroke-width=\"0.5\"/>\n",
                           k_margin_left, y_of(m), width - k_margin, y_of(m),
                           is_c ? "#999999" : "#e3e3e3");
        if (is_c) {
            svg += std::format(
                "<text x=\"4\" y=\"{:.1f}\" font-size=\"10\" fill=\"#555555\">{}</text>\n",
                y_of(m) + 3.0, midi_name(m));
        }
    }
    for (const Note& n : analysis.notes) {
        const double semitone = n.perceived_pitch.value() / 100.0;
        svg += std::format(
            "<rect class=\"note\" x=\"{:.1f}\" y=\"{:.1f}\" width=\"{:.1f}\" height=\"{:.1f}\" "
            "fill=\"#3366cc\" fill-opacity=\"0.35\" stroke=\"#3366cc\"/>\n"
            "<text x=\"{:.1f}\" y=\"{:.1f}\" font-size=\"9\" fill=\"#223a6b\">{}</text>\n",
            x_of(seconds(n.source_start)), y_of(semitone + k_note_half_height_semitones),
            (seconds(n.source_end) - seconds(n.source_start)) * k_px_per_second,
            2.0 * k_note_half_height_semitones * k_px_per_semitone,
            x_of(seconds(n.source_start)) + 2.0,
            y_of(semitone + k_note_half_height_semitones) - 2.0, n.id.value);
    }
    if (analysis.pitch) {
        const PitchTrack& p = *analysis.pitch;
        std::string run;
        const auto flush = [&] {
            if (!run.empty()) {
                svg += std::format("<polyline class=\"pitch\" fill=\"none\" stroke=\"#cc3333\" "
                                   "stroke-width=\"1\" points=\"{}\"/>\n",
                                   run);
                run.clear();
            }
        };
        for (std::size_t i = 0; i < p.f0.size(); ++i) {
            if (std::isnan(p.f0[i].value())) {
                flush();
                continue;
            }
            const double t = seconds(p.grid.start) + static_cast<double>(i) * p.grid.hop.value();
            run += std::format("{:.1f},{:.1f} ", x_of(t), y_of(p.f0[i].value() / 100.0));
        }
        flush();
    }
    for (const SourceTime t : analysis.onsets.times) {
        svg += std::format("<line class=\"onset\" x1=\"{:.1f}\" y1=\"{:.1f}\" x2=\"{:.1f}\" "
                           "y2=\"{:.1f}\" stroke=\"#008800\" stroke-width=\"1\"/>\n",
                           x_of(seconds(t)), height - k_margin, x_of(seconds(t)),
                           height - k_margin + 10.0);
    }
    svg += "</svg>\n";
    return svg;
}

Result<void> write_plots(const AnalysisResult& analysis, const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return fail(ErrorCode::io_error, "cannot create " + dir.string() + ": " + ec.message());
    }
    const auto write = [&](const char* name, const std::string& text) -> Result<void> {
        std::ofstream out{dir / name, std::ios::binary};
        out << text;
        if (!out) {
            return fail(ErrorCode::io_error, std::string{"cannot write "} + (dir / name).string());
        }
        return {};
    };
    if (auto r = write("pitch.csv", pitch_csv(analysis)); !r) {
        return r;
    }
    if (auto r = write("notes.csv", notes_csv(analysis)); !r) {
        return r;
    }
    return write("plot.svg", plot_svg(analysis));
}

}  // namespace anno::cli
