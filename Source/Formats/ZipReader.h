#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace kaia
{

// Minimal read-only zip reader used for MPC expansion packs (.xpn).
// Supports stored and deflated entries and Zip64 archives (> 4 GB).
// Encrypted entries are detected and reported, never decoded.
// Thread-safe after construction: every read opens its own stream.
class ZipReader
{
public:
    struct Entry
    {
        juce::String name;            // path inside the archive, '/' separated
        juce::uint64 compressedSize   = 0;
        juce::uint64 uncompressedSize = 0;
        juce::uint64 localHeaderOffset = 0;
        int  method    = 0;
        int  flags     = 0;
        bool encrypted = false;
        bool isDirectory() const noexcept { return name.endsWithChar ('/'); }
    };

    explicit ZipReader (const juce::File& archive);

    bool isValid() const noexcept                   { return valid; }
    const juce::String& getError() const noexcept   { return error; }
    const juce::File& getFile() const noexcept      { return file; }
    const std::vector<Entry>& getEntries() const    { return entries; }

    int indexOf (const juce::String& name, bool ignoreCase = true) const;
    bool hasEncryptedEntries() const noexcept       { return numEncrypted > 0; }

    // Reads and decompresses a whole entry into memory.
    bool readEntry (int index, juce::MemoryBlock& out, juce::String& errorOut) const;

    // Shared, cached readers so the central directory of a big archive is parsed only once.
    static std::shared_ptr<ZipReader> open (const juce::File& archive);

private:
    bool parse();
    bool findEndOfCentralDirectory (juce::FileInputStream&, juce::int64& eocdPos);

    juce::File file;
    std::vector<Entry> entries;
    juce::String error;
    bool valid = false;
    int numEncrypted = 0;
};

}
