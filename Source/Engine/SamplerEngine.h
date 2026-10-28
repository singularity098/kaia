#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "KitRuntime.h"
#include <atomic>

namespace kaia
{

struct OutputBus
{
    float* left  = nullptr;
    float* right = nullptr;
};

// Real-time sampler voice engine.
// Audio thread: process(). Message thread: setRuntime(), collectGarbage(), audition*().
// The audio thread never allocates, frees or locks.
class SamplerEngine
{
public:
    static constexpr int kMaxVoices = 128;
    static constexpr int kNumOutputs = kNumAuxOutputs + 1;

    SamplerEngine();
    ~SamplerEngine();

    void prepare (double sampleRate);
    double getSampleRate() const noexcept { return sampleRate; }

    // Hands a new kit snapshot to the audio thread. Voices that are still ringing keep
    // using the previous snapshot until they finish.
    void setRuntime (std::unique_ptr<KitRuntime> runtime);

    // Frees snapshots the audio thread no longer uses. Call regularly from the message thread.
    void collectGarbage();

    // outputs: kNumOutputs stereo buses (aux buses may point at the main bus). Outputs are added to, not cleared.
    void process (const OutputBus* outputs, int numSamples, const juce::MidiBuffer& midi);

    // Pad audition from the UI (message thread, lock-free). Notes are program notes: they bypass the trigger map.
    void auditionNoteOn (int note, int velocity);
    void auditionNoteOff (int note);

    // Plays KitRuntime::preview once the snapshot with this id is active (message thread, lock-free).
    void startPreview (juce::uint32 previewId);
    void stopPreview();
    void allSoundOff() noexcept { panicRequested.store (true); }

    // Feedback for the UI
    int getActiveVoiceCount() const noexcept           { return activeVoiceCount.load (std::memory_order_relaxed); }
    juce::uint32 getPadHitCount (int pad) const noexcept { return pad >= 0 && pad < kMaxPads ? padHits[(size_t) pad].load (std::memory_order_relaxed) : 0; }
    int getPadVoiceCount (int pad) const noexcept      { return pad >= 0 && pad < kMaxPads ? padVoices[(size_t) pad].load (std::memory_order_relaxed) : 0; }
    juce::uint32 getLastHitPad() const noexcept        { return lastHitPad.load (std::memory_order_relaxed); }
    juce::uint32 getSoundingPreview() const noexcept   { return soundingPreview.load (std::memory_order_relaxed); }   // preview id, 0 if silent

    // Last MIDI note-on received from the host: (sequence << 8) | note, 0 if none yet. For MIDI Learn.
    juce::uint32 getLastInputNote() const noexcept     { return lastInputNote.load (std::memory_order_relaxed); }
    int getLastInputVelocity() const noexcept          { return lastInputVelocity.load (std::memory_order_relaxed); }

    static constexpr int kPreviewPad = -2;   // Voice::padIndex of library previews

private:
    struct Voice
    {
        enum Stage { Attack, Hold, Decay, Sustain, Release, Done };

        bool active = false;
        const KitRuntime* owner = nullptr;
        const SampleData* sample = nullptr;
        const float* srcL = nullptr;
        const float* srcR = nullptr;
        bool stereoSource = false;

        int padIndex = -1, note = -1, muteGroup = 0, output = 0;
        int inputNote = -1;     // the MIDI note that started the voice (its note-off releases it)
        TriggerMode trigger = TriggerMode::OneShot;
        bool keygroup = false;
        bool noteHeld = true;
        juce::uint64 serial = 0;

        double pos = 0.0, baseInc = 1.0, inc = 1.0;
        double startPos = 0.0, endPos = 0.0, loopStart = 0.0, loopEnd = 0.0;
        bool reverse = false, loop = false;
        float gainL = 1.0f, gainR = 1.0f;

        // Envelope
        Stage stage = Attack;
        bool adMode = true;
        float level = 0.0f, attackInc = 1.0f, decayInc = 1.0f, releaseInc = 1.0f;
        float sustain = 1.0f, relStart = 0.0f, t = 0.0f;
        int holdLeft = 0;
        Stage afterHold = Sustain;

        // Fade over the tail of the sample ("decay from end")
        bool fadeAtEnd = false;
        double fadeLen = 1.0;

        // Choke / steal fade
        bool killing = false;
        float killGain = 1.0f, killStep = 0.0f;

        // State variable filter, up to two cascaded stages
        int filterMode = 0;     // 0 off, 1 LP, 2 HP, 3 BP, 4 notch
        int filterStages = 0;
        float fa1[2] {}, fa2[2] {}, fa3[2] {}, fk[2] {};
        float ic1[2][2] {}, ic2[2][2] {};

        float nextEnvelope() noexcept;
        void release (double sampleRate) noexcept;
        void kill (float seconds, double sampleRate) noexcept;
        float filterSample (float x, int ch) noexcept;
    };

    enum UiEventType : juce::uint8 { uiNoteOff, uiNoteOn, uiPreviewStart, uiPreviewStop };
    struct UiEvent { juce::uint8 type, note, velocity; juce::uint32 id; };

    void swapRuntimeIfPending();
    void retire (KitRuntime* rt);
    void sweepRetired();
    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int note, int velocity, bool fromInput);
    void noteOff (int note, bool fromInput);
    void triggerPad (int padIndex, int note, int inputNote, int velocity, int depth);
    void playPreview();
    void pushUiEvent (const UiEvent& e);
    Voice* allocateVoice();
    void startVoice (Voice& v, const PadRT& pad, const LayerRT& layer, int padIndex, int note, int inputNote, int velocity);
    void renderVoice (Voice& v, const OutputBus* outputs, int start, int numSamples) noexcept;
    void applyPitchBend() noexcept;
    juce::uint32 nextRandom() noexcept;

    double sampleRate = 48000.0;
    std::array<Voice, kMaxVoices> voices;
    juce::uint64 voiceSerial = 0;

    std::atomic<KitRuntime*> pending { nullptr };
    KitRuntime* current = nullptr;
    std::array<KitRuntime*, 16> retired {};

    static constexpr int kGarbageSize = 64;
    juce::AbstractFifo garbageFifo { kGarbageSize };
    std::array<KitRuntime*, kGarbageSize> garbage {};

    static constexpr int kUiEventSize = 256;
    juce::AbstractFifo uiFifo { kUiEventSize };
    std::array<UiEvent, kUiEventSize> uiEvents {};

    std::array<juce::uint32, kMaxPads> roundRobin {};
    std::array<int, kMaxPads> lastRandomLayer {};
    std::array<juce::uint8, 128> lastVelocity {};
    juce::uint32 rngState = 0x9e3779b9u;
    float pitchBendSemis = 0.0f;
    std::atomic<bool> panicRequested { false };
    juce::uint32 pendingPreview = 0;          // audio thread: preview waiting for its snapshot
    juce::uint32 playingPreview = 0;          // audio thread: id of the last preview started
    juce::uint32 inputNoteSequence = 0;
    juce::uint64 eventFirstSerial = 0;        // voices started by the current note event have serials above this

    std::array<std::atomic<juce::uint32>, kMaxPads> padHits {};
    std::array<std::atomic<int>, kMaxPads> padVoices {};
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<juce::uint32> soundingPreview { 0 };
    std::atomic<juce::uint32> lastHitPad { 0 };
    std::atomic<juce::uint32> lastInputNote { 0 };
    std::atomic<int> lastInputVelocity { 0 };

    JUCE_DECLARE_NON_COPYABLE (SamplerEngine)
};

}
