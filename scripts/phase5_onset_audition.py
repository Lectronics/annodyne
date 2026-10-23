#!/usr/bin/env python3
"""Real-recording onset check for a human (ADR-0022, docs/design/phase5-transient-pipeline.md).

    python3 scripts/phase5_onset_audition.py [--corpus DIR] [--tracks 2 4 13 28 37]
                                             [--cli build/release/bin/anno-cli]
                                             [--out audition/phase5-onsets]

For each Vocadito track it runs `anno-cli analyse` with both onset methods (the pipeline that
analyse() uses and the Phase 3 chain), reads the annotators' note onsets and writes, under
<out>/<track>/:
  original.wav                  the recording, unchanged
  clicks_pipeline.wav           the recording (x0.6) plus a short click at every detected onset
  clicks_phase3.wav             the same for the Phase 3 chain
  clicks_annotated_A1.wav       the same at annotator 1's note onsets
  clicks_annotated_A2.wav       annotator 2
  onsets.csv                    every onset: time_s, source, kind, strength
  report.md                     matches, misses and extras at +-20 ms and +-50 ms, with times
Click pitch tells the kind: detected attack 2 kHz, consonant 3 kHz, pitch_change 1.4 kHz; annotated 700 Hz.
Listen for: a click that is not on a sung note start (an extra), a note start with no click (a miss),
and clicks that are consistently early or late. The script judges nothing about the audio; the two
annotators agree with each other on only about 75 % of onsets at +-20 ms, so neither is the truth.
Python 3 standard library only; 16-bit PCM WAV input (Vocadito).
"""
import argparse
import array
import json
import math
import shutil
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KIND_HZ = {"attack": 2000.0, "consonant": 3000.0, "pitch_change": 1400.0, "phoneme": 2500.0}
ANNOTATED_HZ = 700.0


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 1:
            sys.exit(f"{path}: only 16-bit mono PCM is supported")
        rate = w.getframerate()
        samples = array.array("h")
        samples.frombytes(w.readframes(w.getnframes()))
    return rate, samples


