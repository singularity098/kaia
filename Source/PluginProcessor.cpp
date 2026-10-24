#include "PluginProcessor.h"
#include "UI/PluginEditor.h"

namespace kaia
{

//==============================================================================
class KaiaProcessor::LoaderThread : public juce::Thread
{
public:
    LoaderThread() : juce::Thread ("Kaia loader") {}

    ~LoaderThread() override
    {
        signalThreadShouldExit();
        wakeUp.signal();
        stopThread (15000);
    }

    void add (std::function<void()> job)
    {
        {
            const juce::ScopedLock sl (lock);
            jobs.push_back (std::move (job));
        }
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::normal);
        wakeUp.signal();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::function<void()> job;
            {
                const juce::ScopedLock sl (lock);
                if (! jobs.empty())
                {
                    job = std::move (jobs.front());
                    jobs.pop_front();
                }
            }

            if (job)
                job();
            else
                wakeUp.wait (500);
        }
    }

private:
    juce::CriticalSection lock;
    std::deque<std::function<void()>> jobs;
    juce::WaitableEvent wakeUp;
};

//==============================================================================
juce::AudioProcessor::BusesProperties KaiaProcessor::makeBuses()
{
    // Aux outputs are off by default in VST3/standalone (hosts can enable them) and on in LV2/CLAP,
    // which can't switch outputs on later. Pads always play through Main unless routed elsewhere.
    auto wrapper = juce::PluginHostType::getPluginLoadedAs();
    bool auxOn = wrapper == juce::AudioProcessor::wrapperType_LV2
              || wrapper == juce::AudioProcessor::wrapperType_Unity
              || juce::String (juce::AudioProcessor::getWrapperTypeDescription (wrapper)).containsIgnoreCase ("clap");

    auto buses = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 1; i <= kNumAuxOutputs; ++i)
        buses = buses.withOutput ("Aux " + juce::String (i), juce::AudioChannelSet::stereo(), auxOn);
    return buses;
}

juce::AudioProcessorValueTreeState::ParameterLayout KaiaProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "master", 1 }, "Master Volume",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

KaiaProcessor::KaiaProcessor()
    : AudioProcessor (makeBuses()),
      parameters (*this, nullptr, "PARAMS", createParameters()),
      alive (std::make_shared<std::atomic<bool>> (true))
{
    masterParam = parameters.getRawParameterValue ("master");
    kit = Kit::makeEmpty();
    loader = std::make_unique<LoaderThread>();
    rebuildRuntime();
    setStatus ("Open an MPC program (.xpm, .pgm), expansion (.xpn) or a folder of samples.", false);
    startTimerHz (10);
}

KaiaProcessor::~KaiaProcessor()
{
    stopTimer();
    alive->store (false);
    ++loadGeneration;          // cancels any load in progress
    loader.reset();
    engine.collectGarbage();
}

//==============================================================================
void KaiaProcessor::prepareToPlay (double sampleRate, int)
{
    if (juce::SystemStats::getEnvironmentVariable ("KAIA_DEBUG", {}).isNotEmpty())
    {
        std::cerr << "[Kaia] prepareToPlay " << sampleRate << " Hz, " << getTotalNumOutputChannels() << " output channels, buses:";
        for (int i = 0; i < getBusCount (false); ++i)
            std::cerr << " " << getBus (false, i)->getNumberOfChannels() << (getBus (false, i)->isEnabled() ? "" : "(off)");
        std::cerr << std::endl;
    }
    engine.prepare (sampleRate);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue ((float) dbToGain (masterParam->load()));
}

void KaiaProcessor::releaseResources() {}

bool KaiaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty())
        for (auto& in : layouts.inputBuses)
            if (! in.isDisabled())
                return false;

    if (layouts.outputBuses.isEmpty() || layouts.outputBuses[0] != juce::AudioChannelSet::stereo())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
        if (! layouts.outputBuses[i].isDisabled() && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;

    return true;
}

void KaiaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    if (buffer.getNumChannels() < 2)
    {
        midi.clear();
        return;
    }

    OutputBus outs[SamplerEngine::kNumOutputs];
    auto mainBus = getBusBuffer (buffer, false, 0);
    outs[0] = { mainBus.getWritePointer (0), mainBus.getWritePointer (mainBus.getNumChannels() > 1 ? 1 : 0) };

    const auto busCount = getBusCount (false);
    for (int i = 1; i < SamplerEngine::kNumOutputs; ++i)
    {
        outs[i] = outs[0];
        if (i < busCount)
            if (auto* bus = getBus (false, i))
                if (bus->isEnabled() && bus->getNumberOfChannels() >= 2)
                {
                    auto b = getBusBuffer (buffer, false, i);
                    outs[i] = { b.getWritePointer (0), b.getWritePointer (1) };
                }
    }

    engine.process (outs, numSamples, midi);

    masterGain.setTargetValue ((float) dbToGain (masterParam->load()));
    if (masterGain.isSmoothing())
    {
        for (int s = 0; s < numSamples; ++s)
        {
            auto g = masterGain.getNextValue();
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.getWritePointer (c)[s] *= g;
        }
    }
    else if (std::abs (masterGain.getTargetValue() - 1.0f) > 1.0e-6f)
    {
        buffer.applyGain (masterGain.getTargetValue());
    }

    midi.clear();
}

//==============================================================================
juce::AudioProcessorEditor* KaiaProcessor::createEditor()
{
    return new KaiaEditor (*this);
}

SampleDataPtr KaiaProcessor::getSample (const SampleRef& ref) const
{
    auto it = samples.find (ref.toString());
    return it != samples.end() ? it->second : nullptr;
}

void KaiaProcessor::timerCallback()
{
    engine.collectGarbage();

    if (stateDirty)
    {
        stateDirty = false;
        auto xml = kit.toXml()->toString();
        const juce::ScopedLock sl (stateLock);
        cachedKitXml = xml;
    }
}

void KaiaProcessor::rebuildRuntime()
{
    engine.setRuntime (buildRuntime (kit, &samples));
    stateDirty = true;
    sendChangeMessage();
}

void KaiaProcessor::setStatus (const juce::String& message, bool isError)
{
    statusMessage = message;
    statusIsError = isError;
    sendChangeMessage();
}

void KaiaProcessor::postToMessageThread (std::function<void()> fn)
{
    juce::MessageManager::callAsync ([flag = alive, fn = std::move (fn)]
    {
        if (flag->load())
            fn();
    });
}

void KaiaProcessor::enqueueJob (std::function<void()> job)
{
    ++loadingJobs;
    loadProgress = 0.0f;
    sendChangeMessage();
    loader->add ([this, job = std::move (job)]
    {
        job();
        postToMessageThread ([this]
        {
            --loadingJobs;
            sendChangeMessage();
        });
    });
}

std::function<bool (float)> KaiaProcessor::progressCallback (juce::uint32 generation)
{
    return [this, generation] (float p)
    {
        loadProgress = p;
        return loadGeneration.load() == generation;
    };
}

void KaiaProcessor::applyLoadResult (LoadResult result, bool keepPrograms)
{
    if (! result.ok)
    {
        setStatus (result.error, true);
        return;
    }

    kit = std::move (result.kit);
    samples = std::move (result.samples);
    warnings = result.warnings;
    if (! keepPrograms)
    {
        programs.clear();
        currentProgramIndex = -1;
        packInfo = {};
        packFile = juce::File();
    }

    // Select something that makes a sound
    if (selectedPad < 0 || selectedPad >= (int) kit.pads.size() || ! kit.pads[(size_t) selectedPad].hasAnySample())
        for (int i = 0; i < (int) kit.pads.size(); ++i)
            if (kit.pads[(size_t) i].hasAnySample())
            {
                selectedPad = i;
                break;
            }
    selectedPad = juce::jlimit (0, juce::jmax (0, (int) kit.pads.size() - 1), selectedPad);
    selectedBank = selectedPad / kPadsPerBank;

    rebuildRuntime();

    auto msg = "Loaded \"" + kit.name + "\" (" + juce::String (kit.countSamples()) + " samples)";
    if (kit.countMissing() > 0)
        msg << ", " << kit.countMissing() << " missing";
    setStatus (msg, false);
}

