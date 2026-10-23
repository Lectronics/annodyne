# tests/audio/scenarios

Scenario files for the audio checks and the audition packages (`docs/AUDIO_VERIFICATION.md` §2).
The parser's contract is in `src/cli/scenario_json/scenario_json.h`.

- `phase4/*.json`: pitch drags with `"analysis": "analyse"`. `expect.engines` lists the engines the
  `phase4-pitch` package runs (`scripts/phase4_audition.py`); PSOLA is left out above its 700 cent
  limit. `expect.pitch_cents_delta` and `vibrato_preserved` document the intent only: the audition report echoes `expect` and takes the requested shift from the edits (nothing cross-checks them yet).
- `sources/*.wav`: written by `scripts/make_phase4_sources.py` (stdlib only, seeded, 44.1 kHz mono
  16-bit; harmonic phases are random, because a zero-phase sum is a pulse train that the click
  detector flags in the source itself). They are committed on purpose (about 230 KB each) so
  scenarios load without a build step; rerun the script after changing it.
- `vocadito2_*` scenarios read `tests/golden/vocals/vocadito/Audio/vocadito_2.wav`, the committed
  5-track Vocadito subset (licence and attribution: `tests/golden/vocals/vocadito/`).
