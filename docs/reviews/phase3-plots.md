# Phase 3 human check: note plots

Needed to close Phase 3 (ROADMAP: "look at the exported note plots for 5 vocal files and mark wrong
notes"). No audio is rendered in Phase 3, so there is no listening sheet; this is a visual check.

**How:** for each golden recording run
`anno-cli analyse tests/golden/vocals/vocadito/Audio/vocadito_<N>.wav --plot-dir plots/<N> --json plots/<N>/analysis.json`
(<N> = 2, 4, 13, 28, 37, the committed Vocadito tracks; the annotators' notes are in `Annotations/Notes/`)
and open `plots/<name>/plot.svg` in a browser. Blue rectangles are notes (placed at their perceived pitch,
labelled with the note id), the red line is the f0 curve (voiced parts only), green ticks at the bottom
are onsets, grey lines are semitones (labelled at each C). `notes.csv` has the numbers.

**What to mark** for every recording, in the table below: notes that are wrong (missed, extra, split,
merged, wrong pitch, octave error), with the time. One line per problem.

| Recording | Notes detected / expected | Wrong notes (time, what is wrong) | Octave errors seen | Verdict (accept / needs tuning) |
|---|---|---|---|---|
| | | | | |
| | | | | |
| | | | | |
| | | | | |
| | | | | |

Reviewer: ________  Date: ________

Notes for tuning (anything systematic, e.g. "always splits long vibrato notes", "scoops start the note late"):
