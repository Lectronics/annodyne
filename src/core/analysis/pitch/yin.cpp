#include "core/analysis/pitch/yin.h"

#include <algorithm>
#include <cassert>
#include <complex>

namespace anno {

namespace {

std::size_t next_pow2(std::size_t n) {
    std::size_t p = 2;
    while (p < n) {
        p *= 2;
    }
    return p;
}

constexpr double k_silence_energy = 1e-12;

}  // namespace

YinFrame::YinFrame(int window, int max_lag, RealFft fft) noexcept
    : window_{window}, max_lag_{max_lag}, fft_{fft} {}

Result<YinFrame> YinFrame::create(int window, int max_lag) {
    if (window < 2 || max_lag < 2) {
        return fail(ErrorCode::invalid_argument, "YinFrame needs window >= 2 and max_lag >= 2");
    }
    auto fft = RealFft::create(
        next_pow2(static_cast<std::size_t>(window) + static_cast<std::size_t>(max_lag)));
    if (!fft) {
        return Unexpected{fft.error()};
    }
    return YinFrame{window, max_lag, *fft};
}

void YinFrame::analyse(std::span<const double> segment, std::vector<double>& cmnd,
                       std::vector<double>& nccf) const {
    assert(static_cast<int>(segment.size()) == segment_length());
    const auto n = fft_.size();
    const auto w = static_cast<std::size_t>(window_);
    const auto lags = static_cast<std::size_t>(max_lag_) + 1;
    cmnd.assign(lags, 1.0);
    nccf.assign(lags, 0.0);

    // c(tau) = sum_{j < w} x[j] x[j + tau] via FFT: IFFT(conj(A) B) with A = x[0, w), B = x[0, L).
    std::vector<double> a(n, 0.0);
    std::vector<double> b(n, 0.0);
    std::copy(segment.begin(), segment.begin() + static_cast<std::ptrdiff_t>(w), a.begin());
    std::ranges::copy(segment, b.begin());
    std::vector<std::complex<double>> fa(n / 2 + 1);
    std::vector<std::complex<double>> fb(n / 2 + 1);
    fft_.forward(a, fa);
    fft_.forward(b, fb);
    for (std::size_t k = 0; k < fa.size(); ++k) {
        fa[k] = std::conj(fa[k]) * fb[k];
    }
    std::vector<double> corr(n);
    fft_.inverse(fa, corr);
    const double scale = 1.0 / static_cast<double>(n);

    // Energies of the window and of every lagged window: e(tau) = sum_{j < w} x[j + tau]^2.
    std::vector<double> prefix(segment.size() + 1, 0.0);
    for (std::size_t i = 0; i < segment.size(); ++i) {
        prefix[i + 1] = prefix[i] + segment[i] * segment[i];
    }
    const double e0 = prefix[w];
    if (e0 < k_silence_energy) {
        return;  // silent: d' stays 1, nccf 0
    }
    double running = 0.0;
    for (std::size_t tau = 1; tau < lags; ++tau) {
        const double e_tau = prefix[tau + w] - prefix[tau];
        const double c = corr[tau] * scale;
        const double d = std::max(0.0, e0 + e_tau - 2.0 * c);
        running += d;
        cmnd[tau] = running > 0.0 ? d * static_cast<double>(tau) / running : 1.0;
        const double denom = e0 + e_tau;
        nccf[tau] = denom > k_silence_energy ? 2.0 * c / denom : 0.0;
    }
    cmnd[0] = 1.0;
    nccf[0] = 1.0;
}

}  // namespace anno
