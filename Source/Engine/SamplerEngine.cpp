#include "SamplerEngine.h"
#include "SincTable.h"

namespace kaia
{

namespace
{
    constexpr float kChokeFadeSec = 0.005f;
    constexpr float kMonoFadeSec  = 0.003f;
    constexpr float kStealFadeSec = 0.002f;
    constexpr float kPanicFadeSec = 0.005f;
    constexpr float kMinReleaseSec = 0.002f;
    constexpr double kPi = 3.14159265358979323846;
}

//==============================================================================
float SamplerEngine::Voice::nextEnvelope() noexcept
{
    switch (stage)
    {
        case Attack:
            level += attackInc;
            if (level >= 1.0f)
            {
                level = 1.0f;
                if (holdLeft > 0)
                    stage = Hold;
                else
                {
                    stage = afterHold;
                    t = 0.0f;
                }
            }
            return level;

        case Hold:
            if (--holdLeft <= 0)
            {
                stage = afterHold;
                t = 0.0f;
            }
            return 1.0f;

        case Decay:
        {
            t += decayInc;
            if (t >= 1.0f)
            {
                level = sustain;
                stage = (adMode || sustain <= 0.0f) ? Done : Sustain;
                return level;
            }
            auto u = 1.0f - t;
            level = sustain + (1.0f - sustain) * u * u;
            return level;
        }

        case Sustain:
            return level;

        case Release:
        {
            t += releaseInc;
            if (t >= 1.0f)
            {
                level = 0.0f;
                stage = Done;
                return 0.0f;
            }
            auto u = 1.0f - t;
            level = relStart * u * u;
            return level;
        }

        case Done:
        default:
            return 0.0f;
    }
}

void SamplerEngine::Voice::release (double) noexcept
{
    if (stage == Done || stage == Release)
        return;
    relStart = level;
    t = 0.0f;
    stage = Release;
}

void SamplerEngine::Voice::kill (float seconds, double sr) noexcept
{
    auto step = 1.0f / juce::jmax (1.0f, seconds * (float) sr);
    if (killing)
    {
        killStep = juce::jmax (killStep, step);
        return;
    }
    killing = true;
    killGain = 1.0f;
    killStep = step;
}

float SamplerEngine::Voice::filterSample (float x, int ch) noexcept
{
    for (int s = 0; s < filterStages; ++s)
    {
        auto& c1 = ic1[s][ch];
        auto& c2 = ic2[s][ch];
        auto v3 = x - c2;
        auto v1 = fa1[s] * c1 + fa2[s] * v3;
        auto v2 = c2 + fa2[s] * c1 + fa3[s] * v3;
        c1 = 2.0f * v1 - c1;
        c2 = 2.0f * v2 - c2;

        switch (filterMode)
        {
            case 1:  x = v2; break;                              // low-pass
            case 2:  x = x - fk[s] * v1 - v2; break;             // high-pass
            case 3:  x = fk[s] * v1; break;                      // band-pass (unity peak)
            default: x = x - fk[s] * v1; break;                  // notch
        }
    }
    return x;
}

//==============================================================================
SamplerEngine::SamplerEngine()
{
    lastRandomLayer.fill (-1);
    lastVelocity.fill (100);
    SincTable::get();   // build the tables now, not on the audio thread
}

SamplerEngine::~SamplerEngine()
{
    delete pending.exchange (nullptr);
    delete current;
    for (auto*& r : retired)
    {
        delete r;
        r = nullptr;
    }
    collectGarbage();
}

void SamplerEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0 ? newSampleRate : 48000.0;
    for (auto& v : voices)
        v.active = false;
}

void SamplerEngine::setRuntime (std::unique_ptr<KitRuntime> runtime)
{
    // The audio thread only ever takes from `pending`, so whatever we replace here was never used.
    delete pending.exchange (runtime.release());
}

void SamplerEngine::collectGarbage()
{
    int s1, n1, s2, n2;
    garbageFifo.prepareToRead (garbageFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) { delete garbage[(size_t) (s1 + i)]; garbage[(size_t) (s1 + i)] = nullptr; }
    for (int i = 0; i < n2; ++i) { delete garbage[(size_t) (s2 + i)]; garbage[(size_t) (s2 + i)] = nullptr; }
    garbageFifo.finishedRead (n1 + n2);
}