//==============================================================================
void KaiaProcessor::openPack (const juce::File& target)
{
    if (target.existsAsFile() && isAudioFileName (target.getFileName()))
    {
        loadAudioFiles ({ target });
        return;
    }

    auto generation = ++loadGeneration;
    setStatus ("Opening " + target.getFileName() + "...", false);

    enqueueJob ([this, target, generation]
    {
        if (loadGeneration.load() != generation)
            return;

        PackInfo info;
        juce::String error;
        auto list = KitLoader::listPrograms (target, &info, error);

        // A folder with no programs: build a kit from its audio files
        if (list.empty() && target.isDirectory())
        {
            juce::Array<juce::File> audio;
            for (const auto& e : juce::RangedDirectoryIterator (target, false, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
                if (isAudioFileName (e.getFile().getFileName()))
                    audio.add (e.getFile());

            if (! audio.isEmpty())
            {
                LoadResult r;
                r.kit = KitLoader::kitFromAudioFiles (audio, target.getFileName());
                r.ok = KitLoader::loadSamples (r.kit, r.samples, r.warnings, progressCallback (generation));
                if (! r.ok)
                    r.error = "Loading cancelled.";
                postToMessageThread ([this, r = std::move (r), generation]() mutable
                {
                    if (loadGeneration.load() == generation)
                        applyLoadResult (std::move (r), false);
                });
                return;
            }
        }

        postToMessageThread ([this, target, info, list, error, generation]
        {
            if (loadGeneration.load() != generation)
                return;

            if (list.empty())
            {
                setStatus (error.isNotEmpty() ? error : "Nothing to load in " + target.getFileName(), true);
                return;
            }

            packFile = target;
            packInfo = info;
            programs = list;
            currentProgramIndex = -1;

            // Opening a program file: select it in the list. Otherwise load the first program.
            int toLoad = -1;
            for (int i = 0; i < (int) programs.size(); ++i)
            {
                if (target.existsAsFile() && ! programs[(size_t) i].location.inArchive()
                     && programs[(size_t) i].location.file == target)
                    toLoad = i;
            }
            if (toLoad < 0)
                for (int i = 0; i < (int) programs.size(); ++i)
                    if (! programs[(size_t) i].isArchive)
                    {
                        toLoad = i;
                        break;
                    }

            sendChangeMessage();
            if (toLoad >= 0)
                loadProgramAt (toLoad);
            else
                setStatus (juce::String ((int) programs.size()) + " expansions found. Pick one from the list.", false);
        });
    });
}

void KaiaProcessor::loadProgramAt (int index)
{
    if (index < 0 || index >= (int) programs.size())
        return;

    auto entry = programs[(size_t) index];
    if (entry.isArchive)
    {
        openPack (entry.location.file);
        return;
    }

    currentProgramIndex = index;
    loadProgram (entry.location);
}

void KaiaProcessor::stepProgram (int delta)
{
    if (programs.empty())
        return;

    auto n = (int) programs.size();
    auto i = currentProgramIndex < 0 ? (delta > 0 ? -1 : 0) : currentProgramIndex;
    for (int tries = 0; tries < n; ++tries)
    {
        i = (i + delta + n) % n;
        if (! programs[(size_t) i].isArchive)
        {
            loadProgramAt (i);
            return;
        }
    }
}

void KaiaProcessor::loadProgram (const ProgramLocation& location)
{
    auto generation = ++loadGeneration;
    setStatus ("Loading " + location.displayName() + "...", false);

    // Keep the list position in sync when a program is loaded directly
    for (int i = 0; i < (int) programs.size(); ++i)
        if (programs[(size_t) i].location.toString() == location.toString())
            currentProgramIndex = i;

    enqueueJob ([this, location, generation]
    {
        if (loadGeneration.load() != generation)
            return;
        auto result = KitLoader::loadProgram (location, progressCallback (generation));
        postToMessageThread ([this, result = std::move (result), generation]() mutable
        {
            if (loadGeneration.load() == generation)
                applyLoadResult (std::move (result), true);
        });
    });
}

void KaiaProcessor::loadAudioFiles (const juce::Array<juce::File>& files)
{
    for (auto& f : files)
    {
        auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".xpm" || ext == ".pgm" || ext == ".xpn" || f.isDirectory())
        {
            openPack (f);
            return;
        }
    }

    auto generation = ++loadGeneration;
    setStatus ("Loading samples...", false);
    enqueueJob ([this, files, generation]
    {
        LoadResult r;
        r.kit = KitLoader::kitFromAudioFiles (files, files.size() == 1 ? files[0].getFileNameWithoutExtension()
                                                                        : files[0].getParentDirectory().getFileName());
        r.ok = KitLoader::loadSamples (r.kit, r.samples, r.warnings, progressCallback (generation));
        if (! r.ok)
            r.error = "Loading cancelled.";
        postToMessageThread ([this, r = std::move (r), generation]() mutable
        {
            if (loadGeneration.load() == generation)
                applyLoadResult (std::move (r), false);
        });
    });
}

void KaiaProcessor::assignSamplesToPad (int padIndex, const juce::Array<juce::File>& files, AssignMode mode, int layerIndex)
{
    if (padIndex < 0 || padIndex >= (int) kit.pads.size() || files.isEmpty())
        return;

    enqueueJob ([this, padIndex, files, mode, layerIndex]
    {
        std::vector<std::pair<SampleRef, SampleDataPtr>> loaded;
        juce::StringArray errors;
        for (auto& f : files)
        {
            if (! isAudioFileName (f.getFileName()) || (int) loaded.size() >= kMaxLayers)
                continue;
            SampleRef ref { f.getFullPathName(), {} };
            juce::String error;
            if (auto data = loadSample (ref, error))
                loaded.push_back ({ ref, data });
            else
                errors.add (f.getFileName() + ": " + error);
        }

        postToMessageThread ([this, padIndex, loaded, errors, mode, layerIndex]
        {
            if (padIndex >= (int) kit.pads.size())
                return;
            if (loaded.empty())
            {
                setStatus (errors.isEmpty() ? "No audio files to load." : errors.joinIntoString ("; "), true);
                return;
            }

            auto& pad = kit.pads[(size_t) padIndex];
            auto makeLayer = [] (const SampleRef& ref, const Layer* base)
            {
                Layer l = base != nullptr ? *base : Layer();
                l.sample = ref;
                l.name = ref.displayName();
                l.missing = false;
                l.start = l.end = 0;
                l.loop = false;
                l.loopStart = l.loopEnd = 0;
                l.rootNote = -1;
                return l;
            };

            if (mode == AssignMode::Append)
            {
                for (auto& [ref, data] : loaded)
                    if ((int) pad.layers.size() < kMaxLayers)
                        pad.layers.push_back (makeLayer (ref, nullptr));
            }
            else if (mode == AssignMode::ReplaceLayer && layerIndex >= 0 && layerIndex < (int) pad.layers.size())
            {
                pad.layers[(size_t) layerIndex] = makeLayer (loaded[0].first, &pad.layers[(size_t) layerIndex]);
            }
            else if (loaded.size() == 1)
            {
                if (pad.layers.empty())
                    pad.layers.push_back (makeLayer (loaded[0].first, nullptr));
                else
                    pad.layers.front() = makeLayer (loaded[0].first, &pad.layers.front());
            }
            else
            {
                pad.layers.clear();
                for (auto& [ref, data] : loaded)
                    pad.layers.push_back (makeLayer (ref, nullptr));
                pad.layerMode = LayerMode::Cycle;
            }

            for (auto& [ref, data] : loaded)
                samples[ref.toString()] = data;

            if (! pad.isKeygroup() && pad.note < 0)
                pad.note = defaultPadNote (padIndex);

            rebuildRuntime();
            setStatus ("Loaded " + juce::String ((int) loaded.size()) + " sample" + (loaded.size() == 1 ? "" : "s")
                       + " on " + (pad.isKeygroup() ? "keygroup " + juce::String (padIndex + 1) : "pad " + padName (padIndex)), false);
        });
    });
}

void KaiaProcessor::routeOutputs (int scheme)
{
    int next = 1;
    for (int i = 0; i < (int) kit.pads.size(); ++i)
    {
        auto& pad = kit.pads[(size_t) i];
        switch (scheme)
        {
            case 1:  pad.output = (i % kPadsPerBank) + 1; break;
            case 2:  pad.output = pad.hasAnySample() && next <= kNumAuxOutputs ? next++ : 0; break;
            default: pad.output = 0; break;
        }
    }
    rebuildRuntime();
    setStatus (scheme == 0 ? "All pads play through the main output."
                           : "Pads routed to the aux outputs. Enable the plugin's extra outputs in your DAW.", false);
}

void KaiaProcessor::relinkMissingSamples (const juce::File& folder)
{
    auto copy = kit;
    auto generation = ++loadGeneration;
    setStatus ("Searching " + folder.getFileName() + " for missing samples...", false);
    enqueueJob ([this, copy, folder, generation]() mutable
    {
        auto fixed = KitLoader::relinkMissing (copy, folder);
        auto result = KitLoader::reloadKit (copy, progressCallback (generation));
        postToMessageThread ([this, result = std::move (result), fixed, generation]() mutable
        {
            if (loadGeneration.load() != generation)
                return;
            applyLoadResult (std::move (result), true);
            setStatus ("Found " + juce::String (fixed) + " missing sample" + (fixed == 1 ? "" : "s")
                       + (kit.countMissing() > 0 ? ", " + juce::String (kit.countMissing()) + " still missing" : juce::String()), false);
        });
    });
}

void KaiaProcessor::newEmptyKit()
{
    ++loadGeneration;
    kit = Kit::makeEmpty();
    samples.clear();
    warnings.clear();
    currentProgramIndex = -1;
    selectedPad = 0;
    selectedBank = 0;
    rebuildRuntime();
    setStatus ("New empty kit. Drop samples on the pads.", false);
}

void KaiaProcessor::editPad (int padIndex, const std::function<void (Pad&)>& edit)
{
    if (padIndex < 0 || padIndex >= (int) kit.pads.size())
        return;
    edit (kit.pads[(size_t) padIndex]);
    rebuildRuntime();
}

void KaiaProcessor::editKit (const std::function<void (Kit&)>& edit)
{
    edit (kit);
    rebuildRuntime();
}

//==============================================================================
void KaiaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("KaiaState");
    state.setAttribute ("version", 1);
    state.setAttribute ("pack", packFile.getFullPathName());
    state.setAttribute ("selectedPad", selectedPad);
    state.setAttribute ("selectedBank", selectedBank);

    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
         && juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        state.addChildElement (kit.toXml().release());
    }
    else
    {
        // Called from a host thread: use the copy the message thread keeps up to date
        juce::String xmlText;
        {
            const juce::ScopedLock sl (stateLock);
            xmlText = cachedKitXml;
        }
        if (auto kitXml = juce::parseXML (xmlText))
            state.addChildElement (kitXml.release());
    }

    if (auto params = parameters.copyState().createXml())
        state.addChildElement (params.release());

    copyXmlToBinary (state, destData);
}

void KaiaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("KaiaState"))
        return;

    if (auto* params = xml->getChildByName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*params));

    auto savedPad  = xml->getIntAttribute ("selectedPad", 0);
    auto savedBank = xml->getIntAttribute ("selectedBank", 0);
    auto pack      = xml->getStringAttribute ("pack");

    auto* kitXml = xml->getChildByName ("Kit");
    if (kitXml == nullptr)
        return;

    {
        const juce::ScopedLock sl (stateLock);
        cachedKitXml = kitXml->toString();
    }

    ++loadGeneration;

    // Load synchronously so offline renders right after a project load have their samples.
    auto result = KitLoader::reloadKit (Kit::fromXml (*kitXml));
    if (juce::SystemStats::getEnvironmentVariable ("KAIA_DEBUG", {}).isNotEmpty())
        std::cerr << "[Kaia] setState: kit " << result.kit.name << " samples=" << result.samples.size() << " counted=" << result.kit.countSamples() << " missing=" << result.kit.countMissing() << " warnings=" << result.warnings.joinIntoString ("; ") << std::endl;
    engine.setRuntime (buildRuntime (result.kit, &result.samples));

    auto apply = [this, result = std::move (result), savedPad, savedBank, pack]() mutable
    {
        kit = std::move (result.kit);
        samples = std::move (result.samples);
        warnings = result.warnings;
        selectedPad = juce::jlimit (0, juce::jmax (0, (int) kit.pads.size() - 1), savedPad);
        selectedBank = juce::jlimit (0, kNumBanks - 1, savedBank);
        sendChangeMessage();

        auto msg = "Restored \"" + kit.name + "\" (" + juce::String (kit.countSamples()) + " samples)";
        if (kit.countMissing() > 0)
            msg << ", " << kit.countMissing() << " missing: use Relink to find them";
        setStatus (msg, kit.countMissing() > 0);

        // Restore the program list of the pack in the background
        juce::File packTarget (pack);
        if (pack.isNotEmpty() && packTarget.exists())
        {
            auto source = kit.sourcePath;
            enqueueJob ([this, packTarget, source]
            {
                PackInfo info;
                juce::String error;
                auto list = KitLoader::listPrograms (packTarget, &info, error);
                postToMessageThread ([this, packTarget, info, list, source]
                {
                    packFile = packTarget;
                    packInfo = info;
                    programs = list;
                    currentProgramIndex = -1;
                    for (int i = 0; i < (int) programs.size(); ++i)
                        if (programs[(size_t) i].location.toString() == source)
                            currentProgramIndex = i;
                    sendChangeMessage();
                });
            });
        }
    };

    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
         && juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        postToMessageThread (std::move (apply));
}

}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kaia::KaiaProcessor();
}
