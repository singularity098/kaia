#pragma once

#include "Kit.h"
#include <map>

namespace kaia
{

// The drum voices of an electronic drum kit. Each zone plays a fixed pad of bank A, so any kit
// laid out this way plays correctly from any supported drum module:
//
//   A13 Crash 1   A14 Crash 2     A15 Ride        A16 Ride bell
//   A09 Tom 1     A10 Tom 2       A11 Tom 3       A12 Tom 4
//   A05 Snare rim A06 Cross-stick A07 Aux         A08 Hi-hat open
//   A01 Kick      A02 Snare       A03 Hi-hat closed A04 Hi-hat pedal
enum class DrumZone : int
{
    Kick, Snare, HiHatClosed, HiHatPedal,
    SnareRim, CrossStick, Aux, HiHatOpen,
    Tom1, Tom2, Tom3, Tom4,
    Crash1, Crash2, Ride, RideBell,
    NumZones
};

juce::String drumZoneName (DrumZone zone);
int drumZonePad (DrumZone zone);                  // pad index (0 = A01)

struct DrumModule
{
    juce::String id;                              // stable, stored in settings and projects
    juce::String name;                            // e.g. "Roland TD-10"
    juce::String brand;
    std::vector<std::pair<int, DrumZone>> notes;  // factory default note -> zone (several notes per zone)
};

const std::vector<DrumModule>& drumModules();
const DrumModule* findDrumModule (const juce::String& id);

// Mute group the hi-hat pads share while a drum module map is active (outside the 1..32 the UI offers).
constexpr int kHiHatChokeGroup = 33;

// Decides which pads an incoming MIDI note plays. Without a module and without learned notes the
// program's own note map is used, unchanged. With a module, its notes play the zone pads above and
// other notes are ignored. Learned notes (MIDI Learn on a pad) win over both.
// Keygroup programs always use their own note ranges.
struct TriggerMap
{
    juce::String moduleId;          // empty: the program's note map
    bool hiHatChoke = true;         // module maps: closed/pedal hi-hat cut the open hi-hat
    std::map<int, int> learned;     // MIDI note -> pad index

    const DrumModule* module() const { return findDrumModule (moduleId); }
    bool usesModule() const          { return module() != nullptr; }
    bool isDefault() const           { return ! usesModule() && learned.empty(); }

    // For each MIDI note, the pads it plays (pads without samples included).
    std::array<std::vector<int>, 128> resolve (const Kit& kit) const;
    std::vector<int> notesForPad (const Kit& kit, int padIndex) const;

    void learn (int note, int padIndex);            // `note` now plays `padIndex` (and nothing else)
    void forgetPad (int padIndex);                  // drops the notes learned for this pad
    juce::String learnedToString() const;           // "36:0,38:1"
    void learnedFromString (const juce::String&);

    std::unique_ptr<juce::XmlElement> toXml() const;
    static TriggerMap fromXml (const juce::XmlElement&);

    bool operator== (const TriggerMap& o) const
    {
        return moduleId == o.moduleId && hiHatChoke == o.hiHatChoke && learned == o.learned;
    }
};

}
