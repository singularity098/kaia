// Offline tests for the Kaia parsers and engine.
// Usage: KaiaTests [--real-data <dir>] [--keep]
//
// Generates synthetic MPC programs in every supported format (XPM XML, XPM JSON, XPN, PGM for
// MPC1000 / MPC2000XL / MPC3000), loads them, renders MIDI through the engine and checks the audio.

#include <juce_audio_formats/juce_audio_formats.h>
#include "Formats/KitLoader.h"
#include "Formats/Parsers.h"
#include "Formats/SndDecoder.h"
#include "Engine/SamplerEngine.h"
#include "Engine/KitRuntime.h"
#include "Model/TriggerMap.h"
#include "Model/UserFiles.h"
#include "Formats/Library.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>

//==============================================================================
// Allocation tracking: counts heap allocations made while `trackAllocations` is set on this thread.
static thread_local bool trackAllocations = false;
static std::atomic<int> trackedAllocations { 0 };

void* operator new (std::size_t size)
{
    if (trackAllocations)
        ++trackedAllocations;
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (trackAllocations)
        ++trackedAllocations;
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t& nt) noexcept { return operator new (size, nt); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace kaia;

//==============================================================================
static int failures = 0, checks = 0;

#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "  [" #cond "] (line " << __LINE__ << ")\n"; } } while (0)
#define SECTION(name) std::cout << "\n== " << name << "\n"

static juce::File workDir;

//==============================================================================
// Fixture helpers
struct Tone { double freq; float amp; double seconds; int channels = 1; double rate = 44100.0; int bits = 16; };

static juce::MemoryBlock makeWav (const Tone& t)
{
    juce::MemoryBlock mb;
    auto* out = new juce::MemoryOutputStream (mb, false);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out, t.rate, (unsigned int) t.channels, t.bits, {}, 0));
    auto frames = (int) (t.seconds * t.rate);
    juce::AudioBuffer<float> buf (t.channels, frames);
    for (int i = 0; i < frames; ++i)
    {
        auto v = t.amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * t.freq * i / t.rate);
        for (int c = 0; c < t.channels; ++c)
            buf.setSample (c, i, c == 0 ? v : v * 0.5f);   // right channel at half level for stereo files
    }
    writer->writeFromAudioSampleBuffer (buf, 0, frames);
    writer.reset();
    return mb;
}

static juce::File writeWav (const juce::File& f, const Tone& t)
{
    f.getParentDirectory().createDirectory();
    auto mb = makeWav (t);
    f.replaceWithData (mb.getData(), mb.getSize());
    return f;
}

static juce::MemoryBlock makeSnd2000 (const juce::String& name, const Tone& t, int tuneTenths = 0)
{
    auto frames = (int) (t.seconds * t.rate);
    juce::MemoryOutputStream o;
    o.writeByte (0x01); o.writeByte (0x04);
    auto n = name.paddedRight (' ', 16).substring (0, 16);
    o.write (n.toRawUTF8(), 16); o.writeByte (0);
    o.writeByte (100);                 // level
    o.writeByte ((char) tuneTenths);   // tune
    o.writeByte (0);                   // mono
    o.writeInt (0);                    // start
    o.writeInt (frames);               // end
    o.writeInt (frames);               // frames
    o.writeInt (0);                    // loop length
    o.writeByte (0); o.writeByte (4);
    o.writeShort ((short) (int) t.rate);
    for (int i = 0; i < frames; ++i)
        o.writeShort ((short) juce::roundToInt (32767.0 * t.amp * std::sin (2.0 * juce::MathConstants<double>::pi * t.freq * i / t.rate)));
    return o.getMemoryBlock();
}

static juce::MemoryBlock makeSnd3000 (const juce::String& name, const Tone& t)
{
    auto frames = (int) (t.seconds * 44100.0);
    juce::MemoryOutputStream o;
    o.writeByte (0x01); o.writeByte (0x02);
    auto n = name.paddedRight (' ', 16).substring (0, 16);
    o.write (n.toRawUTF8(), 16); o.writeByte (0);
    o.writeByte (100); o.writeByte (0); o.writeByte (0);
    o.writeInt (0); o.writeInt (frames); o.writeInt (frames); o.writeInt (0);
    for (int i = 0; i < frames; ++i)
        o.writeShort ((short) juce::roundToInt (32767.0 * t.amp * std::sin (2.0 * juce::MathConstants<double>::pi * t.freq * i / 44100.0)));
    return o.getMemoryBlock();
}

//==============================================================================
// XPM XML writer (mirrors what MPC software 2.x writes)
struct XLayer { juce::String sample; int velLo = 0, velHi = 127; double volume = 1.0; double pitch = 0; int direction = 0; };
struct XPad
{
    int number;        // 1-based pad number
    std::vector<XLayer> layers;
    int zonePlay = 1;  // 0 cycle, 1 velocity, 2 random
    int muteGroup = 0;
    int triggerMode = 0;
    bool mono = false;
    double volume = 0.707946;
    double pan = 0.5;
    int tuneCoarse = 0;
    bool decayFromEnd = true;
    double decay = 0.047244;
    int filterType = 0;
    double cutoff = 1.0;
};

static juce::String makeXpmXml (const juce::String& name, const std::vector<XPad>& pads, const std::map<int, int>& noteMap,
                                bool keygroup = false)
{
    juce::String x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<MPCVObject>\n  <Version>\n    <File_Version>2.1</File_Version>\n"
      << "    <Application>MPC-V</Application>\n    <Application_Version>2.10.1.0</Application_Version>\n    <Platform>Linux</Platform>\n  </Version>\n"
      << "  <Program type=\"" << (keygroup ? "Keygroup" : "Drum") << "\">\n    <ProgramName>" << name << "</ProgramName>\n"
      << "    <Volume>0.707946</Volume>\n    <Pan>0.500000</Pan>\n";
    if (keygroup)
        x << "    <KeygroupNumKeygroups>" << (int) pads.size() << "</KeygroupNumKeygroups>\n";
    x << "    <Instruments>\n";

    int maxPad = keygroup ? (int) pads.size() : 128;
    for (int n = 1; n <= maxPad; ++n)
    {
        const XPad* p = nullptr;
        for (auto& pp : pads)
            if (pp.number == n)
                p = &pp;

        XPad empty { n, {} };
        if (p == nullptr)
            p = &empty;

        x << "      <Instrument number=\"" << n << "\">\n"
          << "        <Volume>" << p->volume << "</Volume>\n        <Mute>False</Mute>\n        <Pan>" << p->pan << "</Pan>\n"
          << "        <TuneCoarse>" << p->tuneCoarse << "</TuneCoarse>\n        <TuneFine>0</TuneFine>\n"
          << "        <Mono>" << (p->mono ? "True" : "False") << "</Mono>\n        <Polyphony>1</Polyphony>\n";
        if (keygroup)
            x << "        <LowNote>" << (48 + (n - 1) * 12) << "</LowNote>\n        <HighNote>" << (59 + (n - 1) * 12) << "</HighNote>\n"
              << "        <IgnoreBaseNote>False</IgnoreBaseNote>\n";
        x << "        <ZonePlay>" << p->zonePlay << "</ZonePlay>\n        <MuteGroup>" << p->muteGroup << "</MuteGroup>\n"
          << "        <MuteTarget1>0</MuteTarget1>\n        <TriggerMode>" << p->triggerMode << "</TriggerMode>\n"
          << "        <FilterType>" << p->filterType << "</FilterType>\n        <Cutoff>" << p->cutoff << "</Cutoff>\n        <Resonance>0.000000</Resonance>\n"
          << "        <VolumeHold>0.000000</VolumeHold>\n        <VolumeDecayType>" << (p->decayFromEnd ? "True" : "False") << "</VolumeDecayType>\n"
          << "        <VolumeADEnvelope>True</VolumeADEnvelope>\n        <VolumeAttack>0.000000</VolumeAttack>\n"
          << "        <VolumeDecay>" << p->decay << "</VolumeDecay>\n        <VolumeSustain>1.000000</VolumeSustain>\n        <VolumeRelease>0.000000</VolumeRelease>\n"
          << "        <VelocitySensitivity>1.000000</VelocitySensitivity>\n        <Layers>\n";
        for (int l = 0; l < 4; ++l)
        {
            XLayer layer;
            if (l < (int) p->layers.size())
                layer = p->layers[(size_t) l];
            x << "          <Layer number=\"" << (l + 1) << "\">\n            <Active>True</Active>\n"
              << "            <Volume>" << layer.volume << "</Volume>\n            <Pan>0.500000</Pan>\n"
              << "            <Pitch>" << layer.pitch << "</Pitch>\n            <TuneCoarse>0</TuneCoarse>\n            <TuneFine>0</TuneFine>\n"
              << "            <VelStart>" << layer.velLo << "</VelStart>\n            <VelEnd>" << layer.velHi << "</VelEnd>\n"
              << "            <SampleStart>0</SampleStart>\n            <SampleEnd>0</SampleEnd>\n            <Loop>False</Loop>\n"
              << "            <RootNote>0</RootNote>\n            <KeyTrack>False</KeyTrack>\n"
              << "            <SampleName>" << layer.sample << "</SampleName>\n            <SampleFile></SampleFile>\n"
              << "            <SliceIndex>128</SliceIndex>\n            <Direction>" << layer.direction << "</Direction>\n            <Offset>0</Offset>\n"
              << "            <SliceStart>0</SliceStart>\n            <SliceEnd>0</SliceEnd>\n            <SliceLoopStart>0</SliceLoopStart>\n            <SliceLoop>0</SliceLoop>\n"
              << "          </Layer>\n";
        }
        x << "        </Layers>\n      </Instrument>\n";
    }
    x << "    </Instruments>\n";
    if (! keygroup)
    {
        x << "    <PadNoteMap>\n";
        for (auto& [pad, note] : noteMap)
            x << "      <PadNote number=\"" << pad << "\">\n        <Note>" << note << "</Note>\n      </PadNote>\n";
        x << "    </PadNoteMap>\n";
    }
    x << "  </Program>\n</MPCVObject>\n";
    return x;
}

