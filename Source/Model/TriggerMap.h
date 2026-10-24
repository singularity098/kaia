#pragma once

#include "Kit.h"
#include <map>

namespace kaia
{

// The drum voices of an electronic drum kit. Each zone plays a fixed pad of bank A, so any kit
// laid out this way plays correctly from any supported drum module:
//
//   A13 Ride            A14 Ride bell  A15 Crash 2 edge  A16 Crash 2
//   A09 Tom 4 (floor)   A10 Tom 3      A11 Tom 2         A12 Tom 1 (high)
//   A05 Snare rim       A06 Cross-stick A07 Crash 1      A08 Hi-hat open
//   A01 Kick            A02 Snare      A03 Hi-hat closed A04 Hi-hat pedal
//
// The open hi-hat notes also play A04, so an open hi-hat can be layered from two pads.
enum class DrumZone : int
{
    Kick, Snare, HiHatClosed, HiHatPedal,
    SnareRim, CrossStick, Crash1, HiHatOpen,
    Tom4, Tom3, Tom2, Tom1,
    Ride, RideBell, Crash2Edge, Crash2,
    NumZones
};

juce::String drumZoneName (DrumZone zone);
int drumZonePad (DrumZone zone);                  // pad index (0 = A01)

using DrumNoteTable = std::vector<std::pair<int, DrumZone>>;   // MIDI note -> zone (several notes per zone)

struct DrumModule
{
    juce::String id;                              // stable, stored in settings and projects
    juce::String name;                            // e.g. "Roland TD-10"
    juce::String brand;
    DrumNoteTable notes;                          // factory default notes
};

const std::vector<DrumModule>& drumModules();     // built-in modules
const DrumModule* findDrumModule (const juce::String& id);

// Mute group the hi-hat pads share while a drum module map is active (outside the 1..32 the UI offers).
constexpr int kHiHatChokeGroup = 33;

// Drum maps the user saved from the settings (drum-maps/ in the config folder). Their module id
// is "custom:<name>".
namespace CustomDrumMaps
{
    constexpr const char* kIdPrefix = "custom:";
    constexpr const char* kExtension = ".kaiamap";

    std::vector<DrumModule> list();               // sorted by name
    bool load (const juce::String& name, DrumNoteTable& notes);
    bool save (const juce::String& name, const DrumNoteTable& notes, juce::String& error);
    bool remove (const juce::String& name);       // only deletes inside drum-maps/
}

// Decides which pads an incoming MIDI note plays. Without a module and without learned notes the
// program's own note map is used, unchanged. With a module, its notes play the zone pads above and
// other notes are ignored. Learned notes (MIDI Learn on a pad) win over both.
// Keygroup programs always use their own note ranges.
struct TriggerMap
{
    juce::String moduleId;          // built-in id, "custom:<name>", or empty: the program's note map
    DrumNoteTable notes;            // with a module: the notes that play each zone (editable)
    bool hiHatChoke = true;         // module maps: closed/pedal hi-hat cut the open hi-hat
    std::map<int, int> learned;     // MIDI note -> pad index

    bool usesModule() const          { return moduleId.isNotEmpty(); }
    bool isDefault() const           { return ! usesModule() && learned.empty(); }
    bool isCustom() const            { return moduleId.startsWith (CustomDrumMaps::kIdPrefix); }
    bool isEdited() const;           // a built-in module whose notes differ from its factory table
    juce::String moduleName() const; // "Roland TD-10", the custom map's name, or empty

    // Picks a built-in module or a saved custom map and loads its notes ("" = no module).
    // Returns false (and leaves the map unchanged) if the id is unknown.
    bool selectModule (const juce::String& id);
    void resetToFactory();                          // built-in modules: back to the factory notes

    // Editing the module's note table (zone = pad index 0..15)
    std::vector<int> notesForZone (int zone) const;
    void setZoneNotes (int zone, const std::vector<int>& notes);   // replaces the zone's notes
    void moveNoteToZone (int note, int zone);       // the note plays only this zone from now on
    void removeNote (int note, int zone);

    // For each MIDI note, the pads it plays (pads without samples included).
    std::array<std::vector<int>, 128> resolve (const Kit& kit) const;
    std::vector<int> notesForPad (const Kit& kit, int padIndex) const;

    void learn (int note, int padIndex);            // `note` now plays `padIndex` (and nothing else)
    void forgetPad (int padIndex);                  // drops the notes learned for this pad
    juce::String learnedToString() const;           // "36:0,38:1"
    void learnedFromString (const juce::String&);

    static juce::String notesToString (const DrumNoteTable&);   // "36:0,38:1,42:2"
    static DrumNoteTable notesFromString (const juce::String&);

    std::unique_ptr<juce::XmlElement> toXml() const;
    static TriggerMap fromXml (const juce::XmlElement&);

    // From stored values: module id plus its note table ("" = the module's own notes)
    void restore (const juce::String& id, const juce::String& noteTable);

    bool operator== (const TriggerMap& o) const
    {
        return moduleId == o.moduleId && notes == o.notes && hiHatChoke == o.hiHatChoke && learned == o.learned;
    }
};

}
