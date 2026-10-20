"""Sweeps: (a) how much breath noise a sung tone survives; (b) how quiet a periodic hum still becomes a note."""
import json
import subprocess
import numpy as np
from scipy.io import wavfile
from make_probes import harmonic, band_noise, pink, db, SR

CLI = "/home/claude/lectronics/annodyne/build/clang-rel/bin/anno-cli"


def analyse(buf, name):
    wavfile.write(f"{name}.wav", SR, buf.astype(np.float32))
    subprocess.run([CLI, "analyse", f"{name}.wav", "--pitch-track", "--json", f"{name}.json"],
                   check=True, capture_output=True)
    return json.load(open(f"{name}.json"))


def voiced_share(a, lo, hi):
    p = a["pitch"]
    h = p["hop_s"]
    idx = range(int(lo / h), int(hi / h))
    ok = [i for i in idx if p["f0_cents"][i] is not None and p["voicing"][i] >= 0.5]
    return len(ok) / len(idx)


def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)))


print("(a) sung E4 at about -30 dBFS RMS with breath noise (200-8000 Hz)")
tone = harmonic(330, 1.5)
tone = tone / np.sqrt(np.mean(tone ** 2)) * db(-30)
noise = band_noise(1.5, 200, 8000)
noise = noise / np.sqrt(np.mean(noise ** 2))
for hnr in (30, 20, 15, 12, 10, 8, 6, 3, 0):
    buf = np.zeros(int(2.0 * SR))
    sig = tone + noise * db(-30 - hnr)
    buf[int(0.25 * SR):int(0.25 * SR) + len(sig)] = sig
    a = analyse(buf, f"hnr_{hnr}")
    print(f"  HNR {hnr:>2} dB: voiced {voiced_share(a, 0.35, 1.65):5.0%}, notes {len(a['notes'])}")

print("(b) 50 Hz hum (5 harmonics) alone over pink noise at -75 dBFS")
for level in (-45, -55, -65, -75, -85):
    buf = harmonic(50, 2.0, amps=[1, 0.6, 0.4, 0.3, 0.2])
    buf = buf / np.sqrt(np.mean(buf ** 2)) * db(level) + pink(2.0) * db(-75)
    a = analyse(buf, f"hum_{-level}")
    notes = ", ".join(f'{n["confidence"]:.2f}' for n in a["notes"])
    print(f"  hum RMS {level} dBFS: voiced {voiced_share(a, 0.1, 1.9):5.0%}, notes {len(a['notes'])} (confidence {notes})")

print("(c) sung A3 tone alone at decreasing level (is level used at all?)")
for level in (-20, -40, -60, -80):
    t = harmonic(220, 1.0)
    t = t / np.sqrt(np.mean(t ** 2)) * db(level)
    buf = np.zeros(int(1.5 * SR))
    buf[int(0.25 * SR):int(0.25 * SR) + len(t)] = t
    a = analyse(buf, f"lvl_{-level}")
    print(f"  tone RMS {level} dBFS: voiced {voiced_share(a, 0.3, 1.2):5.0%}, notes {len(a['notes'])}, "
          f"confidence {[round(n['confidence'], 2) for n in a['notes']]}")
