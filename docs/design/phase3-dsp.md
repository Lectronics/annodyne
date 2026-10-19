# Design note: Phase 3 — core/dsp signal basics

- **Phase:** 3
- **Status:** implemented (reviewed in two rounds; see Review log and Implementation notes)
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

/// A mono signal with its rate: what every analysis stage reads. Value type.
struct MonoAudio {
    std::vector<float> samples;
    Hz sample_rate;
};

struct StftParams {
    int n_fft = 2048;
    int hop_samples = 512;
    WindowKind window = WindowKind::hann_periodic;
};

/// Frame i is centred at sample i * hop_samples of the input (the signal is zero-padded by n_fft/2 at
/// both ends), so frame times are on a regular grid. The hop is a whole number of samples: callers
/// that want a hop in seconds round it (`hop_samples = round(hop * rate)`) and use the ACTUAL hop,
/// `hop_samples / rate`, for frame times (at 44.1 kHz a 5 ms hop is 220 samples = 4.9887 ms).
struct Spectrogram {
    int n_fft = 0;
    int hop_samples = 0;
    WindowKind window = WindowKind::hann_periodic;   // stored so istft cannot be given another one
    Hz sample_rate;
    std::int64_t n_frames = 0;                       // floor(N / hop_samples) + 1 for an N-sample input
    std::int64_t n_bins = 0;                       // n_fft / 2 + 1
    std::vector<std::complex<float>> data;         // frame-major: data[frame * n_bins + bin]
    [[nodiscard]] std::complex<float> at(std::int64_t frame, std::int64_t bin) const;
};
```

## Public functions
```cpp
/// Window of `length` samples (periodic form for hann so it satisfies COLA at hop = length/4, /2).
/// Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<double> make_window(WindowKind kind, int length);

/// Mean of the channels as float (antiphase channels cancel, which is correct for a mono mix). Pure.
[[nodiscard]] std::vector<float> mix_to_mono(const AudioBuffer& audio);

/// The whole source as a mono mix, read once so every stage shares it. Errors: out_of_range /
/// io errors from the source; an empty source gives an empty signal (not an error).
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<MonoAudio> read_mono(const AudioSource& src);

/// Real FFT of fixed length n (pocketfft, double precision). Uses pocketfft's function API
/// (`r2c`/`c2r`), whose plans live in an internal mutex-protected cache: calls are thread-safe but
/// may lock and allocate, so nothing here is real-time safe.
class RealFft {
public:
    /// Errors: invalid_argument if n < 2.
    [[nodiscard]] static Result<RealFft> create(std::size_t n);
    /// Pre: in.size() == size(), out.size() == size()/2 + 1 (asserted in debug).
    void forward(std::span<const double> in, std::span<std::complex<double>> out) const;
    /// Pre: in.size() == size()/2 + 1, out.size() == size(). Unnormalised like pocketfft (the result is
    /// size() times the original).
    void inverse(std::span<const std::complex<double>> in, std::span<double> out) const;
    [[nodiscard]] std::size_t size() const noexcept;
private:
    explicit RealFft(std::size_t n) noexcept;
    std::size_t n_;
};

/// STFT of a mono signal. Errors: invalid_argument for bad params or an empty signal.
/// Pure; thread-safe; not real-time safe. Cost: O(N log n_fft).
[[nodiscard]] Result<Spectrogram> stft(std::span<const float> mono, Hz sample_rate, const StftParams& params);

/// Inverse by weighted overlap-add with window-power normalisation (uses spec.window); returns exactly
/// `length` samples. Reconstructs `stft` output to within 1e-5 (float storage) wherever the window
/// sum is above 1e-3. Errors: invalid_argument if the spectrogram is invalid or `length` does not fit
/// its frame count.
[[nodiscard]] Result<std::vector<float>> istft(const Spectrogram& spec, std::int64_t length);

/// Errors: invalid_argument if n_fft/hop are not positive, n_bins != n_fft/2 + 1, or data.size() !=
/// n_frames * n_bins. Pure.
[[nodiscard]] Result<void> validate(const Spectrogram& spec);

