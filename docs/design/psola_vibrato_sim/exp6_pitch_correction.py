"""Pitch correction: naive per-instant ratio vs note-only shift that keeps the vibrato."""
import numpy as np, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psola_sim import *

FV, EXT, DUR, NOTE = 5.5, 60.0, 4.0, 220.0
# singer is flat by 40 cents and drifts sharp by another 30 cents over the note
drift = lambda t: -40.0 + 30.0 * t / DUR
x, gci = make_vowel(NOTE, FV, EXT, DUR, extra_cents=drift)

# input contour (cents re NOTE) from true marks
mid = 0.5 * (gci[:-1] + gci[1:])
c_in = 1200 * np.log2((1 / np.diff(gci)) / NOTE)
tg = np.arange(mid[0] + 0.3, mid[-1] - 0.3, 1 / FS_C)
cg = np.interp(tg, mid, c_in)
r, A, _ = fit_vibrato(tg, cg)
print("input : rate %.3f Hz  extent +-%.1f c  mean pitch error %+.1f c (vs %.0f Hz note)" % (r, A, np.mean(cg), NOTE))


def report(name, sm):
    tg, c, _ = f0_cents_from_marks(sm)
    r, A, res = fit_vibrato(tg, c) if np.std(c) > 3 else (np.nan, np.std(c) * 1.41, 0)
    print("%-34s rate %6.3f Hz  extent +-%5.1f c  mean pitch error %+5.1f c  std of residual %.1f c" %
          (name, r, A, np.mean(c), np.std(c)))


# M1: naive -- ratio target/F0_in(s) at every instant (flattens everything, vibrato included)
f_in = lambda s: 1.0 / period_at(gci, s)
sm1, _ = synth_identity(gci, lambda s: NOTE / f_in(s))
report("M1 naive instant ratio", sm1)

# M2: split contour into note-level part and vibrato with a boxcar of exactly one vibrato period,
#     shift only the note-level part to the target, re-impose the vibrato
L = int(round(FS_C / FV))
box = np.ones(L) / L
slow = np.convolve(np.pad(cg, (L, L), mode='edge'), box, mode='same')[L:-L]
vib = cg - slow
vib_fn = lambda s: np.interp(s, tg, vib)
sm2, src2 = synth_from_f0_target(gci, lambda s: NOTE * 2 ** (vib_fn(s) / 1200))
report("M2 note-only shift, vibrato kept", sm2)

# M2b: same, but with the vibrato's own rate changed on purpose (to 6.5 Hz) -- grains still read at tau(s)=s
Af, fr_, ph = None, None, None
vib2 = lambda s: EXT * np.sin(2 * np.pi * 6.5 * s)
sm2b, _ = synth_from_f0_target(gci, lambda s: NOTE * 2 ** (vib2(s) / 1200))
report("M2b note-only, vibrato set to 6.5 Hz", sm2b)

# Check the synthesised audio with an independent tracker for M2
y = psola_ola(x, gci, sm2, src2)
ty, fy = yin_f0(y, fmin=70, fmax=900)
sel = (ty > 0.3) & (ty < DUR - 0.3)
cy = 1200 * np.log2(fy[sel] / NOTE)
tgy = np.arange(ty[sel][0], ty[sel][-1], 1 / FS_C)
cyg = np.interp(tgy, ty[sel], cy)
r, A, _ = fit_vibrato(tgy, cyg)
print("M2 audio checked with YIN: rate %.3f Hz  extent +-%.1f c  mean pitch error %+.1f c" % (r, A, np.mean(cyg)))
