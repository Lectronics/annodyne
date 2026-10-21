#!/usr/bin/env python3
"""Writes the synthetic voices of the phase4-pitch audition scenarios (deterministic, stdlib only).

    python3 scripts/make_phase4_sources.py            # -> tests/audio/scenarios/sources/*.wav

vowel_vibrato.wav         two sustained vowels (220 Hz, 262 Hz), 5.5 Hz vibrato of +-40 cents,
                          a two-formant spectral envelope (700 Hz and 1800 Hz), random harmonic
                          phases (a zero-phase sum is a pulse train, not a voice).
vowel_sibilant_breath.wav a vowel, a sibilant (high-passed noise), a breath (low-passed noise) and
                          a second vowel, one after the other.
Mono, 44.1 kHz, 16-bit PCM.
"""
import math
import random
import struct
import wave
from pathlib import Path

RATE = 44100
OUT = Path(__file__).resolve().parent.parent / "tests" / "audio" / "scenarios" / "sources"


def envelope_db(f):
    def bump(c, w, h):
        x = (f - c) / w
        return h * math.exp(-0.5 * x * x)

    return -6.0 * math.log2(1.0 + f / 500.0) + bump(700.0, 150.0, 18.0) + bump(1800.0, 250.0, 12.0)


def vowel(f0, seconds, vibrato_cents=40.0, vibrato_hz=5.5):
    n = int(seconds * RATE)
    out = [0.0] * n
    # Seeded random starting phases: with all-zero phases the sum is a pulse train, a waveform no
    # voice has, and it makes the click detector fire on the source itself.
    rng = random.Random(int(f0 * 10))
    phases = [rng.uniform(0.0, 2 * math.pi) for _ in range(40)]
    for i in range(n):
        t = i / RATE
        f = f0 * 2.0 ** (vibrato_cents * math.sin(2 * math.pi * vibrato_hz * t) / 1200.0)
        gate = min(1.0, t / 0.03, (seconds - t) / 0.05)
        s = 0.0
        for k in range(1, 40):
            hz = f * k
            if hz > 0.45 * RATE:
                break
            phases[k] += 2 * math.pi * hz / RATE
            s += 10 ** (envelope_db(hz) / 20.0) * math.cos(phases[k])
        out[i] = 0.04 * gate * s
    return out


def noise(seconds, highpass, seed):
    rng = random.Random(seed)
    n = int(seconds * RATE)
    prev = 0.0
    low = 0.0
    out = []
    for i in range(n):
        t = i / RATE
        gate = min(1.0, t / 0.01, (seconds - t) / 0.02)
        x = rng.uniform(-1.0, 1.0)
        if highpass:
            y = x - prev  # first difference: tilts the noise up (sibilant-like)
            prev = x
            out.append(0.12 * gate * y)
        else:
            low += 0.08 * (x - low)  # one-pole low-pass (breath-like)
            out.append(0.9 * gate * low)
    return out


def silence(seconds):
    return [0.0] * int(seconds * RATE)


def write(name, samples):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(abs(s) for s in samples)
    scale = 0.5 / peak if peak > 0.5 else 1.0
    with wave.open(str(OUT / name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(round(32767 * s * scale))) for s in samples))


def main():
    write("vowel_vibrato.wav", silence(0.2) + vowel(220.0, 1.0) + silence(0.2) + vowel(262.0, 1.0) + silence(0.2))
    write("vowel_sibilant_breath.wav",
          silence(0.2) + vowel(196.0, 0.8) + silence(0.05) + noise(0.25, True, 1) + silence(0.05) +
          noise(0.3, False, 2) + silence(0.05) + vowel(247.0, 0.8) + silence(0.2))


if __name__ == "__main__":
    main()
