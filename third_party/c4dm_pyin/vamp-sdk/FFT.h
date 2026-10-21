/* SPDX-License-Identifier: GPL-2.0-or-later

    Annodyne shim for the one Vamp SDK class that the pYIN sources use (Vamp::FFTReal), written
    for this repository and not part of the upstream pYIN distribution. It has the interface that
    YinUtil.cpp expects: real input of n samples, interleaved complex output of n + 2 doubles
    (n / 2 + 1 bins), and an inverse that returns the real signal scaled by 1 / n. Implemented with
    pocketfft (BSD-3-Clause, ADR-0009). See ANNODYNE.md.
*/
#ifndef ANNODYNE_C4DM_PYIN_VAMP_SDK_FFT_H
#define ANNODYNE_C4DM_PYIN_VAMP_SDK_FFT_H

#include <pocketfft_hdronly.h>

#include <complex>
#include <cstddef>
#include <vector>

namespace Vamp {

class FFTReal {
public:
    explicit FFTReal(unsigned int n) : m_n(n), m_bins(n / 2 + 1), m_spectrum(n / 2 + 1) {}

    /// complexOut: n + 2 doubles, (re, im) per bin.
    void forward(const double *realIn, double *complexOut) {
        const pocketfft::shape_t shape{m_n};
        const pocketfft::stride_t strideIn{static_cast<std::ptrdiff_t>(sizeof(double))};
        const pocketfft::stride_t strideOut{
            static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
        pocketfft::r2c(shape, strideIn, strideOut, 0, pocketfft::FORWARD, realIn,
                       m_spectrum.data(), 1.0);
        for (std::size_t k = 0; k < m_bins; ++k) {
            complexOut[2 * k] = m_spectrum[k].real();
            complexOut[2 * k + 1] = m_spectrum[k].imag();
        }
    }

    /// complexIn: n + 2 doubles. Output scaled by 1 / n.
    void inverse(const double *complexIn, double *realOut) {
        for (std::size_t k = 0; k < m_bins; ++k) {
            m_spectrum[k] = std::complex<double>(complexIn[2 * k], complexIn[2 * k + 1]);
        }
        const pocketfft::shape_t shape{m_n};
        const pocketfft::stride_t strideIn{
            static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
        const pocketfft::stride_t strideOut{static_cast<std::ptrdiff_t>(sizeof(double))};
        pocketfft::c2r(shape, strideIn, strideOut, 0, pocketfft::BACKWARD, m_spectrum.data(),
                       realOut, 1.0 / static_cast<double>(m_n));
    }

private:
    std::size_t m_n;
    std::size_t m_bins;
    std::vector<std::complex<double>> m_spectrum;
};

}  // namespace Vamp

#endif
