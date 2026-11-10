// =============================================================================
// Fft.h
//
// A small, self-contained Fast Fourier Transform.
//
// The FFT turns a short slice of audio (amplitude over time) into a spectrum
// (how much energy there is at each frequency). The classifier needs the
// spectrum because a kick is mostly low frequencies and a hi-hat is mostly
// high frequencies.
//
// This is the classic "iterative radix-2" algorithm. It only works when the
// number of points is a power of two (512, 1024, 2048, ...).
// =============================================================================

#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace kushaya
{

// Returns the smallest power of two that is >= n.
inline int nextPowerOfTwo (int n)
{
    int p = 1;
    while (p < n)
        p *= 2;
    return p;
}

// Transforms `data` in place. data.size() must be a power of two.
// No memory is allocated here, so it is safe to call on the audio thread.
inline void fftInPlace (std::vector<std::complex<double>>& data)
{
    const int n = (int) data.size();
    const double pi = 3.14159265358979323846;

    // Step 1: reorder the input into "bit-reversed" order.
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j)
            std::swap (data[(size_t) i], data[(size_t) j]);
    }

    // Step 2: combine pairs, then groups of 4, then 8, ... up to n.
    for (int len = 2; len <= n; len *= 2)
    {
        const double angle = -2.0 * pi / (double) len;
        const std::complex<double> step (std::cos (angle), std::sin (angle));

        for (int start = 0; start < n; start += len)
        {
            std::complex<double> w (1.0, 0.0);

            for (int k = 0; k < len / 2; ++k)
            {
                const auto even = data[(size_t) (start + k)];
                const auto odd  = data[(size_t) (start + k + len / 2)] * w;

                data[(size_t) (start + k)]           = even + odd;
                data[(size_t) (start + k + len / 2)] = even - odd;

                w *= step;
            }
        }
    }
}

} // namespace kushaya
