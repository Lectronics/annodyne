import sys, json, subprocess
import numpy as np
sys.path.insert(0, '.')
import superflux_ref_patched as S
src = open('run_all.py').read()
exec(src.split("methods = {}")[0].replace("import superflux_ref_patched as S", ""))
POOL = ['A_sharp', 'A0_sharp_novib', 'B_soft80ms', 'D_vib_trem', 'F_sharp_noise30dB', 'G_fast_8perSec']
tol = 0.025
def pooled(fn, thr):
    TP = FP = FN = 0
    per = {}
    for n in POOL:
        tp, fp, fn_, _ = score(fn(n, thr), truth[n]['truth'], tol)
        TP += tp; FP += fp; FN += fn_; per[n] = (tp, fp, fn_)
    return 2 * TP / max(1, (2 * TP + FP + FN)), TP, FP, FN, per
out = {}
grid = [0.005, 0.01, 0.02, 0.03, 0.058, 0.1, 0.15, 0.2, 0.3, 0.5, 0.8, 1.2, 2.0, 4.0]
for m in ['default', 'complex', 'specflux', 'energy', 'phase', 'wphase', 'specdiff', 'kl', 'mkl']:
    f = lambda n, thr, m=m: aubio(n, m, 1024, 512, thr)
    best = max(((pooled(f, g)[0], g) for g in grid), key=lambda x: x[0])
    r = pooled(f, best[1])
    out[f'aubio {m}'] = dict(best_thr=best[1], F=best[0], TP=r[1], FP=r[2], FN=r[3], per=r[4])
sgrid = [0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 1.1, 1.6, 2.5, 4.0]
for online in (False, True):
    f = lambda n, thr, o=online: superflux(n, o, thr)
    best = max(((pooled(f, g)[0], g) for g in sgrid), key=lambda x: x[0])
    r = pooled(f, best[1])
    out[f'SuperFlux {"online" if online else "offline"}'] = dict(best_thr=best[1], F=best[0], TP=r[1], FP=r[2], FN=r[3], per=r[4])
    # sensitivity: F at each threshold
    out[f'SuperFlux {"online" if online else "offline"} sweep'] = {g: round(pooled(f, g)[0], 3) for g in sgrid}
json.dump(out, open('sweep.json', 'w'), indent=1)
# defaults for comparison
dflt = {}
for m, thr in [('default', None), ('complex', None), ('specflux', None), ('energy', None), ('phase', None), ('wphase', None), ('specdiff', None), ('kl', None), ('mkl', None)]:
    r = pooled(lambda n, t, m=m: aubio(n, m, 1024, 512, None), None); dflt[f'aubio {m}'] = (round(r[0], 3), r[1], r[2], r[3])
for online in (False, True):
    r = pooled(lambda n, t, o=online: superflux(n, o, 1.1), None); dflt[f'SuperFlux {"online" if online else "offline"}'] = (round(r[0], 3), r[1], r[2], r[3])
print('pooled over', POOL, '(total truth', sum(len(truth[n]['truth']) for n in POOL), ')')
print('method'.ljust(22), 'default F (TP,FP,FN)'.ljust(28), 'best threshold -> F (TP,FP,FN)')
for k, v in dflt.items():
    o = out[k]
    print(k.ljust(22), f'{v[0]} ({v[1]},{v[2]},{v[3]})'.ljust(28), f'{o["best_thr"]} -> {o["F"]:.3f} ({o["TP"]},{o["FP"]},{o["FN"]})')
print(out['SuperFlux offline sweep']); print(out['SuperFlux online sweep'])