void SamplerEngine::pushUiEvent (const UiEvent& e)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        uiEvents[(size_t) s1] = e;
    uiFifo.finishedWrite (n1);
}

void SamplerEngine::auditionNoteOn (int note, int velocity)
{
    pushUiEvent ({ uiNoteOn, (juce::uint8) juce::jlimit (0, 127, note), (juce::uint8) juce::jlimit (1, 127, velocity), 0 });
}

void SamplerEngine::auditionNoteOff (int note)
{
    pushUiEvent ({ uiNoteOff, (juce::uint8) juce::jlimit (0, 127, note), 0, 0 });
}

void SamplerEngine::startPreview (juce::uint32 previewId)
{
    pushUiEvent ({ uiPreviewStart, 0, 0, previewId });
}

void SamplerEngine::stopPreview()
{
    pushUiEvent ({ uiPreviewStop, 0, 0, 0 });
}

//==============================================================================
void SamplerEngine::swapRuntimeIfPending()
{
    if (auto* next = pending.exchange (nullptr))
    {
        if (current != nullptr)
            retire (current);
        current = next;
    }
}

void SamplerEngine::retire (KitRuntime* rt)
{
    for (auto*& slot : retired)
    {
        if (slot == nullptr)
        {
            slot = rt;
            return;
        }
    }

    // All retirement slots busy (many edits while long sounds ring): stop the voices of the
    // oldest snapshot so it can be handed back, then reuse its slot.
    auto* oldest = retired[0];
    for (auto& v : voices)
        if (v.active && v.owner == oldest)
            v.active = false;

    int s1, n1, s2, n2;
    garbageFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
    {
        garbage[(size_t) s1] = oldest;
        garbageFifo.finishedWrite (1);
    }
    // else: leak rather than free on the audio thread (practically unreachable)

    for (size_t i = 0; i + 1 < retired.size(); ++i)
        retired[i] = retired[i + 1];
    retired.back() = rt;
}

void SamplerEngine::sweepRetired()
{
    for (auto*& r : retired)
    {
        if (r == nullptr)
            continue;

        bool inUse = false;
        for (auto& v : voices)
            if (v.active && v.owner == r)
            {
                inUse = true;
                break;
            }

        if (inUse)
            continue;

        int s1, n1, s2, n2;
        garbageFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0)
        {
            garbage[(size_t) s1] = r;
            garbageFifo.finishedWrite (1);
            r = nullptr;
        }
    }
}

juce::uint32 SamplerEngine::nextRandom() noexcept
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

void SamplerEngine::applyPitchBend() noexcept
{
    auto factor = std::pow (2.0, pitchBendSemis / 12.0);
    for (auto& v : voices)
        if (v.active && v.keygroup)
            v.inc = v.baseInc * factor;
}

//==============================================================================
void SamplerEngine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        auto note = m.getNoteNumber();
        lastInputNote.store ((++inputNoteSequence << 8) | (juce::uint32) (note & 0x7f), std::memory_order_relaxed);
        noteOn (note, m.getVelocity(), true);
    }
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber(), true);
    else if (m.isPitchWheel())
    {
        auto range = current != nullptr ? current->pitchBendRange : 2.0f;
        pitchBendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * range;
        applyPitchBend();
    }
    else if (m.isAllSoundOff())
    {
        for (auto& v : voices)
            if (v.active)
                v.kill (kPanicFadeSec, sampleRate);
    }
    else if (m.isAllNotesOff() || m.isResetAllControllers())
    {
        if (m.isResetAllControllers())
        {
            pitchBendSemis = 0.0f;
            applyPitchBend();
        }
        if (m.isAllNotesOff())
            for (auto& v : voices)
                if (v.active && v.trigger != TriggerMode::OneShot)
                    v.release (sampleRate);
    }
}

void SamplerEngine::noteOn (int note, int velocity, bool fromInput)
{
    if (note < 0 || note > 127)
        return;
    lastVelocity[(size_t) note] = (juce::uint8) velocity;

    if (current == nullptr)
        return;

    // MIDI input goes through the trigger map; the voice carries the pad's program note so mute
    // targets keep working, and remembers the input note for its note-off.
    const auto& pads = fromInput ? current->padsForInput[(size_t) note] : current->padsForNote[(size_t) note];
    for (auto padIndex : pads)
    {
        auto& pad = current->pads[(size_t) padIndex];
        if (pad.trigger == TriggerMode::NoteOff)
            continue;
        auto programNote = padIndex < kMaxPads && current->padNote[(size_t) padIndex] >= 0 ? current->padNote[(size_t) padIndex] : note;
        triggerPad (padIndex, programNote, note, velocity, 0);
    }
}

