#include "TriggerMap.h"

namespace kaia
{

juce::String drumZoneName (DrumZone zone)
{
    switch (zone)
    {
        case DrumZone::Kick:        return "Kick";
        case DrumZone::Snare:       return "Snare";
        case DrumZone::HiHatClosed: return "Hi-hat closed";
        case DrumZone::HiHatPedal:  return "Hi-hat pedal";
        case DrumZone::SnareRim:    return "Snare rim";
        case DrumZone::CrossStick:  return "Cross-stick";
        case DrumZone::Aux:         return "Aux / percussion";
        case DrumZone::HiHatOpen:   return "Hi-hat open";
        case DrumZone::Tom1:        return "Tom 1";
        case DrumZone::Tom2:        return "Tom 2";
        case DrumZone::Tom3:        return "Tom 3";
        case DrumZone::Tom4:        return "Tom 4";
        case DrumZone::Crash1:      return "Crash 1";
        case DrumZone::Crash2:      return "Crash 2";
        case DrumZone::Ride:        return "Ride";
        case DrumZone::RideBell:    return "Ride bell";
        case DrumZone::NumZones:    break;
    }
    return {};
}

int drumZonePad (DrumZone zone)
{
    // The enum is declared in pad order
    return (int) zone;
}

namespace
{
    using Z = DrumZone;
    using NoteTable = std::vector<std::pair<int, DrumZone>>;

    // Roland V-Drums before the TD-12: one zone per hi-hat state, rims on their own notes.
    // Tom rims, cymbal edges and the ride edge play the same pad as the head/bow.
    NoteTable rolandClassic()
    {
        return { { 36, Z::Kick }, { 38, Z::Snare }, { 40, Z::SnareRim },
                 { 48, Z::Tom1 }, { 50, Z::Tom1 }, { 45, Z::Tom2 }, { 47, Z::Tom2 },
                 { 43, Z::Tom3 }, { 58, Z::Tom3 }, { 41, Z::Tom4 }, { 39, Z::Tom4 },
                 { 42, Z::HiHatClosed }, { 46, Z::HiHatOpen }, { 44, Z::HiHatPedal },
                 { 49, Z::Crash1 }, { 55, Z::Crash1 }, { 57, Z::Crash2 }, { 52, Z::Crash2 },
                 { 51, Z::Ride }, { 59, Z::Ride }, { 53, Z::RideBell } };
    }

    // TD-9/11/12 and later: adds cross-stick and the hi-hat edge notes.
    NoteTable rolandModern()
    {
        auto t = rolandClassic();
        t.push_back ({ 37, Z::CrossStick });
        t.push_back ({ 22, Z::HiHatClosed });
        t.push_back ({ 26, Z::HiHatOpen });
        return t;
    }

    // Alesis modules follow the Roland layout.
    NoteTable alesis()   { return rolandModern(); }

    // Yamaha DTX: toms 48 / 47 / 43 (DTX402 reference), open rim 40, closed rim 37, foot splash 83.
    NoteTable yamaha()
    {
        return { { 36, Z::Kick }, { 38, Z::Snare }, { 40, Z::SnareRim }, { 37, Z::CrossStick },
                 { 48, Z::Tom1 }, { 50, Z::Tom1 }, { 47, Z::Tom2 }, { 45, Z::Tom3 }, { 43, Z::Tom3 }, { 41, Z::Tom4 },
                 { 42, Z::HiHatClosed }, { 46, Z::HiHatOpen }, { 44, Z::HiHatPedal }, { 83, Z::HiHatOpen },
                 { 49, Z::Crash1 }, { 55, Z::Crash1 }, { 57, Z::Crash2 }, { 52, Z::Crash2 },
                 { 51, Z::Ride }, { 59, Z::Ride }, { 53, Z::RideBell } };
    }

