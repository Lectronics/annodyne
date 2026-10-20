"""Synthetic probe signals for "what should and shouldn't be a pitch".

Each probe is a WAV plus a list of labelled regions with the expected answer
(voiced or not, note or not). Deterministic (seeded).
"""
import json
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

SR = 44100
rng = np.random.default_rng(1234)


def db(x):
    return 10 ** (x / 20)


def harmonic(f0, dur, amps=None, vib_hz=0.0, vib_cents=0.0, jitter=0.0):
    n = int(dur * SR)
    t = np.arange(n) / SR
    cents = vib_cents * np.sin(2 * np.pi * vib_hz * t)
    if jitter:
        cents = cents + np.cumsum(rng.normal(0, jitter, n)) * 0.0 + rng.normal(0, jitter, n)
    f = f0 * 2 ** (cents / 1200)
    ph = 2 * np.pi * np.cumsum(f) / SR
    if amps is None:
        amps = [1 / k for k in range(1, 16)]
    x = np.zeros(n)
    for k, a in enumerate(amps, start=1):
        if f0 * k < SR / 2 - 1000:
            x += a * np.sin(k * ph)
    return x / np.max(np.abs(x))


def band_noise(dur, lo, hi):
    n = int(dur * SR)
    x = rng.normal(0, 1, n)
    sos = butter(4, [lo, hi], btype="band", fs=SR, output="sos")
    y = sosfilt(sos, x)
    return y / np.max(np.abs(y))


def pink(dur):
    n = int(dur * SR)
    w = np.fft.rfft(rng.normal(0, 1, n))
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = 1
    y = np.fft.irfft(w / np.sqrt(f), n)
    return y / np.max(np.abs(y))


def env(n, attack=0.03, release=0.05):
    e = np.ones(n)
    a = int(attack * SR)
    r = int(release * SR)
    e[:a] = np.linspace(0, 1, a)
    e[-r:] = np.linspace(1, 0, r)
    return e


def place(buf, start, sig):
    i = int(start * SR)
    buf[i:i + len(sig)] += sig


def write(name, buf, regions):
    peak = np.max(np.abs(buf))
    assert peak < 1.0, (name, peak)
    wavfile.write(f"{name}.wav", SR, buf.astype(np.float32))
    with open(f"{name}.regions.json", "w") as fh:
        json.dump(regions, fh, indent=1)