void SamplerEngine::noteOff (int note, bool fromInput)
{
    if (note < 0 || note > 127)
        return;

    for (auto& v : voices)
    {
        if (v.active && v.inputNote == note && v.noteHeld)
        {
            v.noteHeld = false;
            if (v.trigger == TriggerMode::NoteOn)
                v.release (sampleRate);
        }
    }

    if (current == nullptr)
        return;

    const auto& pads = fromInput ? current->padsForInput[(size_t) note] : current->padsForNote[(size_t) note];
    for (auto padIndex : pads)
        if (current->pads[(size_t) padIndex].trigger == TriggerMode::NoteOff)
        {
            auto programNote = padIndex < kMaxPads && current->padNote[(size_t) padIndex] >= 0 ? current->padNote[(size_t) padIndex] : note;
            triggerPad (padIndex, programNote, note, lastVelocity[(size_t) note], 0);
        }
}

SamplerEngine::Voice* SamplerEngine::allocateVoice()
{
    for (auto& v : voices)
        if (! v.active)
            return &v;

    // Steal: prefer voices already fading out, then released ones, then the oldest
    Voice* best = nullptr;
    int bestRank = -1;
    for (auto& v : voices)
    {
        int rank = v.killing ? 3 : (v.stage == Voice::Release ? 2 : 1);
        if (best == nullptr || rank > bestRank || (rank == bestRank && v.serial < best->serial))
        {
            best = &v;
            bestRank = rank;
        }
    }
    return best;
}

void SamplerEngine::triggerPad (int padIndex, int note, int inputNote, int velocity, int depth)
{
    if (current == nullptr || padIndex < 0 || padIndex >= (int) current->pads.size())
        return;

    auto& pad = current->pads[(size_t) padIndex];
    if (padIndex < kMaxPads)
    {
        padHits[(size_t) padIndex].fetch_add (1, std::memory_order_relaxed);
        lastHitPad.store ((juce::uint32) padIndex, std::memory_order_relaxed);
    }

    if (! pad.audible || pad.numLayers == 0)
        return;

    // Mute groups and mute targets cut off other pads (hi-hat choke)
    for (auto& v : voices)
    {
        if (! v.active || v.killing)
            continue;
        bool sameVoicePad = v.padIndex == padIndex && v.owner == current;
        if (! sameVoicePad && pad.muteGroup > 0 && v.muteGroup == pad.muteGroup)
            v.kill (kChokeFadeSec, sampleRate);
        else if (! sameVoicePad)
            for (auto t : pad.muteTargets)
                if (t >= 0 && v.note == t)
                    v.kill (kChokeFadeSec, sampleRate);
    }

    // Mono pads cut their previous hit; polyphony limit cuts the oldest hits
    if (pad.mono)
    {
        for (auto& v : voices)
            if (v.active && ! v.killing && v.padIndex == padIndex)
                v.kill (kMonoFadeSec, sampleRate);
    }
    else if (pad.polyphony > 0)
    {
        // Count distinct hits by serial ranges: each hit can start several layer voices.
        for (;;)
        {
            int count = 0;
            Voice* oldest = nullptr;
            for (auto& v : voices)
            {
                if (v.active && ! v.killing && v.padIndex == padIndex)
                {
                    ++count;
                    if (oldest == nullptr || v.serial < oldest->serial)
                        oldest = &v;
                }
            }
            if (count < pad.polyphony || oldest == nullptr)
                break;
            oldest->kill (kMonoFadeSec, sampleRate);
        }
    }

    // Pick layers
    int candidates[kMaxLayers];
    int numCandidates = 0;
    for (int i = 0; i < pad.numLayers; ++i)
    {
        auto& l = pad.layers[(size_t) i];
        if (velocity >= l.velLow && velocity <= l.velHigh)
            candidates[numCandidates++] = i;
    }

    if (pad.layerMode != LayerMode::Velocity && numCandidates == 0)
        for (int i = 0; i < pad.numLayers; ++i)
            candidates[numCandidates++] = i;

    if (numCandidates == 0)
        return;

    auto slot = (size_t) juce::jlimit (0, kMaxPads - 1, padIndex);
    if (pad.layerMode == LayerMode::Cycle && numCandidates > 1)
    {
        auto choice = candidates[roundRobin[slot]++ % (juce::uint32) numCandidates];
        candidates[0] = choice;
        numCandidates = 1;
    }
    else if (pad.layerMode == LayerMode::Random && numCandidates > 1)
    {
        int choice;
        int attempts = 0;
        do
        {
            choice = candidates[nextRandom() % (juce::uint32) numCandidates];
        } while (numCandidates > 2 && choice == lastRandomLayer[slot] && ++attempts < 8);
        lastRandomLayer[slot] = choice;
        candidates[0] = choice;
        numCandidates = 1;
    }

    for (int c = 0; c < numCandidates; ++c)
    {
        auto* v = allocateVoice();
        if (v == nullptr)
            break;
        startVoice (*v, pad, pad.layers[(size_t) candidates[c]], padIndex, note, inputNote, velocity);
    }

    // "Simult" pads play along
    if (depth == 0)
        for (auto t : pad.simultTargets)
            if (t >= 0 && t < 128 && t != note)
                for (auto other : current->padsForNote[(size_t) t])
                    if (other != padIndex)
                        triggerPad (other, t, t, velocity, 1);
}

