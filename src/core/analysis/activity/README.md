# core/analysis/activity

Which frames of a recording are above its own noise floor (layer 1; links `core/types`, `core/dsp`).
Design: `docs/design/phase3-activity.md` §3, ADR-0011. An **internal** analysis stage: it is not part of
`AnalysisResult`, and there is no gap, breath or segment-timeline behaviour (Phases 5-6).

`detect_activity(mono, grid, params)`:
1. frame level = RMS of the zero-phase 60 Hz high-passed mono signal in a 25 ms window centred on each
   frame, in dBFS;
2. noise floor = the median of the lowest 3 dB band of frame levels that holds at least 2 % of the frames
   (digital silence below -90 dBFS excluded), so a take with only a few percent of silence still finds its
   floor; if the 95th percentile is less than 10 dB above it the file has no silence and every frame above
   digital silence is active;
3. Schmitt trigger: on at floor + 9 dB, off below floor + 5 dB;
4. active runs shorter than 40 ms become silent (clicks); silent runs of at most 120 ms between active runs
   become active (plosive closures).

`gate_pitch(track, activity)` returns a copy of a `PitchTrack` with the silent frames unvoiced (f0 NaN,
voicing 0, confidence 1, alternates NaN). The orchestrator calls it before the onsets and segmentation.

**Measured on Vocadito (40 vocals):** 3-22 % of each track is labelled silent, and 0.00-0.5 % of the
annotated-voiced frames are wrongly silenced. The gate changed the corpus pitch metrics by less than 0.3
percentage points: the pitch tracker already called silence unvoiced, and most remaining false-voiced
frames are within 50 ms of an annotated voiced stretch (boundary placement), not in silence. The stage is
kept because it is safe, cheap, tested on synthetic noisy scenes and is what Phase 5's noise floor will
extend.

**Limitations:** consonants and breaths are *active* here (no voiced / unvoiced / breath classes yet); a
steady noise floor is assumed (one global floor, no per-gap floor: `silence-and-gaps.md` §2 must be
reconciled in Phase 5); levels are not K-weighted. Compiled with `-ffp-contract=off`.

Tuning parameters: `ActivityParams` (window, highpass, floor mass and band, on/off margins, minimum active
run, maximum dip, minimum range, absolute floor).
