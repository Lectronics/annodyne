import sys, json, subprocess, itertools
import numpy as np
sys.path.insert(0, '.')
import superflux_ref_patched as S

SIG = 'sig'; FS = 44100
truth = json.load(open(f'{SIG}/truth.json'))
names = list(truth)

def score(det, tr, tol):
    det = sorted(det); tr = list(tr); used = set(); offs = []
    for t in tr:
        best = None
        for j, d in enumerate(det):
            if j in used: continue
            if abs(d - t) <= tol and (best is None or abs(d - t) < abs(det[best] - t)): best = j
        if best is not None: used.add(best); offs.append(det[best] - t)
    tp = len(offs); fp = len(det) - tp; fn = len(tr) - tp
    return tp, fp, fn, offs

def aubio(name, method, buf, hop, thr=None):
    cmd = ['./aubio_onsets', f'{SIG}/{name}.f32', str(FS), method, str(buf), str(hop)] + ([str(thr)] if thr is not None else [])
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.split()
    return [float(x) for x in out]

fb = S.Filter(2048 // 2, FS, 24, 30, 17000, False).filterbank
def superflux(name, online, thr=1.1):
    w = S.Wav(f'{SIG}/{name}.wav')
    s = S.Spectrogram(w, frame_size=2048, fps=200, filterbank=fb, log=True, mul=1, add=1, online=online, block_size=2048)
    sodf = S.SpectralODF(s, ratio=0.5, max_bins=3)
    act = sodf.superflux()
    o = S.Onset(act, 200, online); o.detect(thr, combine=30, pre_avg=0.15, pre_max=0.01, post_avg=0, post_max=0.05, delay=0)
    d = list(o.detections)
    if online: d = [x for x in d]  # online mode: window ends at the frame, no shift
    return d

methods = {}
for m in ['default', 'complex', 'specflux', 'energy', 'phase', 'wphase', 'specdiff', 'kl', 'mkl']:
    methods[f'aubio {m} 1024/512'] = (lambda n, m=m: aubio(n, m, 1024, 512))
methods['aubio default 1024/256'] = lambda n: aubio(n, 'default', 1024, 256)
methods['aubio complex 1024/256'] = lambda n: aubio(n, 'complex', 1024, 256)
methods['SuperFlux ref offline'] = lambda n: superflux(n, False)
methods['SuperFlux ref online'] = lambda n: superflux(n, True)

res = {}
for mname, fn in methods.items():
    res[mname] = {}
    for n in names:
        det = fn(n)
        res[mname][n] = det

json.dump(res, open('detections.json', 'w'))
print('saved')
