# core/analysis/pitch_target

**Purpose.** Decide which pitch a note is judged by (`PitchTarget`, ADR-0013): one number for a steady or hunting note, the core for a note with a scoop or fall, both ends for a glide, both levels for a trill. Design: `docs/design/note-pitch-target.md` (sections 11-16 are the binding ones).

**Method.** Per-frame reliability (voicing, consonant ramp, creak) -> smoothed curve (centre + drift when a vibrato is present) -> turning-point split (35 cents) into level and moving segments (d'Alessandro & Mertens 1995): a change of >= 35 cents in a transient zone (<= 120 ms, <= 20 % of a short note) is a transient; elsewhere moving means |delta| > g * 100 / T (g = 0.32, T <= 0.4 s) or a fast step of >= 60 cents -> kind `whole / settled / glide / multi / ornament / unpitched` -> weighted median (weighted mean over whole vibrato cycles) of the core -> approach (`legato / scoop / overshoot`), release (`fall / rise / preparation`), flags, confidence. `find_ornament` finds trills and mordents (two flat levels >= 80 cents apart); style `gamaka` also treats dense turning points as an ornament (provisional). `annotate_take` adds the take-level flags `run`, `consistent_off_grid` (take offset removed first), `expressive_peak`.

**Per-note flags** set here: `short_note`, `creak`, `ambiguous_intent` (40-60 cents from a semitone), `register_break` (multi with an abrupt step >= 300 cents).

**Not in this target:** which semitone was intended (needs key/context), tuning drift, ensemble tuning, large-shift artifacts (design note section 9; follow-up notes), the `pitch_target_mode` edit (Phase 4, ADR-0015).

**Tuning parameters** (`PitchTargetParams`, all with units): `ReliabilityParams`, `SegmentingParams` (including `glissando_g`), `TargetParams` (weights; stability factor and end bias are off, see the design note section 15), `OrnamentParams`, `TakeParams`; `pitch_target_params_for(StylePreset)`. Style presets live in `core/tuning` (pop validated, the rest provisional).

**Known limits.** On the Vocadito subset the target is within -10/+15 cents of the annotation for 57 % of matched notes against 63 % for the plain median of the note's frames: the annotators' pitch includes the scoops and falls this target excludes by design. Gamaka and the non-pop presets have no data behind them. `register_break` and `ambiguous_intent` use thresholds from the research, untested on data.
