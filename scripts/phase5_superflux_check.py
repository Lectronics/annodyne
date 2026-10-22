#!/usr/bin/env python3
"""Cross-checks the onset stages against the research's reference C++ SuperFlux
(docs/design/onset_test/cpp/superflux.hpp). Not a ctest.

    python3 scripts/phase5_superflux_check.py [--test build/debug-asan/bin/onset_bench_test]

It builds the reference CLI, asks the test binary to dump the bench families, our activations and our
onsets (ANNO_ONSET_DUMP), runs the reference on each family and prints, per family:
  - the Pearson correlation of the two activation curves over their common frames (the scales differ:
    the reference sums bands and uses log10; ours is a mean of bands and natural log, so only the
    SHAPE is compared);
  - the onset times of both detectors against the truth at +-25 ms, with the median offsets.
Known differences: the reference's frames are 220.5 samples (200 fps), ours 221 (whole-sample hop);
its Hann is symmetric, ours periodic; its peak picking compares with the previous raw detection, ours
with the previous kept peak; its threshold (1.1) is on its own scale. Python 3 stdlib only.
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FAMILIES = ["A_sharp", "A0_sharp_novib", "B_soft80ms", "C_legato", "D_vib_trem", "E_consonants",
            "F_sharp_noise30dB", "G_fast_8perSec"]


def read_floats(path):
    return [float(x) for x in Path(path).read_text().split()]


def pearson(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    den = math.sqrt(sum((x - ma) ** 2 for x in a) * sum((y - mb) ** 2 for y in b))
    return num / den if den else float("nan")


def score(det, truth, tol=0.025):
    det = sorted(det)
    used, offsets = set(), []
    for t in truth:
        best = None
        for j, d in enumerate(det):
            if j not in used and abs(d - t) <= tol and (best is None or abs(d - t) < abs(det[best] - t)):
                best = j
        if best is not None:
            used.add(best)
            offsets.append(det[best] - t)
    tp = len(offsets)
    median = sorted(offsets)[len(offsets) // 2] if offsets else float("nan")
    return tp, len(det) - tp, len(truth) - tp, median


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--test", default=str(ROOT / "build/debug-asan/bin/onset_bench_test"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        cli = Path(tmp) / "sf_cli"
        subprocess.run(["c++", "-std=c++17", "-O2", str(ROOT / "docs/design/onset_test/cpp/sf_cli.cpp"),
                        "-o", str(cli)], check=True)
        env = dict(os.environ, ANNO_ONSET_DUMP=tmp)
        subprocess.run([args.test, "onset_dump_for_the_reference_check"], check=True, env=env,
                       stdout=subprocess.DEVNULL)
        print(f"{'family':<20} {'act corr':>8} | {'reference TP/FP/FN (median ms)':<34} | ours TP/FP/FN (median ms)")
        for name in FAMILIES:
            base = Path(tmp) / name
            act_ref = Path(tmp) / f"{name}.ref_act.txt"
            out = subprocess.run([str(cli), f"{base}.f32", "44100", "0", "1.1", str(act_ref)],
                                 capture_output=True, text=True, check=True).stdout
            ref_onsets = [float(x) for x in out.split()]
            ours_act = read_floats(f"{base}.ours_act.txt")[1:]  # first value is the hop
            ref_act = read_floats(act_ref)
            truth = read_floats(f"{base}.truth.txt")
            ours_onsets = read_floats(f"{base}.ours_onsets.txt")
            r = score(ref_onsets, truth)
            o = score(ours_onsets, truth)
            print(f"{name:<20} {pearson(ours_act, ref_act):>8.3f} | "
                  f"{r[0]}/{r[1]}/{r[2]} ({1000 * r[3]:+.1f}){'':<18} | {o[0]}/{o[1]}/{o[2]} ({1000 * o[3]:+.1f})")


if __name__ == "__main__":
    sys.exit(main())
