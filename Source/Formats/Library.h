#pragma once

#include "KitLoader.h"
#include <juce_events/juce_events.h>

namespace kaia
{

// One expansion (.xpn), program folder or sample folder found in the user's library.
// Paths of programs and samples are relative to the pack: archive entries for .xpn files,
// '/'-separated paths below the folder otherwise.
struct LibraryPack
{
    juce::File file;                    // the .xpn or the folder
    juce::String title;                 // expansion title or folder name
    juce::String relativePath;          // from the library root, for display and search
    bool isArchive   = false;
    bool isProtected = false;           // copy-protected expansion: listed, not playable
    juce::int64 fileSize = 0;           // archives: used to reuse cached scans
    juce::int64 modified = 0;

    juce::StringArray programs;
    juce::StringArray samples;

    ProgramLocation programLocation (int index) const;
    SampleRef sampleRef (int index) const;
    static juce::String displayName (const juce::String& path);   // file name without extension
    static juce::String folderOf (const juce::String& path);      // "" for the pack's top level
};

struct LibraryIndex
{
    juce::File root;
    std::vector<LibraryPack> packs;

    int numPrograms() const;
    int numSamples() const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static LibraryIndex fromXml (const juce::XmlElement&);
};

namespace Library
{
    // Walks `root`: every .xpn is a pack, every folder that holds programs is a pack (with all the
    // audio below it), and every other folder with audio files is a sample pack.
    // Unchanged archives are taken from `previous` instead of being re-read.
    // `keepGoing` is polled with a short status text and may return false to cancel.
    // Only reads: nothing in the library is ever written.
    LibraryIndex scan (const juce::File& root, const LibraryIndex* previous,
                       const std::function<bool (const juce::String&)>& keepGoing = {});

    LibraryPack scanArchive (const juce::File& xpn);
}

// The library index shared by every Kaia instance in the process. Scans in the background and
// caches the result in the config folder. Message thread API; listeners get a change message
// when a scan finishes. Use through juce::SharedResourcePointer<LibraryService>.
class LibraryService : public juce::ChangeBroadcaster,
                       private juce::AsyncUpdater
{
public:
    LibraryService();
    ~LibraryService() override;

    juce::File getRoot() const;
    void setRoot (const juce::File& folder);          // saves the setting and rescans
    void rescan();
    void ensureScanned();                             // first use in this process: load the cache, then refresh

    std::shared_ptr<const LibraryIndex> getIndex() const;
    bool isScanning() const noexcept                  { return scanning.load(); }
    juce::String getStatus() const;

private:
    class ScanThread;
    void handleAsyncUpdate() override;
    void startScan (bool loadCacheFirst);

    std::unique_ptr<ScanThread> thread;
    mutable juce::CriticalSection lock;
    std::shared_ptr<const LibraryIndex> index;
    juce::String status;
    juce::File root;
    std::atomic<bool> scanning { false };
    bool started = false;
};

}
