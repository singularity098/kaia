#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/SamplerEngine.h"
#include "Formats/KitLoader.h"
#include <deque>

namespace kaia
{

class KaiaProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    KaiaProcessor();
    ~KaiaProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "Kaia"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor
    const Kit& getKit() const noexcept                  { return kit; }
    SampleDataPtr getSample (const SampleRef& ref) const;
    SamplerEngine& getEngine() noexcept                 { return engine; }
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    void openPack (const juce::File& fileOrFolder);           // .xpn, folder, or a program file
    void loadProgram (const ProgramLocation& location);
    void loadProgramAt (int index);                           // index into getPrograms()
    void stepProgram (int delta);
    void loadAudioFiles (const juce::Array<juce::File>& files);
    enum class AssignMode { Replace, Append, ReplaceLayer };
    // Replace: one file replaces layer 1, several files become round-robin layers.
    void assignSamplesToPad (int padIndex, const juce::Array<juce::File>& files,
                             AssignMode mode = AssignMode::Replace, int layerIndex = 0);
    void routeOutputs (int scheme);   // 0 all main, 1 pad number in bank -> aux, 2 loaded pads in order -> aux
    void relinkMissingSamples (const juce::File& folder);
    void newEmptyKit();

    void editPad (int padIndex, const std::function<void (Pad&)>& edit);
    void editKit (const std::function<void (Kit&)>& edit);

    const std::vector<ProgramEntry>& getPrograms() const noexcept { return programs; }
    int getCurrentProgramIndex() const noexcept         { return currentProgramIndex; }
    const PackInfo& getPackInfo() const noexcept        { return packInfo; }
    const juce::File& getPackFile() const noexcept      { return packFile; }

    bool isLoading() const noexcept                     { return loadingJobs.load() > 0; }
    float getLoadProgress() const noexcept              { return loadProgress.load(); }
    const juce::String& getStatusMessage() const noexcept { return statusMessage; }
    const juce::StringArray& getWarnings() const noexcept { return warnings; }
    bool lastLoadFailed() const noexcept                { return statusIsError; }

    int selectedPad = 0;          // UI state, saved with the project
    int selectedBank = 0;

private:
    class LoaderThread;

    void timerCallback() override;
    void rebuildRuntime();
    void applyLoadResult (LoadResult result, bool keepPrograms);
    void setStatus (const juce::String& message, bool isError);
    void enqueueJob (std::function<void()> job);
    void postToMessageThread (std::function<void()> fn);
    std::function<bool (float)> progressCallback (juce::uint32 generation);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    static BusesProperties makeBuses();

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* masterParam = nullptr;
    juce::SmoothedValue<float> masterGain;

    SamplerEngine engine;

    Kit kit;
    SampleMap samples;

    juce::File packFile;
    PackInfo packInfo;
    std::vector<ProgramEntry> programs;
    int currentProgramIndex = -1;

    juce::String statusMessage;
    juce::StringArray warnings;
    bool statusIsError = false;

    std::unique_ptr<LoaderThread> loader;
    std::atomic<int> loadingJobs { 0 };
    std::atomic<float> loadProgress { 0.0f };
    std::atomic<juce::uint32> loadGeneration { 0 };
    std::shared_ptr<std::atomic<bool>> alive;

    juce::CriticalSection stateLock;
    juce::String cachedKitXml;     // for hosts that save state off the message thread
    bool stateDirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaiaProcessor)
};

}