//==============================================================================
// MPC 3 JSON program writer (gzip "ACVS" container)
static juce::MemoryBlock makeXpmJson (const juce::String& name)
{
    auto layer = [] (const juce::String& sample, int lo, int hi)
    {
        auto* l = new juce::DynamicObject();
        l->setProperty ("active", true);
        auto* vol = new juce::DynamicObject();
        vol->setProperty ("gainCoefficient", 1.0);
        l->setProperty ("volume", juce::var (vol));
        l->setProperty ("pan", 0.5);
        l->setProperty ("pitch", 0.0);
        l->setProperty ("coarseTune", 0);
        l->setProperty ("fineTune", 0);
        l->setProperty ("velocityStart", lo);
        l->setProperty ("velocityEnd", hi);
        l->setProperty ("sampleStart", 0);
        l->setProperty ("sampleEnd", 0);
        l->setProperty ("rootNote", 0);
        l->setProperty ("keyTrackEnable", false);
        l->setProperty ("sampleName", sample);
        l->setProperty ("sampleFile", sample.isEmpty() ? juce::String() : sample + ".WAV");
        l->setProperty ("direction", 0);
        l->setProperty ("offset", 0);
        auto* slice = new juce::DynamicObject();
        slice->setProperty ("Start", 0);
        slice->setProperty ("End", 0);
        slice->setProperty ("LoopMode", 0);
        l->setProperty ("sliceInfo", juce::var (slice));
        return juce::var (l);
    };

    juce::Array<juce::var> instruments;
    for (int i = 0; i < 128; ++i)
    {
        auto* inst = new juce::DynamicObject();
        inst->setProperty ("coarseTune", 0);
        inst->setProperty ("fineTune", 0);
        inst->setProperty ("monophonic", false);
        inst->setProperty ("triggerMode", 0);
        inst->setProperty ("zonePlay", 1);
        inst->setProperty ("whichMuteGroup", i == 2 || i == 3 ? 1 : 0);
        auto* mix = new juce::DynamicObject();
        mix->setProperty ("volume", 0.707946);
        mix->setProperty ("pan", 0.5);
        inst->setProperty ("mixable", juce::var (mix));
        juce::Array<juce::var> layers;
        juce::String s = i == 0 ? "JKick" : i == 1 ? "JSnare" : "";
        layers.add (layer (s, 0, 127));
        layers.add (layer ("", 0, 127));
        inst->setProperty ("layersv", layers);
        instruments.add (juce::var (inst));
    }

    auto* drum = new juce::DynamicObject();
    drum->setProperty ("instruments", instruments);
    auto* noteForPad = new juce::DynamicObject();
    for (int i = 0; i < 128; ++i)
        noteForPad->setProperty (("value" + juce::String (i)).toRawUTF8(), i == 0 ? 36 : i == 1 ? 38 : defaultPadNote (i));
    auto* padNoteMap = new juce::DynamicObject();
    padNoteMap->setProperty ("noteForPad", juce::var (noteForPad));

    auto* program = new juce::DynamicObject();
    program->setProperty ("name", name);
    program->setProperty ("type", 0);
    program->setProperty ("drum", juce::var (drum));
    program->setProperty ("padNoteMap", juce::var (padNoteMap));

    auto* root = new juce::DynamicObject();
    root->setProperty ("data", juce::var (program));

    juce::String text = "ACVS\n3.7.0.38\nSerialisableProgramData\njson\nLinux\n" + juce::JSON::toString (juce::var (root));

    juce::MemoryBlock mb;
    {
        juce::MemoryOutputStream raw (mb, false);
        juce::GZIPCompressorOutputStream gz (raw, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        gz.writeText (text, false, false, nullptr);
    }
    return mb;
}

//==============================================================================
// MPC1000 PGM writer
static juce::MemoryBlock makePgm1000 (const std::vector<std::tuple<int, int, juce::String, int, int, int>>& samples,
                                      std::map<int, int> padNotes, int muteGroupPad = -1)
{
    // samples: pad, slot, name, velLo, velHi, tuneCents
    juce::MemoryBlock mb (0x2A04, true);
    auto* d = static_cast<juce::uint8*> (mb.getData());
    d[0] = 0x04; d[1] = 0x2A;
    std::memcpy (d + 4, "MPC1000 PGM 1.00", 16);
    for (int p = 0; p < 64; ++p)
    {
        auto* pd = d + 0x18 + p * 0xA4;
        for (int s = 0; s < 4; ++s)
        {
            auto* sd = pd + s * 0x18;
            sd[0x11] = 100; sd[0x12] = 0; sd[0x13] = 127;
        }
        pd[0x65] = 1; pd[0x66] = 0; pd[0x67] = 5; pd[0x68] = 0; pd[0x6B] = 100;
        pd[0x72] = 100; pd[0x8F] = 100; pd[0x90] = 50;
        if (p == muteGroupPad || p == muteGroupPad + 1)
            pd[0x63] = 1;
        d[0x2918 + p] = (juce::uint8) defaultPadNote (p);
    }
    for (auto& [pad, slot, name, lo, hi, tune] : samples)
    {
        auto* sd = d + 0x18 + pad * 0xA4 + slot * 0x18;
        std::memcpy (sd, name.toRawUTF8(), (size_t) juce::jmin (16, name.length()));
        sd[0x12] = (juce::uint8) lo; sd[0x13] = (juce::uint8) hi;
        auto t = (juce::int16) tune;
        sd[0x14] = (juce::uint8) (t & 0xff); sd[0x15] = (juce::uint8) ((t >> 8) & 0xff);
    }
    for (auto& [pad, note] : padNotes)
        d[0x2918 + pad] = (juce::uint8) note;
    return mb;
}

// MPC2000XL PGM writer: sounds, per-note params (index = note - 35), pad -> note map
static juce::MemoryBlock makePgm2000 (const juce::StringArray& sounds, const std::map<int, int>& noteSound,
                                      const std::map<int, int>& padNote, int velSwitchNote = -1, int velSwitchTarget = -1)
{
    juce::MemoryOutputStream o;
    o.writeByte (0x07); o.writeByte (0x04);
    o.writeShort ((short) sounds.size());
    for (auto& s : sounds)
    {
        auto n = s.paddedRight (' ', 16).substring (0, 16);
        o.write (n.toRawUTF8(), 16);
        o.writeByte (0);
    }
    o.writeByte (0x1e); o.writeByte (0);
    auto pname = juce::String ("TESTPGM").paddedRight (' ', 16);
    o.write (pname.toRawUTF8(), 16); o.writeByte (0);
    for (int i = 0; i < 9; ++i) o.writeByte (0);
    o.writeByte (0);
    o.writeByte (0x23); o.writeByte (0x40); o.writeByte (0); o.writeByte (0x19); o.writeByte (0);
    for (int i = 0; i < 64; ++i)
    {
        auto note = 35 + i;
        auto it = noteSound.find (note);
        juce::uint8 rec[25] = {};
        rec[0] = it != noteSound.end() ? (juce::uint8) it->second : 0xff;
        rec[1] = 0;
        rec[2] = 44; rec[3] = 34; rec[4] = 88; rec[5] = 34;
        if (note == velSwitchNote)
        {
            rec[1] = 2;            // velocity switch
            rec[2] = 64;           // > 64 plays the target note's sound
            rec[3] = (juce::uint8) velSwitchTarget;
            rec[4] = 127;
            rec[5] = 34;
        }
        rec[6] = 0; rec[12] = 5; rec[13] = 0; rec[14] = 100; rec[19] = 100;
        o.write (rec, 25);
    }
    o.writeByte (0);
    for (int i = 0; i < 64; ++i)
    {
        juce::uint8 mix[6] = { 0, 100, 50, 100, 0, 0 };
        o.write (mix, 6);
    }
    o.writeByte (0); o.writeByte (0x40); o.writeByte (0);
    for (int p = 0; p < 64; ++p)
    {
        auto it = padNote.find (p);
        o.writeByte ((char) (it != padNote.end() ? it->second : defaultPadNote (p)));
    }
    for (int i = 0; i < 200; ++i) o.writeByte (0);
    return o.getMemoryBlock();
}

static juce::MemoryBlock makePgm3000 (const juce::StringArray& sounds, const std::map<int, int>& noteSound)
{
    juce::MemoryBlock mb (4608, true);
    auto* d = static_cast<juce::uint8*> (mb.getData());
    d[0] = 0x07; d[1] = 0x00;
    for (int i = 0; i < sounds.size(); ++i)
        std::memcpy (d + 2 + i * 17, sounds[i].toRawUTF8(), (size_t) juce::jmin (16, sounds[i].length()));
    std::memcpy (d + 2690, "PGM3000", 7);
    const int notes = 2752, mixers = notes + 64 * 24, pads = mixers + 64 * 4;
    for (int i = 0; i < 64; ++i)
    {
        auto* n = d + notes + i * 24;
        auto it = noteSound.find (35 + i);
        n[0] = it != noteSound.end() ? (juce::uint8) it->second : 0xff;
        n[12] = 6; n[14] = 100; n[19] = 100;
        d[mixers + i * 4] = 100;
        d[mixers + i * 4 + 1] = 50;
        d[pads + i] = (juce::uint8) defaultPadNote (i);
    }
    return mb;
}

//==============================================================================
// Rendering + analysis
struct Render
{
    std::vector<std::vector<float>> ch;   // 2 * outputs channels
    int numSamples = 0;
    float* get (int output, int c) { return ch[(size_t) (output * 2 + c)].data(); }
};

struct Ev { int sample; int note; int vel; };   // vel 0 = note off

static Render render (SamplerEngine& engine, const std::vector<Ev>& events, double seconds, double rate, int block = 256, bool countAllocs = false)
{
    Render r;
    r.numSamples = (int) (seconds * rate);
    r.ch.assign (SamplerEngine::kNumOutputs * 2, std::vector<float> ((size_t) r.numSamples + (size_t) block, 0.0f));

    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    OutputBus outs[SamplerEngine::kNumOutputs];

    for (int pos = 0; pos < r.numSamples; pos += block)
    {
        auto n = juce::jmin (block, r.numSamples - pos);
        midi.clear();
        for (auto& e : events)
            if (e.sample >= pos && e.sample < pos + n)
                midi.addEvent (e.vel > 0 ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.vel)
                                         : juce::MidiMessage::noteOff (1, e.note), e.sample - pos);
        for (int o = 0; o < SamplerEngine::kNumOutputs; ++o)
            outs[o] = { r.get (o, 0) + pos, r.get (o, 1) + pos };

        trackAllocations = countAllocs;
        engine.process (outs, n, midi);
        trackAllocations = false;
    }
    return r;
}

static double rms (const float* d, int start, int len)
{
    double s = 0;
    for (int i = start; i < start + len; ++i)
        s += (double) d[i] * d[i];
    return std::sqrt (s / juce::jmax (1, len));
}

static double zeroCrossFreq (const float* d, int start, int len, double rate)
{
    int crossings = 0;
    for (int i = start + 1; i < start + len; ++i)
        if ((d[i - 1] < 0) != (d[i] < 0))
            ++crossings;
    return crossings / 2.0 / (len / rate);
}

// Silences everything still ringing from a previous sub-test
static void reset (SamplerEngine& engine)
{
    engine.allSoundOff();
    render (engine, {}, 0.05, engine.getSampleRate());
}

static LoadResult loadFile (const juce::File& f)
{
    return KitLoader::loadProgram ({ f, {} });
}

static void install (SamplerEngine& engine, const LoadResult& r)
{
    engine.setRuntime (buildRuntime (r.kit, &r.samples));
}

