#!/usr/bin/env python3
"""Subtracts two engines' renders of the same scenario (psola minus harmonic_noise) so the places
where they disagree stand out. Both packages must come from scripts/phase4_audition.py.

    python3 scripts/phase4_engine_diff.py [--a audition/phase4-real] [--b audition/phase4-real-hn]
                                          [--out audition/phase4-real-diff]

Per scenario it writes <name>_diff.wav (the raw a - b) and <name>_diff_norm.wav (the same scaled to
-3 dBFS peak, for listening), and prints the RMS of the difference against the RMS of the render.
Float32 mono WAV only. It judges nothing: the two engines differ in every harmonic they move, so
the difference is a listening aid, not a check.
"""
import argparse
import array
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read_f32(path):
    data = path.read_bytes()
    pos, rate, samples = 12, 0, None
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt, channels, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if fmt not in (3, 0xFFFE) or channels != 1 or bits != 32:
                sys.exit(f"{path}: only float32 mono is supported")
        elif tag == b"data":
            samples = array.array("f")
            samples.frombytes(body[:size - size % 4])
        pos += 8 + size + (size & 1)
    return rate, samples


def write_f32(path, rate, samples):
    body = samples.tobytes()
    header = (b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVEfmt " +
              struct.pack("<IHHIIHH", 16, 3, 1, rate, rate * 4, 4, 32) +
              b"data" + struct.pack("<I", len(body)))
    path.write_bytes(header + body)


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(len(x), 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", default=str(ROOT / "audition/phase4-real"))
    ap.add_argument("--b", default=str(ROOT / "audition/phase4-real-hn"))
    ap.add_argument("--out", default=str(ROOT / "audition/phase4-real-diff"))
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for folder in sorted(Path(args.a).iterdir()):
        a_wav = folder / "psola" / "edited.wav"
        b_wav = Path(args.b) / folder.name / "harmonic_noise" / "edited.wav"
        if not (a_wav.exists() and b_wav.exists()):
            continue
        rate, a = read_f32(a_wav)
        _, b = read_f32(b_wav)
        n = min(len(a), len(b))
        diff = array.array("f", (a[i] - b[i] for i in range(n)))
        active = [d for d in diff if d != 0.0]  # outside the edit both renders are the source, bit for bit
        peak = max((abs(d) for d in diff), default=0.0)
        write_f32(out / f"{folder.name}_diff.wav", rate, diff)
        scale = 0.708 / peak if peak > 0 else 1.0
        write_f32(out / f"{folder.name}_diff_norm.wav", rate, array.array("f", (d * scale for d in diff)))
        rows.append((folder.name, rms(active), rms([v for v in a if v != 0.0]), len(active) / rate))
        print(rows[-1], file=sys.stderr)
    lines = ["# psola minus harmonic_noise", "",
             "`*_diff_norm.wav` is the difference scaled to -3 dBFS peak. Listen for isolated clicks or "
             "bursts (hiccups) against a steady background.", "",
             "| Scenario | diff RMS | render RMS | edited seconds |", "|---|---|---|---|"]
    lines += [f"| {n} | {d:.4f} | {r:.4f} | {s:.2f} |" for n, d, r, s in rows]
    (out / "INDEX.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
