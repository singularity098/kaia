#include "TriggerMap.h"
#include "UserFiles.h"

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
        case DrumZone::Crash1:      return "Crash 1";
        case DrumZone::HiHatOpen:   return "Hi-hat open";
        case DrumZone::Tom4:        return "Tom 4 (floor)";
        case DrumZone::Tom3:        return "Tom 3";
        case DrumZone::Tom2:        return "Tom 2";
        case DrumZone::Tom1:        return "Tom 1 (high)";
        case DrumZone::Ride:        return "Ride";
        case DrumZone::RideBell:    return "Ride bell";
        case DrumZone::Crash2Edge:  return "Crash 2 edge";
        case DrumZone::Crash2:      return "Crash 2";
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

    // Roland V-Drums before the TD-12 (TD-3 .. TD-10). Tom rims and the ride edge play the same
    // pad as the head/bow; crash 2's edge has its own pad. Hi-hat edges send 22 (closed) and
    // 26 (open); the open hi-hat also plays the pedal pad.
    DrumNoteTable rolandClassic()
    {
        return { { 36, Z::Kick }, { 38, Z::Snare }, { 40, Z::SnareRim },
                 { 48, Z::Tom1 }, { 50, Z::Tom1 }, { 45, Z::Tom2 }, { 47, Z::Tom2 },
                 { 43, Z::Tom3 }, { 58, Z::Tom3 }, { 41, Z::Tom4 }, { 39, Z::Tom4 },
                 { 42, Z::HiHatClosed }, { 22, Z::HiHatClosed },
                 { 46, Z::HiHatOpen }, { 46, Z::HiHatPedal }, { 26, Z::HiHatOpen }, { 26, Z::HiHatPedal },
                 { 44, Z::HiHatPedal },
                 { 49, Z::Crash1 }, { 55, Z::Crash1 }, { 57, Z::Crash2 }, { 52, Z::Crash2Edge },
                 { 51, Z::Ride }, { 59, Z::Ride }, { 53, Z::RideBell } };
    }

    // TD-10 (the owner's own module): the classic toms, snare and hi-hat, but the cymbals are
    // 55 -> crash 2, 49 -> ride, 52 and 10 -> crash 2 edge; 51, 53 and 57 play nothing.
    DrumNoteTable rolandTd10()
    {
        DrumNoteTable t;
        for (auto& e : rolandClassic())
            if (e.first < 49 || e.first == 50 || e.first == 52 || e.first == 58 || e.first == 59)
                t.push_back (e);
        t.push_back ({ 49, Z::Ride });
        t.push_back ({ 55, Z::Crash2 });
        t.push_back ({ 10, Z::Crash2Edge });
        return t;
    }

    // TD-9/11/12 and later: adds cross-stick.
    DrumNoteTable rolandModern()
    {
        auto t = rolandClassic();
        t.push_back ({ 37, Z::CrossStick });
        return t;
    }

    // Alesis modules follow the Roland layout.
    DrumNoteTable alesis()   { return rolandModern(); }

    // Yamaha DTX: toms 48 / 47 / 43 (DTX402 reference), open rim 40, closed rim 37, foot splash 83.
    DrumNoteTable yamaha()
    {
        return { { 36, Z::Kick }, { 38, Z::Snare }, { 40, Z::SnareRim }, { 37, Z::CrossStick },
                 { 48, Z::Tom1 }, { 50, Z::Tom1 }, { 47, Z::Tom2 }, { 45, Z::Tom3 }, { 43, Z::Tom3 }, { 41, Z::Tom4 },
                 { 42, Z::HiHatClosed }, { 46, Z::HiHatOpen }, { 46, Z::HiHatPedal }, { 44, Z::HiHatPedal }, { 83, Z::HiHatOpen },
                 { 49, Z::Crash1 }, { 55, Z::Crash1 }, { 57, Z::Crash2 }, { 52, Z::Crash2Edge },
                 { 51, Z::Ride }, { 59, Z::Ride }, { 53, Z::RideBell } };
    }

    // General MIDI drum map, for modules that default to it (and as a fallback).
    DrumNoteTable generalMidi()
    {
        return { { 35, Z::Kick }, { 36, Z::Kick }, { 38, Z::Snare }, { 40, Z::SnareRim }, { 37, Z::CrossStick },
                 { 50, Z::Tom1 }, { 48, Z::Tom1 }, { 47, Z::Tom2 }, { 45, Z::Tom2 }, { 43, Z::Tom3 }, { 41, Z::Tom4 },
                 { 42, Z::HiHatClosed }, { 46, Z::HiHatOpen }, { 46, Z::HiHatPedal }, { 44, Z::HiHatPedal },
                 { 49, Z::Crash1 }, { 55, Z::Crash1 }, { 57, Z::Crash2 }, { 52, Z::Crash2Edge },
                 { 51, Z::Ride }, { 59, Z::Ride }, { 53, Z::RideBell } };
    }

    std::vector<DrumModule> makeModules()
    {
        std::vector<DrumModule> m;
        auto add = [&m] (const char* id, const char* brand, const char* name, DrumNoteTable notes)
        {
            m.push_back ({ id, name, brand, std::move (notes) });
        };

        add ("roland-td3",   "Roland", "Roland TD-3",  rolandClassic());
        add ("roland-td6",   "Roland", "Roland TD-6",  rolandClassic());
        add ("roland-td8",   "Roland", "Roland TD-8",  rolandClassic());
        add ("roland-td10",  "Roland", "Roland TD-10", rolandTd10());
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

    bool validZone (int zone) { return zone >= 0 && zone < (int) DrumZone::NumZones; }

    juce::File customMapFile (const juce::String& name)
    {
        return UserFiles::drumMapsDirectory().getChildFile (UserFiles::safeFileName (name) + CustomDrumMaps::kExtension);
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
std::vector<DrumModule> CustomDrumMaps::list()
{
    std::vector<DrumModule> result;
    auto dir = UserFiles::drumMapsDirectory();
    if (! dir.isDirectory())
        return result;

    for (const auto& e : juce::RangedDirectoryIterator (dir, false, juce::String ("*") + kExtension, juce::File::findFiles))
    {
        auto xml = juce::parseXML (e.getFile());
        if (xml == nullptr || ! xml->hasTagName ("KaiaDrumMap"))
            continue;
        DrumModule m;
        m.name = xml->getStringAttribute ("name", e.getFile().getFileNameWithoutExtension()).trim();
        m.id = kIdPrefix + m.name;
        m.brand = "Custom";
        m.notes = TriggerMap::notesFromString (xml->getStringAttribute ("notes"));
        if (m.name.isNotEmpty())
            result.push_back (std::move (m));
    }
    std::sort (result.begin(), result.end(), [] (const DrumModule& a, const DrumModule& b) { return a.name.compareNatural (b.name) < 0; });
    return result;
}

bool CustomDrumMaps::load (const juce::String& name, DrumNoteTable& notes)
{
    for (auto& m : list())
        if (m.name == name)
        {
            notes = m.notes;
            return true;
        }
    return false;
}

bool CustomDrumMaps::save (const juce::String& name, const DrumNoteTable& notes, juce::String& error)
{
    auto trimmed = name.trim();
    if (trimmed.isEmpty())
    {
        error = "The drum map needs a name.";
        return false;
    }
    auto file = customMapFile (trimmed);
    if (! file.getParentDirectory().createDirectory())
    {
        error = "Can't create " + file.getParentDirectory().getFullPathName();
        return false;
    }
    juce::XmlElement xml ("KaiaDrumMap");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("name", trimmed);
    xml.setAttribute ("notes", TriggerMap::notesToString (notes));
    if (! xml.writeTo (file))
    {
        error = "Can't write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool CustomDrumMaps::remove (const juce::String& name)
{
    auto file = customMapFile (name.trim());
    return file.isAChildOf (UserFiles::drumMapsDirectory()) && file.existsAsFile() && file.deleteFile();
}

//==============================================================================
bool TriggerMap::isEdited() const
{
    auto* m = findDrumModule (moduleId);
    return m != nullptr && m->notes != notes;
}

juce::String TriggerMap::moduleName() const
{
    if (isCustom())
        return moduleId.fromFirstOccurrenceOf (CustomDrumMaps::kIdPrefix, false, false);
    if (auto* m = findDrumModule (moduleId))
        return m->name;
    return {};
}

bool TriggerMap::selectModule (const juce::String& id)
{
    if (id.isEmpty())
    {
        moduleId = {};
        notes.clear();
        return true;
    }
    if (auto* m = findDrumModule (id))
    {
        moduleId = id;
        notes = m->notes;
        return true;
    }
    if (id.startsWith (CustomDrumMaps::kIdPrefix))
    {
        DrumNoteTable table;
        if (CustomDrumMaps::load (id.fromFirstOccurrenceOf (CustomDrumMaps::kIdPrefix, false, false), table))
        {
            moduleId = id;
            notes = std::move (table);
            return true;
        }
    }
    return false;
}

void TriggerMap::resetToFactory()
{
    if (auto* m = findDrumModule (moduleId))
        notes = m->notes;
}

std::vector<int> TriggerMap::notesForZone (int zone) const
{
    std::vector<int> result;
    for (auto& [note, z] : notes)
        if ((int) z == zone && std::find (result.begin(), result.end(), note) == result.end())
            result.push_back (note);
    return result;
}

void TriggerMap::setZoneNotes (int zone, const std::vector<int>& newNotes)
{
    if (! validZone (zone))
        return;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [zone] (auto& e) { return (int) e.second == zone; }), notes.end());
    for (auto n : newNotes)
        if (n >= 0 && n < 128 && std::find (notes.begin(), notes.end(), std::make_pair (n, (DrumZone) zone)) == notes.end())
            notes.push_back ({ n, (DrumZone) zone });
}

void TriggerMap::moveNoteToZone (int note, int zone)
{
    if (! validZone (zone) || note < 0 || note > 127)
        return;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [note] (auto& e) { return e.first == note; }), notes.end());
    notes.push_back ({ note, (DrumZone) zone });
}