static const Pad* padWithNote (const Kit& k, int note)
{
    for (auto& p : k.pads)
        if (p.note == note && ! p.layers.empty())
            return &p;
    return nullptr;
}

//==============================================================================
static void testXpmXml()
{
    SECTION ("XPM drum program (XML, MPC 2.x)");
    auto dir = workDir.getChildFile ("XmlKit");
    // Samples in a sub folder, as MPC packs often do
    writeWav (dir.getChildFile ("Samples/Kick Soft.wav"), { 100, 0.5f, 0.5 });
    writeWav (dir.getChildFile ("Samples/Kick Hard.WAV"), { 200, 0.5f, 0.5 });
    writeWav (dir.getChildFile ("Samples/Snare RR1.wav"), { 300, 0.5f, 0.5 });
    writeWav (dir.getChildFile ("Samples/Snare RR2.wav"), { 400, 0.5f, 0.5 });
    writeWav (dir.getChildFile ("Samples/Snare RR3.wav"), { 500, 0.5f, 0.5 });
    writeWav (dir.getChildFile ("Samples/Hat Open.wav"),  { 1000, 0.5f, 2.0 });
    writeWav (dir.getChildFile ("Samples/Hat Closed.wav"), { 2000, 0.5f, 0.3 });
    writeWav (dir.getChildFile ("Samples/Tom 48k Stereo.wav"), { 440, 0.5f, 1.0, 2, 48000.0, 24 });

    std::vector<XPad> pads;
    XPad kick { 1, { { "Kick Soft", 0, 63 }, { "Kick Hard", 64, 127 } } };
    XPad snare { 2, { { "Snare RR1" }, { "Snare RR2" }, { "Snare RR3" } } };
    snare.zonePlay = 0;   // cycle
    XPad openHat { 3, { { "Hat Open" } } };
    openHat.muteGroup = 1;
    XPad closedHat { 4, { { "Hat Closed" } } };
    closedHat.muteGroup = 1;
    XPad tom { 5, { { "Tom 48k Stereo" } } };
    tom.tuneCoarse = 12;
    XPad missing { 6, { { "Does Not Exist" } } };
    XPad noteOn { 7, { { "Hat Open" } } };
    noteOn.triggerMode = 2;
    XPad reversed { 8, { { "Kick Hard", 0, 127, 1.0, 0, 1 } } };
    XPad layered { 9, { { "Kick Soft" }, { "Hat Closed" } } };
    pads = { kick, snare, openHat, closedHat, tom, missing, noteOn, reversed, layered };

    std::map<int, int> noteMap { { 1, 36 }, { 2, 38 }, { 3, 46 }, { 4, 42 }, { 5, 50 }, { 6, 51 }, { 7, 52 }, { 8, 53 }, { 9, 54 } };
    auto xpm = dir.getChildFile ("Test Kit.xpm");
    xpm.replaceWithText (makeXpmXml ("Test Kit", pads, noteMap));

    auto r = loadFile (xpm);
    CHECK (r.ok, "load XPM: " << r.error);
    if (! r.ok) return;

    CHECK (r.kit.name == "Test Kit", "program name");
    CHECK (! r.kit.isKeygroupProgram, "drum program");
    CHECK ((int) r.kit.pads.size() == 128, "128 pads");
    CHECK (r.kit.pads[0].note == 36 && r.kit.pads[1].note == 38 && r.kit.pads[2].note == 46, "pad note map applied");
    CHECK (r.kit.pads[0].layers.size() == 2, "kick has 2 layers");
    CHECK (r.kit.pads[0].layers[0].velHigh == 63 && r.kit.pads[0].layers[1].velLow == 64, "velocity ranges");
    CHECK (r.kit.pads[1].layerMode == LayerMode::Cycle, "snare cycles");
    CHECK (r.kit.pads[2].muteGroup == 1 && r.kit.pads[3].muteGroup == 1, "mute groups");
    CHECK (std::abs (r.kit.pads[0].volumeDb) < 0.05f, "0.707946 is 0 dB (got " << r.kit.pads[0].volumeDb << ")");
    CHECK (r.kit.pads[4].tuneSemis == 12.0f, "pad tune");
    CHECK (r.kit.pads[5].layers.size() == 1 && r.kit.pads[5].layers[0].missing, "missing sample flagged");
    CHECK (r.warnings.joinIntoString (" ").contains ("Does Not Exist"), "missing sample reported: " << r.warnings.joinIntoString ("|"));
    CHECK (r.kit.pads[6].trigger == TriggerMode::NoteOn, "trigger mode note on");
    CHECK (r.kit.pads[7].layers[0].reverse, "reverse layer");
    CHECK (r.kit.countSamples() == 12, "sample count " << r.kit.countSamples());
    CHECK (r.samples.size() == 8, "unique samples decoded: " << (int) r.samples.size());

    SamplerEngine engine;
    const double rate = 48000.0;
    engine.prepare (rate);
    install (engine, r);

    // Velocity layers: soft -> 100 Hz, hard -> 200 Hz (44.1 kHz files played at 48 kHz: pitch must be preserved)
    {
        reset (engine);
        auto out = render (engine, { { 0, 36, 40 }, { 24000, 36, 120 } }, 1.0, rate);
        auto f1 = zeroCrossFreq (out.get (0, 0), 1000, 12000, rate);
        auto f2 = zeroCrossFreq (out.get (0, 0), 25000, 12000, rate);
        CHECK (std::abs (f1 - 100) < 3, "soft velocity plays soft layer at correct pitch (" << f1 << " Hz)");
        CHECK (std::abs (f2 - 200) < 3, "hard velocity plays hard layer (" << f2 << " Hz)");
        // velocity sensitivity: vel 40 quieter than 120
        auto a1 = rms (out.get (0, 0), 1000, 4000), a2 = rms (out.get (0, 0), 25000, 4000);
        CHECK (a1 < a2 * 0.5, "velocity scales level (" << a1 << " vs " << a2 << ")");
        CHECK (std::abs (a2 - 0.5 / std::sqrt (2.0) * 120.0 / 127.0) < 0.02, "unity gain at 0 dB (" << a2 << ")");
    }

    // Round robin: 3 hits on the snare -> 300, 400, 500 Hz
    {
        reset (engine);
        auto out = render (engine, { { 0, 38, 100 }, { 24000, 38, 100 }, { 48000, 38, 100 }, { 72000, 38, 100 } }, 2.0, rate);
        auto f1 = zeroCrossFreq (out.get (0, 0), 500, 9000, rate);
        auto f2 = zeroCrossFreq (out.get (0, 0), 24500, 9000, rate);
        auto f3 = zeroCrossFreq (out.get (0, 0), 48500, 9000, rate);
        auto f4 = zeroCrossFreq (out.get (0, 0), 72500, 9000, rate);
        // The round-robin counter continues from earlier tests, so check that 3 consecutive hits are all different
        CHECK (std::abs (f1 - f2) > 50 && std::abs (f2 - f3) > 50 && std::abs (f1 - f3) > 50,
               "cycle plays three different layers (" << f1 << ", " << f2 << ", " << f3 << ")");
        CHECK (std::abs (f4 - f1) < 5, "cycle wraps around (" << f4 << " vs " << f1 << ")");
    }

    // Choke: open hat (2 s), closed hat at 0.25 s must cut it within ~6 ms
    {
        reset (engine);
        auto out = render (engine, { { 0, 46, 100 }, { 12000, 42, 100 } }, 1.0, rate);
        auto before = rms (out.get (0, 0), 6000, 4800);
        // After the choke only the 2 kHz closed hat (0.3 s) should remain; check that 1 kHz is gone:
        // after the closed hat ends (0.25 s + 0.3 s = 0.55 s) everything must be silent.
        auto after = rms (out.get (0, 0), (int) (0.6 * rate), 4800);
        CHECK (before > 0.2, "open hat plays (" << before << ")");
        CHECK (after < 1e-4, "open hat choked by closed hat (" << after << ")");
        auto f = zeroCrossFreq (out.get (0, 0), 12000 + 600, 4800, rate);
        CHECK (std::abs (f - 2000) < 30, "only the closed hat after the choke (" << f << " Hz)");
    }

    // Pitch: 440 Hz stereo 48 kHz sample tuned +12 -> 880 Hz; right channel at half level
    {
        reset (engine);
        auto out = render (engine, { { 0, 50, 127 } }, 0.5, rate);
        auto f = zeroCrossFreq (out.get (0, 0), 1000, 9600, rate);
        CHECK (std::abs (f - 880) < 5, "tune +12 doubles the pitch (" << f << " Hz)");
        auto l = rms (out.get (0, 0), 1000, 4800), rr = rms (out.get (0, 1), 1000, 4800);
        CHECK (std::abs (rr / l - 0.5) < 0.02, "stereo sample keeps channel balance (" << rr / l << ")");
        // pitched up one octave the 1 s sample lasts 0.5 s
        auto tail = rms (out.get (0, 0), (int) (0.49 * rate), 200);
        juce::ignoreUnused (tail);
    }

    // Note-on trigger mode: releases on note off
    {
        reset (engine);
        auto out = render (engine, { { 0, 52, 100 }, { 4800, 52, 0 } }, 0.5, rate);
        auto during = rms (out.get (0, 0), 1000, 3000);
        auto after = rms (out.get (0, 0), 4800 + 960, 4800);
        CHECK (during > 0.2 && after < 1e-4, "note-on mode stops at note off (" << during << " / " << after << ")");
    }

    // One-shot ignores note off
    {
        reset (engine);
        auto out = render (engine, { { 0, 46, 100 }, { 4800, 46, 0 } }, 0.5, rate);
        auto after = rms (out.get (0, 0), 9600, 4800);
        CHECK (after > 0.2, "one-shot keeps playing after note off (" << after << ")");
    }

    // Layered pad: both layers sound together (100 Hz + 2 kHz)
    {
        reset (engine);
        auto out = render (engine, { { 0, 54, 127 } }, 0.2, rate);
        auto level = rms (out.get (0, 0), 500, 4000);
        CHECK (level > 0.45, "velocity-mode layers play together (" << level << ")");
    }

    // Reverse: plays the 200 Hz kick backwards (fade near the end of playback is the sample start)
    {
        reset (engine);
        auto out = render (engine, { { 0, 53, 127 } }, 0.6, rate);
        auto f = zeroCrossFreq (out.get (0, 0), 1000, 9600, rate);
        CHECK (std::abs (f - 200) < 4, "reverse playback (" << f << " Hz)");
    }

    // Multi-out: route the kick to aux 3
    {
        auto copy = r.kit;
        copy.pads[0].output = 3;
        engine.setRuntime (buildRuntime (copy, &r.samples));
        reset (engine);
        auto out = render (engine, { { 0, 36, 127 } }, 0.2, rate);
        CHECK (rms (out.get (3, 0), 500, 4000) > 0.3 && rms (out.get (0, 0), 500, 4000) < 1e-6, "pad routed to aux output 3");
        install (engine, r);
    }

    // Kit snapshot swap while voices ring + no allocations on the audio thread
    {
        trackedAllocations = 0;
        std::vector<Ev> evs;
        for (int i = 0; i < 200; ++i)
            evs.push_back ({ i * 480, 36 + (i % 20), 30 + (i * 7) % 97 });
        reset (engine);
        auto out = render (engine, evs, 2.5, rate, 128, true);
        CHECK (trackedAllocations.load() == 0, "no heap allocations in process() (" << trackedAllocations.load() << ")");

        engine.setRuntime (buildRuntime (r.kit, &r.samples));
        trackedAllocations = 0;
        render (engine, { { 0, 36, 100 } }, 0.1, rate, 128, true);
        CHECK (trackedAllocations.load() == 0, "no heap allocations when swapping kits");
        engine.collectGarbage();
    }
}

