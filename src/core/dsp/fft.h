#pragma once

#include "core/types/result.h"

#include <complex>
#include <cstddef>
#include <span>

// Real FFT (design: docs/design/phase3-dsp.md; pocketfft, ADR-0009). No pocketfft header appears
// here.

namespace anno {

/// Real FFT of fixed length n in double precision. Uses pocketfft's function API (r2c / c2r), whose
/// plans live in an internal mutex-protected cache: calls are thread-safe but may lock and
/// allocate, so nothing here is real-time safe.
class RealFft {
public:
    /// Errors: invalid_argument if n < 2.
    [[nodiscard]] static Result<RealFft> create(std::size_t n);

    /// Pre (asserted in debug): in.size() == size(), out.size() == size() / 2 + 1.
    void forward(std::span<const double> in, std::span<std::complex<double>> out) const;

    /// Pre (asserted in debug): in.size() == size() / 2 + 1, out.size() == size(). Unnormalised,
    /// like pocketfft: the result is size() times the original signal.
    void inverse(std::span<const std::complex<double>> in, std::span<double> out) const;

    [[nodiscard]] std::size_t size() const noexcept { return n_; }

private:
    explicit RealFft(std::size_t n) noexcept : n_{n} {}

    std::size_t n_;
};

}  // namespace anno