void SamplerEngine::playPreview()
{
    // Previews are mono: a new one cuts the previous one
    for (auto& v : voices)
        if (v.active && v.padIndex == kPreviewPad)
            v.kill (kMonoFadeSec, sampleRate);

    auto& pad = current->preview;
    if (pad.numLayers == 0)
        return;

    constexpr int velocity = 110;
    int layer = 0;
    for (int i = 0; i < pad.numLayers; ++i)
        if (velocity >= pad.layers[(size_t) i].velLow && velocity <= pad.layers[(size_t) i].velHigh)
        {
            layer = i;
            break;
        }

    if (auto* v = allocateVoice())
        startVoice (*v, pad, pad.layers[(size_t) layer], kPreviewPad, -1, -1, velocity);
}

void SamplerEngine::startVoice (Voice& v, const PadRT& pad, const LayerRT& layer, int padIndex, int note, int inputNote, int velocity)
{
    auto* sd = layer.sample;
    auto sr = sampleRate;

    v = Voice();
    v.active = true;
    v.owner = current;
    v.sample = sd;
    v.srcL = sd->channel (0);
    v.srcR = sd->numChannels > 1 ? sd->channel (1) : v.srcL;
    v.stereoSource = sd->numChannels > 1;
    v.padIndex = padIndex;
    v.note = note;
    v.inputNote = inputNote;
    v.muteGroup = pad.muteGroup;
    v.output = pad.output;
    v.trigger = pad.trigger;
    v.keygroup = pad.keygroup;
    v.noteHeld = pad.trigger != TriggerMode::NoteOff;
    v.serial = ++voiceSerial;

    // Pitch
    double semis = current->tuneSemis + pad.tuneSemis + layer.tuneSemis;
    if (pad.keygroup && pad.keyTrack)
        semis += note - layer.rootNote;
    v.baseInc = (sd->sampleRate / sr) * std::pow (2.0, semis / 12.0);
    v.inc = v.baseInc * (pad.keygroup ? std::pow (2.0, pitchBendSemis / 12.0) : 1.0);

    // Region
    v.startPos = (double) layer.start;
    v.endPos = (double) layer.end;
    v.reverse = layer.reverse;
    v.loop = layer.loop && ! layer.reverse;
    v.loopStart = (double) layer.loopStart;
    v.loopEnd = (double) layer.loopEnd;
    v.pos = v.reverse ? v.endPos - 1.0 : v.startPos;

    // Level and pan
    auto velGain = 1.0f - pad.velToLevel * (1.0f - (float) velocity / 127.0f);
    auto gain = current->gain * pad.gain * layer.gain * velGain;
    auto pan = juce::jlimit (-1.0f, 1.0f, pad.pan + layer.pan);
    v.gainL = gain * (pan <= 0.0f ? 1.0f : (float) std::cos (pan * kPi * 0.5));
    v.gainR = gain * (pan >= 0.0f ? 1.0f : (float) std::cos (-pan * kPi * 0.5));

    // Amp envelope
    v.adMode = pad.envAD;
    v.stage = Voice::Attack;
    v.level = 0.0f;
    auto attackSamples = pad.attackSec * (float) sr;
    v.attackInc = attackSamples >= 1.0f ? 1.0f / attackSamples : 1.0f;
    v.holdLeft = (int) (pad.holdSec * sr);
    if (pad.envAD)
    {
        v.afterHold = pad.decayFromEnd ? Voice::Sustain : Voice::Decay;
        v.sustain = pad.decayFromEnd ? 1.0f : 0.0f;
    }
    else
    {
        v.afterHold = Voice::Decay;
        v.sustain = pad.sustain;
    }
    v.decayInc = 1.0f / juce::jmax (1.0f, pad.decaySec * (float) sr);
    v.releaseInc = 1.0f / juce::jmax (1.0f, juce::jmax (kMinReleaseSec, pad.releaseSec) * (float) sr);

    v.fadeAtEnd = pad.envAD && pad.decayFromEnd && ! v.loop;
    v.fadeLen = juce::jmax (1.0, (double) pad.decaySec * sr * v.inc);

    // Filter
    v.filterMode = 0;
    v.filterStages = 0;
    if (pad.filter != FilterType::Off)
    {
        double fc = pad.cutoffHz;
        if (pad.velToCutoff > 0.0f)
            fc *= std::pow (2.0, -pad.velToCutoff * 5.0 * (1.0 - velocity / 127.0));
        auto nyquistLimit = 0.45 * sr;
        fc = juce::jlimit (20.0, nyquistLimit, fc);

        int mode = 0, stages = 1;
        switch (pad.filter)
        {
            case FilterType::LP12:  mode = 1; break;
            case FilterType::LP24:  mode = 1; stages = 2; break;
            case FilterType::HP12:  mode = 2; break;
            case FilterType::HP24:  mode = 2; stages = 2; break;
            case FilterType::BP12:  mode = 3; break;
            case FilterType::BP24:  mode = 3; stages = 2; break;
            case FilterType::Notch: mode = 4; break;
            case FilterType::Off:   break;
        }

        bool transparent = (mode == 1 && fc >= nyquistLimit * 0.98 && pad.resonance < 0.02f)
                        || (mode == 2 && fc <= 20.5 && pad.resonance < 0.02f);
        if (mode != 0 && ! transparent)
        {
            v.filterMode = mode;
            v.filterStages = stages;
            auto g = std::tan (kPi * fc / sr);
            auto r = (double) juce::jlimit (0.0f, 1.0f, pad.resonance);
            for (int s = 0; s < stages; ++s)
            {
                double q = stages == 1 ? 0.7071 : (s == 0 ? 0.5412 : 1.3066);
                if (s == stages - 1)
                    q += r * r * 11.0;
                auto k = 1.0 / q;
                auto a1 = 1.0 / (1.0 + g * (g + k));
                v.fk[s]  = (float) k;
                v.fa1[s] = (float) a1;
                v.fa2[s] = (float) (g * a1);
                v.fa3[s] = (float) (g * g * a1);
            }
        }
    }
}

