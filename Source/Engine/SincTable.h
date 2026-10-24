#pragma once

#include <array>

namespace kaia
{

// Polyphase windowed-sinc interpolation tables. Several bands with lower cut-offs are used when a
// sample is played faster than its native rate (pitched up, or a 96 kHz file in a 48 kHz session)
// so the result doesn't alias.
struct SincTable
{
    static constexpr int kTaps   = 24;              // even
    static constexpr int kHalf   = kTaps / 2;       // taps run from i - (kHalf - 1) to i + kHalf
    static constexpr int kPhases = 256;
    static constexpr int kBands  = 8;

    alignas (32) float coeffs[kBands][kPhases + 1][kTaps];
    float bandMaxIncrement[kBands];

    static const SincTable& get();

    int bandFor (double increment) const noexcept
    {
        for (int b = 0; b < kBands - 1; ++b)
            if (increment <= bandMaxIncrement[b])
                return b;
        return kBands - 1;
    }

private:
    SincTable();
};

}
