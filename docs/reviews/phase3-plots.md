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
28 | Fine | Missing two important low notes and short notes that are sang as a staccato "zhah" | missing F1 @ 1.308s, missing E1 @ 4.064s, At 11.718s the note is not split, it is a large roll into a note somewhere between G#1 and A1, missing B1 @ 7.218s (staccato), missing B1 @ 9.38s, missing B1 @ 9.977s | None | needs tuning |
13 | fine | missing some and has some out of control pitch "octave" issues | n001785 is incorrect. It should not be counted as a note here. Even if it was though, the downward portion should be counted with n001495 and the upward sloping pitch should count towards n001865. N002225 is not correct at all. It should be an F2ish. At 2.756s there is a short note that begins with the consonant L in leather which should be around A1 there is a pitch for it, but no note. In the png. Missing Bb1 @ 7.193s, Missing B1 @ 7.25s, Missing an "A" (just rolls through) @ 7.589s, Missing the roll up on n007785 which should begin @ 7.674s. n011050 should not be a note it should count as the tail on n010870. N011121 should be the beginning on n011255. Missing a low note at 20.291s whose fundamental sounds to me like its at E1, but looks like C1 on a spectrum analyzer. | 1 with note n002225, which should actually be about an F2. | |
| | | | | |
| | | | | |
| | | | | |

Reviewer: Owner Date: 10/19/2026.

Notes for tuning (anything systematic, e.g. "always splits long vibrato notes", "scoops start the note late"):
Skipped 3. This takes a long time. If you need more, let me know. These should be good tips for the minute.
Overall, There are some large "rolls" where the pitch starts low or high, and moves somewhat linearly into the actual note region. These rolls are part of the main note, but they seem to be sometimes split into two notes. Staccato notes seem to be missed entirely, though not sure why. They do not sound like breaths.  
Additionally as with the silence being removed, so that pitches can't be placed there, pitches also need to be weighted towards transients. See note n011611 in wave 13. It actually should have been placed on the transient that comes slightly after where the note actually was placed.
 