//==============================================================================
void SamplerEngine::renderVoice (Voice& v, const OutputBus* outputs, int start, int numSamples) noexcept
{
    auto& bus = outputs[juce::jlimit (0, kNumOutputs - 1, v.output)];
    float* outL = bus.left + start;
    float* outR = bus.right + start;

    const auto& table = SincTable::get();
    const int band = table.bandFor (v.inc);
    const bool directRead = juce::exactlyEqual (v.inc, 1.0) && juce::exactlyEqual (v.pos, std::floor (v.pos));
    const bool stereo = v.stereoSource;
    const float* srcL = v.srcL;
    const float* srcR = v.srcR;
    const double step = v.reverse ? -v.inc : v.inc;

    for (int s = 0; s < numSamples; ++s)
    {
        // Bounds and looping
        if (v.reverse)
        {
            if (v.pos < v.startPos)
            {
                v.active = false;
                return;
            }
        }
        else
        {
            if (v.loop && v.pos >= v.loopEnd)
                v.pos = v.loopStart + std::fmod (v.pos - v.loopEnd, v.loopEnd - v.loopStart);
            if (v.pos >= v.endPos)
            {
                v.active = false;
                return;
            }
        }

        // Read
        float l, r;
        if (directRead)
        {
            auto i = (juce::int64) v.pos;
            l = srcL[i];
            r = stereo ? srcR[i] : l;
        }
        else
        {
            auto i = (juce::int64) std::floor (v.pos);
            auto frac = (float) (v.pos - (double) i);
            auto phase = frac * (float) SincTable::kPhases;
            auto p = juce::jmin ((int) phase, SincTable::kPhases - 1);
            auto pf = phase - (float) p;
            const float* c0 = table.coeffs[band][p];
            const float* c1 = table.coeffs[band][p + 1];
            const float* xl = srcL + (i - (SincTable::kHalf - 1));

            float coef[SincTable::kTaps];
            for (int t = 0; t < SincTable::kTaps; ++t)
                coef[t] = c0[t] + pf * (c1[t] - c0[t]);

            float accL = 0.0f;
            for (int t = 0; t < SincTable::kTaps; ++t)
                accL += xl[t] * coef[t];
            l = accL;

            if (stereo)
            {
                const float* xr = srcR + (i - (SincTable::kHalf - 1));
                float accR = 0.0f;
                for (int t = 0; t < SincTable::kTaps; ++t)
                    accR += xr[t] * coef[t];
                r = accR;
            }
            else
            {
                r = l;
            }
        }

        if (v.filterMode != 0)
        {
            l = v.filterSample (l, 0);
            r = stereo ? v.filterSample (r, 1) : l;
        }

        // Envelope, tail fade and choke fade
        auto g = v.nextEnvelope();
        if (v.stage == Voice::Done)
        {
            v.active = false;
            return;
        }

        if (v.fadeAtEnd)
        {
            auto dist = v.reverse ? (v.pos - v.startPos) : (v.endPos - v.pos);
            if (dist < v.fadeLen)
                g *= (float) juce::jmax (0.0, dist / v.fadeLen);
        }

        if (v.killing)
        {
            v.killGain -= v.killStep;
            if (v.killGain <= 0.0f)
            {
                v.active = false;
                return;
            }
            g *= v.killGain;
        }

        outL[s] += l * g * v.gainL;
        outR[s] += r * g * v.gainR;
        v.pos += step;
    }
}

