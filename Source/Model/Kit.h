#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace kaia
{

constexpr int kMaxPads       = 128;   // 8 banks of 16, like a modern MPC drum program
constexpr int kPadsPerBank   = 16;
constexpr int kNumBanks      = kMaxPads / kPadsPerBank;
constexpr int kMaxLayers     = 8;     // MPC 3.4+ allows 8 layers per pad/keygroup
constexpr int kNumAuxOutputs = 16;    // stereo aux outputs besides the main output

enum class TriggerMode : int { OneShot = 0, NoteOn = 1, NoteOff = 2 };
enum class LayerMode   : int { Velocity = 0, Cycle = 1, Random = 2 };
enum class FilterType  : int { Off = 0, LP12, LP24, HP12, HP24, BP12, BP24, Notch };

// A reference to a sample somewhere on disk, optionally inside a zip archive (.xpn).
struct SampleRef
{
    juce::String file;      // absolute path of the audio file, or of the archive
    juce::String entry;     // path inside the archive; empty for plain files

    bool isEmpty() const noexcept       { return file.isEmpty(); }
    bool isInArchive() const noexcept   { return entry.isNotEmpty(); }
    juce::String toString() const       { return entry.isEmpty() ? file : file + "|" + entry; }
    static SampleRef fromString (const juce::String& s);
    juce::String displayName() const;   // file name without extension
    bool operator== (const SampleRef& o) const noexcept { return file == o.file && entry == o.entry; }
};

struct EnvelopeSettings
{
    bool  adMode       = true;   // AD (attack/hold/decay) or AHDSR
    bool  decayFromEnd = true;   // AD only: decay fades out the tail of the sample instead of starting after the attack
    float attackMs     = 0.0f;
    float holdMs       = 0.0f;
    float decayMs      = 2.0f;
    float sustain      = 1.0f;   // AHDSR only, 0..1
    float releaseMs    = 2.0f;
};

struct Layer
{
    SampleRef sample;
    juce::String name;            // display name (MPC sample name)
    bool  enabled   = true;
    float gainDb    = 0.0f;
    float pan       = 0.0f;       // -1..+1
    float tuneSemis = 0.0f;       // fractional semitones
    int   velLow    = 0;
    int   velHigh   = 127;
    juce::int64 start = 0;        // in source frames
    juce::int64 end   = 0;        // 0 = end of sample
    bool  reverse   = false;
    bool  loop      = false;
    juce::int64 loopStart = 0;
    juce::int64 loopEnd   = 0;    // 0 = end of sample
    int   rootNote  = -1;         // keygroups only, -1 = use sample metadata or pad root
    bool  missing   = false;      // sample could not be found when the kit was loaded
    juce::String fileHint;        // transient: file name stored in the program, used while resolving

    bool hasSample() const noexcept { return ! sample.isEmpty(); }
};

struct Pad
{
    juce::String name;            // optional label
    int   note      = -1;         // drum: trigger note. keygroup: unused (see noteLow/noteHigh)
    int   noteLow   = -1;         // keygroup range; -1 = drum pad (single note)
    int   noteHigh  = -1;
    bool  keyTrack  = false;      // pitch follows the played note (keygroups)
    int   rootNote  = 60;

    std::vector<Layer> layers;
    LayerMode   layerMode = LayerMode::Velocity;
    TriggerMode trigger   = TriggerMode::OneShot;

    float volumeDb   = 0.0f;
    float pan        = 0.0f;
    float tuneSemis  = 0.0f;
    float velToLevel = 1.0f;      // 0..1, how much velocity scales the level
    EnvelopeSettings env;

    FilterType filter      = FilterType::Off;
    float cutoffHz         = 20000.0f;
    float resonance        = 0.0f;   // 0..1
    float velToCutoff      = 0.0f;   // 0..1

    int   muteGroup = 0;                    // 0 = none, 1..32
    std::array<int, 4> muteTargets { -1, -1, -1, -1 };  // notes to cut off when this pad plays
    std::array<int, 4> simultTargets { -1, -1, -1, -1 };// notes that also play when this pad plays
    bool  mono      = false;
    int   polyphony = 0;                    // 0 = unlimited
    int   output    = 0;                    // 0 = main, 1..16 = aux stereo out
    bool  mute      = false;
    bool  solo      = false;

    bool isKeygroup() const noexcept   { return noteLow >= 0; }
    bool hasAnySample() const noexcept
    {
        for (auto& l : layers) if (l.hasSample() && l.enabled) return true;
        return false;
    }
    int firstNote() const noexcept     { return isKeygroup() ? noteLow : note; }
    bool respondsTo (int midiNote) const noexcept
    {
        return isKeygroup() ? (midiNote >= noteLow && midiNote <= noteHigh) : midiNote == note;
    }
};

struct Kit
{
    juce::String name;
    juce::String sourcePath;      // program file (or archive|entry) it was loaded from
    juce::String formatName;      // e.g. "MPC XPM (XML 2.1)"
    bool  isKeygroupProgram = false;
    float volumeDb          = 0.0f;
    float tuneSemis         = 0.0f;
    float pitchBendRange    = 2.0f;   // semitones

    std::vector<Pad> pads;            // drum programs: index = pad number - 1

    Kit();
    static Kit makeEmpty();           // 128 pads with the MPC default note map
    int countSamples() const;
    int countMissing() const;
    std::vector<SampleRef> allSampleRefs() const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static Kit fromXml (const juce::XmlElement&);
};

// The note map the MPC uses for a fresh drum program, pad A01..H16.
int defaultPadNote (int padIndex);
juce::String padName (int padIndex);          // "A01" etc.
juce::String noteName (int midiNote);         // "C#1 (37)"

// MPC levels: 0..1 fader position where 0.707946 is 0 dB and 1.0 is +6 dB.
float mpcFaderToDb (double value);
double dbToGain (double db);
}
