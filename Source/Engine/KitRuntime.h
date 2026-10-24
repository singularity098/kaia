#pragma once

#include "../Model/Kit.h"
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
    std::array<std::vector<int>, 128> padsForNote;   // which pads respond to each MIDI note
    float  gain = 1.0f;
    double tuneSemis = 0.0;
    float  pitchBendRange = 2.0f;

    std::vector<SampleDataPtr> keepAlive;
};

std::unique_ptr<KitRuntime> buildRuntime (const Kit& kit, const SampleMap* samples);

}
