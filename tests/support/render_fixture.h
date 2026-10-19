#pragma once

#include "core/render/render.h"
#include "support/audio_checks.h"
#include "support/test_data.h"

#include <memory>
#include <string>
#include <vector>

// A source, an analysis and an edit list assembled for render tests. Notes use make_note, so spans
// are in seconds on the source axis; the analysis carries the source's id.

namespace anno::test {

struct Fixture {
    AudioSource source;
    std::shared_ptr<AnalysisResult> analysis;
    std::shared_ptr<const EditList> edits;

    [[nodiscard]] RenderInputs inputs() const { return {source, analysis, edits}; }
    [[nodiscard]] Fixture with_edits(EditList list) const {
        return {source, analysis, std::make_shared<const EditList>(std::move(list))};
    }
};

inline Fixture make_fixture(const std::vector<Note>& notes, double seconds = 3.0, int channels = 2,
                            double sample_rate_hz = 48000.0) {
    AudioSource source = AudioSource::from_buffer(make_signal(channels, seconds, sample_rate_hz));
    auto analysis = std::make_shared<AnalysisResult>();
    analysis->version = 1;
    analysis->source_id = source.id();
    analysis->notes = notes;
    return {source, analysis, std::make_shared<const EditList>(TrackId{"t1"})};
}

inline OutputRange whole(const AudioSource& source) {
    return OutputRange::from_start(OutputTime{},
                                   to_seconds(source.frame_count(), source.sample_rate()));
}

inline EditSpec gain_edit(const std::string& note_id, double db) {
    return make_spec(note_id, EditCategory::amplitude, EditParam::gain, EditOp::add, Decibels{db});
}

inline const AudioBuffer& audio_of(const std::shared_ptr<const RenderResult>& r) {
    return r->buffer.audio;
}

}  // namespace anno::test
