"""
Small TD-PSOLA test bench for vibrato.

Synthetic sustained vowel (impulse train -> tilt -> fixed formant resonators) with
sinusoidal vibrato in cents. True glottal closure instants (GCIs) are used as
analysis pitch marks, so pitch-tracker error is not a variable.
"""
import numpy as np
from scipy.signal import lfilter, butter, filtfilt
from scipy.optimize import least_squares

FS = 44100
FORMANTS = ((700, 80), (1100, 90), (2500, 120), (3500, 150))


# ----------------------------------------------------------------------------
# Synthetic vowel
# ----------------------------------------------------------------------------
def make_vowel(f0c=220.0, fv=5.5, ext_cents=60.0, dur=4.0, formants=FORMANTS,
               jitter=0.0, shimmer=0.0, noise_db=None, seed=0, extra_cents=None):
    """Returns x, gci_seconds. Vibrato: cents(t) = ext_cents * sin(2*pi*fv*t) (+ extra_cents(t))."""
    rng = np.random.default_rng(seed)
    n = int(dur * FS)
    t = np.arange(n) / FS
    cents = ext_cents * np.sin(2 * np.pi * fv * t)
    if extra_cents is not None:
        cents = cents + extra_cents(t)
    f0 = f0c * 2 ** (cents / 1200)
    cycles = np.cumsum(f0) / FS
    k = np.arange(1, int(cycles[-1]))
    gci = np.interp(k, cycles, t)
    if jitter > 0:
        per = np.gradient(gci)
        gci = gci + rng.normal(0, jitter, len(gci)) * per
        gci = np.sort(gci)
    amp = 1.0 + (rng.normal(0, shimmer, len(gci)) if shimmer > 0 else 0.0)
    src = np.zeros(n + 4)
    pos = gci * FS
    i0 = np.floor(pos).astype(int)
    fr = pos - i0
    ok = (i0 >= 0) & (i0 < n)
    np.add.at(src, i0[ok], (amp * (1 - fr))[ok])
    np.add.at(src, i0[ok] + 1, (amp * fr)[ok])
    src = src[:n]
    # glottal-like tilt
    src = lfilter([1.0], [1.0, -0.95], src)
    if noise_db is not None:
        nz = rng.normal(0, 1, n)
        nz *= np.sqrt(np.mean(src ** 2)) * 10 ** (noise_db / 20)
        src = src + nz
    y = src
    for F, B in formants:
        r = np.exp(-np.pi * B / FS)
        a1 = -2 * r * np.cos(2 * np.pi * F / FS)
        a2 = r * r
        y = lfilter([1 + a1 + a2], [1, a1, a2], y)
    y = y / np.max(np.abs(y))
    return y, gci


# ----------------------------------------------------------------------------
# Helpers on pitch marks
# ----------------------------------------------------------------------------
def local_period(marks):
    """Per-mark local period (s), central difference."""
    p = np.gradient(marks)
    return p


def period_at(marks, t):
    mid = 0.5 * (marks[:-1] + marks[1:])
    per = np.diff(marks)
    return np.interp(t, mid, per)


# ----------------------------------------------------------------------------
# Synthesis-mark generators (the "time mapping")
# ----------------------------------------------------------------------------
def synth_identity(marks, beta_fn, t_end=None):
    """Duration-preserving: synthesis time s maps to analysis time tau(s) = s.
    Synthesis period = analysis period at tau / beta(s). Source mark = nearest analysis mark."""
    t_end = marks[-1] if t_end is None else t_end
    s = marks[0]
    out = [s]
    while True:
        T = period_at(marks, s) / beta_fn(s)
        s = s + T
        if s >= t_end:
            break
        out.append(s)
    out = np.array(out)
    src = np.abs(marks[None, :] - out[:, None]).argmin(axis=1)
    return out, src


def synth_index_driven(marks, beta):
    """One-to-one: every analysis mark is used once, spacing divided by beta.
    No duration compensation -> output length D/beta (varispeed-like timeline)."""
    per = np.diff(marks)
    s = marks[0] + np.concatenate([[0.0], np.cumsum(per / beta)])
    src = np.arange(len(marks))
    return s, src


def synth_additive_hz(marks, delta_hz, t_end=None):
    """Duration-preserving, but F0_out(s) = F0_in(s) + delta_hz (not a ratio)."""
    t_end = marks[-1] if t_end is None else t_end
    s = marks[0]
    out = [s]
    while True:
        f_in = 1.0 / period_at(marks, s)
        s = s + 1.0 / (f_in + delta_hz)
        if s >= t_end:
            break
        out.append(s)
    out = np.array(out)
    src = np.abs(marks[None, :] - out[:, None]).argmin(axis=1)
    return out, src


