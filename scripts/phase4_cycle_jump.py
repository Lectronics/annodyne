"""Cycle-to-cycle jump measure for the PSOLA hiccups (ADR-0019, phase4-pitch-edit.md "Stage 9b").

    python3 scripts/phase4_cycle_jump.py <engine> <audition-dir>

For each <audition-dir>/<scenario>/<engine>/ package it takes the samples where edited.wav differs
from original.wav (minus 60 ms at each end) and, every 5 ms, 1 - the normalised correlation between
two consecutive periods (the period from the autocorrelation of a 40 ms window, 70..700 Hz). 0 means
identical consecutive cycles; 1 means they are in anti-phase. It prints the median, 90th percentile
and maximum for the source and for the edited render; a hiccup is a render maximum far above the
source's. Needs numpy; float32 mono WAV only. A measurement, not a pass/fail check.
"""
import sys, struct, numpy as np
def read(p):
    d=open(p,'rb').read(); pos=12
    while pos+8<=len(d):
        tag=d[pos:pos+4]; size=struct.unpack('<I',d[pos+4:pos+8])[0]
        if tag==b'data': return np.frombuffer(d[pos+8:pos+8+size],dtype='<f4').astype(float)
        pos+=8+size+(size&1)
FS=44100
def cycle_dissim(x, t0, t1, fmin=70, fmax=700):
    """per 5 ms hop: 1 - normalised corr between consecutive periods (period by autocorr of a 3-period-ish window)."""
    out=[]; hop=int(.005*FS)
    for c in range(t0, t1-int(.05*FS), hop):
        w=x[c:c+int(.04*FS)]
        if np.sqrt(np.mean(w**2))<1e-3: continue
        lo,hi=int(FS/fmax),int(FS/fmin)
        ac=np.array([np.dot(w[:-l],w[l:]) for l in range(lo,hi)])
        T=lo+int(np.argmax(ac))
        a=x[c:c+T]; b=x[c+T:c+2*T]
        den=np.sqrt(np.dot(a,a)*np.dot(b,b))
        if den>0: out.append(1-np.dot(a,b)/den)
    return np.array(out)
def score(folder, eng):
    o=read(f'{folder}/original.wav'); e=read(f'{folder}/edited.wav')
    n=min(len(o),len(e)); idx=np.nonzero(o[:n]!=e[:n])[0]
    if len(idx)<FS*0.2: return None
    t0,t1=idx[0],idx[-1]
    # skip 60 ms at the edges
    t0+=int(.06*FS); t1-=int(.06*FS)
    a=cycle_dissim(o,t0,t1); b=cycle_dissim(e,t0,t1)
    return len(b), np.median(a), np.median(b), np.percentile(a,90), np.percentile(b,90), np.max(a), np.max(b)
if __name__=='__main__':
    eng=sys.argv[1]; base=sys.argv[2]
    import glob,os
    for f in sorted(glob.glob(f'{base}/*/{eng}/original.wav')):
        f=os.path.dirname(f)
        r=score(f,eng)
        if r: print(f.split('/')[-2].ljust(30),'n=%d med src %.3f out %.3f | p90 src %.3f out %.3f | max src %.2f out %.2f'%r)
