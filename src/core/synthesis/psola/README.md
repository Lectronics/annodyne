# synthesis/psola

Time-domain pitch shifting by pitch-synchronous overlap-add (TD-PSOLA). It is the comparison engine
of Phase 4: the same interface as `synthesis/harmonic_noise`, a different trade-off. Listening gate
#1 decides which one is the default (a later ADR). The header is the contract.

## Method

1. The note's pitch (centre + drift + modulation, per frame) gives the local period.
2. Analysis marks one period apart, snapped to the largest positive sample within +-0.2 period.
3. Synthesis marks one shifted period apart; each takes the nearest analysis mark.
4. A two-period Hann grain around the analysis mark is read at the fractional position
   (Catmull-Rom) and overlap-added at the synthesis mark; the sum is divided by the sum of the
   windows.
5. The overlap-add result has its mean over one shifted period taken out (zero for every harmonic of
   the shifted pitch, so harmonic content is untouched). Down shifts space the grains wider than the
   source period and the gaps are filled from different phases of the waveform, which left a DC
   of -58.5 dBFS at -700 cents on the synthetic vowel (limit -60); with the mean taken out it is
   -135 dBFS. It is applied to every shifted sample (every sample covered by a grain), so content
   below about half the shifted pitch is attenuated too; listeners at gate #1 should know.
6. Runs of frames of a shifted class (default voiced, breathy, as in ADR-0017) are crossfaded
   (equal power, 4 ms) with the original; everything else is the original bits.

Reference: Moulines and Charpentier, "Pitch-synchronous waveform processing techniques for
text-to-speech synthesis using diphones", Speech Communication 9 (1990).

## Limits

- Everything inside a shifted frame moves, noise included (the harmonic engine keeps it).
- `max_shift` (700 cents, provisional) is where the engine returns `not_implemented` so the caller
  can fall back; large down-shifts leave gaps in the grain overlap.
- Mono only; pitch and `onset_shift` edits only; no time stretching.
- The marks follow the stored pitch curve. A curve that disagrees with the waveform by more than
  the snap window gives a rough result.

## Tuning parameters

`shifted_classes`, `class_edge_fade` (4 ms), `max_shift` (700 cents). Constants in the source: period
limits 40..2000 Hz, snap +-0.2 period, margin 0.1 s, context three longest periods.
