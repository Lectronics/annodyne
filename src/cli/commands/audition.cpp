#include "cli/commands/audition.h"

#include "cli/checks/audio_checks.h"
#include "cli/scenario/scenario.h"
#include "core/edits/dirty_regions.h"
#include "core/io/wav.h"
#include "core/synthesis/passthrough/passthrough_engine.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

namespace anno::cli {

namespace {

struct CheckRow {
    std::string name;
    bool passed;
    std::string measured;
};

Result<void> write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out{path, std::ios::binary};
    out << text;
    if (!out) {
        return fail(ErrorCode::io_error, "cannot write " + path.string());
    }
    return {};
}

/// Per-frame mask of samples inside the dirty regions of `edits` against an empty list.
Result<std::vector<bool>> dirty_mask(const LoadedScenario& run, std::int64_t frames) {
    const Hz rate = run.source.sample_rate();
    const SourceTime end = to_time_point<SourceAxis>(Samples{frames}, rate);
    auto regions = dirty_regions(run.analysis->notes, EditList{scenario_track()}, *run.edits,
                                 DirtyParams{.source_end = end});
    if (!regions) {
        return Unexpected{regions.error()};
    }
    std::vector<bool> mask(static_cast<std::size_t>(frames), false);
    for (const SourceRange& range : *regions) {
        const auto lo =
            std::max<std::int64_t>(0, to_sample_index(range.start(), rate, Rounding::down).value());
        const auto hi = std::min(frames, to_sample_index(range.end(), rate, Rounding::up).value());
        for (std::int64_t i = lo; i < hi; ++i) {
            mask[static_cast<std::size_t>(i)] = true;
        }
    }
    return mask;
}

std::int64_t leaked_samples(const AudioBuffer& original, const AudioBuffer& edited,
                            const std::vector<bool>& dirty) {
    std::int64_t leaks = 0;
    for (int c = 0; c < original.channel_count(); ++c) {
        for (std::size_t i = 0; i < dirty.size(); ++i) {
            if (!dirty[i] && edited.channel(c)[i] != original.channel(c)[i]) {
                ++leaks;
            }
        }
    }
    return leaks;
}

AudioBuffer scaled_copy(const AudioBuffer& audio, double gain) {
    AudioBuffer out = audio;
    for (int c = 0; c < out.channel_count(); ++c) {
        for (float& v : out.channel(c)) {
            v = static_cast<float>(static_cast<double>(v) * gain);
        }
    }
    return out;
}

AudioBuffer difference(const AudioBuffer& edited, const AudioBuffer& original) {
    AudioBuffer out = edited;
    for (int c = 0; c < out.channel_count(); ++c) {
        for (std::size_t i = 0; i < out.channel(c).size(); ++i) {
            out.channel(c)[i] = edited.channel(c)[i] - original.channel(c)[i];
        }
    }
    return out;
}

CheckRow make_row(std::string name, bool passed, std::string measured) {
    return CheckRow{.name = std::move(name), .passed = passed, .measured = std::move(measured)};
}

struct ClickCount {
    std::size_t added = 0;  // detections in the render with no detection near them in the source
    std::size_t in_source = 0;  // detections in the render that the source already has
};

/// A click the source already contains (a hard edit, a clipped transient) is not the render's
/// doing: a detection counts as new only if the source has none within k_same_click frames.
ClickCount count_new_clicks(const AudioBuffer& original, const AudioBuffer& rendered) {
    constexpr std::int64_t k_same_click = 2;
    const std::vector<std::int64_t> before = find_clicks(original);
    ClickCount count;
    for (const std::int64_t frame : find_clicks(rendered)) {
        const bool known = std::ranges::any_of(
            before, [&](std::int64_t b) { return std::abs(b - frame) <= k_same_click; });
        (known ? count.in_source : count.added) += 1;
    }
    return count;
}

struct Measurements {
    std::vector<CheckRow> rows;
    double match_scale = 1.0;
    std::size_t notes_rendered = 0;
    std::string cache_id;
};

std::string_view verdict(bool passed) {
    return passed ? "PASS" : "FAIL";
}

std::string report_text(const LoadedScenario& run, std::string_view engine_name,
                        const Measurements& m, bool all_passed) {
    std::string text = std::format("# Audition report: {}\n\n", run.name);
    text += std::format("- engine: {}\n- algorithm (scenario): {}\n- notes: {}\n", engine_name,
                        run.algorithm, run.analysis->notes.size());
    text += std::format("- edited notes asked of the engine: {}\n- render id: {}\n",
                        m.notes_rendered, m.cache_id);
    text += std::format("- original_matched.wav is the original scaled by {:.6f} (RMS match)\n\n",
                        m.match_scale);
    text += "## Objective checks\n\n| Check | Result | Measured |\n|---|---|---|\n";
    for (const CheckRow& row : m.rows) {
        text += std::format("| {} | {} | {} |\n", row.name, verdict(row.passed), row.measured);
    }
    text += std::format("\nOverall: {}\n", verdict(all_passed));
    text += "\nThe click detector thresholds are initial values pending human review "
            "(ClickParams in cli/checks).\n";
    if (!run.expect_json.empty()) {
        text += "\n## Scenario `expect` (not checked in Phase 2)\n\n```json\n" + run.expect_json +
                "\n```\n";
    }
    text += "\nThese checks are structural. A human still has to listen to `original_matched.wav` "
            "against `edited.wav`; this report makes no statement about how either sounds.\n";
    return text;
}

std::string index_text(const LoadedScenario& run) {
    return std::format(
        "# Audition package: {}\n\n"
        "| File | What it is |\n|---|---|\n"
        "| original.wav | the source recording |\n"
        "| edited.wav | the render |\n"
        "| original_matched.wav | the original at the edited loudness, for a fair A/B |\n"
        "| difference.wav | edited minus original, for spotting leaks (no timing edits only) |\n"
        "| edits.json | the edits that were applied |\n"
        "| report.md | objective check results |\n\n"
        "Listen to each pair at least twice, once without reading the report "
        "(docs/AUDIO_VERIFICATION.md section 4).\n",
        run.name);
}

}  // namespace

