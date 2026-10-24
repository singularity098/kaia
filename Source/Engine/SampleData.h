#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <vector>

namespace kaia
{

// Immutable decoded sample shared between the model, the UI and the audio thread.
// Channel data is padded with zeros on both sides so the interpolator never needs bounds checks.
struct SampleData
{
    static constexpr int kPad = 32;

    juce::String ref;               // SampleRef::toString()
    double sampleRate  = 44100.0;
    int    numChannels = 1;         // 1 or 2
    juce::int64 numFrames = 0;

    // Metadata from the file (WAV smpl chunk, MPC .SND header, ...)
    juce::int64 defaultStart = 0;
    juce::int64 defaultEnd   = 0;   // 0 = numFrames
    bool  loopEnabled = false;
    juce::int64 loopStart = 0;
    juce::int64 loopEnd   = 0;
    int   rootNote  = -1;
    float tuneSemis = 0.0f;
    float gain      = 1.0f;

    std::vector<float> channels[2];
    std::vector<float> overview;    // peak per bucket (0..1), for drawing

    const float* channel (int c) const noexcept
    {
        return channels[c < numChannels ? c : 0].data() + kPad;
    }

    juce::int64 effectiveEnd() const noexcept
    {
        return (defaultEnd > 0 && defaultEnd <= numFrames) ? defaultEnd : numFrames;
    }

    size_t memoryBytes() const noexcept
    {
        return (channels[0].size() + channels[1].size()) * sizeof (float);
    }

    // Allocates padded buffers for the given size.
    void allocate (int chans, juce::int64 frames)
    {
        numChannels = juce::jlimit (1, 2, chans);
        numFrames = frames;
        for (int c = 0; c < 2; ++c)
            channels[c].clear();
        for (int c = 0; c < numChannels; ++c)
            channels[c].assign ((size_t) (frames + 2 * kPad), 0.0f);
    }

    float* writePointer (int c) noexcept { return channels[c].data() + kPad; }

    void buildOverview (int buckets = 400)
    {
        overview.assign ((size_t) buckets, 0.0f);
        if (numFrames <= 0)
            return;
        for (int b = 0; b < buckets; ++b)
        {
            auto s = (juce::int64) ((double) b / buckets * (double) numFrames);
            auto e = juce::jmax (s + 1, (juce::int64) ((double) (b + 1) / buckets * (double) numFrames));
            float peak = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                auto* d = channel (c);
                for (auto i = s; i < e && i < numFrames; ++i)
                    peak = juce::jmax (peak, std::abs (d[i]));
            }
            overview[(size_t) b] = juce::jmin (1.0f, peak);
        }
    }
};

using SampleDataPtr = std::shared_ptr<const SampleData>;
using SampleMap = std::map<juce::String, SampleDataPtr>;   // SampleRef::toString() -> decoded audio

}
