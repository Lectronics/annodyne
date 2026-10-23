import json, sys
import numpy as np
exec(open('run_all.py').read().split("fb = S.Filter")[0].split("def aubio")[0].replace("import superflux_ref_patched as S",""))
res = json.load(open('detections.json')); truth = json.load(open('sig/truth.json'))
tol = float(sys.argv[1]) if len(sys.argv) > 1 else 0.025
main = ['A_sharp', 'A0_sharp_novib', 'B_soft80ms', 'C_legato', 'D_vib_trem', 'F_sharp_noise30dB', 'G_fast_8perSec']
print(f'tolerance +-{tol*1000:.0f} ms. Cell = TP/FP/FN (median offset ms)')
print('method'.ljust(26) + ''.join(n[:12].ljust(19) for n in main))
for m in res:
    row = m.ljust(26)
    for n in main:
        tp, fp, fn, offs = score(res[m][n], truth[n]['truth'], tol)
        med = f'{np.median(offs)*1000:+.0f}' if offs else ' - '
        row += f'{tp}/{fp}/{fn} ({med})'.ljust(19)
    print(row)
print()
print('E_consonants: per syllable, C = detection within tol of consonant start, V = within tol of vowel start; extras = other detections')
tc = truth['E_consonants']['t_consonant']; tv = truth['E_consonants']['t_vowel']
for m in res:
    det = res[m]['E_consonants']; C = V = 0; usedd = set(); lab = []
    for k, (c, v) in enumerate(zip(tc, tv)):
        dc = [i for i, d in enumerate(det) if abs(d - c) <= tol]; dv = [i for i, d in enumerate(det) if abs(d - v) <= tol]
        usedd |= set(dc) | set(dv); C += bool(dc); V += bool(dv)
        lab.append(('C' if dc else '-') + ('V' if dv else '-'))
    extra = len(det) - len(usedd)
    print(m.ljust(26), f'plosive syllables(1,3,5,7): {" ".join(lab[0::2])}   fricative syllables(2,4,6,8): {" ".join(lab[1::2])}   extras {extra}   all dets {len(det)}')
