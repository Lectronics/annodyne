# core/analysis/perceived_pitch

The pitch a listener hears and the split of a note's f0 into centre, slow drift and fast modulation
(layer 1; links `core/types`, `core/analysis/segmentation` for `NoteSpan`). Design:
`docs/design/phase3-notes.md` §2, ADR-0010; spec §4.2; implementation guide B4.

- `stability_weights` (`stability.cpp`): one weight definition for both functions, so the constants exist
  once: exp(-(|d f0/dt| / 600 cents/s)^2)^gamma times a level term (frames more than 30 dB below the
  note's loudest get 0).
- `decompose_pitch_curve`: f0 at the frames of `note_grid` (unvoiced gaps filled by linear interpolation in
  cents), low-passed at 2.5 Hz (zero-phase one-pole, forward and backward, twice) into slow = centre +
  drift; mod = f0 - slow (the vibrato); centre = stability-weighted mean of slow over the trimmed note.
  Reconstruction is exact to double rounding.
- `perceived_pitch`: stability-weighted mean of centre + drift + mod over the trimmed note (d'Alessandro &
  Castellengo 1994; Mesz & Eguia; pyAMPACT's rate-of-change weighting). With a vibrato found in `mod`
  (autocorrelation peak at 3-9 Hz, >= 5 cents rms) the average runs over whole cycles to avoid a
  partial-cycle bias. Trims are min(40 / 60 ms, 25 % of the note) per side; a note whose trimmed region
  has < 3 frames uses all its frames.

**Limitations / tuning:** the weight scale (600 cents/s), the trims and the 2.5 Hz cutoff are initial values
and need listening calibration (implementation guide B4: have musicians match a tone to the note). The
vibrato detector assumes a roughly constant rate. Compiled with `-ffp-contract=off`.