Result<AuditionSummary> run_audition(const std::filesystem::path& scenario_path,
                                     const std::filesystem::path& out_dir, EngineChoice engine,
                                     const std::filesystem::path& repo_root) {
    auto run = load_run(scenario_path, repo_root);
    if (!run) {
        return Unexpected{run.error()};
    }
    const PassthroughEngine passthrough;
    const SynthesisEngine& chosen = passthrough;
    (void)engine;  // passthrough is the only engine (render.h: EngineChoice)

    const AudioSource& source = run->source;
    const OutputRange whole = OutputRange::from_start(
        OutputTime{}, to_seconds(source.frame_count(), source.sample_rate()));
    const RenderInputs edited_inputs{
        .source = source, .analysis = run->analysis, .edits = run->edits};
    const RenderInputs empty_inputs{.source = source,
                                    .analysis = run->analysis,
                                    .edits = std::make_shared<const EditList>(scenario_track())};

    auto edited = render_region(edited_inputs, whole, chosen, RenderParams{});
    if (!edited) {
        return Unexpected{edited.error()};
    }
    auto again = render_region(edited_inputs, whole, chosen, RenderParams{});
    auto empty = render_region(empty_inputs, whole, chosen, RenderParams{});
    if (!again) {
        return Unexpected{again.error()};
    }
    if (!empty) {
        return Unexpected{empty.error()};
    }
    auto original = source.read(Samples{0}, source.frame_count());
    if (!original) {
        return Unexpected{original.error()};
    }
    const AudioBuffer& out_audio = (*edited)->buffer.audio;
    auto mask = dirty_mask(*run, source.frame_count().value());
    if (!mask) {
        return Unexpected{mask.error()};
    }

    Measurements m;
    m.notes_rendered = (*edited)->notes_rendered.size();
    m.cache_id = (*edited)->cache_id;
    const bool null_ok = bit_identical((*empty)->buffer.audio, *original);
    m.rows.push_back(make_row("Null test (empty edit list)", null_ok,
                              null_ok ? "bit-identical to the source" : "differs from the source"));
    const std::int64_t leaks = leaked_samples(*original, out_audio, *mask);
    m.rows.push_back(make_row("Untouched regions (outside dirty_regions)", leaks == 0,
                              std::format("{} differing samples", leaks)));
    const ClickCount clicks = count_new_clicks(*original, out_audio);
    m.rows.push_back(make_row("Click detector (new vs the source)", clicks.added == 0,
                              std::format("{} new detections, {} already in the source",
                                          clicks.added, clicks.in_source)));
    const SanityReport sane = sanity(out_audio);
    m.rows.push_back(make_row(
        "Sanity (NaN/Inf, DC)", sane.nan_inf_samples == 0 && sane.worst_dc_dbfs.value() < -60.0,
        std::format("{} NaN/Inf, DC {:.1f} dBFS, peak {:.4f}", sane.nan_inf_samples,
                    sane.worst_dc_dbfs.value(), static_cast<double>(sane.peak))));
    const bool same_hash = hash_audio(out_audio) == hash_audio((*again)->buffer.audio);
    m.rows.push_back(make_row("Determinism (two renders)", same_hash,
                              same_hash ? "equal hashes" : "hashes differ"));
    const std::int64_t length_diff = out_audio.frame_count().value() - source.frame_count().value();
    m.rows.push_back(
        make_row("Length", std::abs(length_diff) <= 1,
                 std::format("{} frames vs {} expected", out_audio.frame_count().value(),
                             source.frame_count().value())));
    const double edited_rms = rms(out_audio);
    const double original_rms = rms(*original);
    m.match_scale = original_rms > 0.0 ? edited_rms / original_rms : 1.0;
    const bool all_passed = std::ranges::all_of(m.rows, &CheckRow::passed);

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        return fail(ErrorCode::io_error, "cannot create " + out_dir.string() + ": " + ec.message());
    }
    const auto write = [&](const char* file, const AudioBuffer& audio) {
        return io::write_wav(out_dir / file, audio);
    };
    for (const auto& r : {write("original.wav", *original), write("edited.wav", out_audio),
                          write("original_matched.wav", scaled_copy(*original, m.match_scale)),
                          write("difference.wav", difference(out_audio, *original))}) {
        if (!r) {
            return Unexpected{r.error()};
        }
    }
    const std::string report = report_text(*run, chosen.name(), m, all_passed);
    for (const auto& r : {write_text(out_dir / "edits.json", run->edits_json + "\n"),
                          write_text(out_dir / "report.md", report),
                          write_text(out_dir / "INDEX.md", index_text(*run))}) {
        if (!r) {
            return Unexpected{r.error()};
        }
    }
    return AuditionSummary{.all_checks_passed = all_passed, .report = report};
}

}  // namespace anno::cli
