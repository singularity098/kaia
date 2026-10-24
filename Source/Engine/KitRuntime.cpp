#include "KitRuntime.h"

namespace kaia
{

namespace
{
    void buildPad (const Pad& src, PadRT& dst, const SampleMap* samples, bool audible, bool playLoops, std::vector<SampleDataPtr>& keepAlive)
    {
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
        dst.audible   = audible;

        dst.keygroup = src.isKeygroup();
        dst.keyTrack = src.keyTrack;
        dst.rootNote = src.rootNote;

        dst.numLayers = 0;
        if (samples == nullptr)
            return;

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
            if (std::find (keepAlive.begin(), keepAlive.end(), it->second) == keepAlive.end())
                keepAlive.push_back (it->second);

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
            if (src.trigger == TriggerMode::NoteOn && playLoops)
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
    }
}

std::unique_ptr<KitRuntime> buildRuntime (const Kit& kit, const SampleMap* samples, const TriggerMap* triggers, const PreviewSound* preview,
                                          bool playLoops)
{
    auto rt = std::make_unique<KitRuntime>();
    rt->gain = (float) dbToGain (kit.volumeDb);
    rt->tuneSemis = kit.tuneSemis;
    rt->pitchBendRange = kit.pitchBendRange;
    rt->pads.resize (kit.pads.size());
    rt->padNote.fill (-1);

    bool anySolo = false;
    for (auto& p : kit.pads)
        anySolo = anySolo || (p.solo && p.hasAnySample());

    bool useTriggerMap = triggers != nullptr && ! triggers->isDefault() && ! kit.isKeygroupProgram;
    bool hiHatChoke = useTriggerMap && triggers->usesModule() && triggers->hiHatChoke;

    for (size_t i = 0; i < kit.pads.size(); ++i)
    {
        auto& src = kit.pads[i];
        auto& dst = rt->pads[i];
        buildPad (src, dst, samples, ! src.mute && (! anySolo || src.solo), playLoops, rt->keepAlive);

        if (i < (size_t) kMaxPads)
            rt->padNote[i] = src.isKeygroup() ? -1 : src.note;

        // With a drum module the hi-hat pads cut each other off, unless the kit already groups them
        if (hiHatChoke && dst.muteGroup == 0
             && ((int) i == drumZonePad (DrumZone::HiHatClosed) || (int) i == drumZonePad (DrumZone::HiHatPedal)
                  || (int) i == drumZonePad (DrumZone::HiHatOpen)))
            dst.muteGroup = kHiHatChokeGroup;

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

    if (useTriggerMap)
    {
        auto table = triggers->resolve (kit);
        for (size_t n = 0; n < 128; ++n)
            for (auto pad : table[n])
                if (rt->pads[(size_t) pad].numLayers > 0)
                    rt->padsForInput[n].push_back (pad);
    }
    else
    {
        rt->padsForInput = rt->padsForNote;
    }

    if (preview != nullptr)
    {
        // Auditions play once through Main, whatever the pad's own trigger mode and routing
        auto pad = preview->pad;
        pad.trigger = TriggerMode::OneShot;
        pad.noteLow = pad.noteHigh = -1;
        pad.output = 0;
        pad.muteGroup = 0;
        buildPad (pad, rt->preview, &preview->samples, true, false, rt->keepAlive);
        rt->previewId = preview->id;
    }

    return rt;
}

}
