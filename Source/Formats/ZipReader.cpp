#include "ZipReader.h"
#include <map>
#include <mutex>

namespace kaia
{

namespace
{
    constexpr juce::uint32 kSigEOCD      = 0x06054b50;
    constexpr juce::uint32 kSigZip64EOCD = 0x06064b50;
    constexpr juce::uint32 kSigZip64Loc  = 0x07064b50;
    constexpr juce::uint32 kSigCentral   = 0x02014b50;
    constexpr juce::uint32 kSigLocal     = 0x04034b50;

    juce::uint16 rd16 (const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
    juce::uint32 rd32 (const juce::uint8* p) { return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24); }
    juce::uint64 rd64 (const juce::uint8* p) { return (juce::uint64) rd32 (p) | ((juce::uint64) rd32 (p + 4) << 32); }

    juce::String decodeName (const juce::uint8* p, int len, bool utf8Flag)
    {
        auto* chars = reinterpret_cast<const char*> (p);
        if (utf8Flag || juce::CharPointer_UTF8::isValidString (chars, len))
            return juce::String::fromUTF8 (chars, len);

        // Fallback: treat as Latin-1 (close enough to CP437 for file names)
        juce::String s;
        s.preallocateBytes ((size_t) len * 2);
        for (int i = 0; i < len; ++i)
            s += (juce::juce_wchar) p[i];
        return s;
    }

    bool readFully (juce::InputStream& in, void* dest, int numBytes)
    {
        return in.read (dest, numBytes) == numBytes;
    }
}

ZipReader::ZipReader (const juce::File& archive) : file (archive)
{
    valid = parse();
}

std::shared_ptr<ZipReader> ZipReader::open (const juce::File& archive)
{
    static std::mutex lock;
    static std::map<juce::String, std::pair<juce::Time, std::weak_ptr<ZipReader>>> cache;

    std::lock_guard<std::mutex> sl (lock);
    auto key = archive.getFullPathName();
    auto modTime = archive.getLastModificationTime();

    auto it = cache.find (key);
    if (it != cache.end() && it->second.first == modTime)
        if (auto existing = it->second.second.lock())
            return existing;

    auto reader = std::make_shared<ZipReader> (archive);
    cache[key] = { modTime, reader };
    return reader;
}

bool ZipReader::findEndOfCentralDirectory (juce::FileInputStream& in, juce::int64& eocdPos)
{
    auto fileSize = in.getTotalLength();
    if (fileSize < 22)
        return false;

    // The EOCD record is at the end, followed by an optional comment of up to 64 KB.
    auto searchLen = (int) juce::jmin<juce::int64> (fileSize, 22 + 65535);
    juce::HeapBlock<juce::uint8> buf ((size_t) searchLen);
    in.setPosition (fileSize - searchLen);
    if (! readFully (in, buf.get(), searchLen))
        return false;

    for (int i = searchLen - 22; i >= 0; --i)
    {
        if (rd32 (buf + i) == kSigEOCD)
        {
            eocdPos = fileSize - searchLen + i;
            return true;
        }
    }
    return false;
}

bool ZipReader::parse()
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
    {
        error = "Could not open " + file.getFileName();
        return false;
    }

    juce::int64 eocdPos = 0;
    if (! findEndOfCentralDirectory (in, eocdPos))
    {
        error = file.getFileName() + " is not a zip archive. It may be a protected or damaged expansion.";
        return false;
    }

    juce::uint8 eocd[22];
    in.setPosition (eocdPos);
    if (! readFully (in, eocd, 22))
        return false;

    juce::uint64 numEntries = rd16 (eocd + 10);
    juce::uint64 cdSize     = rd32 (eocd + 12);
    juce::uint64 cdOffset   = rd32 (eocd + 16);

    // Zip64: look for the locator just before the EOCD record
    if (eocdPos >= 20 && (numEntries == 0xFFFF || cdSize == 0xFFFFFFFF || cdOffset == 0xFFFFFFFF))
    {
        juce::uint8 loc[20];
        in.setPosition (eocdPos - 20);
        if (readFully (in, loc, 20) && rd32 (loc) == kSigZip64Loc)
        {
            auto z64Pos = (juce::int64) rd64 (loc + 8);
            juce::uint8 z64[56];
            in.setPosition (z64Pos);
            if (readFully (in, z64, 56) && rd32 (z64) == kSigZip64EOCD)
            {
                numEntries = rd64 (z64 + 32);
                cdSize     = rd64 (z64 + 40);
                cdOffset   = rd64 (z64 + 48);
            }
        }
    }

    if (cdSize > (juce::uint64) 512 * 1024 * 1024 || (juce::int64) (cdOffset + cdSize) > in.getTotalLength())
    {
        error = file.getFileName() + " has a damaged directory.";
        return false;
    }

