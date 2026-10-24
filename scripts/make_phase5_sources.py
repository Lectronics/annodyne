#!/usr/bin/env python3
"""Writes the synthetic voice of the phase5-gaps audition scenarios (deterministic, stdlib only).

    python3 scripts/make_phase5_sources.py            # -> tests/audio/scenarios/sources/gap_fricative.wav

gap_fricative.wav  room noise (about -50 dBFS RMS) all through; a vowel (196 Hz, 0.5-1.2 s); in the
                   pause after it a high-passed noise burst (a fricative, 1.5-1.65 s); a second vowel
                   (247 Hz, 2.0-2.7 s). The gap between the vowels is [1.2, 2.0] s. Muting it must
                   take the room noise out of the pause and leave the fricative and both vowels alone.
Mono, 44.1 kHz, 16-bit PCM. Reuses the voice and noise generators of make_phase4_sources.py.
"""
import random

import make_phase4_sources as base

RATE = base.RATE
LENGTH_S = 3.2
ROOM_RMS = 0.0032  # about -50 dBFS


def room_noise(seconds, seed):
    rng = random.Random(seed)
    # uniform(-a, a) has RMS a / sqrt(3)
    amp = ROOM_RMS * 3.0 ** 0.5
    return [rng.uniform(-amp, amp) for _ in range(int(seconds * RATE))]


def place(out, samples, start_s):
    first = int(round(start_s * RATE))
    for i, s in enumerate(samples):
        out[first + i] += s


def main():
    out = room_noise(LENGTH_S, seed=5)
    place(out, base.vowel(196.0, 0.7), 0.5)
    place(out, base.noise(0.15, True, 7), 1.5)
    place(out, base.vowel(247.0, 0.7), 2.0)
    base.write("gap_fricative.wav", out)


if __name__ == "__main__":
    main()
