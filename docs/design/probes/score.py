"""Score anno-cli analysis JSON against the probe regions."""
import json
import math
import sys


def name_of(c):
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    m = c / 100.0
    r = round(m)
    return f"{names[r % 12]}{r // 12 - 1}{(m - r) * 100:+.0f}c"


for stem in sys.argv[1:]:
    a = json.load(open(f"{stem}.json"))
    regions = json.load(open(f"{stem}.regions.json"))
    p = a["pitch"]
    hop = p["hop_s"]
    f0 = p["f0_cents"]
    v = p["voicing"]
    print(f"== {stem}")
    for r in regions:
        i0, i1 = int(r["from"] / hop), int(r["to"] / hop)
        idx = range(i0, i1)
        voiced = [i for i in idx if f0[i] is not None and not (isinstance(f0[i], float) and math.isnan(f0[i])) and v[i] >= 0.5]
        share = len(voiced) / max(1, len(idx))
        med = sorted(f0[i] for i in voiced)[len(voiced) // 2] if voiced else None
        notes = [n for n in a["notes"] if n["start_s"] < r["to"] and n["end_s"] > r["from"]]
        ntxt = ", ".join(f'{n["id"]} {name_of(n["perceived_pitch_cents"])} {n["start_s"]:.2f}-{n["end_s"]:.2f} conf {n["confidence"]:.2f}' for n in notes)
        print(f'  {r["what"]:<46} expect {r["expect"]:<16} voiced {share:5.0%}'
              + (f" median {name_of(med)}" if med is not None else "")
              + (f"  notes: {ntxt}" if ntxt else "  notes: none"))
    print(f"  total notes: {len(a['notes'])}")
