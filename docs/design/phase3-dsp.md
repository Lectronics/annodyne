# Design note: Phase 3 — core/dsp signal basics

- **Phase:** 3
- **Status:** draft
- **Spec sections:** architecture §4.1 (signal basics); implementation guide B2 (STFT use), B13 (K-weighting); ADR-0009 (pocketfft)
- **Folder / CMake target:** `src/core/dsp` (layer 0; links `core/types`, private dep `pocketfft`)

## Purpose
The shared signal maths every analysis stage uses: an FFT wrapper, windows, STFT/ISTFT, resampling, BS.1770 K-weighting and curve interpolation. Pure functions on `std::span<const float>` / `double`, no I/O. Analysis works on a **mono mix** of the source (mean of channels); the source file is never modified.

## Public types
| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `WindowKind` | enum class | — | — | value |
| `StftParams` | struct | n_fft > 0 even; 0 < hop_samples <= n_fft | caller | value |
| `Spectrogram` | struct | data.size() == n_frames * n_bins; n_bins == n_fft/2 + 1 | caller | value (`shared_ptr<const>` when long-lived) |
| `RealFft` | class | fixed length n >= 2, plan owned | caller | not copyable, thread-compatible (const methods are thread-safe) |
| `ResampleParams` | struct | taps_per_side >= 8 | caller | value |
| `InterpKind` | enum class | — | — | value |

```cpp
enum class WindowKind { hann_periodic, blackman_harris };
enum class InterpKind { linear, cubic, hold };

struct StftParams {
    int n_fft = 2048;
    int hop_samples = 512;
    WindowKind window = WindowKind::hann_periodic;
};

/// Frame i is centred at sample i * hop_samples of the input (the signal is zero-padded by n_fft/2 at
/// both ends), so frame times are on a regular grid and a FrameGrid describes them.
struct Spectrogram {
    int n_fft = 0;
    int hop_samples = 0;
    Hz sample_rate;
    std::int64_t n_frames = 0;
    std::int64_t n_bins = 0;                       // n_fft / 2 + 1
    std::vector<std::complex<float>> data;         // frame-major: data[frame * n_bins + bin]
    [[nodiscard]] std::complex<float> at(std::int64_t frame, std::int64_t bin) const;
};
```

## Public functions
```cpp
/// Window of `length` samples (periodic form for hann so it satisfies COLA at hop = length/4, /2).
/// Pure; real-time safe except for the allocation of the result.
[[nodiscard]] std::vector<double> make_window(WindowKind kind, int length);

/// Mean of the channels as float. Pure.
[[nodiscard]] std::vector<float> mix_to_mono(const AudioBuffer& audio);

/// Real FFT of fixed length n (pocketfft, double precision).
class RealFft {
public:
    explicit RealFft(std::size_t n);
    /// in.size() == n, out.size() == n/2 + 1. Const and thread-safe; allocates scratch internally.
    void forward(std::span<const double> in, std::span<std::complex<double>> out) const;
    /// out.size() == n; unnormalised like pocketfft (result is n times the original).
    void inverse(std::span<const std::complex<double>> in, std::span<double> out) const;
    [[nodiscard]] std::size_t size() const noexcept;
};

/// STFT of a mono signal. Errors: invalid_argument for bad params or an empty signal.
/// Pure; thread-safe; not real-time safe. Cost: O(N log n_fft).
[[nodiscard]] Result<Spectrogram> stft(std::span<const float> mono, Hz sample_rate, const StftParams& params);

/// Inverse by weighted overlap-add with window-power normalisation; returns exactly `length`
/// samples. Reconstructs `stft` output to within 1e-5 (float storage) wherever the window sum is
/// above 1e-3. Errors: invalid_argument if length does not fit the frame count.
[[nodiscard]] Result<std::vector<float>> istft(const Spectrogram& spec, WindowKind window, std::int64_t length);

struct ResampleParams { int taps_per_side = 32; };   // Kaiser (beta 8.6) windowed sinc

/// Band-limited resampling to `to` (cutoff 0.97 * min(from, to) / 2). Output length
/// round(n * to / from). Errors: invalid_argument for non-finite or non-positive rates.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<float>> resample(std::span<const float> mono, Hz from, Hz to, const ResampleParams& params);

/// ITU-R BS.1770 K-weighting (high shelf +4 dB at 1.68 kHz, then 38 Hz high-pass), coefficients
/// derived analytically for any sample rate (De Man, "Evaluation of implementations of the
/// ITU-R BS.1770 loudness measure"). Zero initial state. Pure.
[[nodiscard]] std::vector<float> k_weight(std::span<const float> mono, Hz sample_rate);

/// Value of a frame curve at arbitrary source times. values.size() == grid.n_frames. Outside the
/// grid the nearest frame value is held. Pure; real-time safe except the result allocation.
[[nodiscard]] std::vector<double> interp_curve(std::span<const double> values, const FrameGrid& grid,
                                               std::span<const SourceTime> times, InterpKind kind);
```

Time axes: all sample-index based; `interp_curve` is the only function on the source time axis. Units: Hz for rates, samples for sizes, `SourceTime` for times.

## Effect on audio
None. Analysis only.

## Split / join decisions
- `RealFft` is a class (it owns a plan; one planning cost, many transforms) — split class rule 2 does not apply, it is the only owner of pocketfft state.
- `stft`/`istft` are free functions (pure) — join rule 4.
- `k_weight` and `resample` stay separate from `stft`: independently testable with synthetic tones (split function rule 4).
- No `Window` class: `make_window` returns a vector; a class would be a middle man.

## Dependencies
`core/types`; external **pocketfft** (BSD-3, header-only), `core/dsp` only, via `cmake/AnnoPocketfft.cmake` and the allowlist in `cmake/AnnoLayers.cmake` (ADR-0009). `-ffp-contract=off` stays on this target.

## Tests
- Unit (synthetic): window values and COLA sum for hann at hop n/4; `stft` of a sine peaks at the right bin; `stft`→`istft` reconstruction ≤ 1e-5; `RealFft` round trip and agreement with a naive DFT on random data; `resample` 48k→16k and 44.1k→48k keeps a 1 kHz tone's frequency (±0.1 %), level (±0.1 dB) and length; `k_weight`: gain at 1 kHz ≈ 0 dB (±0.1) and −23 dBFS 1 kHz sine measures −23 LUFS after the −0.691 offset; low frequencies attenuated (38 Hz ≈ −3 dB); `interp_curve` exact on linear data, hold is stepwise, cubic exact on quadratics inside the grid.
- Contract: determinism (same input twice → same hash); `stft` frame times regular.

## Open questions
None blocking. Sample-rate conversion quality is verified on tones only; a listening check is not needed (analysis path).

## Review log
*(structure-reviewer findings go here)*
