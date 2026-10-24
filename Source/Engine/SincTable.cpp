#include "SincTable.h"
#include <cmath>

namespace kaia
{

namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum)
                break;
        }
        return sum;
    }
}

const SincTable& SincTable::get()
{
    static const SincTable table;
    return table;
}

SincTable::SincTable()
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double beta = 8.6;   // Kaiser window: ~ -90 dB side lobes
    const double i0beta = besselI0 (beta);

    const double maxInc[kBands] = { 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 6.0 };

    for (int b = 0; b < kBands; ++b)
    {
        bandMaxIncrement[b] = (float) maxInc[b];
        const double cutoff = 0.91 / maxInc[b];   // relative to the source Nyquist

        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double) p / kPhases;
            double sum = 0.0;
            double h[kTaps];

            for (int t = 0; t < kTaps; ++t)
            {
                const double x = (double) (t - (kHalf - 1)) - frac;   // distance from the read position
                const double sincArg = pi * cutoff * x;
                const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (sincArg) / sincArg;
                const double wPos = x / (double) kHalf;
                const double w = std::abs (wPos) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - wPos * wPos)) / i0beta;
                h[t] = cutoff * sinc * w;
                sum += h[t];
            }

            for (int t = 0; t < kTaps; ++t)
                coeffs[b][p][t] = (float) (h[t] / sum);   // unity gain at DC
        }
    }
}

}
