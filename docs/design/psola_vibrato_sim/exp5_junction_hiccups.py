"""Cycle-to-cycle shape change at grain repeat/drop junctions ('hiccups'), with and without
pitch-mark error, nearest-grain vs blended grains."""
import numpy as np, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psola_sim import *

FV, EXT, DUR, F0C = 5.5, 60.0, 8.0, 220.0


def cycles(y, sm, npts=200):
    out = []
    for a, b in zip(sm[:-1], sm[1:]):
        t = np.linspace(a, b, npts, endpoint=False)
        out.append(np.interp(t * FS, np.arange(len(y)), y))
    return np.array(out)


def D_series(y, sm):
    C = cycles(y, sm)
    num = np.sqrt(np.mean((C[1:] - C[:-1]) ** 2, axis=1))
    den = np.sqrt(0.5 * (np.mean(C[1:] ** 2, axis=1) + np.mean(C[:-1] ** 2, axis=1)))
    return num / np.maximum(den, 1e-9)   # D[j] compares cycle j and j+1


def junctions(src):
    """index j where the pair (cycle j, cycle j+1) straddles a repeat (same source) or a drop (skip >= 2)."""
    d = np.diff(src)
    return np.where(d == 0)[0], np.where(d >= 2)[0]


rng = np.random.default_rng(7)
print("Mean cycle-to-cycle shape change D at junctions divided by D at ordinary pairs (1.0 = no hiccup)")
print("  repeat junction: the same grain used twice in a row (D tends to 0 = 'stall')")
print("  drop junction:   a grain skipped (D tends to >1 = 'jump')")
print("%-26s %-9s %-7s | %-22s | %-22s" % ("source", "mark err", "shift", "nearest grain rep / drop", "blended grains rep / drop"))
for label, kw in (("clean", dict()),
                  ("jitter+shimmer+noise", dict(jitter=0.005, shimmer=0.03, noise_db=-25))):
    x, gci = make_vowel(F0C, FV, EXT, DUR, seed=3, **kw)
    for err_ms in (0.0, 0.2):
        marks = np.sort(gci + rng.normal(0, err_ms * 1e-3, len(gci))) if err_ms > 0 else gci
        for cents in (50, -50, 100):
            beta = 2 ** (cents / 1200)
            sm, src = synth_identity(marks, lambda s: beta)
            y1 = psola_ola(x, marks, sm, src)
            y2 = psola_ola_blend(x, marks, sm)
            rep, drp = junctions(src)
            res = []
            for y in (y1, y2):
                D = D_series(y, sm)
                rep = rep[(rep > 3) & (rep < len(D) - 3)]
                drp = drp[(drp > 3) & (drp < len(D) - 3)]
                near = np.zeros(len(D), bool)
                for j in np.concatenate([rep, drp]):
                    near[max(j - 2, 0):j + 3] = True
                base = np.median(D[~near][50:-50])
                r = np.mean(D[rep]) / base if len(rep) else np.nan
                d = np.mean(D[drp]) / base if len(drp) else np.nan
                res.append((r, d))
            print("%-26s %-9s %+4d c  | %9.2f / %-9.2f    | %9.2f / %-9.2f" %
                  (label, "%.1f ms" % err_ms, cents, res[0][0], res[0][1], res[1][0], res[1][1]))
