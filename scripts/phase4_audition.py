#!/usr/bin/env python3
"""Builds the phase4-pitch audition package: every scenario in tests/audio/scenarios/phase4 through
every engine it lists, side by side (docs/AUDIO_VERIFICATION.md section 3, ROADMAP Phase 4).

    python3 scripts/phase4_audition.py [--cli build/release/bin/anno-cli] [--out audition/phase4-pitch]

audition/<package>/<scenario>/<engine>/ gets the package of `anno-cli audition`; a scenario/engine
pair that produces no package (an engine refusal such as not_implemented, a bad scenario) gets
REFUSED.txt (the engine said not_implemented) or FAILED.txt (anything else) instead. A written package whose objective checks FAIL is listed as such.
INDEX.md lists the pairs. The script judges nothing about how the audio sounds.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LISTEN_FOR = ("phasiness, warble, metallic or robotic edge, chipmunk/giant timbre, clicks at the note "
              "edges, smeared or lispy consonants, level jumps (docs/AUDIO_VERIFICATION.md section 4)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default=str(ROOT / "build/release/bin/anno-cli"))
    ap.add_argument("--out", default=str(ROOT / "audition/phase4-pitch"))
    ap.add_argument("--scenarios", default=str(ROOT / "tests/audio/scenarios/phase4"))
    args = ap.parse_args()
    out = Path(args.out)
    rows = []
    for path in sorted(Path(args.scenarios).glob("*.json")):
        scenario = json.loads(path.read_text())
        for engine in scenario.get("expect", {}).get("engines", ["harmonic_noise", "psola"]):
            folder = out / scenario["name"] / engine
            folder.mkdir(parents=True, exist_ok=True)
            run = subprocess.run([args.cli, "audition", str(path), "--out", str(folder), "--engine", engine],
                                 capture_output=True, text=True)
            report = folder / "report.md"
            if report.exists():
                overall = [l for l in report.read_text().splitlines() if l.startswith("Overall:")]
                status = "written, objective checks " + (overall[0].split(":")[1].strip() if overall else "?")
            else:  # no package: the engine (or the scenario) was refused before any rendering finished
                message = (run.stderr.strip() or run.stdout.strip() or "no output")
                refused = "not_implemented" in message
                (folder / ("REFUSED.txt" if refused else "FAILED.txt")).write_text(run.stdout + run.stderr)
                status = ("refused: " if refused else "FAILED TO RUN: ") + message.splitlines()[-1]
            rows.append((scenario["name"], engine, status,
                         scenario["edits"][0].get("offset_cents", scenario["edits"][0].get("target_cents"))))
            print(rows[-1], file=sys.stderr)
    lines = ["# phase4-pitch audition package", "",
             "Each scenario is rendered by each engine it lists; compare the engines on the same scenario "
             "(`original_matched.wav` against `edited.wav`, once without reading the report). "
             f"Listen for: {LISTEN_FOR}.", "",
             "No claim is made here about how any of it sounds; the fill-in sheet is "
             "`docs/reviews/phase4-listening.md`.", "",
             "| Scenario | Offset (cents) | Engine | Result | Files |", "|---|---|---|---|---|"]
    for name, engine, status, cents in rows:
        lines.append(f"| {name} | {cents} | {engine} | {status} | [{name}/{engine}/]({name}/{engine}/report.md) |"
                     if status.startswith("written") else f"| {name} | {cents} | {engine} | {status} | |")
    (out / "INDEX.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