def synth_from_f0_target(marks, f0_target_fn, tau_fn=None, t_end=None):
    """Explicit F0 target contour f0_target_fn(s) (Hz). Grains read from tau(s) (default s)."""
    t_end = marks[-1] if t_end is None else t_end
    tau_fn = (lambda s: s) if tau_fn is None else tau_fn
    s = marks[0]
    out = [s]
    while True:
        s = s + 1.0 / f0_target_fn(s)
        if s >= t_end:
            break
        out.append(s)
    out = np.array(out)
    src = np.array([np.abs(marks - tau_fn(si)).argmin() for si in out])
    return out, src


# ----------------------------------------------------------------------------
# TD-PSOLA overlap-add with fractional placement and window-sum normalisation
# ----------------------------------------------------------------------------
def psola_ola(x, marks, synth_marks, src_idx, out_len=None):
    n_out = int(np.ceil(synth_marks[-1] * FS)) + 2048 if out_len is None else out_len
    y = np.zeros(n_out)
    wsum = np.zeros(n_out)
    loc = local_period(marks)
    for s, i in zip(synth_marks, src_idx):
        Tm = loc[i] * FS                      # local period in samples (float)
        half = int(np.ceil(Tm))               # window half-width = 1 period
        m = marks[i] * FS                     # analysis mark (float, samples)
        a0 = int(round(m)) - half - 2
        a1 = int(round(m)) + half + 3
        if a0 < 0 or a1 > len(x):
            continue
        idx = np.arange(a0, a1)
        w = np.where(np.abs(idx - m) < Tm, 0.5 * (1 + np.cos(np.pi * (idx - m) / Tm)), 0.0)
        g = x[a0:a1] * w
        # place so that the analysis mark m lands exactly on synthesis mark s*FS
        dst = s * FS - (m - a0)               # float output position of g[0]
        d0 = int(np.floor(dst))
        fr = dst - d0
        L = len(g)
        nfft = L + 16
        G = np.fft.rfft(g, nfft)
        k = np.arange(len(G))
        G *= np.exp(-2j * np.pi * k * fr / nfft)
        gd = np.fft.irfft(G, nfft)
        Wd = np.fft.irfft(np.fft.rfft(w, nfft) * np.exp(-2j * np.pi * k * fr / nfft), nfft)
        if d0 < 0 or d0 + nfft > n_out:
            continue
        y[d0:d0 + nfft] += gd
        wsum[d0:d0 + nfft] += Wd
    wsum = np.maximum(wsum, 1e-3 * wsum.max())
    return y / wsum


# ----------------------------------------------------------------------------
# Measurements
# ----------------------------------------------------------------------------
FS_C = 200.0


def f0_cents_from_marks(synth_marks, trim=0.3):
    mid = 0.5 * (synth_marks[:-1] + synth_marks[1:])
    f = 1.0 / np.diff(synth_marks)
    tg = np.arange(mid[0] + trim, mid[-1] - trim, 1 / FS_C)
    fg = np.interp(tg, mid, f)
    c = 1200 * np.log2(fg / np.median(fg))
    return tg, c, fg


def fit_vibrato(tg, c, fmin=3.0, fmax=9.0):
    """Nonlinear sinusoid fit with linear trend. Returns rate_Hz, extent_cents (peak), residual rms."""
    t0 = tg - tg[0]
    c0 = c - np.mean(c)
    nfft = 1 << 16
    spec = np.abs(np.fft.rfft(c0 * np.hanning(len(c0)), nfft))
    fr = np.fft.rfftfreq(nfft, 1 / FS_C)
    band = (fr >= fmin) & (fr <= fmax)
    f_init = fr[band][spec[band].argmax()]

    def resid(p):
        f, A, ph, b0, b1 = p
        return A * np.sin(2 * np.pi * f * t0 + ph) + b0 + b1 * t0 - c
    best = None
    for ph0 in (0, 1.5, 3.1, 4.7):
        r = least_squares(resid, [f_init, np.std(c0) * 1.41, ph0, 0.0, 0.0])
        if best is None or r.cost < best.cost:
            best = r
    f, A, ph, b0, b1 = best.x
    return abs(f), abs(A), float(np.sqrt(2 * best.cost / len(c)))


def yin_f0(x, fmin=80.0, fmax=800.0, frame=0.04, hop=0.005, thresh=0.15):
    """Compact YIN-style tracker (independent of pitch marks). Returns t, f0."""
    N = int(frame * FS)
    H = int(hop * FS)
    tau_max = int(FS / fmin)
    tau_min = int(FS / fmax)
    ts, fs_ = [], []
    for st in range(0, len(x) - N - tau_max, H):
        seg = x[st:st + N + tau_max]
        # YIN difference function d(tau) = sum_j (x[j] - x[j+tau])^2 over an N-sample window, via FFT
        nfft = 1 << int(np.ceil(np.log2(len(seg) * 2)))
        a = seg[:N]
        A = np.fft.rfft(a, nfft)
        B = np.fft.rfft(seg, nfft)
        cc = np.fft.irfft(np.conj(A) * B, nfft)[:tau_max + 1]
        c2 = np.concatenate([[0], np.cumsum(seg ** 2)])
        e0 = c2[N]
        et = c2[np.arange(tau_max + 1) + N] - c2[np.arange(tau_max + 1)]
        d = e0 + et - 2 * cc
        d[0] = 0
        cmnd = np.ones_like(d)
        run = np.cumsum(d[1:])
        cmnd[1:] = d[1:] * np.arange(1, tau_max + 1) / np.maximum(run, 1e-12)
        cand = np.where(cmnd[tau_min:] < thresh)[0]
        if len(cand) == 0:
            continue
        tau = cand[0] + tau_min
        while tau + 1 <= tau_max and cmnd[tau + 1] < cmnd[tau]:
            tau += 1
        if 1 <= tau < tau_max:
            y0, y1, y2 = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
            den = y0 - 2 * y1 + y2
            shift = 0.5 * (y0 - y2) / den if den != 0 else 0.0
            ts.append((st + N / 2) / FS)
            fs_.append(FS / (tau + shift))
    return np.array(ts), np.array(fs_)