//==============================================================================
static void testXpmJson()
{
    SECTION ("XPM drum program (gzip JSON, MPC 3.x)");
    auto dir = workDir.getChildFile ("JsonKit");
    writeWav (dir.getChildFile ("JsonKit_[ProgramData]/JKick.WAV"), { 150, 0.5f, 0.5 });
    writeWav (dir.getChildFile ("JsonKit_[ProgramData]/JSnare.WAV"), { 350, 0.5f, 0.5 });
    auto f = dir.getChildFile ("JsonKit.xpm");
    auto mb = makeXpmJson ("Json Kit");
    f.replaceWithData (mb.getData(), mb.getSize());

    auto r = loadFile (f);
    CHECK (r.ok, "load JSON XPM: " << r.error);
    if (! r.ok) return;
    CHECK (r.kit.name == "Json Kit", "name");
    CHECK (r.kit.pads[0].note == 36 && r.kit.pads[1].note == 38, "JSON pad note map");
    CHECK (r.kit.countSamples() == 2 && r.kit.countMissing() == 0, "JSON samples resolved");
    CHECK (r.kit.pads[2].muteGroup == 1, "JSON mute group");
    CHECK (r.kit.formatName.contains ("3.7"), "format version noted: " << r.kit.formatName);

    SamplerEngine engine;
    engine.prepare (44100.0);
    install (engine, r);
    auto out = render (engine, { { 0, 38, 127 } }, 0.3, 44100.0);
    auto freq = zeroCrossFreq (out.get (0, 0), 500, 8000, 44100.0);
    CHECK (std::abs (freq - 350) < 4, "JSON kit plays (" << freq << " Hz)");
}

//==============================================================================
static void testXpn()
{
    SECTION ("XPN expansion (zip)");
    auto xpnFile = workDir.getChildFile ("Test Expansion.xpn");

    juce::ZipFile::Builder builder;
    std::vector<std::unique_ptr<juce::MemoryBlock>> keep;
    auto add = [&] (const juce::String& path, juce::MemoryBlock mb, int compression = 9)
    {
        auto* stream = new juce::MemoryInputStream (mb, true);
        builder.addEntry (stream, compression, path, juce::Time::getCurrentTime());
    };

    const juce::String expansionXml ("<?xml version=\"1.0\"?><expansion version=\"2.0.0.0\"><title>Test Expansion</title>"
                                     "<manufacturer>Kaia Tests</manufacturer><img>Cover.png</img></expansion>");
    add ("Expansion.xml", juce::MemoryBlock (expansionXml.toRawUTF8(), expansionXml.getNumBytesAsUTF8()));
    add ("[Samples]/XKick.wav", makeWav ({ 120, 0.5f, 0.5 }));
    add ("[Samples]/XSnare.wav", makeWav ({ 330, 0.5f, 0.5 }), 0);   // stored, not deflated
    std::vector<XPad> pads { XPad { 1, { { "XKick" } } }, XPad { 2, { { "XSnare" } } } };
    add ("Kits/Kit One.xpm", juce::MemoryBlock (makeXpmXml ("Kit One", pads, { { 1, 36 }, { 2, 38 } }).toRawUTF8(),
                                                 makeXpmXml ("Kit One", pads, { { 1, 36 }, { 2, 38 } }).getNumBytesAsUTF8()));
    add ("Kits/Kit Two.xpm", juce::MemoryBlock (makeXpmXml ("Kit Two", { XPad { 1, { { "XSnare" } } } }, { { 1, 40 } }).toRawUTF8(),
                                                 makeXpmXml ("Kit Two", { XPad { 1, { { "XSnare" } } } }, { { 1, 40 } }).getNumBytesAsUTF8()));
    add ("__MACOSX/Kits/._Kit One.xpm", juce::MemoryBlock (16, true));
    add ("Cover.png", juce::MemoryBlock ("cover bytes", 11));
    add ("other.jpg", juce::MemoryBlock ("other", 5));

    xpnFile.deleteFile();
    {
        juce::FileOutputStream out (xpnFile);
        builder.writeToStream (out, nullptr);
    }

    PackInfo info;
    juce::String error;
    auto list = KitLoader::listPrograms (xpnFile, &info, error);
    CHECK (list.size() == 2, "two programs listed (got " << (int) list.size() << ") " << error);
    CHECK (info.title == "Test Expansion" && info.manufacturer == "Kaia Tests", "expansion info: " << info.title);
    if (list.size() != 2) return;
    CHECK (list[0].name == "Kit One" && list[0].location.inArchive(), "program inside archive");
    {
        juce::MemoryBlock cover;
        CHECK (KitLoader::readPackArtwork (xpnFile, cover) && cover == juce::MemoryBlock ("cover bytes", 11), "cover art named by Expansion.xml");
        auto folder = workDir.getChildFile ("Cover Folder");
        folder.createDirectory();
        folder.getChildFile ("a.png").replaceWithText ("a");
        folder.getChildFile ("Folder.jpg").replaceWithText ("folder art");
        CHECK (KitLoader::readPackArtwork (folder, cover) && cover.toString() == "folder art", "folder pack: folder.jpg preferred");
        folder.deleteRecursively();
    }

    auto r = KitLoader::loadProgram (list[0].location);
    CHECK (r.ok, "load program from XPN: " << r.error);
    CHECK (r.kit.countSamples() == 2 && r.kit.countMissing() == 0, "XPN samples resolved inside the archive");
    CHECK (r.kit.pads[0].layers[0].sample.isInArchive(), "sample ref points into the archive");

    SamplerEngine engine;
    engine.prepare (44100.0);
    install (engine, r);
    auto out = render (engine, { { 0, 36, 127 }, { 22050, 38, 127 } }, 1.0, 44100.0);
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 500, 8000, 44100.0) - 120) < 3, "deflated sample plays");
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 22550, 8000, 44100.0) - 330) < 3, "stored sample plays");

    // State round trip: kit XML -> kit -> reload from archive
    auto xml = r.kit.toXml();
    auto restored = KitLoader::reloadKit (Kit::fromXml (*xml));
    CHECK (restored.ok && restored.kit.countSamples() == 2, "kit restored from saved state");
    CHECK (restored.kit.pads[1].note == 38 && restored.kit.name == "Kit One", "restored settings");

    // Not a zip
    auto bogus = workDir.getChildFile ("Bogus.xpn");
    bogus.replaceWithText ("this is not a zip file");
    auto bl = KitLoader::listPrograms (bogus, nullptr, error);
    CHECK (bl.empty() && error.isNotEmpty(), "non-zip XPN reports an error: " << error);
}

//==============================================================================
static void testPgm1000()
{
    SECTION ("MPC1000/2500 PGM");
    auto dir = workDir.getChildFile ("Pgm1000");
    writeWav (dir.getChildFile ("BD_SOFT.WAV"), { 90, 0.5f, 0.4 });
    writeWav (dir.getChildFile ("BD_HARD.WAV"), { 180, 0.5f, 0.4 });
    writeWav (dir.getChildFile ("SN_1.wav"), { 270, 0.5f, 0.4 });
    writeWav (dir.getChildFile ("OH.WAV"), { 900, 0.5f, 2.0 });
    writeWav (dir.getChildFile ("CH.WAV"), { 1800, 0.5f, 0.2 });

    auto pgm = makePgm1000 ({ { 0, 0, "BD_SOFT", 0, 70, 0 }, { 0, 1, "BD_HARD", 71, 127, 0 },
                              { 1, 0, "SN_1", 0, 127, 1200 },
                              { 6, 0, "OH", 0, 127, 0 }, { 7, 0, "CH", 0, 127, 0 } },
                            { { 0, 36 }, { 1, 38 }, { 6, 46 }, { 7, 42 } }, 6);
    auto f = dir.getChildFile ("KIT1000.PGM");
    f.replaceWithData (pgm.getData(), pgm.getSize());

    auto r = loadFile (f);
    CHECK (r.ok, "load MPC1000 PGM: " << r.error);
    if (! r.ok) return;
    CHECK (r.kit.formatName.contains ("MPC1000"), "format " << r.kit.formatName);
    CHECK (r.kit.pads[0].note == 36 && r.kit.pads[0].layers.size() == 2, "pad 1 with two velocity samples");
    CHECK (r.kit.pads[1].layers[0].tuneSemis == 12.0f, "tuning in cents");
    CHECK (r.kit.pads[6].muteGroup == 1 && r.kit.pads[7].muteGroup == 1, "mute group");
    CHECK (r.kit.countMissing() == 0 && r.kit.countSamples() == 5, "samples found (case-insensitive extension)");

    SamplerEngine engine;
    engine.prepare (44100.0);
    install (engine, r);
    auto out = render (engine, { { 0, 36, 50 }, { 22050, 36, 110 }, { 44100, 38, 100 }, { 66150, 46, 100 }, { 70000, 42, 100 } }, 2.5, 44100.0);
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 500, 8000, 44100.0) - 90) < 3, "soft sample at low velocity");
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 22550, 8000, 44100.0) - 180) < 3, "hard sample at high velocity");
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 44600, 8000, 44100.0) - 540) < 6, "+12 semitones");
    CHECK (rms (out.get (0, 0), 70000 + 10000, 20000) < 1e-4, "hats choke each other");
}

