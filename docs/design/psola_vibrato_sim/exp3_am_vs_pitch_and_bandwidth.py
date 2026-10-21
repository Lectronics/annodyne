import numpy as np, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psola_sim import *

FV, EXT, DUR = 5.5, 60.0, 4.0
print("E3b: PSOLA vs same-tract-at-new-pitch control, harmonics 2..8 (below ~2 kHz)")
print("%-8s %-6s %-6s %-9s %-9s %-9s %-10s" % ("F0_Hz", "B1", "shift", "mean|dAM1|", "mean|dAM2|", "ctrl_AM1", "phase_inv"))
for f0c in (110.0, 233.3):
    for B in (80, 150, 250):
        fm = ((700, B), (1100, B + 10), (2500, 120), (3500, 150))
        x0, g0 = make_vowel(f0c, FV, EXT, DUR, formants=fm)
        for st in (2, -3, 5):
            beta = 2 ** (st / 12)
            sm, srci = synth_identity(g0, lambda s: beta)
            y = psola_ola(x0, g0, sm, srci)
            xf, gf = make_vowel(f0c * beta, FV, EXT, DUR, formants=fm)
            d1, d2, c1, inv, cnt = [], [], [], 0, 0
            for k in range(2, 9):
                if k * f0c * beta * (1 + 0.04) > 2200:
                    continue
                a1, a2, ph, pp = am_of_harmonic(y, sm, k, FV)
                b1, b2, phb, ppb = am_of_harmonic(xf, gf, k, FV)
                d1.append(abs(a1 - b1)); d2.append(abs(a2 - b2)); c1.append(b1)
                if b1 > 0.3 and a1 > 0.3:
                    cnt += 1
                    if abs((ph - phb + 180) % 360 - 180) > 90:
                        inv += 1
            print("%-8.1f %-6d %+4d st %-9.2f %-9.2f %-9.2f %d of %d" %
                  (f0c, B, st, np.mean(d1), np.mean(d2), np.mean(c1), inv, cnt))