void TriggerMap::removeNote (int note, int zone)
{
    notes.erase (std::remove (notes.begin(), notes.end(), std::make_pair (note, (DrumZone) zone)), notes.end());
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

    auto mod = usesModule();
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

        if (mod)
        {
            for (auto& [note, zone] : notes)
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

juce::String TriggerMap::notesToString (const DrumNoteTable& table)
{
    juce::StringArray parts;
    for (auto& [note, zone] : table)
        parts.add (juce::String (note) + ":" + juce::String ((int) zone));
    return parts.joinIntoString (",");
}

DrumNoteTable TriggerMap::notesFromString (const juce::String& s)
{
    DrumNoteTable table;
    for (auto& part : juce::StringArray::fromTokens (s, ",", {}))
    {
        if (! part.containsChar (':'))
            continue;
        auto note = part.upToFirstOccurrenceOf (":", false, false).trim().getIntValue();
        auto zone = part.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
        auto entry = std::make_pair (note, (DrumZone) zone);
        if (note >= 0 && note < 128 && validZone (zone) && std::find (table.begin(), table.end(), entry) == table.end())
            table.push_back (entry);
    }
    return table;
}

void TriggerMap::restore (const juce::String& id, const juce::String& noteTable)
{
    moduleId = {};
    notes.clear();
    if (id.isEmpty())
        return;

    // A stored table wins (edited or custom maps, and projects from another computer); otherwise
    // the module's current factory notes or the saved custom map.
    auto table = notesFromString (noteTable);
    if (! table.empty() && (findDrumModule (id) != nullptr || id.startsWith (CustomDrumMaps::kIdPrefix)))
    {
        moduleId = id;
        notes = std::move (table);
        return;
    }
    selectModule (id);
}

std::unique_ptr<juce::XmlElement> TriggerMap::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("TriggerMap");
    xml->setAttribute ("module", moduleId);
    if (usesModule())
        xml->setAttribute ("notes", notesToString (notes));
    xml->setAttribute ("hiHatChoke", hiHatChoke ? 1 : 0);
    xml->setAttribute ("learned", learnedToString());
    return xml;
}

TriggerMap TriggerMap::fromXml (const juce::XmlElement& xml)
{
    TriggerMap m;
    m.restore (xml.getStringAttribute ("module"), xml.getStringAttribute ("notes"));
    m.hiHatChoke = xml.getIntAttribute ("hiHatChoke", 1) != 0;
    m.learnedFromString (xml.getStringAttribute ("learned"));
    return m;
}

}
