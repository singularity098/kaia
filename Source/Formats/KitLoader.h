#pragma once

#include "SampleSource.h"

namespace kaia
{

struct ProgramEntry
{
    ProgramLocation location;
    juce::String name;          // program name (file name without extension)
    juce::String folder;        // sub folder, for grouping in the browser
    bool isArchive = false;     // an .xpn inside a folder: open it to see its programs
};

struct PackInfo
{
    juce::String title;         // expansion title or folder name
    juce::String manufacturer;
    juce::String description;
    juce::MemoryBlock image;    // cover art from the expansion, if any
    bool protectedContent = false;
};

struct LoadResult
{
    bool ok = false;
    juce::String error;
    juce::StringArray warnings;
    Kit kit;
    SampleMap samples;
};

namespace KitLoader
{
    // Programs inside a folder, an .xpn archive, or next to a program file.
    std::vector<ProgramEntry> listPrograms (const juce::File& fileOrFolder, PackInfo* info, juce::String& error);

    // Parses a program and resolves its sample names to files. Does not decode audio.
    bool parseProgram (const ProgramLocation& location, Kit& kit, juce::StringArray& warnings, juce::String& error);

    // Builds a kit from loose audio files: one sample per pad.
    Kit kitFromAudioFiles (juce::Array<juce::File> files, const juce::String& name);

    // Decodes every sample of the kit (in parallel). Layers whose audio can't be loaded are marked missing.
    // `progress` receives 0..1 and may return false to cancel.
    bool loadSamples (Kit& kit, SampleMap& samples, juce::StringArray& warnings,
                      const std::function<bool (float)>& progress = {});

    // parseProgram + loadSamples
    LoadResult loadProgram (const ProgramLocation& location, const std::function<bool (float)>& progress = {});

    // Restores a kit saved in the plugin state: reloads its samples from disk.
    LoadResult reloadKit (Kit kit, const std::function<bool (float)>& progress = {});

    // Tries to find missing samples by name below `folder` (after the user moved files).
    int relinkMissing (Kit& kit, const juce::File& folder);

    // Cover art of a pack (.xpn or folder) without listing its programs: the image Expansion.xml
    // names, else an image file at the top of the pack. Safe to call from any thread.
    bool readPackArtwork (const juce::File& fileOrFolder, juce::MemoryBlock& image);

    juce::String fileFilter();   // wildcard pattern for file choosers
}

}
