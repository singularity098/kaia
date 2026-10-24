#include "KitRuntime.h"

namespace kaia
{

std::unique_ptr<KitRuntime> buildRuntime (const Kit& kit, const SampleMap* samples)
{
    auto rt = std::make_unique<KitRuntime>();
    rt->gain = (float) dbToGain (kit.volumeDb);
    rt->tuneSemis = kit.tuneSemis;
    rt->pitchBendRange = kit.pitchBendRange;
    rt->pads.resize (kit.pads.size());

    bool anySolo = false;
    for (auto& p : kit.pads)
        anySolo = anySolo || (p.solo && p.hasAnySample());

    for (size_t i = 0; i < kit.pads.size(); ++i)
    {
        auto& src = kit.pads[i];
        auto& dst = rt->pads[i];

        dst.layerMode  = src.layerMode;
        dst.trigger    = src.trigger;
        dst.gain       = (float) dbToGain (src.volumeDb);
        dst.pan        = src.pan;
        dst.tuneSemis  = src.tuneSemis;
        dst.velToLevel = src.velToLevel;

        dst.envAD        = src.env.adMode;
        dst.decayFromEnd = src.env.decayFromEnd;
        dst.attackSec    = juce::jmax (0.0f, src.env.attackMs) * 0.001f;
        dst.holdSec      = juce::jmax (0.0f, src.env.holdMs) * 0.001f;
        dst.decaySec     = juce::jmax (0.0f, src.env.decayMs) * 0.001f;
        dst.sustain      = juce::jlimit (0.0f, 1.0f, src.env.sustain);
        dst.releaseSec   = juce::jmax (0.0f, src.env.releaseMs) * 0.001f;

        dst.filter      = src.filter;
        dst.cutoffHz    = src.cutoffHz;
        dst.resonance   = src.resonance;
        dst.velToCutoff = src.velToCutoff;

        dst.muteGroup     = src.muteGroup;
        dst.muteTargets   = src.muteTargets;
        dst.simultTargets = src.simultTargets;
        dst.mono      = src.mono;
        dst.polyphony = src.polyphony;
        dst.output    = juce::jlimit (0, kNumAuxOutputs, src.output);
        dst.audible   = ! src.mute && (! anySolo || src.solo);

        dst.keygroup = src.isKeygroup();
        dst.keyTrack = src.keyTrack;
        dst.rootNote = src.rootNote;

        dst.numLayers = 0;
        if (samples == nullptr)
            continue;

        for (auto& layer : src.layers)
        {
            if (dst.numLayers >= kMaxLayers)
                break;
            if (! layer.enabled || ! layer.hasSample() || layer.missing)
                continue;

            auto it = samples->find (layer.sample.toString());
            if (it == samples->end() || it->second == nullptr || it->second->numFrames < 2)
                continue;

            auto* sd = it->second.get();
            if (std::find (rt->keepAlive.begin(), rt->keepAlive.end(), it->second) == rt->keepAlive.end())
                rt->keepAlive.push_back (it->second);

            auto& l = dst.layers[(size_t) dst.numLayers++];
            l.sample    = sd;
            l.gain      = (float) dbToGain (layer.gainDb) * sd->gain;
            l.pan       = layer.pan;
            l.tuneSemis = layer.tuneSemis + sd->tuneSemis;
            l.velLow    = layer.velLow;
            l.velHigh   = layer.velHigh;
            l.reverse   = layer.reverse;

            auto n = sd->numFrames;
            l.start = layer.start > 0 ? layer.start : sd->defaultStart;
            l.end   = layer.end > 0 ? layer.end : sd->effectiveEnd();
            l.start = juce::jlimit<juce::int64> (0, n - 1, l.start);
            l.end   = juce::jlimit<juce::int64> (1, n, l.end);
            if (l.end <= l.start)
            {
                l.start = 0;
                l.end = n;
            }

            // Loops only make sense while a note is held
            if (src.trigger == TriggerMode::NoteOn)
            {
                if (layer.loop)
                {
                    l.loopStart = layer.loopStart;
                    l.loopEnd = layer.loopEnd > 0 ? layer.loopEnd : l.end;
                }
                else if (sd->loopEnabled)
                {
                    l.loopStart = sd->loopStart;
                    l.loopEnd = sd->loopEnd;
                }
                l.loopStart = juce::jlimit<juce::int64> (l.start, l.end, l.loopStart);
                l.loopEnd = juce::jlimit<juce::int64> (l.start, l.end, l.loopEnd);
                l.loop = l.loopEnd - l.loopStart >= 16;
            }

            l.rootNote = layer.rootNote >= 0 ? layer.rootNote : (sd->rootNote >= 0 ? sd->rootNote : src.rootNote);
        }

        if (dst.numLayers == 0)
            continue;

        if (src.isKeygroup())
        {
            for (int n = juce::jmax (0, src.noteLow); n <= juce::jmin (127, src.noteHigh); ++n)
                rt->padsForNote[(size_t) n].push_back ((int) i);
        }
        else if (src.note >= 0 && src.note < 128)
        {
            rt->padsForNote[(size_t) src.note].push_back ((int) i);
        }
    }

    return rt;
}

}
