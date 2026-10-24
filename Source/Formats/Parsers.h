#pragma once

#include "../Model/Kit.h"

namespace kaia
{

// Program parsers. They fill a Kit whose layers carry the MPC sample *names* (Layer::name and
// Layer::fileHint); KitLoader resolves those names to audio files afterwards.

// Akai MPC .xpm: XML (MPC software 1.x / 2.x) or gzip-compressed JSON (MPC 3.x).
bool parseXpm (const juce::MemoryBlock& data, Kit& kit, juce::StringArray& warnings, juce::String& error);

// Akai .pgm: MPC500/1000/2500 ("MPC1000 PGM 1.00"), MPC2000/2000XL and MPC3000.
bool parsePgm (const juce::MemoryBlock& data, Kit& kit, juce::StringArray& warnings, juce::String& error);

// Helpers shared by the parsers
float mpcEnvTimeMs (double normalized);        // MPC software envelope: 0..1 -> 1 ms .. 100 s (log)
float mpcCutoffHz (double normalized);         // MPC software filter cutoff 0..1 -> Hz
float classicEnvTimeMs (int value0to100);      // MPC1000/2000XL attack/decay 0..100 -> ms
float classicCutoffHz (int value0to100);       // MPC1000/2000XL/3000 cutoff 0..100 -> Hz
FilterType mpcFilterType (int xpmFilterType);

}
