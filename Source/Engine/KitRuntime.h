#pragma once

#include "../Model/Kit.h"
#include "../Model/TriggerMap.h"
#include "SampleData.h"
#include <map>

namespace kaia
{

// A render-ready, immutable snapshot of a Kit. Built on the message thread, handed to the audio
// thread through SamplerEngine::setRuntime(). Holds references to all sample data it uses.
struct LayerRT
{
    const SampleData* sample = nullptr;
    float  gain      = 1.0f;     // linear, includes the sample file's own level
    float  pan       = 0.0f;
    double tuneSemis = 0.0;      // includes the sample file's own tuning
    int    velLow    = 0;
    int    velHigh   = 127;
    juce::int64 start = 0;
    juce::int64 end   = 0;       // exclusive, always valid
    bool   reverse   = false;
    bool   loop      = false;
    juce::int64 loopStart = 0;
    juce::int64 loopEnd   = 0;
    int    rootNote  = 60;
};

struct PadRT
{
    int numLayers = 0;
    std::array<LayerRT, kMaxLayers> layers;

    LayerMode   layerMode = LayerMode::Velocity;
    TriggerMode trigger   = TriggerMode::OneShot;
    float  gain       = 1.0f;
    float  pan        = 0.0f;
    double tuneSemis  = 0.0;
    float  velToLevel = 1.0f;

    bool  envAD = true, decayFromEnd = true;
    float attackSec = 0, holdSec = 0, decaySec = 0.002f, sustain = 1.0f, releaseSec = 0.002f;

    FilterType filter = FilterType::Off;
    float cutoffHz = 20000.0f, resonance = 0.0f, velToCutoff = 0.0f;

    int  muteGroup = 0;
    std::array<int, 4> muteTargets { -1, -1, -1, -1 };
    std::array<int, 4> simultTargets { -1, -1, -1, -1 };
    bool mono = false;
    int  polyphony = 0;
    int  output = 0;
    bool audible = true;          // mute/solo resolved

    bool keygroup = false;
    bool keyTrack = false;
    int  rootNote = 60;
};

struct KitRuntime
{
    std::vector<PadRT> pads;
    std::array<std::vector<int>, 128> padsForNote;   // which pads respond to each program note (UI audition, simult targets)
    std::array<std::vector<int>, 128> padsForInput;  // which pads incoming MIDI notes play (after the trigger map)
    std::array<int, kMaxPads> padNote {};            // the program note a pad's voices carry (mute targets refer to it)

    // A sound auditioned from the library, played on Main outside the kit
    PadRT preview;
    juce::uint32 previewId = 0;
    float  gain = 1.0f;
    double tuneSemis = 0.0;
    float  pitchBendRange = 2.0f;

    std::vector<SampleDataPtr> keepAlive;
};

// Something to audition that is not part of the kit (a library sample or a pad of another program)
struct PreviewSound
{
    Pad pad;
    SampleMap samples;
    juce::uint32 id = 0;
};

// triggers: how incoming MIDI notes reach the pads (nullptr = the program's own note map).
// playLoops: honour sample/layer loops on note-on pads (otherwise every sample plays once).
std::unique_ptr<KitRuntime> buildRuntime (const Kit& kit, const SampleMap* samples,
                                          const TriggerMap* triggers = nullptr, const PreviewSound* preview = nullptr,
                                          bool playLoops = true);

}