static void testPgm2000()
{
    SECTION ("MPC2000XL PGM + SND");
    auto dir = workDir.getChildFile ("Pgm2000");
    dir.createDirectory();
    auto write = [&] (const juce::String& file, const juce::MemoryBlock& mb) { dir.getChildFile (file).replaceWithData (mb.getData(), mb.getSize()); };
    write ("KICK1.SND", makeSnd2000 ("KICK1", { 110, 0.5f, 0.5 }));
    write ("SNARE1.SND", makeSnd2000 ("SNARE1", { 220, 0.5f, 0.5 }, 120));   // +12 semitones in the sound itself
    write ("SNARE2.SND", makeSnd2000 ("SNARE2", { 660, 0.5f, 0.5 }));
    write ("HAT.WAV", makeWav ({ 1500, 0.5f, 0.3 }));

    // notes: 36 kick (sound 0), 38 snare (sound 1) velocity-switching to note 40 (sound 2), 42 hat (wav)
    auto pgm = makePgm2000 ({ "KICK1", "SNARE1", "SNARE2", "HAT" }, { { 36, 0 }, { 38, 1 }, { 40, 2 }, { 42, 3 } },
                            { { 0, 36 }, { 1, 38 }, { 2, 42 } }, 38, 40);
    write ("TESTPGM.PGM", pgm);

    auto r = loadFile (dir.getChildFile ("TESTPGM.PGM"));
    CHECK (r.ok, "load MPC2000XL PGM: " << r.error);
    if (! r.ok) return;
    CHECK (r.kit.name == "TESTPGM", "program name '" << r.kit.name << "'");
    CHECK (r.kit.pads[0].note == 36 && r.kit.pads[1].note == 38 && r.kit.pads[2].note == 42, "pad to note map");
    CHECK (r.kit.pads[1].layers.size() == 2, "velocity switch becomes two layers");
    CHECK (r.kit.countMissing() == 0, "SND and WAV samples found");
    // Note 40 has a sound but no pad: it must still play from MIDI
    CHECK (padWithNote (r.kit, 40) != nullptr, "unmapped note with a sound gets a pad");

    SamplerEngine engine;
    engine.prepare (44100.0);
    install (engine, r);
    auto out = render (engine, { { 0, 36, 100 }, { 22050, 38, 50 }, { 44100, 38, 100 } }, 1.5, 44100.0);
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 500, 8000, 44100.0) - 110) < 3, "SND sample plays");
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 22550, 8000, 44100.0) - 440) < 5, "SND header tuning applied");
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 44600, 8000, 44100.0) - 660) < 6, "velocity switch to the other note's sound");
}

static void testPgm3000()
{
    SECTION ("MPC3000 PGM + SND");
    auto dir = workDir.getChildFile ("Pgm3000");
    dir.createDirectory();
    auto snd = makeSnd3000 ("BD3000", { 130, 0.5f, 0.4 });
    dir.getChildFile ("BD3000.SND").replaceWithData (snd.getData(), snd.getSize());
    auto pgm = makePgm3000 ({ "BD3000" }, { { 37, 0 } });
    dir.getChildFile ("PGM3000.PGM").replaceWithData (pgm.getData(), pgm.getSize());

    auto r = loadFile (dir.getChildFile ("PGM3000.PGM"));
    CHECK (r.ok, "load MPC3000 PGM: " << r.error);
    if (! r.ok) return;
    CHECK (r.kit.formatName.contains ("3000"), "format");
    auto* pad = padWithNote (r.kit, 37);
    CHECK (pad != nullptr && pad->layers.size() == 1 && ! pad->layers[0].missing, "MPC3000 note 37 has its sound");

    SamplerEngine engine;
    engine.prepare (48000.0);
    install (engine, r);
    auto out = render (engine, { { 0, 37, 127 } }, 0.3, 48000.0);
    CHECK (std::abs (zeroCrossFreq (out.get (0, 0), 500, 8000, 48000.0) - 130) < 3, "MPC3000 SND plays at 44.1k in a 48k session");
}

//==============================================================================
static void testKeygroup()
{
    SECTION ("XPM keygroup program");
    auto dir = workDir.getChildFile ("Keys");
    writeWav (dir.getChildFile ("Piano C3.wav"), { 261.63, 0.5f, 1.0 });
    writeWav (dir.getChildFile ("Piano C4.wav"), { 523.25, 0.5f, 1.0 });
    // Root notes: XPM stores MIDI note + 1
    auto xml = makeXpmXml ("Keys", { XPad { 1, { { "Piano C3" } } }, XPad { 2, { { "Piano C4" } } } }, {}, true);
    xml = xml.replace ("<RootNote>0</RootNote>\n            <KeyTrack>False</KeyTrack>\n            <SampleName>Piano C3",
                       "<RootNote>61</RootNote>\n            <KeyTrack>True</KeyTrack>\n            <SampleName>Piano C3")
             .replace ("<RootNote>0</RootNote>\n            <KeyTrack>False</KeyTrack>\n            <SampleName>Piano C4",
                       "<RootNote>73</RootNote>\n            <KeyTrack>True</KeyTrack>\n            <SampleName>Piano C4");
    dir.getChildFile ("Keys.xpm").replaceWithText (xml);

    auto r = loadFile (dir.getChildFile ("Keys.xpm"));
    CHECK (r.ok, "load keygroup: " << r.error);
    if (! r.ok) return;
    CHECK (r.kit.isKeygroupProgram && r.kit.pads.size() == 2, "two keygroups");
    CHECK (r.kit.pads[0].noteLow == 48 && r.kit.pads[0].noteHigh == 59, "key range");
    CHECK (r.kit.pads[0].layers[0].rootNote == 60, "root note decoded (+1 offset)");

    SamplerEngine engine;
    engine.prepare (44100.0);
    install (engine, r);
    // note 55 (G2) in keygroup 1 -> 261.63 * 2^(-5/12) = 196.0 Hz ; note 67 in keygroup 2 -> 523.25*2^(-5/12) = 392 Hz
    auto out = render (engine, { { 0, 55, 100 }, { 0, 55, 0 } , { 22050, 67, 100 } }, 1.0, 44100.0);
    auto f2 = zeroCrossFreq (out.get (0, 0), 22550, 8000, 44100.0);
    CHECK (std::abs (f2 - 392.0) < 4, "keygroup transposes from the root note (" << f2 << " Hz)");
}

//==============================================================================
static void testFolderAndEdgeCases()
{
    SECTION ("Folder kit, listing and edge cases");
    auto dir = workDir.getChildFile ("LooseSamples");
    for (int i = 1; i <= 20; ++i)
        writeWav (dir.getChildFile ("Hit " + juce::String (i) + ".wav"), { 100.0 + i * 10, 0.3f, 0.1 });

    juce::Array<juce::File> files;
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*.wav"))
        files.add (e.getFile());
    auto kit = KitLoader::kitFromAudioFiles (files, "Loose");
    CHECK (kit.pads[0].layers.size() == 1 && kit.pads[0].layers[0].name == "Hit 1", "natural sort: Hit 1 first");
    CHECK (kit.pads[1].layers[0].name == "Hit 2", "natural sort: Hit 2 second (not Hit 10)");
    CHECK (kit.pads[19].layers.size() == 1 && kit.pads[20].layers.empty(), "20 samples on 20 pads");

    // Program listing in a folder
    juce::String error;
    auto progs = KitLoader::listPrograms (workDir, nullptr, error);
    int xpm = 0, pgm = 0, xpn = 0;
    for (auto& p : progs)
    {
        if (p.isArchive) ++xpn;
        else if (p.location.file.hasFileExtension ("xpm")) ++xpm;
        else ++pgm;
    }
    CHECK (xpm >= 3 && pgm >= 3 && xpn >= 1, "folder listing finds programs and expansions (" << xpm << "/" << pgm << "/" << xpn << ")");

    // Garbage input never crashes
    juce::Random rnd (1234);
    for (int i = 0; i < 300; ++i)
    {
        juce::MemoryBlock junk ((size_t) rnd.nextInt (12000), false);
        for (size_t b = 0; b < junk.getSize(); ++b)
            static_cast<juce::uint8*> (junk.getData())[b] = (juce::uint8) rnd.nextInt (256);
        if (i % 3 == 0 && junk.getSize() > 2) { static_cast<juce::uint8*> (junk.getData())[0] = 0x07; static_cast<juce::uint8*> (junk.getData())[1] = 0x04; }
        if (i % 3 == 1 && junk.getSize() > 20) std::memcpy (junk.getData(), "\x04\x2A\0\0MPC1000 PGM 1.00", 20);
        Kit k;
        juce::StringArray w;
        juce::String e;
        parsePgm (junk, k, w, e);
        parseXpm (junk, k, w, e);
        SampleData sd;
        if (junk.getSize() > 2) { static_cast<juce::uint8*> (junk.getData())[0] = 0x01; static_cast<juce::uint8*> (junk.getData())[1] = (i & 1) ? 0x04 : 0x02; }
        decodeMpcSnd (junk.getData(), junk.getSize(), sd, e);
    }
    CHECK (true, "fuzzed parsers survived");

    // Truncated XML
    Kit k;
    juce::StringArray w;
    juce::String e;
    auto partial = makeXpmXml ("X", { XPad { 1, { { "a" } } } }, {}).substring (0, 900);
    CHECK (! parseXpm (juce::MemoryBlock (partial.toRawUTF8(), (size_t) partial.getNumBytesAsUTF8()), k, w, e), "truncated XPM rejected: " << e);
}

//==============================================================================
static void testPerformance()
{
    SECTION ("Performance");
    auto dir = workDir.getChildFile ("XmlKit");
    auto r = loadFile (dir.getChildFile ("Test Kit.xpm"));
    if (! r.ok) return;

    SamplerEngine engine;
    const double rate = 48000.0;
    engine.prepare (rate);
    install (engine, r);

    // A busy pattern: 16th notes on 6 pads at 180 BPM with long open hats -> ~40-60 voices, all resampled
    std::vector<Ev> evs;
    const int notes[] = { 36, 38, 46, 50, 54, 53 };
    for (int i = 0; i < 2000; ++i)
        for (auto n : notes)
            evs.push_back ({ i * (int) (rate * 60.0 / 180.0 / 4.0), n, 60 + (i * 13) % 67 });

    const double seconds = 20.0;
    auto t0 = std::chrono::steady_clock::now();
    auto out = render (engine, evs, seconds, rate, 256);
    auto t1 = std::chrono::steady_clock::now();
    {
        // measure typical polyphony of this pattern
        reset (engine);
        int maxVoices = 0;
        double sum = 0;
        int blocks = 0;
        juce::MidiBuffer midi;
        std::vector<float> l (256), r (256);
        OutputBus outs[SamplerEngine::kNumOutputs];
        for (auto& o : outs) o = { l.data(), r.data() };
        for (int pos = 0; pos < (int) (4 * rate); pos += 256, ++blocks)
        {
            midi.clear();
            for (auto& e : evs)
                if (e.sample >= pos && e.sample < pos + 256)
                    midi.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.vel), e.sample - pos);
            engine.process (outs, 256, midi);
            maxVoices = juce::jmax (maxVoices, engine.getActiveVoiceCount());
            sum += engine.getActiveVoiceCount();
        }
        std::cout << "  polyphony: average " << juce::String (sum / blocks, 1) << " voices, peak " << maxVoices << "\n";
    }
    auto elapsed = std::chrono::duration<double> (t1 - t0).count();
    auto load = elapsed / seconds * 100.0;
    std::cout << "  rendered " << seconds << " s of busy drums in " << juce::String (elapsed, 3) << " s = "
              << juce::String (load, 2) << "% of one core\n";
    CHECK (load < 15.0, "engine is cheap (" << load << "% of a core)");

    float peak = 0;
    for (int i = 0; i < out.numSamples; ++i)
        peak = juce::jmax (peak, std::abs (out.get (0, 0)[i]));
    CHECK (std::isfinite (peak) && peak > 0.1f, "output is finite and non-silent");
}

