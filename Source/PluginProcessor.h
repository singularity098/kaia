#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/SamplerEngine.h"
#include "Formats/KitLoader.h"
#include "Formats/Library.h"
#include "Model/TriggerMap.h"
#include "Model/UserFiles.h"
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

    // .xpn, folder, program file or preset. `select`: program of the pack to load (default: the first).
    void openPack (const juce::File& fileOrFolder, const ProgramLocation& select = {});
    void loadProgram (const ProgramLocation& location);
    void loadProgramAt (int index);                           // index into getPrograms()
    void stepProgram (int delta);
    void loadAudioFiles (const juce::Array<juce::File>& files);
    enum class AssignMode { Replace, Append, ReplaceLayer };
    // Replace: one file replaces layer 1, several files become round-robin layers.
    void assignSamplesToPad (int padIndex, const juce::Array<juce::File>& files,
                             AssignMode mode = AssignMode::Replace, int layerIndex = 0);
    void assignSamplesToPad (int padIndex, const std::vector<SampleRef>& refs,     // files or archive entries
                             AssignMode mode = AssignMode::Replace, int layerIndex = 0);
    // Copies a pad of another program (layers and settings) onto `padIndex`, keeping its note.
    void importPad (int padIndex, const Pad& source);
    void routeOutputs (int scheme);   // 0 all main, 1 pad number in bank -> aux, 2 loaded pads in order -> aux
    void relinkMissingSamples (const juce::File& folder);
    void newEmptyKit();

    void editPad (int padIndex, const std::function<void (Pad&)>& edit);
    void editKit (const std::function<void (Kit&)>& edit);

    // Library auditions: played on Main, never added to the kit
    void previewSample (const SampleRef& ref);
    void previewPad (const Pad& pad);
    void stopPreview();

    // Presets live in the config folder; library files are never written
    bool savePreset (const juce::String& name, juce::String& error);
    void loadPreset (const juce::File& presetFile);

    // How MIDI notes reach the pads (drum module map + learned notes). Also becomes the default
    // for new instances.
    const TriggerMap& getTriggerMap() const noexcept    { return triggers; }
    void setTriggerMap (const TriggerMap& map);
    std::vector<int> getInputNotesForPad (int padIndex) const;   // MIDI notes that play this pad

    // MIDI Learn: the next note-on from the host is assigned to the pad
    void startMidiLearn (int padIndex);
    void cancelMidiLearn();
    int getMidiLearnPad() const noexcept                { return learnPad; }
    int getLastInputNote() const noexcept               { return (int) (engine.getLastInputNote() & 0x7f); }
    bool hasReceivedInputNote() const noexcept          { return engine.getLastInputNote() != 0; }

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
    void pollMidiLearn();
    static TriggerMap defaultTriggerMap();
    void saveTriggerMapAsDefault();
    void setPreview (std::unique_ptr<PreviewSound> sound);
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
    TriggerMap triggers;
    std::array<std::vector<int>, 128> inputTable;   // triggers resolved against the kit (for the UI)

    std::unique_ptr<PreviewSound> preview;
    juce::uint32 previewCounter = 0;
    std::atomic<juce::uint32> previewGeneration { 0 };

    int learnPad = -1;
    juce::uint32 learnSequence = 0;

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
    juce::SharedResourcePointer<SharedSettings> settings;
    juce::ThreadPool previewPool { juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("Kaia preview") };

    juce::CriticalSection stateLock;
    juce::String cachedKitXml;     // for hosts that save state off the message thread
    juce::String cachedTriggerXml;
    bool stateDirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaiaProcessor)
};

}
