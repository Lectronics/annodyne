#include "core/analysis/orchestrator/analyse.h"

#include "core/dsp/k_weight.h"
#include "core/dsp/mono.h"

#include <utility>

namespace anno {

namespace {

constexpr double k_min_source_rate_hz = 4000.0;  // K-weighting design limit (core/dsp/k_weight.h)

/// Adds the stage name to an error.
Unexpected stage_error(const char* stage, const Error& e) {
    return Unexpected{Error{.code = e.code, .message = std::string{stage} + ": " + e.message}};
}

/// What the pitch target needs from the neighbouring spans: their rough pitch and the silent time
/// between the notes.
NoteContext context_of(const std::vector<NoteSpan>& spans, std::size_t i) {
    NoteContext ctx;
    if (i > 0) {
        ctx.previous_pitch = spans[i - 1].rough_pitch;
        ctx.gap_before = spans[i].source_start - spans[i - 1].source_end;
    }
    if (i + 1 < spans.size()) {
        ctx.next_pitch = spans[i + 1].rough_pitch;
        ctx.gap_after = spans[i + 1].source_start - spans[i].source_end;
    }
    return ctx;
}

/// One finished Note from a span, with its pitch target.
Result<Note> describe_note(const NoteSpan& span, const NoteContext& context,
                           const PitchTrack& pitch, const OnsetList& onsets, const MonoAudio& plain,
                           const MonoAudio& weighted, const AnalyseParams& params) {
    const FrameGrid grid = note_grid(span, pitch.grid);
    auto loud = measure_loudness(plain, weighted, span, grid, params.loudness);
    if (!loud) {
        return stage_error("loudness", loud.error());
    }
    auto decomposed = decompose_pitch_curve(span, pitch, loud->amp, params.decomposition);
    if (!decomposed) {
        return stage_error("pitch decomposition", decomposed.error());
    }
    Note note;
    note.id = span.id;
    note.track_id = params.track;
    note.voice = 0;
    note.source_start = span.source_start;
    note.attack_end = span.attack_end;
    note.release_start = span.release_start;
    note.source_end = span.source_end;
    note.pitch_center = decomposed->center;
    note.grid = grid;
    note.pitch_drift = std::move(decomposed->drift);
    note.pitch_mod = std::move(decomposed->mod);
    note.amp = std::move(loud->amp);
    note.loudness = loud->loudness;
    note.separation_in = span.separation_in;
    note.confidence_prob = span.confidence_prob;
    note.is_active = true;
    note.pitch_target.target = decomposed->center;  // placeholder; replaced below
    auto target =
        estimate_pitch_target(note, {.pitch = &pitch, .onsets = &onsets, .disagreement = {}},
                              context, params.pitch_target);
    if (!target) {
        return stage_error("pitch target", target.error());
    }
    note.pitch_target = std::move(*target);
    return note;
}

}  // namespace

Result<std::shared_ptr<const AnalysisResult>> analyse(const AudioSource& src,
                                                      const AnalyseParams& params) {
    if (params.algorithm != Algorithm::melodic) {
        return fail(ErrorCode::not_implemented,
                    "analyse: only the melodic algorithm exists so far");
    }
    if (src.sample_rate().value() < k_min_source_rate_hz) {
        return fail(ErrorCode::invalid_argument,
                    "analyse: the source sample rate must be at least 4 kHz");
    }
    auto mono = read_mono(src);
    if (!mono) {
        return stage_error("read", mono.error());
    }
    auto pitch = PYinEstimator{params.pyin}.estimate(src, params.pitch);
    if (!pitch) {
        return stage_error("pitch", pitch.error());
    }
    auto activity = detect_activity(*mono, pitch->grid, params.activity);
    if (!activity) {
        return stage_error("activity", activity.error());
    }
    auto gated = gate_pitch(*pitch, *activity);
    if (!gated) {
        return stage_error("activity gate", gated.error());
    }
    *pitch = std::move(*gated);
    auto onsets = detect_onsets(src, *pitch, params.onsets);
    if (!onsets) {
        return stage_error("onsets", onsets.error());
    }
    auto spans = segment_notes(*pitch, *onsets, params.segmentation);
    if (!spans) {
        return stage_error("segmentation", spans.error());
    }
    const MonoAudio weighted{.samples = k_weight(mono->samples, mono->sample_rate),
                             .sample_rate = mono->sample_rate};

    auto result = std::make_shared<AnalysisResult>();
    result->version = params.version;
    result->source_id = src.id();
    result->algorithm = params.algorithm;
    for (std::size_t i = 0; i < spans->size(); ++i) {
        const NoteSpan& span = (*spans)[i];
        auto note =
            describe_note(span, context_of(*spans, i), *pitch, *onsets, *mono, weighted, params);
        if (!note) {
            return Unexpected{
                Error{.code = note.error().code,
                      .message = "note " + span.id.value + ": " + note.error().message}};
        }
        result->notes.push_back(std::move(*note));
    }
    result->onsets = std::move(*onsets);
    result->pitch = std::move(*pitch);
    if (auto r = validate(*result); !r) {
        return stage_error("validation", r.error());
    }
    return std::shared_ptr<const AnalysisResult>{std::move(result)};
}

}  // namespace anno
