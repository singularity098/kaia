#include "SndDecoder.h"

namespace kaia
{

namespace
{
    juce::uint16 rd16 (const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
    juce::uint32 rd32 (const juce::uint8* p) { return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24); }
    float s16 (const juce::uint8* p) { return (float) (juce::int16) rd16 (p) / 32768.0f; }
}

bool isMpcSnd (const void* data, size_t size)
{
    auto* d = static_cast<const juce::uint8*> (data);
    return size >= 38 && d[0] == 0x01 && (d[1] == 0x04 || d[1] == 0x02);
}

bool decodeMpcSnd (const void* data, size_t size, SampleData& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data);
    if (! isMpcSnd (data, size))
    {
        error = "not an MPC .SND file";
        return false;
    }

    if (d[1] == 0x04)
    {
        // MPC2000 / MPC2000XL: 42 byte header
        // 0 magic(2) 2 name(17) 19 level 20 tune(s8, 1/10 semitone) 21 stereo
        // 22 start 26 end 30 frameCount 34 loopLength(u32) 38 loopEnabled 39 beats 40 sampleRate(u16)
        if (size < 42)
        {
            error = "truncated .SND header";
            return false;
        }

        auto level      = d[19];
        auto tune       = (juce::int8) d[20];
        auto stereo     = d[21] != 0;
        auto start      = (juce::int64) rd32 (d + 22);
        auto end        = (juce::int64) rd32 (d + 26);
        auto frames     = (juce::int64) rd32 (d + 30);
        auto loopLength = (juce::int64) rd32 (d + 34);
        auto loopOn     = d[38] != 0;
        auto rate       = (int) rd16 (d + 40);

        auto available = (juce::int64) ((size - 42) / 2) / (stereo ? 2 : 1);
        frames = juce::jmin (frames > 0 ? frames : available, available);

        out.allocate (stereo ? 2 : 1, frames);
        out.sampleRate = rate > 4000 ? rate : 44100;

        // Stereo sounds are stored as the whole left channel followed by the whole right channel.
        for (int c = 0; c < (stereo ? 2 : 1); ++c)
        {
            auto* src = d + 42 + (size_t) c * (size_t) frames * 2;
            auto* dst = out.writePointer (c);
            for (juce::int64 i = 0; i < frames; ++i)
                dst[i] = s16 (src + i * 2);
        }

        out.defaultStart = juce::jlimit<juce::int64> (0, frames, start);
        out.defaultEnd   = (end > out.defaultStart && end <= frames) ? end : frames;
        out.tuneSemis    = (float) tune / 10.0f;
        out.gain         = level > 0 ? (float) level / 100.0f : 1.0f;
        out.loopEnabled  = loopOn && loopLength > 0;
        out.loopEnd      = out.defaultEnd;
        out.loopStart    = juce::jlimit<juce::int64> (0, out.loopEnd, out.defaultEnd - loopLength);
        return true;
    }

    // MPC3000: 38 byte header, 44.1 kHz, 16-bit
    // 0 magic(2) 2 name(17) 19 level 20 tune 21 stereo 22 softStart 26 softEnd 30 frameCount 34 hardStart
    auto level  = d[19];
    auto stereo = d[21] != 0;
    auto start  = (juce::int64) rd32 (d + 22);
    auto end    = (juce::int64) rd32 (d + 26);
    auto count  = (juce::int64) rd32 (d + 30);
    auto totalSamples = (juce::int64) ((size - 38) / 2);
    count = juce::jmin (count > 0 ? count : totalSamples, totalSamples);

    juce::int64 frames = count;
    int chans = 1;
    if (stereo && totalSamples >= count * 2)
        chans = 2;                       // count frames per channel, channels stored one after the other
    else if (stereo)
    {
        chans = 2;
        frames = count / 2;              // count is the total number of samples
    }

    out.allocate (chans, frames);
    out.sampleRate = 44100.0;
    for (int c = 0; c < chans; ++c)
    {
        auto* src = d + 38 + (size_t) c * (size_t) frames * 2;
        auto* dst = out.writePointer (c);
        for (juce::int64 i = 0; i < frames; ++i)
            dst[i] = s16 (src + i * 2);
    }

    out.defaultStart = juce::jlimit<juce::int64> (0, frames, start);
    out.defaultEnd   = (end > out.defaultStart && end <= frames) ? end : frames;
    out.gain         = level > 0 ? (float) level / 100.0f : 1.0f;
    return true;
}

}