def write_wav(path, rate, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(array.array("h", samples).tobytes())


def with_clicks(rate, samples, clicks):
    """clicks: [(time_s, hz)]. The recording is scaled by 0.6 so a click is easy to hear."""
    out = [0.6 * s for s in samples]
    length = int(0.008 * rate)
    for t, hz in clicks:
        start = int(round(t * rate))
        for i in range(length):
            if 0 <= start + i < len(out):
                window = 0.5 - 0.5 * math.cos(2.0 * math.pi * i / length)
                out[start + i] += 11000.0 * window * math.sin(2.0 * math.pi * hz * i / rate)
    return [max(-32768, min(32767, int(round(v)))) for v in out]


def read_annotated(corpus, track, annotator):
    path = corpus / "Annotations" / "Notes" / f"vocadito_{track}_notesA{annotator}.csv"
    return sorted(float(line.split(",")[0]) for line in path.read_text().splitlines() if line.strip())


def match(detected, truth, tol):
    """The research's greedy scorer: each truth takes the nearest unused detection within tol."""
    detected = sorted(detected)
    used, pairs = set(), []
    for t in truth:
        best = None
        for j, d in enumerate(detected):
            if j not in used and abs(d - t) <= tol and (best is None or abs(d - t) < abs(detected[best] - t)):
                best = j
        if best is not None:
            used.add(best)
            pairs.append((t, detected[best]))
    missed = [t for t in truth if t not in [p[0] for p in pairs]]
    extra = [d for j, d in enumerate(detected) if j not in used]
    return pairs, missed, extra


def f_measure(tp, fp, fn):
    return 2.0 * tp / (2 * tp + fp + fn) if (2 * tp + fp + fn) else 1.0


def analyse(cli, wav, method, tmp):
    out = tmp / f"{wav.stem}.{method}.json"
    subprocess.run([str(cli), "analyse", str(wav), "--onsets", method, "--json", str(out)], check=True,
                   stdout=subprocess.DEVNULL)
    onsets = json.loads(out.read_text())["onsets"]
    return [(o["time_s"], o["kind"], o["strength"]) for o in onsets]


def main():
    ap = argparse.ArgumentParser()
    committed = ROOT / "tests/golden/vocals/vocadito"
    ap.add_argument("--corpus", default=str(committed))
    ap.add_argument("--tracks", nargs="+", type=int, default=[2, 4, 13, 28, 37])
    ap.add_argument("--cli", default=str(ROOT / "build/release/bin/anno-cli"))
    ap.add_argument("--out", default=str(ROOT / "audition/phase5-onsets"))
    args = ap.parse_args()
    corpus, out_root, cli = Path(args.corpus), Path(args.out), Path(args.cli)
    out_root.mkdir(parents=True, exist_ok=True)
    tmp = out_root / "_tmp"
    tmp.mkdir(exist_ok=True)
    index = ["# phase5-onsets: real-recording onset check", "",
             "Click tracks at the detected onsets of both methods and at the annotators' onsets; see each "
             "`report.md`. Click pitch: detected attack 2 kHz, consonant 3 kHz, pitch change 1.4 kHz; "
             "annotated 700 Hz. The annotators agree on only about 75 % of onsets at +-20 ms. The F values are for "
             "the raw onset list (attacks, consonants and pitch changes), not for segmented notes, so they "
             "differ from the note-level figures in ADR-0022.", "",
             "| Track | Method | detected | F20 A1 | F20 A2 | F50 A1 | F50 A2 |", "|---|---|---|---|---|---|---|"]
    for track in args.tracks:
        wav = corpus / "Audio" / f"vocadito_{track}.wav"
        rate, samples = read_wav(wav)
        folder = out_root / f"vocadito_{track}"
        folder.mkdir(exist_ok=True)
        shutil.copy(wav, folder / "original.wav")
        truth = {1: read_annotated(corpus, track, 1), 2: read_annotated(corpus, track, 2)}
        methods = {"pipeline": analyse(cli, wav, "pipeline", tmp), "phase3": None}
        methods["phase3"] = analyse(cli, wav, "adaptive", tmp)
        rows = ["time_s,source,kind,strength"]
        report = [f"# vocadito_{track}", "", f"{len(samples) / rate:.1f} s, annotator 1: {len(truth[1])} onsets, "
                  f"annotator 2: {len(truth[2])} onsets.", ""]
        for name, onsets in methods.items():
            clicks = [(t, KIND_HZ.get(k, 2000.0)) for t, k, _ in onsets]
            write_wav(folder / f"clicks_{name}.wav", rate, with_clicks(rate, samples, clicks))
            rows += [f"{t:.4f},{name},{k},{s:.3f}" for t, k, s in onsets]
            times = [t for t, _, _ in onsets]
            report += [f"## {name}: {len(onsets)} onsets", ""]
            cells = []
            for a in (1, 2):
                for tol in (0.02, 0.05):
                    pairs, missed, extra = match(times, truth[a], tol)
                    cells.append(f_measure(len(pairs), len(extra), len(missed)))
                    if tol == 0.02:
                        offsets = sorted(d - t for t, d in pairs)
                        median = 1000 * offsets[len(offsets) // 2] if offsets else float("nan")
                        report += [f"Annotator {a}, +-20 ms: {len(pairs)} matched (median offset {median:+.1f} ms), "
                                   f"{len(missed)} missed, {len(extra)} extra.", ""]
                        if missed:
                            report += ["Missed note starts (s): " + ", ".join(f"{t:.2f}" for t in missed), ""]
                        if extra:
                            detail = {round(t, 4): (k, s) for t, k, s in onsets}
                            report += ["Extra detections (s, kind, strength): " + "; ".join(
                                f"{d:.2f} {detail[round(d, 4)][0]} {detail[round(d, 4)][1]:.1f}" for d in extra), ""]
            index.append(f"| {track} | {name} | {len(onsets)} | {cells[0]:.3f} | {cells[2]:.3f} | {cells[1]:.3f} | {cells[3]:.3f} |")
        for a in (1, 2):
            write_wav(folder / f"clicks_annotated_A{a}.wav", rate,
                      with_clicks(rate, samples, [(t, ANNOTATED_HZ) for t in truth[a]]))
            rows += [f"{t:.4f},annotator{a},note,0" for t in truth[a]]
        body = sorted(rows[1:], key=lambda row: float(row.split(",")[0]))
        (folder / "onsets.csv").write_text("\n".join([rows[0]] + body) + "\n")
        (folder / "report.md").write_text("\n".join(report) + "\n")
        print(f"vocadito_{track}: pipeline {len(methods['pipeline'])} onsets, phase3 {len(methods['phase3'])}", file=sys.stderr)
    shutil.rmtree(tmp)
    (out_root / "INDEX.md").write_text("\n".join(index) + "\n")


if __name__ == "__main__":
    main()
