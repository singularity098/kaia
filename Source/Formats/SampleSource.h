#pragma once

#include "../Model/Kit.h"
#include "../Engine/SampleData.h"
#include "ZipReader.h"
#include <unordered_map>

namespace kaia
{

bool isAudioFileName (const juce::String& name);

// Converts file bytes to text: honours UTF-16/UTF-8 BOMs, falls back to Latin-1 for invalid UTF-8.
juce::String bytesToText (const juce::MemoryBlock& data);
bool isProgramFileName (const juce::String& name);   // .xpm / .pgm

// Reads the raw bytes behind a SampleRef (plain file or archive entry).
bool readRefBytes (const SampleRef& ref, juce::MemoryBlock& out, juce::String& error);

// Decodes a sample (WAV, AIFF, FLAC, MP3, OGG or MPC .SND) into memory.
SampleDataPtr loadSample (const SampleRef& ref, juce::String& error);

// Where a program lives: a file on disk or an entry inside an archive.
struct ProgramLocation
{
    juce::File file;            // the program file, or the archive
    juce::String entry;         // entry inside the archive (empty for plain files)

    bool inArchive() const noexcept { return entry.isNotEmpty(); }
    juce::String displayName() const;
    juce::String toString() const { return entry.isEmpty() ? file.getFullPathName() : file.getFullPathName() + "|" + entry; }
    static ProgramLocation fromString (const juce::String&);
    juce::String folderKey() const;  // directory of the program ('/' separated, used for proximity ranking)
};

// Finds the audio file an MPC program refers to by name. MPC programs only store the
// sample name (and sometimes a file name), so we index every audio file below a root
// folder (or inside an archive) and pick the closest match.
class SampleIndex
{
public:
    static SampleIndex forDirectory (const juce::File& root, int maxDepth = 5);
    static SampleIndex forArchive (const ZipReader& zip);

    bool isEmpty() const noexcept { return entries.empty(); }
    size_t size() const noexcept  { return entries.size(); }

    // name: MPC sample name (no extension). fileHint: optional file name / path from the program.
    // nearFolder: folder of the program, used to prefer samples next to it.
    SampleRef find (const juce::String& name, const juce::String& fileHint, const juce::String& nearFolder) const;

    void merge (const SampleIndex& other);

private:
    struct Item { juce::String path; juce::String folder; };   // path: absolute file or archive entry
    void add (const juce::String& path);
    const Item* best (const std::vector<size_t>& candidates, const juce::String& nearFolder) const;

    bool archive = false;
    juce::String archivePath;
    std::vector<Item> entries;
    std::unordered_map<std::string, std::vector<size_t>> byStem;      // lower-case name without extension
    std::unordered_map<std::string, std::vector<size_t>> byFileName;  // lower-case name with extension
};

}