struct ResampleParams { int taps_per_side = 32; };   // Kaiser (beta 8.6) windowed sinc

/// Band-limited resampling to `to` (cutoff 0.97 * min(from, to) / 2). Output length
/// round(n * to / from). Errors: invalid_argument for non-finite or non-positive rates.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<float>> resample(std::span<const float> mono, Hz from, Hz to, const ResampleParams& params);

/// ITU-R BS.1770 K-weighting (high shelf +4 dB at 1.68 kHz, then 38 Hz high-pass), coefficients
/// derived analytically for any sample rate (De Man, "Evaluation of implementations of the
/// ITU-R BS.1770 loudness measure"). Zero initial state. Pure.
[[nodiscard]] std::vector<float> k_weight(std::span<const float> mono, Hz sample_rate);

/// Value of a frame curve at arbitrary source times. The unit of the output is the unit of the input
/// (the function is unit-agnostic: cents in, cents out). values.size() == grid.n_frames. Outside the
/// grid the nearest frame value is held. A NaN value gives NaN wherever it contributes (the caller
/// fills unvoiced gaps first). Pure; thread-safe; not real-time safe (allocates).
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
`core/types`; external **pocketfft** (BSD-3, header-only), `core/dsp` only, via `cmake/AnnoPocketfft.cmake` and the allowlist in `cmake/AnnoLayers.cmake` (ADR-0009). `-ffp-contract=off` stays on this target and is added to `core/analysis/*` (same determinism claim). `src/core/dsp/README.md` is updated (methods, references: Harris 1978 windows, Kaiser windowed-sinc resampling, De Man's BS.1770 coefficients).

## Tests
- Unit (synthetic): window values and COLA sum for hann at hop n/4; `stft` of a sine peaks at the right bin; `stft`→`istft` reconstruction ≤ 1e-5; `RealFft` round trip and agreement with a naive DFT on random data; `resample` 48k→16k and 44.1k→48k keeps a 1 kHz tone's frequency (±0.1 %), level (±0.1 dB) and length; `k_weight`: gain at 1 kHz ≈ 0 dB (±0.1) and −23 dBFS 1 kHz sine measures −23 LUFS after the −0.691 offset; low frequencies attenuated (38 Hz ≈ −3 dB); `interp_curve` exact on linear data, hold is stepwise, cubic exact on quadratics inside the grid.
- Edge cases: empty signal (stft is invalid_argument; read_mono returns an empty signal), signal shorter than n_fft (one frame, zero-padded), non-44.1/48 kHz rates (22.05, 32, 96 kHz) for `stft` frame counts and `k_weight`, stereo with antiphase channels mixes to silence, `RealFft::create(1)` is an error.
- Contract: determinism (same input twice → same hash); `stft` frame count = floor(N/hop)+1 and times regular; `validate(Spectrogram)` rejects mismatched shapes.

## Open questions
None blocking. Sample-rate conversion quality is verified on tones only; a listening check is not needed (analysis path).

## Review log
*(structure-reviewer findings go here)*

## Implementation notes
- The pocketfft header is `pocketfft_hdronly.h` (pinned commit in `cmake/AnnoPocketfft.cmake`). The fixture layer rules `allow_dsp_pocketfft` / `deny_analysis_pocketfft` pass.
- `RealFft` wraps pocketfft's function API (`r2c` / `c2r`); the class holds only the length.
- The K-weighting filter has **+0.691 dB** gain at 1 kHz (not 0 dB): that is where the -0.691 in the LUFS formula comes from. The test pins +0.691 +- 0.1 dB at 44.1, 48 and 96 kHz, a -3.01 LUFS reading for a full-scale mono sine, and about -6 dB at 38 Hz (the high-pass has Q = 0.5).
- `interp_curve` is unit-agnostic and propagates NaN; `read_mono` mixes by the mean (antiphase stereo gives silence).
- `-ffp-contract=off` is also set on every `core/analysis/*` target.
- Tolerances for these tests live in `tests/tolerances.h` (analysis DSP block).