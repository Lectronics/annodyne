#pragma once

// The onset pipeline as testable stages (ROADMAP Phase 5; design:
// docs/design/phase5-transient-pipeline.md; research: docs/design/onset-detection-research.md):
//   P0 normalise_level            stages/level_norm.h
//   P1 band_spectrogram           stages/band_spectrogram.h
//   P2 adaptive_whiten (optional) stages/spectral_flux.h
//   P3 log_compress               stages/spectral_flux.h
//   D  superflux                  stages/spectral_flux.h
//   K  pick_peaks_windowed        stages/peak_pick.h
//   L  localise                   stages/localise.h
// composed by transient_onsets / detect_transients (transient_pipeline.h). detect_onsets (onsets.h)
// is built from the same stages. This header only includes them all, for callers and tests that
// want the whole pipeline; library code includes the stage header it uses.

#include "core/analysis/onsets/stages/band_spectrogram.h"
#include "core/analysis/onsets/stages/level_norm.h"
#include "core/analysis/onsets/stages/localise.h"
#include "core/analysis/onsets/stages/peak_pick.h"
#include "core/analysis/onsets/stages/spectral_flux.h"
#include "core/analysis/onsets/transient_pipeline.h"
