#include "core/analysis/orchestrator/analyse.h"

#include "core/dsp/k_weight.h"
#include "core/dsp/mono.h"

#include <utility>

namespace anno {

namespace {

constexpr Cents k_not_yet_perceived{0.0};

/// Adds the stage name to an error.
Unexpected stage_error(const char* stage, const Error& e) {
    return Unexpected{Error{.code = e.code, .message = std::string{stage} + ": " + e.message}};
}

/// One finished Note from a span (without its perceived pitch refined).
Result<Note> describe_note(const NoteSpan& span, const PitchTrack& pitch, const MonoAudio& plain,
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
    note.perceived_pitch = k_not_yet_perceived;
    note.pitch_center = decomposed->center;
    note.grid = grid;
    note.pitch_drift = std::move(decomposed->drift);
    note.pitch_mod = std::move(decomposed->mod);
    note.amp = std::move(loud->amp);
    note.loudness = loud->loudness;
    note.separation_in = span.separation_in;
    note.confidence_prob = span.confidence_prob;
    note.is_active = true;
    auto perceived = perceived_pitch(note, params.perceived);
    if (!perceived) {
        return stage_error("perceived pitch", perceived.error());
    }
    note.perceived_pitch = *perceived;
    return note;
}

}  // namespace

Result<std::shared_ptr<const AnalysisResult>> analyse(const AudioSource& src,
                                                      const AnalyseParams& params) {
    if (params.algorithm != Algorithm::melodic) {
        return fail(ErrorCode::not_implemented,
                    "analyse: only the melodic algorithm exists so far");
    }
    auto mono = read_mono(src);
    if (!mono) {
        return stage_error("read", mono.error());
    }
    auto pitch = PYinEstimator{params.pyin}.estimate(src, params.pitch);
    if (!pitch) {
        return stage_error("pitch", pitch.error());
    }
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
    result->onsets = std::move(*onsets);
    for (const NoteSpan& span : *spans) {
        auto note = describe_note(span, *pitch, *mono, weighted, params);
        if (!note) {
            return Unexpected{
                Error{.code = note.error().code,
                      .message = "note " + span.id.value + ": " + note.error().message}};
        }
        result->notes.push_back(std::move(*note));
    }
    result->pitch = std::move(*pitch);
    if (auto r = validate(*result); !r) {
        return stage_error("validation", r.error());
    }
    return std::shared_ptr<const AnalysisResult>{std::move(result)};
}

}  // namespace anno