def main():
    probes = {}

    # 1. Note with a long reverb-like decay tail (same pitch, exponential decay to -80 dB).
    buf = np.zeros(int(4.0 * SR))
    note = harmonic(220, 1.0) * db(-12) * env(int(1.0 * SR), release=0.01)
    place(buf, 0.5, note)
    tail_t = np.arange(int(2.0 * SR)) / SR
    tail = harmonic(220, 2.0) * db(-12) * np.exp(-tail_t * np.log(10) * 80 / 20 / 2.0)  # -40 dB/s
    place(buf, 1.5, tail)
    buf += pink(4.0) * db(-70)
    probes["p1_reverb_tail"] = (buf, [
        {"from": 0.55, "to": 1.45, "expect": "voiced_note", "what": "sung note A3"},
        {"from": 1.5, "to": 2.0, "expect": "either", "what": "tail -12..-32 dB (audible decay)"},
        {"from": 2.5, "to": 3.5, "expect": "unvoiced", "what": "tail below -52 dB: not a sung pitch"},
    ])

    # 2. Mains hum and room noise between two notes (50 Hz hum + harmonics at -55 dBFS).
    buf = np.zeros(int(4.0 * SR))
    hum = harmonic(50, 4.0, amps=[1, 0.6, 0.4, 0.3, 0.2]) * db(-55)
    buf += hum + pink(4.0) * db(-65)
    place(buf, 0.5, harmonic(330, 0.8) * db(-14) * env(int(0.8 * SR)))
    place(buf, 2.8, harmonic(294, 0.8) * db(-14) * env(int(0.8 * SR)))
    probes["p2_hum"] = (buf, [
        {"from": 0.55, "to": 1.25, "expect": "voiced_note", "what": "E4"},
        {"from": 1.5, "to": 2.6, "expect": "unvoiced", "what": "50 Hz hum at -55 dBFS between notes"},
        {"from": 2.85, "to": 3.55, "expect": "voiced_note", "what": "D4"},
    ])

    # 3. Breath, sibilant, voiced fricative, breathy note.
    buf = np.zeros(int(5.0 * SR))
    buf += pink(5.0) * db(-70)
    place(buf, 0.3, band_noise(0.45, 300, 4000) * db(-30) * np.hanning(int(0.45 * SR)))
    place(buf, 1.0, harmonic(262, 0.7) * db(-14) * env(int(0.7 * SR)))
    place(buf, 2.0, band_noise(0.18, 4000, 11000) * db(-24) * env(int(0.18 * SR), 0.01, 0.02))
    z = harmonic(140, 0.15, amps=[1, 0.3, 0.1]) * db(-36) + band_noise(0.15, 3500, 9000) * db(-30)
    place(buf, 2.6, z * env(len(z), 0.01, 0.02))
    breathy = harmonic(330, 0.8) * db(-24) + band_noise(0.8, 200, 8000) * db(-27)
    place(buf, 3.2, breathy * env(len(breathy)))
    probes["p3_breath_sibilant"] = (buf, [
        {"from": 0.35, "to": 0.7, "expect": "unvoiced", "what": "inhale (band noise -30 dB)"},
        {"from": 1.05, "to": 1.65, "expect": "voiced_note", "what": "C4"},
        {"from": 2.02, "to": 2.16, "expect": "unvoiced", "what": "sibilant 's'"},
        {"from": 2.62, "to": 2.73, "expect": "voiced_not_note", "what": "voiced 'z' (buzz + hiss), 150 ms"},
        {"from": 3.25, "to": 3.95, "expect": "voiced_note", "what": "breathy E4 (HNR ~3 dB)"},
    ])

    # 4. Headphone bleed of a backing chord under a gap (A minor triad at -48 dBFS).
    buf = np.zeros(int(4.0 * SR))
    chord = sum(harmonic(f, 4.0, amps=[1, 0.5, 0.25]) for f in (220, 262, 330)) / 3
    buf += chord * db(-48) + pink(4.0) * db(-68)
    place(buf, 0.4, harmonic(440, 0.9) * db(-14) * env(int(0.9 * SR)))
    place(buf, 2.7, harmonic(392, 0.9) * db(-14) * env(int(0.9 * SR)))
    probes["p4_bleed"] = (buf, [
        {"from": 0.45, "to": 1.25, "expect": "voiced_note", "what": "A4"},
        {"from": 1.5, "to": 2.5, "expect": "unvoiced", "what": "backing chord bleed at -48 dBFS"},
        {"from": 2.75, "to": 3.55, "expect": "voiced_note", "what": "G4"},
    ])

    # 5. Scoop, fall-off and short note (pitch contour problems).
    buf = np.zeros(int(3.5 * SR))
    buf += pink(3.5) * db(-70)
    n = int(0.9 * SR)
    t = np.arange(n) / SR
    cents = -250 * np.exp(-t / 0.05)                      # scoop up into A3
    cents += np.where(t > 0.75, -400 * ((t - 0.75) / 0.15) ** 2, 0)   # fall at the end
    f = 220 * 2 ** (cents / 1200)
    ph = 2 * np.pi * np.cumsum(f) / SR
    x = sum(np.sin(k * ph) / k for k in range(1, 12))
    place(buf, 0.3, x / np.max(np.abs(x)) * db(-14) * env(n, 0.02, 0.04))
    place(buf, 1.6, harmonic(247, 0.07) * db(-16) * env(int(0.07 * SR), 0.01, 0.01))  # 70 ms grace
    place(buf, 1.68, harmonic(262, 0.6) * db(-14) * env(int(0.6 * SR)))
    probes["p5_contours"] = (buf, [
        {"from": 0.45, "to": 1.0, "expect": "voiced_note", "what": "A3 after a 250-cent scoop (target 5700)"},
        {"from": 1.06, "to": 1.18, "expect": "voiced", "what": "fall-off (release)"},
        {"from": 1.6, "to": 1.67, "expect": "either", "what": "70 ms grace note B3"},
        {"from": 1.75, "to": 2.2, "expect": "voiced_note", "what": "C4"},
    ])

    for name, (buf, regions) in probes.items():
        write(name, buf, regions)
    print("wrote", ", ".join(probes))


if __name__ == "__main__":
    sys.exit(main())
