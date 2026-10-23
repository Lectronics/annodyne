// superflux.hpp -- header-only C++17 SuperFlux onset detector (no dependencies).
// Written from the algorithm in Boeck & Widmer, "Maximum Filter Vibrato Suppression for Onset Detection",
// DAFx-13, and checked against the CPJKU/SuperFlux Python reference implementation (BSD-2). Not derived from aubio or Essentia.
//
// Steps: Hann-windowed STFT -> magnitude -> triangular filterbank (24 bands/octave, 30 Hz-17 kHz) -> log10(1+X)
//        -> maximum filter over 3 neighbouring bands on the frame `diff_frames` back -> positive difference, summed over bands
//        -> peak picking: local max in [n-pre_max, n+post_max], >= moving average over [n-pre_avg, n+post_avg] + threshold,
//           and a combine window.
// Input is float samples in [-1, 1]. The threshold is on log10 magnitudes of full-scale-normalised input, so it depends on input level.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace superflux {

struct Params {
  double sample_rate = 44100.0;
  double fps = 200.0;            // frame rate of the detection function
  int frame_size = 2048;         // power of two
  int bands_per_octave = 24;
  double fmin = 30.0, fmax = 17000.0;
  int max_bins = 3;              // maximum-filter width in bands
  double ratio = 0.5;            // picks diff_frames from the window shape (gives 2 at 44.1 kHz / 200 fps)
  double threshold = 1.1;
  double combine_ms = 30.0;      // merge detections closer than this
  double pre_avg = 0.15, pre_max = 0.01, post_avg = 0.0, post_max = 0.05;  // seconds
  bool online = false;           // causal: window ends at the frame, no look-ahead in peak picking
};

namespace detail {

inline void fft(std::vector<std::complex<double>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * M_PI / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (size_t k = 0; k < len / 2; ++k) {
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
      }
    }
  }
}

struct Filter { int start, mid, stop; };

inline std::vector<Filter> make_filters(const Params& p, int num_fft_bins) {
  double fmax = std::min(p.fmax, p.sample_rate / 2.0);
  const double factor = std::pow(2.0, 1.0 / p.bands_per_octave);
  std::vector<double> freqs{440.0};
  for (double f = 440.0; f <= fmax;) { f *= factor; freqs.push_back(f); }
  for (double f = 440.0; f >= p.fmin;) { f /= factor; freqs.push_back(f); }
  std::sort(freqs.begin(), freqs.end());
  const double bin_hz = (p.sample_rate / 2.0) / num_fft_bins;
  std::vector<int> bins;
  for (double f : freqs) {
    const int b = static_cast<int>(std::nearbyint(f / bin_hz));  // round half to even, like numpy
    if (b < num_fft_bins && (bins.empty() || b != bins.back())) bins.push_back(b);
  }
  std::vector<Filter> out;
  for (size_t i = 0; i + 2 < bins.size(); ++i) out.push_back({bins[i], bins[i + 1], bins[i + 2]});
  return out;
}

}  // namespace detail

// Returns onset times in seconds. If `activation` is given it receives the detection function (one value per frame).
inline std::vector<double> detect(const std::vector<float>& x, const Params& p,
                                  std::vector<double>* activation = nullptr) {
  using detail::fft;
  const int N = p.frame_size, K = N / 2;
  const double hop = p.sample_rate / p.fps;
  const long n_samples = static_cast<long>(x.size());
  const long n_frames = static_cast<long>(std::ceil(n_samples / hop));
  const auto filters = detail::make_filters(p, K);
  const int B = static_cast<int>(filters.size());

  std::vector<double> win(N);
  for (int i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (N - 1));  // == numpy.hanning
  int first = 0; while (first < N && !(win[first] > p.ratio)) ++first;
  int diff_frames = std::max(1, static_cast<int>(std::nearbyint((N / 2.0 - first) / hop)));

  // log-filtered magnitude spectrogram
  std::vector<std::vector<double>> spec(n_frames, std::vector<double>(B, 0.0));
  std::vector<std::complex<double>> buf(N);
  for (long f = 0; f < n_frames; ++f) {
    const long seek = p.online ? static_cast<long>((f + 1) * hop - N) : static_cast<long>(f * hop - N / 2.0);
    if (seek >= n_samples) break;
    for (int i = 0; i < N; ++i) {
      const long j = seek + i;
      buf[i] = (j >= 0 && j < n_samples) ? win[i] * x[j] : 0.0;
    }
    fft(buf);
    for (int b = 0; b < B; ++b) {
      const auto& fl = filters[b];
      double s = 0.0;
      for (int k = fl.start; k < fl.mid; ++k) s += std::abs(buf[k]) * (static_cast<double>(k - fl.start) / (fl.mid - fl.start));
      for (int k = fl.mid; k < fl.stop; ++k) s += std::abs(buf[k]) * (1.0 - static_cast<double>(k - fl.mid) / (fl.stop - fl.mid));
      spec[f][b] = std::log10(s + 1.0);
    }
  }

  // SuperFlux: positive difference to the max-filtered frame `diff_frames` earlier
  std::vector<double> act(n_frames, 0.0);
  const int half = p.max_bins / 2;
  for (long f = diff_frames; f < n_frames; ++f) {
    double sum = 0.0;
    for (int b = 0; b < B; ++b) {
      double m = -1e300;
      for (int d = -half; d <= p.max_bins - 1 - half; ++d) {
        const int bb = std::min(std::max(b + d, 0), B - 1);  // scipy 'reflect' at the edges, same result for width 3
        m = std::max(m, spec[f - diff_frames][bb]);
      }
      sum += std::max(spec[f][b] - m, 0.0);
    }
    act[f] = sum;
  }

  // peak picking
  const int pre_avg = static_cast<int>(std::nearbyint(p.fps * p.pre_avg));
  const int pre_max = static_cast<int>(std::nearbyint(p.fps * p.pre_max));
  const int post_max = p.online ? 0 : static_cast<int>(std::nearbyint(p.fps * p.post_max));
  const int post_avg = p.online ? 0 : static_cast<int>(std::nearbyint(p.fps * p.post_avg));
  std::vector<double> det;
  for (long n = 0; n < n_frames; ++n) {
    if (act[n] <= 0.0) continue;
    double mx = 0.0, sum = 0.0;
    for (long i = n - pre_max; i <= n + post_max; ++i) if (i >= 0 && i < n_frames) mx = std::max(mx, act[i]);
    for (long i = n - pre_avg; i <= n + post_avg; ++i) if (i >= 0 && i < n_frames) sum += act[i];
    const double avg = sum / (pre_avg + post_avg + 1);  // zero padding at the edges, as in the reference
    if (act[n] == mx && act[n] >= avg + p.threshold) det.push_back(static_cast<double>(n) / p.fps);
  }
  std::vector<double> out;
  const double combine = p.combine_ms / 1000.0;
  for (size_t i = 0; i < det.size(); ++i)
    if (i == 0 || det[i] - det[i - 1] > combine) out.push_back(det[i]);
  if (activation) *activation = std::move(act);
  return out;
}

}  // namespace superflux