    // General MIDI drum map, for modules that default to it (and as a fallback).
    NoteTable generalMidi()
    {
        return { { 35, Z::Kick }, { 36, Z::Kick }, { 38, Z::Snare }, { 40, Z::SnareRim }, { 37, Z::CrossStick },
                 { 50, Z::Tom1 }, { 48, Z::Tom1 }, { 47, Z::Tom2 }, { 45, Z::Tom2 }, { 43, Z::Tom3 }, { 41, Z::Tom4 },
                 { 42, Z::HiHatClosed }, { 46, Z::HiHatOpen }, { 44, Z::HiHatPedal },
                 { 49, Z::Crash1 }, { 55, Z::Crash1 }, { 57, Z::Crash2 }, { 52, Z::Crash2 },
                 { 51, Z::Ride }, { 59, Z::Ride }, { 53, Z::RideBell },
                 { 39, Z::Aux }, { 54, Z::Aux }, { 56, Z::Aux } };
    }

    std::vector<DrumModule> makeModules()
    {
        std::vector<DrumModule> m;
        auto add = [&m] (const char* id, const char* brand, const char* name, NoteTable notes)
        {
            m.push_back ({ id, name, brand, std::move (notes) });
        };

        add ("roland-td3",   "Roland", "Roland TD-3",  rolandClassic());
        add ("roland-td6",   "Roland", "Roland TD-6",  rolandClassic());
        add ("roland-td8",   "Roland", "Roland TD-8",  rolandClassic());
        add ("roland-td10",  "Roland", "Roland TD-10", rolandClassic());
        add ("roland-td1",   "Roland", "Roland TD-1",  rolandModern());
        add ("roland-td02",  "Roland", "Roland TD-02", rolandModern());
        add ("roland-td07",  "Roland", "Roland TD-07", rolandModern());
        add ("roland-td4",   "Roland", "Roland TD-4",  rolandModern());
        add ("roland-td9",   "Roland", "Roland TD-9",  rolandModern());
        add ("roland-td11",  "Roland", "Roland TD-11", rolandModern());
        add ("roland-td12",  "Roland", "Roland TD-12", rolandModern());
        add ("roland-td15",  "Roland", "Roland TD-15", rolandModern());
        add ("roland-td17",  "Roland", "Roland TD-17", rolandModern());
        add ("roland-td20",  "Roland", "Roland TD-20", rolandModern());
        add ("roland-td25",  "Roland", "Roland TD-25", rolandModern());
        add ("roland-td27",  "Roland", "Roland TD-27", rolandModern());
        add ("roland-td30",  "Roland", "Roland TD-30", rolandModern());
        add ("roland-td50",  "Roland", "Roland TD-50 / TD-50X", rolandModern());
        add ("roland-vad",   "Roland", "Roland V71 / V51 / V31 (VAD)", rolandModern());

        add ("alesis-nitro",   "Alesis", "Alesis Nitro / Nitro Max / Nitro Pro", alesis());
        add ("alesis-turbo",   "Alesis", "Alesis Turbo / Debut", alesis());
        add ("alesis-surge",   "Alesis", "Alesis Surge / Command", alesis());
        add ("alesis-crimson", "Alesis", "Alesis Crimson II", alesis());
        add ("alesis-strike",  "Alesis", "Alesis Strike / Strike Pro", alesis());
        add ("alesis-strata",  "Alesis", "Alesis Strata", alesis());
        add ("alesis-dm10",    "Alesis", "Alesis DM10", alesis());

        add ("yamaha-dtx400",  "Yamaha", "Yamaha DTX400 / DTX402 series", yamaha());
        add ("yamaha-dtx500",  "Yamaha", "Yamaha DTX502 series", yamaha());
        add ("yamaha-dtx700",  "Yamaha", "Yamaha DTX700 / DTX900", yamaha());
        add ("yamaha-dtxpro",  "Yamaha", "Yamaha DTX-PRO / DTX-PROX", yamaha());
        add ("yamaha-dtx6",    "Yamaha", "Yamaha DTX6 / DTX8 / DTX10", yamaha());

        add ("gm",             "Other",  "General MIDI drum map (EFNOTE, Simmons, ATV, Millenium, ...)", generalMidi());
        return m;
    }
}

const std::vector<DrumModule>& drumModules()
{
    static const std::vector<DrumModule> modules = makeModules();
    return modules;
}

const DrumModule* findDrumModule (const juce::String& id)
{
    if (id.isEmpty())
        return nullptr;
    for (auto& m : drumModules())
        if (m.id == id)
            return &m;
    return nullptr;
}

//==============================================================================
std::array<std::vector<int>, 128> TriggerMap::resolve (const Kit& kit) const
{
    std::array<std::vector<int>, 128> result;
    auto numPads = (int) kit.pads.size();

    // The program's own notes
    auto programNotes = [&] (int note, std::vector<int>& out)
    {
        for (int i = 0; i < numPads; ++i)
            if (kit.pads[(size_t) i].respondsTo (note))
                out.push_back (i);
    };

    if (kit.isKeygroupProgram)
    {
        for (int n = 0; n < 128; ++n)
            programNotes (n, result[(size_t) n]);
        return result;
    }

    auto* mod = module();
    for (int n = 0; n < 128; ++n)
    {
        auto& out = result[(size_t) n];
        auto l = learned.find (n);
        if (l != learned.end())
        {
            if (l->second >= 0 && l->second < numPads)
                out.push_back (l->second);
            continue;
        }

        if (mod != nullptr)
        {
            for (auto& [note, zone] : mod->notes)
                if (note == n)
                {
                    auto pad = drumZonePad (zone);
                    if (pad < numPads && std::find (out.begin(), out.end(), pad) == out.end())
                        out.push_back (pad);
                }
            continue;
        }

        programNotes (n, out);
    }
    return result;
}

std::vector<int> TriggerMap::notesForPad (const Kit& kit, int padIndex) const
{
    std::vector<int> notes;
    auto table = resolve (kit);
    for (int n = 0; n < 128; ++n)
        if (std::find (table[(size_t) n].begin(), table[(size_t) n].end(), padIndex) != table[(size_t) n].end())
            notes.push_back (n);
    return notes;
}

void TriggerMap::learn (int note, int padIndex)
{
    if (note < 0 || note > 127 || padIndex < 0)
        return;
    learned[note] = padIndex;
}

void TriggerMap::forgetPad (int padIndex)
{
    for (auto it = learned.begin(); it != learned.end();)
        it = it->second == padIndex ? learned.erase (it) : std::next (it);
}

juce::String TriggerMap::learnedToString() const
{
    juce::StringArray parts;
    for (auto& [note, pad] : learned)
        parts.add (juce::String (note) + ":" + juce::String (pad));
    return parts.joinIntoString (",");
}

void TriggerMap::learnedFromString (const juce::String& s)
{
    learned.clear();
    for (auto& part : juce::StringArray::fromTokens (s, ",", {}))
    {
        if (! part.containsChar (':'))
            continue;
        auto note = part.upToFirstOccurrenceOf (":", false, false).trim().getIntValue();
        auto pad = part.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
        if (note >= 0 && note < 128 && pad >= 0 && pad < kMaxPads)
            learned[note] = pad;
    }
}

std::unique_ptr<juce::XmlElement> TriggerMap::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("TriggerMap");
    xml->setAttribute ("module", moduleId);
    xml->setAttribute ("hiHatChoke", hiHatChoke ? 1 : 0);
    xml->setAttribute ("learned", learnedToString());
    return xml;
}

TriggerMap TriggerMap::fromXml (const juce::XmlElement& xml)
{
    TriggerMap m;
    m.moduleId = xml.getStringAttribute ("module");
    if (findDrumModule (m.moduleId) == nullptr)
        m.moduleId = {};
    m.hiHatChoke = xml.getIntAttribute ("hiHatChoke", 1) != 0;
    m.learnedFromString (xml.getStringAttribute ("learned"));
    return m;
}

}
