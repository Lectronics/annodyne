import numpy as np, json, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psola_sim import *

FV, EXT, F0C, DUR = 5.5, 60.0, 220.0, 4.0
x, gci = make_vowel(F0C, FV, EXT, DUR)

# sanity: input contour
tg, c, fg = f0_cents_from_marks(gci)
print("INPUT (from true marks): rate %.3f Hz, extent +-%.1f cents, resid %.2f" % fit_vibrato(tg, c))

# independent tracker on input audio
ty, fy = yin_f0(x)
sel = (ty > 0.3) & (ty < DUR - 0.3)
cy = 1200 * np.log2(fy[sel] / np.median(fy[sel]))
tgy = np.arange(ty[sel][0], ty[sel][-1], 1 / FS_C)
cyg = np.interp(tgy, ty[sel], cy)
print("INPUT (YIN tracker):     rate %.3f Hz, extent +-%.1f cents, resid %.2f" % fit_vibrato(tgy, cyg))

print()
print("%-8s %-14s %7s %9s %9s %8s %8s" % ("shift", "mapping", "dur_s", "rate_Hz", "ext_c", "ratio_r", "YIN_rate"))
for st in (-5, -2, 2, 5, 12):
    beta = 2 ** (st / 12)
    for name in ("identity", "index-driven"):
        if name == "identity":
            sm, src = synth_identity(gci, lambda s: beta)
        else:
            sm, src = synth_index_driven(gci, beta)
        y = psola_ola(x, gci, sm, src)
        tg, c, fg = f0_cents_from_marks(sm)
        r, A, res = fit_vibrato(tg, c)
        dur = sm[-1] - sm[0]
        # independent tracker on rendered audio
        ty, fy = yin_f0(y, fmin=70, fmax=900)
        sel = (ty > 0.3) & (ty < dur - 0.3) & (fy > 0)
        cy = 1200 * np.log2(fy[sel] / np.median(fy[sel]))
        tgy = np.arange(ty[sel][0], ty[sel][-1], 1 / FS_C)
        cyg = np.interp(tgy, ty[sel], cy)
        ry, Ay, _ = fit_vibrato(tgy, cyg)
        print("%+3d st   %-14s %7.3f %9.3f %9.1f %8.3f %8.3f" % (st, name, dur, r, A, r / FV, ry))