//==============================================================================
static void testRealFiles (const juce::File& root)
{
    SECTION ("Real MPC files from open-source projects");
    if (! root.isDirectory())
    {
        std::cout << "  (skipped: no --real-data folder)\n";
        return;
    }

    // MPC1000 program written by an MPC (mpcmaid test data). Samples are not included.
    {
        juce::MemoryBlock mb;
        auto f = root.getChildFile ("mpcmaid/test.pgm");
        if (f.loadFileAsData (mb))
        {
            Kit k;
            juce::StringArray w;
            juce::String e;
            CHECK (parsePgm (mb, k, w, e), "mpcmaid test.pgm parses: " << e);
            CHECK (k.pads[0].layers.size() == 4 && k.pads[0].layers[0].name == "1KSN_001", "pad 1 sample names");
            CHECK (k.pads[0].note == 37 && k.pads[1].note == 36 && k.pads[2].note == 42, "MPC1000 default notes");
            CHECK (k.pads[0].layers[1].velLow == 2 && k.pads[0].layers[1].velHigh == 3, "velocity range bytes");
            CHECK (std::abs (k.pads[0].layers[3].tuneSemis - 0.04f) < 1e-4, "tuning");
        }
    }

    // MPC2000XL demo kits with their SND files (vmpc2000xl demo data)
    for (auto name : { "vmpc/TEST1/BASIC_KIT.PGM", "vmpc/TEST2/FRUTZLE.PGM" })
    {
        auto f = root.getChildFile (name);
        if (! f.existsAsFile())
            continue;
        auto r = loadFile (f);
        CHECK (r.ok, juce::String (name) << " loads: " << r.error);
        std::cout << "  " << name << ": \"" << r.kit.name << "\", " << r.kit.countSamples() << " samples, "
                  << r.kit.countMissing() << " missing, " << r.warnings.joinIntoString ("; ") << "\n";
        CHECK (r.kit.countSamples() > 0 && r.kit.countMissing() == 0, juce::String (name) << " all sounds found");

        SamplerEngine engine;
        engine.prepare (44100.0);
        install (engine, r);
        std::vector<Ev> evs;
        int t = 0;
        for (auto& p : r.kit.pads)
            if (! p.layers.empty())
            {
                evs.push_back ({ t, p.note, 110 });
                t += 11025;
            }
        auto out = render (engine, evs, t / 44100.0 + 1.0, 44100.0);
        int silentHits = 0;
        for (auto& e : evs)
            if (rms (out.get (0, 0), e.sample, 2048) + rms (out.get (0, 1), e.sample, 2048) < 1e-4)
                ++silentHits;
        CHECK (silentHits == 0, juce::String (name) << ": every pad makes sound (" << silentHits << " silent)");

        // write a preview so a human can listen
        auto wavOut = workDir.getChildFile (juce::File (name).getFileNameWithoutExtension() + "_render.wav");
        if (auto os = std::unique_ptr<juce::FileOutputStream> (wavOut.createOutputStream()))
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.release(), 44100.0, 2, 24, {}, 0));
            const float* chans[2] = { out.get (0, 0), out.get (0, 1) };
            writer->writeFromFloatArrays (chans, 2, out.numSamples);
        }
    }

    // MPC2000XL program saved by real hardware
    {
        auto f = root.getChildFile ("vmpc/RealMpc2000xl/NEWPGM-A.PGM");
        juce::MemoryBlock mb;
        if (f.loadFileAsData (mb))
        {
            Kit k;
            juce::StringArray w;
            juce::String e;
            CHECK (parsePgm (mb, k, w, e), "real MPC2000XL program parses: " << e);
            std::cout << "  NEWPGM-A.PGM: \"" << k.name << "\", pads with sounds: " << k.countSamples() + k.countMissing() << "\n";
        }
    }

    // MPC3000 programs saved by real hardware / MAME
    for (const auto& entry : juce::RangedDirectoryIterator (root.getChildFile ("vmpc/RealMpc3000"), false, "*.pgm"))
    {
        juce::MemoryBlock mb;
        entry.getFile().loadFileAsData (mb);
        Kit k;
        juce::StringArray w;
        juce::String e;
        auto ok = parsePgm (mb, k, w, e);
        CHECK (ok, entry.getFile().getFileName() << " parses: " << e);
        juce::StringArray names;
        for (auto& p : k.pads)
            for (auto& l : p.layers)
                names.addIfNotAlreadyThere (l.name);
        std::cout << "  " << entry.getFile().getFileName() << ": \"" << k.name << "\" sounds: " << names.joinIntoString (", ") << "\n";
    }

    // Keygroup program saved by MPC software 2.1 (mpc_valet reference file): parses, has no samples
    {
        auto f = root.getChildFile ("mpc_valet/Reference.xpm");
        juce::MemoryBlock mb;
        if (f.loadFileAsData (mb))
        {
            Kit k;
            juce::StringArray w;
            juce::String e;
            auto ok = parseXpm (mb, k, w, e);
            CHECK (! ok && e.contains ("no samples"), "MPC 2.1 keygroup template parses (empty): " << e);
        }
    }
}


//==============================================================================
// In-memory kit: pad i plays a sine of freqs[i] (no files involved)
static SampleDataPtr makeTone (double freq, double seconds, double rate = 44100.0)
{
    auto sd = std::make_shared<SampleData>();
    sd->sampleRate = rate;
    sd->allocate (1, (juce::int64) (seconds * rate));
    auto* d = sd->writePointer (0);
    for (juce::int64 i = 0; i < sd->numFrames; ++i)
        d[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / rate);
    return sd;
}

static void addTonePad (Kit& kit, SampleMap& samples, int pad, double freq, double seconds)
{
    Layer l;
    l.sample = { "/mem/tone" + juce::String (pad), {} };
    l.name = "tone" + juce::String (pad);
    kit.pads[(size_t) pad].layers = { l };
    samples[l.sample.toString()] = makeTone (freq, seconds);
}

static double freqAt (Render& out, int start, int len = 4000)
{
    return zeroCrossFreq (out.get (0, 0), start, len, 44100.0);
}