void SamplerEngine::process (const OutputBus* outputs, int numSamples, const juce::MidiBuffer& midi)
{
    swapRuntimeIfPending();
    sweepRetired();

    if (panicRequested.exchange (false))
        for (auto& v : voices)
            if (v.active)
                v.kill (kPanicFadeSec, sampleRate);

    // Pad clicks from the UI
    {
        int s1, n1, s2, n2;
        uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
        auto handle = [this] (const UiEvent& e)
        {
            switch (e.type)
            {
                case uiNoteOn:       noteOn (e.note, e.velocity, false); break;
                case uiNoteOff:      noteOff (e.note, false); break;
                case uiPreviewStart: pendingPreview = e.id; break;
                case uiPreviewStop:
                    pendingPreview = 0;
                    for (auto& v : voices)
                        if (v.active && v.padIndex == kPreviewPad)
                            v.kill (kMonoFadeSec, sampleRate);
                    break;
                default: break;
            }
        };
        for (int i = 0; i < n1; ++i) handle (uiEvents[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) handle (uiEvents[(size_t) (s2 + i)]);
        uiFifo.finishedRead (n1 + n2);
    }

    // A preview starts once the snapshot that holds its sound is active (ids only increase)
    if (pendingPreview != 0 && current != nullptr && current->previewId >= pendingPreview)
    {
        if (current->previewId == pendingPreview)
            playPreview();
        pendingPreview = 0;
    }

    auto renderAll = [&] (int start, int length)
    {
        if (length <= 0)
            return;
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, outputs, start, length);
    };

    int position = 0;
    for (const auto metadata : midi)
    {
        auto when = juce::jlimit (0, numSamples, metadata.samplePosition);
        renderAll (position, when - position);
        position = juce::jmax (position, when);
        handleMidi (metadata.getMessage());
    }
    renderAll (position, numSamples - position);

    // Feedback for the UI
    int perPad[kMaxPads] = {};
    int active = 0;
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        ++active;
        if (v.padIndex >= 0 && v.padIndex < kMaxPads && ! v.killing)
            ++perPad[v.padIndex];
    }
    for (int i = 0; i < kMaxPads; ++i)
        if (padVoices[(size_t) i].load (std::memory_order_relaxed) != perPad[i])
            padVoices[(size_t) i].store (perPad[i], std::memory_order_relaxed);
    activeVoiceCount.store (active, std::memory_order_relaxed);
}

}
