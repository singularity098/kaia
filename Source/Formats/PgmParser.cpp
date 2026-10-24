#include "Parsers.h"

namespace kaia
{

float classicEnvTimeMs (int v)
{
    // MPC2000XL firmware curve (also used for the MPC1000 family): (v^2 + 1) / 2 ms, 0..100 -> 0.5 ms .. 5 s
    v = juce::jlimit (0, 100, v);
    return (float) (v * v + 1) / 2.0f;
}

float classicCutoffHz (int v)
{
    // 0 is about 70 Hz, 100 is fully open
    v = juce::jlimit (0, 100, v);
    return (float) (70.0 * std::pow (22000.0 / 70.0, v / 100.0));
}

namespace
{
juce::uint16 rd16 (const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
juce::int16  rds16 (const juce::uint8* p) { return (juce::int16) rd16 (p); }

juce::String fixedString (const juce::uint8* p, int maxLen)
{
    int len = 0;
    while (len < maxLen && p[len] != 0 && p[len] != 0xff)
        ++len;
    juce::String s;
    for (int i = 0; i < len; ++i)
        s += (juce::juce_wchar) (p[i] < 32 ? ' ' : p[i]);
    return s.trimEnd();
}

float levelToDb (int level0to100)
{
    if (level0to100 <= 0)
        return -100.0f;
    return (float) (20.0 * std::log10 (level0to100 / 100.0));
}

//==============================================================================
// MPC500 / MPC1000 / MPC2500 — fixed 10756 byte file, 64 pads x 4 samples, WAV samples
bool parseMpc1000 (const juce::uint8* d, size_t size, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    constexpr size_t kPadStart = 0x18, kPadLen = 0xA4, kPadNotes = 0x2918;
    if (size < kPadNotes + 64)
    {
        error = "The PGM file is truncated.";
        return false;
    }

    auto header = fixedString (d + 4, 16);
    kit.formatName = header.startsWith ("MPC") ? "Akai " + header.upToFirstOccurrenceOf (" PGM", false, false) + " program (PGM)"
                                               : juce::String ("Akai MPC1000 program (PGM)");
    kit.pads.resize (kMaxPads);
    for (int i = 0; i < kMaxPads; ++i)
        kit.pads[(size_t) i].note = defaultPadNote (i);

    for (int p = 0; p < 64; ++p)
    {
        auto* pd = d + kPadStart + (size_t) p * kPadLen;
        auto& pad = kit.pads[(size_t) p];

        auto note = d[kPadNotes + (size_t) p];
        if (note < 128)
            pad.note = note;

        bool anyNoteOn = false, anyOneShot = false;
        for (int s = 0; s < 4; ++s)
        {
            auto* sd = pd + s * 0x18;
            auto name = fixedString (sd, 16);
            if (name.isEmpty())
                continue;

            Layer l;
            l.name      = name;
            l.gainDb    = levelToDb (sd[0x11]);
            l.velLow    = juce::jlimit (0, 127, (int) sd[0x12]);
            l.velHigh   = juce::jlimit (0, 127, (int) sd[0x13]);
            if (l.velHigh < l.velLow)
                std::swap (l.velLow, l.velHigh);
            l.tuneSemis = (float) rds16 (sd + 0x14) / 100.0f;
            if (sd[0x16] == 1) anyNoteOn = true; else anyOneShot = true;
            pad.layers.push_back (l);
        }

        pad.trigger    = (anyNoteOn && ! anyOneShot) ? TriggerMode::NoteOn : TriggerMode::OneShot;
        pad.layerMode  = LayerMode::Velocity;   // all samples in range play together
        pad.mono       = pd[0x62] == 1;
        pad.muteGroup  = juce::jlimit (0, 32, (int) pd[0x63]);

        pad.env.adMode       = true;
        pad.env.attackMs     = classicEnvTimeMs (pd[0x66]);
        pad.env.decayMs      = classicEnvTimeMs (pd[0x67]);
        pad.env.decayFromEnd = pd[0x68] == 0;
        pad.velToLevel       = juce::jlimit (0.0f, 1.0f, pd[0x6B] / 100.0f);

        switch (pd[0x71])
        {
            case 1:  pad.filter = FilterType::LP12; break;
            case 2:  pad.filter = FilterType::BP12; break;
            case 3:  pad.filter = FilterType::HP12; break;
            default: pad.filter = FilterType::Off;  break;
        }
        pad.cutoffHz    = classicCutoffHz (pd[0x72]);
        pad.resonance   = juce::jlimit (0.0f, 1.0f, pd[0x73] / 100.0f);
        pad.velToCutoff = juce::jlimit (0.0f, 1.0f, pd[0x78] / 100.0f);

        pad.volumeDb = levelToDb (pd[0x8F]);
        pad.pan      = juce::jlimit (-1.0f, 1.0f, ((int) pd[0x90] - 50) / 50.0f);
    }

    juce::ignoreUnused (warnings);
    return true;
}

//==============================================================================
// Shared conversion for the MPC2000XL / MPC3000 note-based programs
struct ClassicNote
{
    int  sound = -1;            // index into sound name table
    int  genMode = 0;           // 0 normal, 1 simult, 2 vel sw, 3 decay sw
    int  velSw1 = 0, alsoNote1 = 0, velSw2 = 0, alsoNote2 = 0;
    int  overlap = 0;           // 0 poly, 1 mono, 2 note off
    int  mute1 = 0, mute2 = 0;
    int  tuneTenths = 0;
    int  attack = 0, decay = 0, decayMode = 0;
    int  cutoff = 100, resonance = 0;   // resonance 0..15
    int  velToLevel = 100, velToCutoff = 0;
    int  mixVolume = 100, mixPan = 50;
};

void buildClassicKit (Kit& kit, const juce::StringArray& soundNames, const ClassicNote (&notes)[64],
                      const int (&padToNote)[64], juce::StringArray& warnings)
{
    auto soundName = [&] (int idx) -> juce::String
    {
        return (idx >= 0 && idx < soundNames.size()) ? soundNames[idx] : juce::String();
    };
    auto noteSound = [&] (int midiNote) -> juce::String
    {
        if (midiNote < 35 || midiNote > 98)
            return {};
        return soundName (notes[midiNote - 35].sound);
    };

    kit.pads.resize (kMaxPads);
    for (int i = 0; i < kMaxPads; ++i)
        kit.pads[(size_t) i].note = defaultPadNote (i);

    bool usedNote[128] = {};
    std::vector<int> padOrder;   // notes in the order they get a pad
    for (int p = 0; p < 64; ++p)
    {
        auto n = padToNote[p];
        if (n >= 35 && n <= 98 && ! usedNote[n])
        {
            usedNote[n] = true;
            padOrder.push_back (n);
        }
        else
        {
            padOrder.push_back (-1);
        }
    }
    // Notes that have a sound but no pad still respond to MIDI: put them on banks E+
    for (int n = 35; n <= 98; ++n)
        if (! usedNote[n] && notes[n - 35].sound >= 0 && soundName (notes[n - 35].sound).isNotEmpty())
            padOrder.push_back (n);

    bool warnedDecaySwitch = false;
    for (size_t p = 0; p < padOrder.size() && p < (size_t) kMaxPads; ++p)
    {
        auto& pad = kit.pads[p];
        auto n = padOrder[p];
        if (n < 0)
        {
            pad.note = padToNote[p] >= 0 && padToNote[p] < 128 ? padToNote[p] : -1;
            continue;
        }

        auto& nt = notes[n - 35];
        pad.note = n;

        auto addLayer = [&] (const juce::String& name, int lo, int hi)
        {
            if (name.isEmpty() || lo > hi)
                return;
            Layer l;
            l.name = name;
            l.velLow = lo;
            l.velHigh = hi;
            l.tuneSemis = (float) nt.tuneTenths / 10.0f;
            pad.layers.push_back (l);
        };

        auto own = soundName (nt.sound);
        switch (nt.genMode)
        {
            case 1: // simultaneous: this sound plus up to two other notes' sounds
                addLayer (own, 0, 127);
                addLayer (noteSound (nt.alsoNote1), 0, 127);
                addLayer (noteSound (nt.alsoNote2), 0, 127);
                break;

            case 2: // velocity switch: velocity > threshold 1 plays note 1's sound, > threshold 2 plays note 2's
            {
                auto s1 = noteSound (nt.alsoNote1);
                auto s2 = noteSound (nt.alsoNote2);
                auto a = s1.isEmpty() ? 128 : juce::jlimit (1, 128, nt.velSw1 + 1);   // first velocity for sound 1
                auto b = s2.isEmpty() ? 128 : juce::jlimit (1, 128, nt.velSw2 + 1);   // first velocity for sound 2
                addLayer (own, 0, juce::jmin (a, b) - 1);
                addLayer (s1, a, b - 1);
                addLayer (s2, b, 127);
                break;
            }

            case 3:
                if (! warnedDecaySwitch)
                    warnings.add ("Decay-switch notes are played as normal notes.");
                warnedDecaySwitch = true;
                addLayer (own, 0, 127);
                break;

            default:
                addLayer (own, 0, 127);
                break;
        }

        pad.layerMode  = LayerMode::Velocity;
        pad.trigger    = nt.overlap == 2 ? TriggerMode::NoteOn : TriggerMode::OneShot;
        pad.mono       = nt.overlap == 1;
        pad.muteTargets = { nt.mute1 >= 35 && nt.mute1 <= 98 ? nt.mute1 : -1,
                            nt.mute2 >= 35 && nt.mute2 <= 98 ? nt.mute2 : -1, -1, -1 };

        pad.env.adMode       = true;
        pad.env.attackMs     = classicEnvTimeMs (nt.attack);
        pad.env.decayMs      = classicEnvTimeMs (nt.decay);
        pad.env.decayFromEnd = nt.decayMode == 0;
        pad.velToLevel       = juce::jlimit (0.0f, 1.0f, nt.velToLevel / 100.0f);

        if (nt.cutoff < 100)
        {
            pad.filter    = FilterType::LP12;
            pad.cutoffHz  = classicCutoffHz (nt.cutoff);
            pad.resonance = juce::jlimit (0.0f, 1.0f, nt.resonance / 15.0f);
        }
        pad.velToCutoff = juce::jlimit (0.0f, 1.0f, nt.velToCutoff / 100.0f);

        pad.volumeDb = levelToDb (nt.mixVolume);
        pad.pan      = juce::jlimit (-1.0f, 1.0f, (nt.mixPan - 50) / 50.0f);
    }
}

//==============================================================================
// MPC2000 / MPC2000XL — note-based program, .SND (or .WAV) samples
bool parseMpc2000 (const juce::uint8* d, size_t size, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    size_t pos = 2;
    if (size < 4)
    {
        error = "The PGM file is truncated.";
        return false;
    }
    auto soundCount = (int) rd16 (d + pos);
    pos += 2;

    auto need = pos + (size_t) soundCount * 17 + 2 + 17 + 9 + 1 + 5 + 64 * 25 + 1 + 64 * 6 + 3 + 64;
    if (soundCount > 256 || size < need)
    {
        error = "The MPC2000XL program is truncated or damaged.";
        return false;
    }

    juce::StringArray soundNames;
    for (int i = 0; i < soundCount; ++i, pos += 17)
        soundNames.add (fixedString (d + pos, 16));

    pos += 2;
    kit.name = fixedString (d + pos, 16);
    pos += 17;
    pos += 9;   // slider
    pos += 1;   // program change
    pos += 5;

    ClassicNote notes[64];
    for (int i = 0; i < 64; ++i, pos += 25)
    {
        auto* n = d + pos;
        auto& nt = notes[i];
        nt.sound      = n[0] == 0xff ? -1 : n[0];
        nt.genMode    = n[1];
        nt.velSw1     = n[2];
        nt.alsoNote1  = n[3];
        nt.velSw2     = n[4];
        nt.alsoNote2  = n[5];
        nt.overlap    = n[6];
        nt.mute1      = n[7];
        nt.mute2      = n[8];
        nt.tuneTenths = rds16 (n + 9);
        nt.attack     = n[11];
        nt.decay      = n[12];
        nt.decayMode  = n[13];
        nt.cutoff     = n[14];
        nt.resonance  = n[15];
        nt.velToLevel = n[19];
        nt.velToCutoff = n[22];
    }

    pos += 1;
    for (int i = 0; i < 64; ++i, pos += 6)
    {
        notes[i].mixVolume = d[pos + 1];
        notes[i].mixPan    = d[pos + 2];
    }

    pos += 3;
    int padToNote[64];
    for (int i = 0; i < 64; ++i)
        padToNote[i] = (juce::int8) d[pos + (size_t) i];

    kit.formatName = "Akai MPC2000XL program (PGM)";
    buildClassicKit (kit, soundNames, notes, padToNote, warnings);
    return true;
}

//==============================================================================
// MPC3000 (PGM version 3) — 4608 bytes, .SND samples
bool parseMpc3000 (const juce::uint8* d, size_t size, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    constexpr size_t kNames = 2, kName = 2 + 128 * 17 + 128 * 4, kNotes = kName + 17 + 9 + 36;
    constexpr size_t kMixers = kNotes + 64 * 24, kPads = kMixers + 64 * 4;
    if (size < kPads + 64)
    {
        error = "The MPC3000 program is truncated or damaged.";
        return false;
    }

    juce::StringArray soundNames;
    for (int i = 0; i < 128; ++i)
        soundNames.add (fixedString (d + kNames + (size_t) i * 17, 16));

    kit.name = fixedString (d + kName, 16);

    ClassicNote notes[64];
    for (int i = 0; i < 64; ++i)
    {
        auto* n = d + kNotes + (size_t) i * 24;
        auto& nt = notes[i];
        nt.sound      = n[0] == 0xff ? -1 : n[0];
        nt.genMode    = n[1];
        nt.velSw1     = n[2];
        nt.alsoNote1  = n[3];
        nt.velSw2     = n[4];
        nt.alsoNote2  = n[5];
        nt.overlap    = n[6];
        nt.mute1      = n[7];
        nt.mute2      = n[8];
        nt.tuneTenths = rds16 (n + 9);
        nt.attack     = n[11];
        nt.decay      = n[12];
        nt.decayMode  = n[13];
        nt.cutoff     = n[14];
        nt.resonance  = n[15];
        nt.velToLevel = n[19];
        nt.velToCutoff = n[22];

        auto* m = d + kMixers + (size_t) i * 4;
        nt.mixVolume = m[0];
        nt.mixPan    = m[1];
    }

    int padToNote[64];
    for (int i = 0; i < 64; ++i)
        padToNote[i] = d[kPads + (size_t) i];

    kit.formatName = "Akai MPC3000 program (PGM)";
    buildClassicKit (kit, soundNames, notes, padToNote, warnings);
    return true;
}

} // namespace

bool parsePgm (const juce::MemoryBlock& data, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    auto size = data.getSize();

    if (size >= 20)
    {
        auto header = fixedString (d + 4, 16);
        if (header.contains ("PGM"))
            return parseMpc1000 (d, size, kit, warnings, error);
    }

    if (size >= 2 && d[0] == 0x07 && d[1] == 0x04)
        return parseMpc2000 (d, size, kit, warnings, error);

    if (size >= 2 && d[0] == 0x07 && d[1] == 0x00)
        return parseMpc3000 (d, size, kit, warnings, error);

    error = "Unrecognised PGM file. Supported: MPC500/1000/2500, MPC2000/2000XL and MPC3000 programs.";
    return false;
}

}