def am_of_harmonic(y, synth_marks, k, f_vib, lp_hz=25.0, trim=0.3):
    """Complex-demodulate harmonic k using phase from synthesis marks.
    Returns (AM1_dB, AM2_dB, phase1_deg_vs_F0, pp_dB). Phase 0 = AM peak coincides with F0 peak."""
    n = len(y)
    t = np.arange(n) / FS
    cyc = np.interp(t, synth_marks, np.arange(len(synth_marks)))
    z = y * np.exp(-2j * np.pi * k * cyc)
    b, a = butter(4, lp_hz / (FS / 2))
    zr = filtfilt(b, a, z.real)
    zi = filtfilt(b, a, z.imag)
    A = 2 * np.hypot(zr, zi)
    dec = int(FS / FS_C)
    td = t[::dec]
    Ad = A[::dec]
    sel = (td > synth_marks[0] + trim) & (td < synth_marks[-1] - trim)
    td, Ad = td[sel], Ad[sel]
    amdb = 20 * np.log10(np.maximum(Ad, 1e-12))
    # phase reference: F0 contour sinusoid fit with same frequency
    tg, c, _ = f0_cents_from_marks(synth_marks, trim)
    # least squares for F0 phase at f_vib
    def lsq(tt, v):
        M = np.column_stack([np.cos(2 * np.pi * f_vib * tt), np.sin(2 * np.pi * f_vib * tt),
                             np.cos(4 * np.pi * f_vib * tt), np.sin(4 * np.pi * f_vib * tt),
                             np.ones_like(tt), tt - tt.mean()])
        coef, *_ = np.linalg.lstsq(M, v, rcond=None)
        return coef
    cf = lsq(tg, c)
    af = lsq(td, amdb)
    phi_f = np.arctan2(-cf[1], cf[0])      # v = R cos(wt + phi)
    phi_a = np.arctan2(-af[1], af[0])
    am1 = np.hypot(af[0], af[1])
    am2 = np.hypot(af[2], af[3])
    dphi = np.degrees((phi_a - phi_f + np.pi) % (2 * np.pi) - np.pi)
    pp = amdb.max() - amdb.min()
    return am1, am2, dphi, pp


def psola_ola_blend(x, marks, synth_marks):
    """Like psola_ola, but the source is a fractional analysis index: the two neighbouring
    grains are cross-faded by fractional position instead of copying the nearest one."""
    n_out = int(np.ceil(synth_marks[-1] * FS)) + 2048
    y = np.zeros(n_out)
    wsum = np.zeros(n_out)
    loc = local_period(marks)
    idxs = np.arange(len(marks))
    for s in synth_marks:
        u = np.interp(s, marks, idxs)
        i = int(np.floor(u))
        a = u - i
        for ii, wt in ((i, 1 - a), (i + 1, a)):
            if wt <= 1e-6 or ii < 0 or ii >= len(marks):
                continue
            Tm = loc[ii] * FS
            half = int(np.ceil(Tm))
            m = marks[ii] * FS
            a0 = int(round(m)) - half - 2
            a1 = int(round(m)) + half + 3
            if a0 < 0 or a1 > len(x):
                continue
            idx = np.arange(a0, a1)
            w = np.where(np.abs(idx - m) < Tm, 0.5 * (1 + np.cos(np.pi * (idx - m) / Tm)), 0.0) * wt
            g = x[a0:a1] * w
            dst = s * FS - (m - a0)
            d0 = int(np.floor(dst)); fr = dst - d0
            nfft = len(g) + 16
            k = np.arange(nfft // 2 + 1)
            ph = np.exp(-2j * np.pi * k * fr / nfft)
            gd = np.fft.irfft(np.fft.rfft(g, nfft) * ph, nfft)
            Wd = np.fft.irfft(np.fft.rfft(w, nfft) * ph, nfft)
            if d0 < 0 or d0 + nfft > n_out:
                continue
            y[d0:d0 + nfft] += gd
            wsum[d0:d0 + nfft] += Wd
    wsum = np.maximum(wsum, 1e-3 * wsum.max())
    return y / wsum
