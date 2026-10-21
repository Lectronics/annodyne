#include "core/dsp/fft.h"

#include <cassert>
#include <pocketfft_hdronly.h>

namespace anno {

Result<RealFft> RealFft::create(std::size_t n) {
    if (n < 2) {
        return fail(ErrorCode::invalid_argument, "RealFft needs a length of at least 2");
    }
    return RealFft{n};
}

void RealFft::forward(std::span<const double> in, std::span<std::complex<double>> out) const {
    assert(in.size() == n_ && out.size() == n_ / 2 + 1);
    const pocketfft::shape_t shape{n_};
    const pocketfft::stride_t stride_in{static_cast<std::ptrdiff_t>(sizeof(double))};
    const pocketfft::stride_t stride_out{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    pocketfft::r2c(shape, stride_in, stride_out, 0, pocketfft::FORWARD, in.data(), out.data(), 1.0);
}

void RealFft::inverse(std::span<const std::complex<double>> in, std::span<double> out) const {
    assert(in.size() == n_ / 2 + 1 && out.size() == n_);
    const pocketfft::shape_t shape{n_};
    const pocketfft::stride_t stride_in{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    const pocketfft::stride_t stride_out{static_cast<std::ptrdiff_t>(sizeof(double))};
    pocketfft::c2r(shape, stride_in, stride_out, 0, pocketfft::BACKWARD, in.data(), out.data(),
                   1.0);
}

}  // namespace anno
