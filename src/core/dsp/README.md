# core/dsp

Signal maths reused by more than one module (layer 0). Design: `docs/design/phase3-dsp.md`, ADR-0009.

- `crossfade`: equal-power, linear and log fades between aligned signals, computed in double and cast
  once; `crossfade` fades a *slice* of a longer ramp so a region cutting through a ramp matches one
  rendered whole.
- `window`: periodic Hann (COLA at hop N/2, N/4) and 4-term Blackman-Harris (Harris 1978).
- `fft`: `RealFft`, a thin wrapper over pocketfft (BSD-3, header-only, pinned commit; the only
  third-party code in this target, never exposed in a public header). Thread-safe, not real-time safe
  (pocketfft keeps a mutex-protected plan cache).
- `stft`: `stft` / `istft` with centred zero-padded frames (frame i at sample i * hop), window power
  normalisation on the inverse. The hop is a whole number of samples; callers use the actual hop.
- `resample`: Kaiser-windowed (beta 8.6) sinc, cutoff 0.97 of the narrower Nyquist, any rate pair.
- `k_weight`: ITU-R BS.1770 K-weighting (shelf + 38 Hz high-pass), analytic coefficients for any rate
  (De Man). Its gain at 1 kHz is +0.691 dB, which is where the -0.691 in the LUFS formula comes from.
- `interp_curve`: linear / cubic (4-point Lagrange) / hold interpolation of a frame curve; unit-agnostic.
- `mono`: `MonoAudio`, `mix_to_mono`, `read_mono` (mean of channels; the source is only read).

Compiled with `-ffp-contract=off` so results are bit-reproducible on one build.

Known limitations: `log` crossfade never reaches zero (-60 dB floor); equal-power adds up to +3 dB at
the midpoint for correlated signals (see ADR-0006); `resample` costs O(n_out x taps) per output sample
(fine for analysis, not tuned); the truncated kernel is slightly off at the first and last
`taps_per_side` samples; nothing here is real-time safe.

## filter (zero-phase Butterworth)
`zero_phase_highpass` / `zero_phase_lowpass`: 2nd-order Butterworth run forward and backward (no group
delay; magnitude squared: -6 dB at the cutoff, 24.6 dB down one octave outside), odd reflection of three
cutoff periods at the edges. One shared `Biquad` (`detail/biquad.h`) serves these and `k_weight`
(bit-exact test). Works on a `double` copy of the signal (about 64 MB per 3 minutes at 44.1 kHz).
