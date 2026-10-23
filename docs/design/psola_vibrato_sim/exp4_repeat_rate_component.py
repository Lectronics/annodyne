"""Size of the 'hiccup' at the grain repeat/drop rate, and a grain-blending fix."""
import numpy as np, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psola_sim import *

FV, EXT, DUR, F0C = 5.5, 60.0, 8.0, 220.0


def remove_vib(t, v, f=FV):
    M = np.column_stack([np.cos(2*np.pi*f*t), np.sin(2*np.pi*f*t), np.cos(4*np.pi*f*t), np.sin(4*np.pi*f*t),
                         np.ones_like(t), t - t.mean(), (t - t.mean())**2])
    coef, *_ = np.linalg.lstsq(M, v, rcond=None)
    return v - M @ coef


def amp_at(t, v, f):
    M = np.column_stack([np.cos(2*np.pi*f*t), np.sin(2*np.pi*f*t), np.ones_like(t)])
    c, *_ = np.linalg.lstsq(M, v, rcond=None)
    return float(np.hypot(c[0], c[1]))


def per_cycle_amp_db(y, sm, trim=0.4):
    amps, tm = [], []
    for a, b in zip(sm[:-1], sm[1:]):
        i0, i1 = int(a*FS), int(b*FS)
        if i1 <= i0 + 2:
            continue
        amps.append(np.sqrt(np.mean(y[i0:i1]**2))); tm.append(0.5*(a+b))
    amps, tm = np.array(amps), np.array(tm)
    tg = np.arange(tm[0]+trim, tm[-1]-trim, 1/FS_C)
    return tg, 20*np.log10(np.interp(tg, tm, amps) + 1e-12)


def measure(y, sm, f_rep):
    ty, fy = yin_f0(y, fmin=70, fmax=900, thresh=0.2)
    sel = (ty > 0.4) & (ty < DUR - 0.4)
    tg = np.arange(ty[sel][0], ty[sel][-1], 1/FS_C)
    cy = np.interp(tg, ty[sel], 1200*np.log2(fy[sel]/np.median(fy[sel])))
    r = remove_vib(tg, cy)
    floor = np.median([amp_at(tg, r, f) for f in np.arange(8.0, 10.5, 0.25)
                       if abs(f - f_rep) > 0.5 and abs(f - 2*FV) > 0.7])
    ta, adb = per_cycle_amp_db(y, sm)
    ra = remove_vib(ta, adb)
    floor_a = np.median([amp_at(ta, ra, f) for f in np.arange(8.0, 10.5, 0.25)
                         if abs(f - f_rep) > 0.5 and abs(f - 2*FV) > 0.7])
    return amp_at(tg, r, f_rep), floor, amp_at(ta, ra, f_rep), floor_a


print("Peak size of the residual component AT the repeat/drop rate (vibrato lines removed)")
print("'floor' = typical size at nearby frequencies with no special meaning")
print("%-28s %-7s %-9s %-26s %-26s" % ("source", "shift", "rep/s", "nearest-grain: F0 c / dB", "blended grains: F0 c / dB"))
for label, kw in (("clean", dict()),
                  ("jitter .5% shimmer 3% noise -25dB", dict(jitter=0.005, shimmer=0.03, noise_db=-25))):
    x, gci = make_vowel(F0C, FV, EXT, DUR, seed=3, **kw)
    for cents in (50, -50, 100, 300):
        beta = 2 ** (cents / 1200)
        f_rep = abs(beta - 1) * F0C
        sm, src = synth_identity(gci, lambda s: beta)
        y1 = psola_ola(x, gci, sm, src)
        y2 = psola_ola_blend(x, gci, sm)
        a = measure(y1, sm, f_rep)
        b = measure(y2, sm, f_rep)
        print("%-28s %+4d c  %-9.2f %5.2f c (floor %4.2f) %5.3f dB    %5.2f c (floor %4.2f) %5.3f dB" %
              (label, cents, f_rep, a[0], a[1], a[2], b[0], b[1], b[2]))
