# core/analysis/loudness

Per-note loudness (layer 1; links `core/types`, `core/dsp`, `core/analysis/segmentation`). Design:
`docs/design/phase3-notes.md` §3, ADR-0010; spec §4.2; implementation guide B13.

`measure_loudness` integrates the K-weighted signal (`k_weight`, ITU-R BS.1770) over [attack_end,
source_end] of the note: -0.691 + 10 log10(mean square), with the BS.1770 absolute gate (-70 LUFS) applied
per block of at most 400 ms (one block for a shorter note). No relative gate and no overlapping-block
programme measure: the note is the programme, so a 180 ms and a 2 s note of the same level agree. An
all-gated note reads -70 LUFS. The amp curve is the short-term RMS (20 ms, centred on each note-grid
frame) of the UNWEIGHTED signal in dBFS, floored at -120 (what the Amplitude tool will change).

**Limitations:** sibilant and breath frames are not excluded until the noise track exists (Phase 4);
a psychoacoustic loudness model (Zwicker / Moore-Glasberg) would be more accurate across very different
pitches (implementation guide B13) and is not attempted. Compiled with `-ffp-contract=off`.
