import sys, json, os, subprocess, shutil
import numpy as np
from scipy.io import wavfile
sys.path.insert(0, '.')
import superflux_ref_patched as S
exec(open('run_all.py').read().split("methods = {}")[0].replace("import superflux_ref_patched as S", ""))
POOL = ['A_sharp', 'A0_sharp_novib', 'B_soft80ms', 'D_vib_trem', 'F_sharp_noise30dB', 'G_fast_8perSec']
os.makedirs('sig_att', exist_ok=True)
print('pooled F (TP,FP,FN) over', len(POOL), 'signals at default settings; level change applied to every file')
rows = {}
for att in [0, -10, -20, -30, -40]:
    g = 10 ** (att / 20)
    for n in POOL:
        x = np.fromfile(f'sig/{n}.f32', dtype=np.float32) * g
        x.astype(np.float32).tofile(f'sig_att/{n}.f32')
        wavfile.write(f'sig_att/{n}.wav', FS, np.round(x * 32767).astype(np.int16))
    SIG_BAK = SIG
    def agg(fn):
        TP = FP = FN = 0
        for n in POOL:
            tp, fp, fn_, _ = score(fn(n), truth[n]['truth'], 0.025); TP += tp; FP += fp; FN += fn_
        return f'{2*TP/max(1,2*TP+FP+FN):.2f} ({TP},{FP},{FN})'
    def au(n, m):
        out = subprocess.run(['./aubio_onsets', f'sig_att/{n}.f32', str(FS), m, '1024', '512'], capture_output=True, text=True).stdout.split(); return [float(v) for v in out]
    def sf(n, online):
        w = S.Wav(f'sig_att/{n}.wav'); s = S.Spectrogram(w, frame_size=2048, fps=200, filterbank=fb, log=True, mul=1, add=1, online=online, block_size=2048)
        act = S.SpectralODF(s, ratio=0.5, max_bins=3).superflux(); o = S.Onset(act, 200, online); o.detect(1.1, combine=30, pre_avg=0.15, pre_max=0.01, post_avg=0, post_max=0.05, delay=0); return list(o.detections)
    rows[att] = {'aubio default': agg(lambda n: au(n, 'default')), 'aubio specflux': agg(lambda n: au(n, 'specflux')), 'aubio mkl': agg(lambda n: au(n, 'mkl')), 'aubio wphase': agg(lambda n: au(n, 'wphase')), 'SuperFlux offline thr1.1': agg(lambda n: sf(n, False))}
ms = list(rows[0])
print('level dB'.ljust(10) + ''.join(m.ljust(28) for m in ms))
for att, r in rows.items(): print(str(att).ljust(10) + ''.join(r[m].ljust(28) for m in ms))