    juce::HeapBlock<juce::uint8> cd ((size_t) cdSize + 1);
    in.setPosition ((juce::int64) cdOffset);
    if (! readFully (in, cd.get(), (int) cdSize))
    {
        error = "Could not read the directory of " + file.getFileName();
        return false;
    }

    entries.reserve ((size_t) juce::jmin<juce::uint64> (numEntries, 1000000));
    size_t pos = 0;
    while (pos + 46 <= cdSize)
    {
        auto* h = cd.get() + pos;
        if (rd32 (h) != kSigCentral)
            break;

        Entry e;
        e.flags            = rd16 (h + 8);
        e.method           = rd16 (h + 10);
        e.compressedSize   = rd32 (h + 20);
        e.uncompressedSize = rd32 (h + 24);
        auto nameLen       = rd16 (h + 28);
        auto extraLen      = rd16 (h + 30);
        auto commentLen    = rd16 (h + 32);
        e.localHeaderOffset = rd32 (h + 42);

        if (pos + 46 + nameLen + extraLen + commentLen > cdSize)
            break;

        e.name = decodeName (h + 46, nameLen, (e.flags & 0x800) != 0).replaceCharacter ('\\', '/');

        // Zip64 extended information
        auto* extra = h + 46 + nameLen;
        size_t ep = 0;
        while (ep + 4 <= extraLen)
        {
            auto id = rd16 (extra + ep);
            auto sz = rd16 (extra + ep + 2);
            if (ep + 4 + sz > extraLen)
                break;
            if (id == 0x0001)
            {
                auto* d = extra + ep + 4;
                size_t dp = 0;
                if (e.uncompressedSize == 0xFFFFFFFF && dp + 8 <= sz) { e.uncompressedSize = rd64 (d + dp); dp += 8; }
                if (e.compressedSize == 0xFFFFFFFF && dp + 8 <= sz)   { e.compressedSize   = rd64 (d + dp); dp += 8; }
                if (e.localHeaderOffset == 0xFFFFFFFF && dp + 8 <= sz){ e.localHeaderOffset = rd64 (d + dp); dp += 8; }
            }
            ep += 4 + sz;
        }

        e.encrypted = (e.flags & 0x1) != 0 || e.method == 99;
        if (e.encrypted)
            ++numEncrypted;

        entries.push_back (std::move (e));
        pos += 46 + (size_t) nameLen + extraLen + commentLen;
    }

    if (entries.empty())
    {
        error = file.getFileName() + " contains no files.";
        return false;
    }
    return true;
}

int ZipReader::indexOf (const juce::String& name, bool ignoreCase) const
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (ignoreCase ? entries[i].name.equalsIgnoreCase (name) : entries[i].name == name)
            return (int) i;
    return -1;
}

bool ZipReader::readEntry (int index, juce::MemoryBlock& out, juce::String& errorOut) const
{
    if (index < 0 || index >= (int) entries.size())
    {
        errorOut = "No such entry";
        return false;
    }

    auto& e = entries[(size_t) index];
    if (e.encrypted)
    {
        errorOut = e.name + " is encrypted (copy-protected content)";
        return false;
    }
    if (e.method != 0 && e.method != 8)
    {
        errorOut = e.name + " uses an unsupported compression method (" + juce::String (e.method) + ")";
        return false;
    }
    if (e.uncompressedSize > (juce::uint64) 0x7fffffff)
    {
        errorOut = e.name + " is too large";
        return false;
    }

    auto stream = std::make_unique<juce::FileInputStream> (file);
    if (! stream->openedOk())
    {
        errorOut = "Could not open " + file.getFileName();
        return false;
    }

    juce::uint8 lh[30];
    stream->setPosition ((juce::int64) e.localHeaderOffset);
    if (! readFully (*stream, lh, 30) || rd32 (lh) != kSigLocal)
    {
        errorOut = "Damaged local header for " + e.name;
        return false;
    }

    auto dataStart = (juce::int64) e.localHeaderOffset + 30 + rd16 (lh + 26) + rd16 (lh + 28);
    auto size = (size_t) e.uncompressedSize;
    out.setSize (size, false);

    if (e.method == 0)
    {
        stream->setPosition (dataStart);
        if (size > 0 && ! readFully (*stream, out.getData(), (int) size))
        {
            errorOut = "Truncated data for " + e.name;
            return false;
        }
        return true;
    }

    auto* sub = new juce::SubregionStream (stream.release(), dataStart, (juce::int64) e.compressedSize, true);
    juce::GZIPDecompressorInputStream inflater (sub, true, juce::GZIPDecompressorInputStream::deflateFormat, (juce::int64) size);

    size_t done = 0;
    while (done < size)
    {
        auto chunk = (int) juce::jmin<size_t> (size - done, 1 << 20);
        auto got = inflater.read (static_cast<char*> (out.getData()) + done, chunk);
        if (got <= 0)
            break;
        done += (size_t) got;
    }

    if (done != size)
    {
        errorOut = "Could not decompress " + e.name;
        return false;
    }
    return true;
}

}
