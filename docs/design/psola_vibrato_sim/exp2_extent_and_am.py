import numpy as np, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psola_sim import *

FV, EXT, DUR = 5.5, 60.0, 4.0

# ---- E1b: the +12 st index-driven case with a wider fit band --------------------
x, gci = make_vowel(220.0, FV, EXT, DUR)
sm, src = synth_index_driven(gci, 2.0)
tg, c, _ = f0_cents_from_marks(sm)
print("E1b index-driven +12 st, wide fit band: rate %.3f Hz, ext %.1f c, resid %.2f" % fit_vibrato(tg, c, 3, 15))
print()

# ---- E2: multiplicative vs additive-Hz shift ------------------------------------
print("E2: extent after shifting 220 Hz -> ~320 Hz (identity time mapping)")
beta = 320.0 / 220.0
for name, (sm, src) in (("ratio x%.3f" % beta, synth_identity(gci, lambda s: beta)),
                        ("additive +100 Hz", synth_additive_hz(gci, 100.0))):
    tg, c, _ = f0_cents_from_marks(sm)
    r, A, res = fit_vibrato(tg, c)
    print("  %-18s rate %.3f Hz  extent +-%.1f cents  (predicted additive: %.1f)" % (name, r, A, EXT * 220 / 320))
print()

# ---- E3: AM of harmonics vs shift ----------------------------------------------
F0C = 700.0 / 3.0   # harmonic 3 sits on F1 = 700 Hz at the nominal pitch
x0, g0 = make_vowel(F0C, FV, EXT, DUR)
print("E3: AM of harmonics (dB), nominal F0 %.1f Hz, F1 = 700 Hz; vibrato +-%d c at %.1f Hz" % (F0C, EXT, FV))
print("    AM1 = amplitude at vibrato rate, AM2 = at twice the rate, ph = AM phase vs F0 (0 deg = AM peaks with F0 peak)")
print("%-9s %-5s %-12s %-6s %-7s %-6s %-7s | %-6s %-7s %-6s" %
      ("shift", "harm", "harm_Hz", "AM1", "AM2", "ph", "pp_dB", "AM1_f", "AM2_f", "ph_f"))
for st in (0, 2, -2, 5):
    beta = 2 ** (st / 12)
    sm, srci = synth_identity(g0, lambda s: beta)
    y = psola_ola(x0, g0, sm, srci)
    xf, gf = make_vowel(F0C * beta, FV, EXT, DUR)       # control: same tract, same vibrato, natively at new pitch
    for k in (2, 3, 4):
        a1, a2, ph, pp = am_of_harmonic(y, sm, k, FV)
        b1, b2, phb, ppb = am_of_harmonic(xf, gf, k, FV)
        print("%+3d st    h%-4d %-12.0f %-6.2f %-7.2f %-6.0f %-7.2f | %-6.2f %-7.2f %-6.0f" %
              (st, k, k * F0C * beta, a1, a2, ph, pp, b1, b2, phb))