static void testTriggerMap()
{
    SECTION ("Drum module trigger maps and MIDI learn");

    auto kit = Kit::makeEmpty();
    SampleMap samples;
    for (int i = 0; i < 16; ++i)
        addTonePad (kit, samples, i, 200.0 + 100.0 * i, 0.3);    // A01 200 Hz, A02 300 Hz, ...

    TriggerMap td10;
    CHECK (td10.selectModule ("roland-td10") && td10.usesModule() && td10.moduleName() == "Roland TD-10", "TD-10 module exists");
    CHECK (! td10.isEdited() && ! td10.isCustom(), "factory notes are not an edit");
    for (auto& m : drumModules())
    {
        bool ok = ! m.notes.empty();
        for (auto& [note, zone] : m.notes)
            ok = ok && note >= 0 && note < 128 && (int) zone < (int) DrumZone::NumZones;
        CHECK (ok, "module note table valid: " << m.name);
    }

    auto table = td10.resolve (kit);
    auto plays = [&table] (int note) { return table[(size_t) note].size() == 1 ? table[(size_t) note].front() : -1; };
    CHECK (plays (36) == 0, "TD-10 kick (36) -> A01");
    CHECK (plays (38) == 1, "TD-10 snare (38) -> A02");
    CHECK (plays (42) == 2 && plays (22) == 2, "TD-10 closed hi-hat (42, 22) -> A03");
    CHECK (table[46] == std::vector<int> ({ 7, 3 }) && table[26] == std::vector<int> ({ 7, 3 }), "TD-10 open hi-hat (46, 26) -> A08 and A04");
    CHECK (plays (44) == 3, "TD-10 pedal hi-hat (44) -> A04");
    CHECK (plays (40) == 4, "TD-10 snare rim (40) -> A05");
    CHECK (plays (41) == 8 && plays (39) == 8, "TD-10 floor tom head and rim -> A09");
    CHECK (plays (48) == 11 && plays (50) == 11, "TD-10 high tom head and rim -> A12");
    CHECK (plays (49) == 12 && plays (59) == 12 && plays (55) == 15 && plays (52) == 14 && plays (10) == 14,
           "TD-10 ride 49/59 -> A13, crash 2 55 -> A16, its edge 52/10 -> A15");
    CHECK (table[51].empty() && table[53].empty() && table[57].empty(), "TD-10 leaves 51, 53 and 57 unmapped");
    CHECK (table[37].empty() && table[60].empty(), "notes the module doesn't send are ignored");
    CHECK (td10.notesForPad (kit, 7) == std::vector<int> ({ 26, 46 }), "notes for the open hi-hat pad");

    // Editing the note table
    {
        auto edited = td10;
        edited.moveNoteToZone (59, 15);
        CHECK (edited.isEdited() && edited.notesForZone (15) == std::vector<int> ({ 55, 59 }) && edited.notesForZone (12) == std::vector<int> { 49 },
               "moving a note to another pad");
        edited.setZoneNotes (2, { 42, 22, 21 });
        edited.removeNote (22, 2);
        CHECK (edited.notesForZone (2) == std::vector<int> ({ 42, 21 }), "setting and removing a pad's notes");
        auto back = TriggerMap::fromXml (*edited.toXml());
        CHECK (back == edited, "edited map XML round trip");
        edited.resetToFactory();
        CHECK (edited == td10, "back to the factory notes");

        // Saved custom maps
        juce::String error;
        CHECK (CustomDrumMaps::save ("My TD-10", back.notes, error), "save a custom map: " << error);
        auto customs = CustomDrumMaps::list();
        CHECK (customs.size() == 1 && customs[0].name == "My TD-10" && customs[0].notes == back.notes, "custom maps listed");
        TriggerMap custom;
        CHECK (custom.selectModule ("custom:My TD-10") && custom.isCustom() && custom.moduleName() == "My TD-10"
                 && custom.notes == back.notes, "select a custom map");
        TriggerMap restored;
        restored.restore ("custom:Gone", TriggerMap::notesToString (back.notes));
        CHECK (restored.usesModule() && restored.notes == back.notes, "a project keeps its custom map even without the file");
        restored.restore ("custom:Gone", {});
        CHECK (! restored.usesModule(), "unknown map without notes falls back to the program's notes");
        CHECK (CustomDrumMaps::remove ("My TD-10") && CustomDrumMaps::list().empty(), "delete a custom map");
    }

    SamplerEngine engine;
    engine.prepare (44100.0);

    // Through the engine: note 36 now plays A01 (200 Hz); MPC default note 37 is silent
    engine.setRuntime (buildRuntime (kit, &samples, &td10));
    auto out = render (engine, { { 0, 36, 127 }, { 22050, 37, 127 } }, 1.0, 44100.0, 256, true);
    CHECK (std::abs (freqAt (out, 500) - 200.0) < 3, "MIDI 36 plays A01 through the TD-10 map (" << freqAt (out, 500) << " Hz)");
    CHECK (rms (out.get (0, 0), 22550, 4000) < 1.0e-6, "unmapped note is silent");
    CHECK (trackedAllocations.load() == 0, "no allocations with a trigger map");
    auto seq = engine.getLastInputNote();
    CHECK ((seq & 0x7f) == 37 && (seq >> 8) == 2, "last input note reported for MIDI Learn");

    // Pad audition from the UI uses program notes and bypasses the map
    reset (engine);
    engine.auditionNoteOn (kit.pads[1].note, 100);
    out = render (engine, {}, 0.2, 44100.0);
    CHECK (std::abs (freqAt (out, 500, 7000) - 300.0) < 3, "UI audition of A02 still works (" << freqAt (out, 500) << " Hz, rms " << rms (out.get (0, 0), 500, 4000) << ")");

    // Learned notes win over the module
    auto learnedMap = td10;
    learnedMap.learn (60, 5);
    learnedMap.learn (36, 9);
    table = learnedMap.resolve (kit);
    CHECK (plays (60) == 5 && plays (36) == 9 && plays (38) == 1, "learned notes override the module map");
    learnedMap.forgetPad (9);
    CHECK (learnedMap.resolve (kit)[36].front() == 0, "forgetting a pad restores the module note");

    // Without a module, learning adds a note to the program's map
    TriggerMap plain;
    CHECK (plain.isDefault(), "default map");
    plain.learn (36, 2);
    table = plain.resolve (kit);
    CHECK (plays (36) == 2 && plays (37) == 0 && plays (42) == 2, "learned note on top of the program note map");

    // XML round trip
    auto xml = learnedMap.toXml();
    auto back = TriggerMap::fromXml (*xml);
    CHECK (back == learnedMap, "trigger map XML round trip");
    TriggerMap junk;
    junk.learnedFromString ("x,1:2:3,200:1,5:900,7:3");
    CHECK (junk.learned.size() == 2 && junk.learned[7] == 3, "learned-notes parser ignores junk");

    // Note-on pads release on the *input* note's note-off
    {
        auto k2 = kit;
        SampleMap s2 = samples;
        addTonePad (k2, s2, 0, 200.0, 2.0);
        k2.pads[0].trigger = TriggerMode::NoteOn;
        k2.pads[0].env.adMode = false;
        k2.pads[0].env.releaseMs = 5.0f;
        reset (engine);
        engine.setRuntime (buildRuntime (k2, &s2, &td10));
        out = render (engine, { { 0, 36, 127 }, { 11025, 36, 0 } }, 1.0, 44100.0);
        CHECK (rms (out.get (0, 0), 2000, 4000) > 0.1 && rms (out.get (0, 0), 14000, 4000) < 1.0e-4,
               "note-off of the mapped note releases the pad");
    }

    // Hi-hat choke: the closed hi-hat cuts the ringing open hi-hat
    {
        auto k3 = kit;
        SampleMap s3 = samples;
        addTonePad (k3, s3, 7, 900.0, 2.0);     // open hi-hat rings for 2 s
        addTonePad (k3, s3, 2, 400.0, 0.05);    // short closed hi-hat
        for (bool choke : { true, false })
        {
            auto m = td10;
            m.hiHatChoke = choke;
            reset (engine);
            engine.setRuntime (buildRuntime (k3, &s3, &m));
            out = render (engine, { { 0, 46, 127 }, { 11025, 42, 127 } }, 1.0, 44100.0);
            auto tail = rms (out.get (0, 0), 22050, 4000);
            if (choke)
                CHECK (tail < 1.0e-4, "closed hi-hat chokes the open hi-hat (" << tail << ")");
            else
                CHECK (tail > 0.1, "without hi-hat choke the open hi-hat keeps ringing");
        }
        // The kit's own choke groups are kept
        k3.pads[7].muteGroup = 5;
        auto rt = buildRuntime (k3, &s3, &td10);
        CHECK (rt->pads[7].muteGroup == 5 && rt->pads[2].muteGroup == kHiHatChokeGroup, "existing mute groups win");
        CHECK (k3.pads[2].muteGroup == 0, "the kit itself is not modified");
    }

    // The open hi-hat plays A08 and A04 together: pads hit by the same note don't choke each other
    {
        auto k4 = kit;
        SampleMap s4 = samples;
        addTonePad (k4, s4, 7, 900.0, 1.0);
        addTonePad (k4, s4, 3, 500.0, 1.0);
        reset (engine);
        engine.setRuntime (buildRuntime (k4, &s4, &td10));
        out = render (engine, { { 0, 46, 127 } }, 0.5, 44100.0);
        auto both = rms (out.get (0, 0), 4000, 8000);
        k4.pads[3].layers.clear();
        reset (engine);
        engine.setRuntime (buildRuntime (k4, &s4, &td10));
        out = render (engine, { { 0, 46, 127 } }, 0.5, 44100.0);
        auto single = rms (out.get (0, 0), 4000, 8000);
        CHECK (both > single * 1.2, "open hi-hat layers A08 and A04 (" << both << " vs " << single << ")");
    }

    // Simult pads stop with the note-off of the note that started them
    {
        auto k5 = kit;
        SampleMap s5 = samples;
        addTonePad (k5, s5, 0, 200.0, 2.0);
        addTonePad (k5, s5, 5, 700.0, 2.0);
        for (int p : { 0, 5 })
        {
            k5.pads[(size_t) p].trigger = TriggerMode::NoteOn;
            k5.pads[(size_t) p].env.adMode = false;
            k5.pads[(size_t) p].env.releaseMs = 5.0f;
        }
        k5.pads[0].simultTargets[0] = k5.pads[5].note;
        reset (engine);
        engine.setRuntime (buildRuntime (k5, &s5, &td10));
        out = render (engine, { { 0, 36, 127 }, { 11025, 36, 0 } }, 1.0, 44100.0);
        CHECK (rms (out.get (0, 0), 2000, 4000) > 0.1 && rms (out.get (0, 0), 14000, 4000) < 1.0e-4,
               "note-off releases the simult pad too");
    }

    // Sample loops only play when enabled
    {
        auto k6 = kit;
        SampleMap s6 = samples;
        addTonePad (k6, s6, 0, 200.0, 0.2);
        k6.pads[0].trigger = TriggerMode::NoteOn;
        k6.pads[0].layers[0].loop = true;
        for (bool loops : { true, false })
        {
            reset (engine);
            engine.setRuntime (buildRuntime (k6, &s6, &td10, nullptr, loops));
            out = render (engine, { { 0, 36, 127 } }, 1.0, 44100.0);
            auto tail = rms (out.get (0, 0), 22050, 4000);
            if (loops)
                CHECK (tail > 0.1, "looped sample keeps playing while held");
            else
                CHECK (tail < 1.0e-6 && engine.getActiveVoiceCount() == 0, "with loops off the sample plays once (" << tail << ")");
        }
    }

    // Keygroup programs keep their key ranges
    {
        Kit kg;
        kg.isKeygroupProgram = true;
        kg.pads.resize (1);
        kg.pads[0].noteLow = 40;
        kg.pads[0].noteHigh = 50;
        auto t = td10.resolve (kg);
        CHECK (t[36].empty() && t[45].size() == 1, "keygroup programs ignore the drum map");
    }

    // A map change doesn't affect what the default (no module) path plays
    reset (engine);
    engine.setRuntime (buildRuntime (kit, &samples, &plain));
    out = render (engine, { { 0, 37, 127 } }, 0.3, 44100.0);
    CHECK (std::abs (freqAt (out, 500) - 200.0) < 3, "program note 37 still plays A01 without a module");
}

static void testPreview()
{
    SECTION ("Library preview");
    auto kit = Kit::makeEmpty();
    SampleMap samples;
    addTonePad (kit, samples, 0, 200.0, 0.5);

    PreviewSound pv;
    Layer l;
    l.sample = { "/mem/preview", {} };
    pv.pad.layers = { l };
    pv.pad.trigger = TriggerMode::NoteOn;      // ignored: previews are one-shots
    pv.pad.output = 3;                         // ignored: previews play on Main
    pv.samples[l.sample.toString()] = makeTone (700.0, 1.0);
    pv.id = 7;

    SamplerEngine engine;
    engine.prepare (44100.0);
    engine.startPreview (7);                   // arrives before its snapshot: waits for it
    render (engine, {}, 0.05, 44100.0);
    auto hits = engine.getPadHitCount (0);
    engine.setRuntime (buildRuntime (kit, &samples, nullptr, &pv));
    auto out = render (engine, {}, 0.5, 44100.0, 256, true);
    CHECK (std::abs (freqAt (out, 500) - 700.0) < 4, "preview plays once its snapshot is active (" << freqAt (out, 500) << " Hz)");
    CHECK (trackedAllocations.load() == 0, "no allocations while previewing");
    CHECK (engine.getPadHitCount (0) == hits, "previews don't count as pad hits");
    CHECK (engine.getSoundingPreview() == 7, "the sounding preview is reported (library play/stop button)");

    engine.stopPreview();
    out = render (engine, {}, 0.2, 44100.0);
    CHECK (rms (out.get (0, 0), 2000, 4000) < 1.0e-6, "stopPreview silences it");
    CHECK (engine.getSoundingPreview() == 0, "no preview reported after stopPreview");

    // A stale preview id never plays
    engine.startPreview (3);
    out = render (engine, {}, 0.2, 44100.0);
    CHECK (rms (out.get (0, 0), 0, 4000) < 1.0e-6, "stale preview ignored");
}

