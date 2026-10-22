"""Synthetic singing-like signals with known onset times. Output: <name>.wav (int16), <name>.f32 (raw float32), truth.json"""
import sys, json
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, lfilter
sys.path.insert(0, '/tmp/outputs/psola_vibrato_sim')
from psola_sim import make_vowel, FS

OUT = sys.argv[1]
rng = np.random.default_rng(11)
PITCHES = [220.0, 247.0, 262.0, 294.0, 330.0, 294.0, 262.0, 220.0, 196.0, 220.0, 247.0, 262.0, 294.0, 330.0, 349.0, 392.0]

def cos_ramp(n):
    return 0.5 - 0.5 * np.cos(np.pi * np.arange(n) / max(n, 1))

def note(f0, dur, attack, release=0.04, vib=50.0, fv=5.5, seed=0):
    y, _ = make_vowel(f0c=f0, fv=fv, ext_cents=vib, dur=dur, seed=seed)
    n = len(y); env = np.ones(n)
    na = int(attack * FS); nr = int(release * FS)
    env[:na] = cos_ramp(na); env[n - nr:] *= cos_ramp(nr)[::-1]
    return y * env

def place(sig, y, t):
    i = int(round(t * FS)); sig[i:i + len(y)] += y[:max(0, len(sig) - i)]

def seq(n, dur, gap, attack, lead=0.2, **kw):
    total = lead + n * (dur + gap) + 0.3
    sig = np.zeros(int(total * FS)); truth = []
    for k in range(n):
        t = lead + k * (dur + gap)
        place(sig, note(PITCHES[k % len(PITCHES)], dur, attack, seed=k, **kw), t); truth.append(t)
    return sig, truth

def legato(n, dur, xf=0.02, lead=0.2):
    total = lead + n * dur + 0.3
    sig = np.zeros(int(total * FS)); truth = []
    for k in range(n):
        t = lead + k * dur
        y, _ = make_vowel(f0c=PITCHES[k % len(PITCHES)], fv=5.5, ext_cents=50.0, dur=dur + xf, seed=k)
        env = np.ones(len(y)); nx = int(xf * FS)
        if k > 0: env[:nx] = cos_ramp(nx)
        else: env[:int(0.005 * FS)] = cos_ramp(int(0.005 * FS))
        if k < n - 1: env[-nx:] = cos_ramp(nx)[::-1]
        else: env[-int(0.04 * FS):] *= cos_ramp(int(0.04 * FS))[::-1]
        place(sig, y * env, t - (xf if k > 0 else 0)); truth.append(t)
    return sig, truth

def noise_hp(n, fc, level):
    b, a = butter(4, fc / (FS / 2), 'high')
    z = lfilter(b, a, rng.normal(0, 1, n)); return z / np.max(np.abs(z)) * level

def cv(n, kinds):
    lead = 0.2; sp = 0.7
    sig = np.zeros(int((lead + n * sp + 0.5) * FS)); tc = []; tv = []
    for k in range(n):
        t0 = lead + k * sp; kind = kinds[k % len(kinds)]
        if kind == 'plosive':
            burst = noise_hp(int(0.006 * FS), 2000, 0.5) * np.exp(-np.arange(int(0.006 * FS)) / (0.002 * FS))
            asp = noise_hp(int(0.060 * FS), 1500, 0.06) * cos_ramp(int(0.060 * FS))
            place(sig, burst, t0); place(sig, asp, t0 + 0.006); tv_k = t0 + 0.066
        else:
            L = int(0.150 * FS); fr = noise_hp(L, 4000, 0.25)
            env = np.ones(L); r = int(0.015 * FS); env[:r] = cos_ramp(r); env[-r:] = cos_ramp(r)[::-1]
            place(sig, fr * env, t0); tv_k = t0 + 0.150
        place(sig, note(PITCHES[k % len(PITCHES)], 0.4, 0.010, seed=k) * 0.5, tv_k)
        tc.append(t0); tv.append(tv_k)
    return sig, tc, tv

signals = {}; truth = {}
def add(name, sig, tr, extra=None):
    sig = sig / np.max(np.abs(sig)) * 0.5
    signals[name] = sig; truth[name] = {'truth': list(map(float, tr))}
    if extra: truth[name].update(extra)

s, t = seq(8, 0.45, 0.15, 0.005); add('A_sharp', s, t)
s, t = seq(8, 0.45, 0.15, 0.005, vib=0.0); add('A0_sharp_novib', s, t)
s, t = seq(8, 0.45, 0.15, 0.080); add('B_soft80ms', s, t)
s, t = legato(8, 0.5); add('C_legato', s, t)
# D: vibrato + tremolo only, one onset
y, _ = make_vowel(f0c=220.0, fv=6.0, ext_cents=100.0, dur=4.0, seed=5)
tt = np.arange(len(y)) / FS; y = y * (1 + 0.15 * np.sin(2 * np.pi * 6.0 * tt)); y[:int(0.005 * FS)] *= cos_ramp(int(0.005 * FS)); y[-int(0.04 * FS):] *= cos_ramp(int(0.04 * FS))[::-1]
sig = np.zeros(int(4.5 * FS)); place(sig, y, 0.2); add('D_vib_trem', sig, [0.2])
s, tc, tv = cv(8, ['plosive', 'fricative']); add('E_consonants', s, tv, {'t_consonant': tc, 't_vowel': tv})
s, t = seq(8, 0.45, 0.15, 0.005)
rms = np.sqrt(np.mean(s ** 2)); s = s + rng.normal(0, 1, len(s)) * rms * 10 ** (-30 / 20); add('F_sharp_noise30dB', s, t)
s, t = seq(16, 0.11, 0.015, 0.005); add('G_fast_8perSec', s, t)

truth_out = {}
for k, v in signals.items():
    wavfile.write(f'{OUT}/{k}.wav', FS, np.round(v * 32767).astype(np.int16))
    v.astype(np.float32).tofile(f'{OUT}/{k}.f32')
    truth_out[k] = truth[k]
json.dump(truth_out, open(f'{OUT}/truth.json', 'w'), indent=1)
print({k: (round(len(v) / FS, 2), len(truth[k]['truth'])) for k, v in signals.items()})