static void testLibrary()
{
    SECTION ("Library scan (read-only)");
    auto lib = workDir.getChildFile ("Library");
    lib.deleteRecursively();

    // An expansion archive (from testXpn), a program folder and plain sample folders
    lib.getChildFile ("Expansions").createDirectory();
    workDir.getChildFile ("Test Expansion.xpn").copyFileTo (lib.getChildFile ("Expansions/Test Expansion.xpn"));
    auto folderPack = lib.getChildFile ("Expansions/My Kit Folder");
    writeWav (folderPack.getChildFile ("Samples/FKick.wav"), { 150, 0.5f, 0.2 });
    folderPack.getChildFile ("Expansion.xml").replaceWithText ("<expansion><title>Folder Expansion</title></expansion>");
    writeWav (folderPack.getChildFile ("Samples/FSnare.wav"), { 350, 0.5f, 0.2 });
    folderPack.getChildFile ("Folder Kit.xpm").replaceWithText (
        makeXpmXml ("Folder Kit", { XPad { 1, { { "FKick" } } }, XPad { 2, { { "FSnare" } } } }, { { 1, 36 }, { 2, 38 } }));
    writeWav (lib.getChildFile ("One Shots/Kicks/Kick A.wav"), { 100, 0.5f, 0.1 });
    writeWav (lib.getChildFile ("One Shots/Kicks/Kick B.wav"), { 110, 0.5f, 0.1 });
    writeWav (lib.getChildFile ("One Shots/Snares/Snare A.wav"), { 300, 0.5f, 0.1 });
    lib.getChildFile ("One Shots/Kicks/._Kick A.wav").replaceWithText ("mac junk");
    lib.getChildFile ("One Shots/readme.txt").replaceWithText ("not audio");

    // Snapshot of every file so we can prove nothing is written
    std::map<juce::String, std::pair<juce::int64, juce::int64>> before;
    auto snapshot = [&lib]
    {
        std::map<juce::String, std::pair<juce::int64, juce::int64>> m;
        for (const auto& e : juce::RangedDirectoryIterator (lib, true, "*", juce::File::findFilesAndDirectories))
            m[e.getFile().getFullPathName()] = { e.getFile().getSize(), e.getFile().getLastModificationTime().toMilliseconds() };
        return m;
    };
    before = snapshot();

    int calls = 0;
    auto index = Library::scan (lib, nullptr, [&calls] (const juce::String&) { ++calls; return true; });
    CHECK (index.packs.size() == 4, "4 packs: archive, program folder, two sample folders (got " << (int) index.packs.size() << ")");

    const LibraryPack* xpn = nullptr;
    const LibraryPack* folder = nullptr;
    const LibraryPack* kicks = nullptr;
    for (auto& p : index.packs)
    {
        if (p.isArchive) xpn = &p;
        if (p.title == "Folder Expansion") folder = &p;
        if (p.title.endsWith ("Kicks")) kicks = &p;
    }
    CHECK (xpn != nullptr && xpn->title == "Test Expansion" && xpn->programs.size() == 2 && xpn->samples.size() == 2,
           "expansion: title, programs and samples from the archive");
    CHECK (folder != nullptr && folder->programs.size() == 1 && folder->samples.size() == 2, "program folder pack with its samples ("
           << (folder != nullptr ? folder->programs.joinIntoString (",") + " / " + folder->samples.joinIntoString (",") : juce::String ("missing")) << ")");
    CHECK (kicks != nullptr && kicks->samples.size() == 2, "sample folder without junk files");
    CHECK (index.numPrograms() == 3 && index.numSamples() == 7, "counts: " << index.numPrograms() << " programs, " << index.numSamples() << " samples");
    CHECK (calls > 0, "progress callback polled");

    if (xpn != nullptr && folder != nullptr && kicks != nullptr)
    {
        // Samples from different packs load and can be mixed on one kit
        juce::String error;
        auto a = loadSample (xpn->sampleRef (0), error);
        auto b = loadSample (kicks->sampleRef (0), error);
        CHECK (a != nullptr && b != nullptr, "samples load from an archive and from a folder: " << error);

        auto loc = folder->programLocation (0);
        auto r = KitLoader::loadProgram (loc);
        CHECK (r.ok && r.kit.countSamples() == 2, "program from the library loads: " << r.error);

        auto xr = KitLoader::loadProgram (xpn->programLocation (0));
        CHECK (xr.ok && xr.kit.countSamples() == 2, "program inside the library archive loads");
    }

    // Cache round trip, and unchanged archives are reused on rescan
    auto cacheXml = index.toXml();
    auto restored = LibraryIndex::fromXml (*cacheXml);
    CHECK (restored.packs.size() == index.packs.size() && restored.numSamples() == index.numSamples()
           && restored.packs[0].title == index.packs[0].title, "library index XML round trip");
    for (auto& p : restored.packs)
        if (p.isArchive)
            p.programs.add ("marker.xpm");    // only present if the cached entry is reused
    auto again = Library::scan (lib, &restored);
    bool reused = false;
    for (auto& p : again.packs)
        if (p.isArchive)
            reused = p.programs.contains ("marker.xpm");
    CHECK (reused, "unchanged archive taken from the cache");

    // Cancelling stops early
    auto cancelled = Library::scan (lib, nullptr, [] (const juce::String&) { return false; });
    CHECK (cancelled.packs.size() < index.packs.size(), "scan can be cancelled");

    CHECK (snapshot() == before, "scanning and loading never modify the library");
}

static void testUserFiles()
{
    SECTION ("Config folder and presets");
    auto configDir = workDir.getChildFile ("config");
    CHECK (UserFiles::configDirectory() == configDir, "KAIA_CONFIG_DIR override");
    CHECK (UserFiles::presetsDirectory().isAChildOf (configDir) && UserFiles::settingsFile().isAChildOf (configDir), "everything inside the config folder");

   #if JUCE_LINUX
    {
        // XDG locations (and legacy migration) with a fake home
        auto home = workDir.getChildFile ("home");
        auto oldHome = juce::SystemStats::getEnvironmentVariable ("HOME", {});
        auto oldXdg = juce::SystemStats::getEnvironmentVariable ("XDG_CONFIG_HOME", {});
        auto hadXdg = std::getenv ("XDG_CONFIG_HOME") != nullptr;
        setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
        unsetenv ("KAIA_CONFIG_DIR");
        unsetenv ("XDG_CONFIG_HOME");
        CHECK (UserFiles::configDirectory() == home.getChildFile (".config/kaia"), "Linux default: ~/.config/kaia ("
               << UserFiles::configDirectory().getFullPathName() << ")");
        setenv ("XDG_CONFIG_HOME", workDir.getChildFile ("xdg").getFullPathName().toRawUTF8(), 1);
        CHECK (UserFiles::configDirectory() == workDir.getChildFile ("xdg/kaia"), "honours XDG_CONFIG_HOME");
        setenv ("XDG_CONFIG_HOME", "relative/path", 1);
        CHECK (UserFiles::configDirectory() == home.getChildFile (".config/kaia"), "ignores a relative XDG_CONFIG_HOME");
        unsetenv ("XDG_CONFIG_HOME");

        // Kaia 1.0 wrote ~/Kaia/Kaia.settings: it moves to ~/.config/kaia/settings.xml
        home.getChildFile ("Kaia/Kaia.settings").create();
        home.getChildFile ("Kaia/Kaia.settings").replaceWithText ("<PROPERTIES><VALUE name=\"recent\" val=\"x\"/></PROPERTIES>");
        UserFiles::migrateLegacySettings();
        CHECK (home.getChildFile (".config/kaia/settings.xml").loadFileAsString().contains ("recent"), "legacy settings migrated");
        CHECK (! home.getChildFile ("Kaia").exists(), "empty legacy ~/Kaia folder removed");

        home.getChildFile ("Kaia").createDirectory();
        home.getChildFile ("Kaia/Kaia.settings").replaceWithText ("old");
        home.getChildFile ("Kaia/mine.txt").replaceWithText ("user file");
        UserFiles::migrateLegacySettings();
        CHECK (home.getChildFile ("Kaia/mine.txt").existsAsFile() && ! home.getChildFile ("Kaia/Kaia.settings").exists(),
               "legacy folder kept when it holds other files");
        CHECK (home.getChildFile (".config/kaia/settings.xml").loadFileAsString().contains ("recent"), "existing settings not overwritten");

        setenv ("HOME", oldHome.toRawUTF8(), 1);
        if (hadXdg)
            setenv ("XDG_CONFIG_HOME", oldXdg.toRawUTF8(), 1);
        setenv ("KAIA_CONFIG_DIR", configDir.getFullPathName().toRawUTF8(), 1);
    }
   #endif

    // Presets
    auto kit = Kit::makeEmpty();
    Layer l;
    l.sample = { workDir.getChildFile ("Library/One Shots/Kicks/Kick A.wav").getFullPathName(), {} };
    l.name = "Kick A";
    kit.pads[0].layers = { l };
    kit.pads[0].tuneSemis = -2.0f;
    Layer x;
    x.sample = { workDir.getChildFile ("Test Expansion.xpn").getFullPathName(), "[Samples]/XSnare.wav" };
    kit.pads[1].layers = { x };

    juce::String error;
    juce::File saved;
    CHECK (Presets::save (kit, "My Mix", error, &saved), "save preset: " << error);
    CHECK (saved == UserFiles::presetsDirectory().getChildFile ("My Mix.kaiapreset"), "preset file in the presets folder");
    CHECK (Presets::save (kit, "../../escape/attempt", error, &saved) && saved.isAChildOf (UserFiles::presetsDirectory()),
           "preset names can't leave the presets folder (" << saved.getFullPathName() << ")");
    auto list = Presets::list();
    CHECK (list.size() == 2, "presets listed (" << (int) list.size() << ")");

    Kit loaded;
    CHECK (Presets::load (UserFiles::presetsDirectory().getChildFile ("My Mix.kaiapreset"), loaded, error), "load preset: " << error);
    CHECK (loaded.name == "My Mix" && loaded.pads[0].tuneSemis == -2.0f && loaded.pads[1].layers[0].sample == x.sample,
           "preset keeps pads and references into the archive");
    auto r = KitLoader::reloadKit (loaded);
    CHECK (r.ok && r.kit.countSamples() == 2 && r.kit.countMissing() == 0, "preset samples load from file and archive");

    auto outside = workDir.getChildFile ("outside.kaiapreset");
    outside.replaceWithText ("x");
    CHECK (! Presets::remove (outside) && outside.existsAsFile(), "remove() refuses files outside the presets folder");
    CHECK (Presets::remove (UserFiles::presetsDirectory().getChildFile ("My Mix.kaiapreset")), "remove preset");
    CHECK (! Presets::load (outside, loaded, error), "non-preset rejected: " << error);
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::File realData;
    bool keep = false;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a == "--real-data" && i + 1 < argc) realData = juce::File (juce::String (argv[++i]));
        if (a == "--keep") keep = true;
        if (a == "--workdir" && i + 1 < argc) workDir = juce::File (juce::String (argv[++i]));
    }

    if (workDir == juce::File())
        workDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kaia-tests");
    workDir.deleteRecursively();
    workDir.createDirectory();

    // Never touch the real config folder
   #if JUCE_WINDOWS
    _putenv_s ("KAIA_CONFIG_DIR", workDir.getChildFile ("config").getFullPathName().toRawUTF8());
   #else
    setenv ("KAIA_CONFIG_DIR", workDir.getChildFile ("config").getFullPathName().toRawUTF8(), 1);
   #endif

    testXpmXml();
    testXpmJson();
    testXpn();
    testPgm1000();
    testPgm2000();
    testPgm3000();
    testKeygroup();
    testFolderAndEdgeCases();
    testTriggerMap();
    testPreview();
    testLibrary();
    testUserFiles();
    testRealFiles (realData);
    testPerformance();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    if (! keep)
        workDir.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
