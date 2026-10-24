#include "SampleSource.h"
#include "SndDecoder.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace kaia
{

namespace
{
    const juce::StringArray& audioExtensions()
    {
        static const juce::StringArray exts { "wav", "wave", "aif", "aiff", "aifc", "flac", "mp3", "ogg", "snd" };
        return exts;
    }

    juce::String extensionOf (const juce::String& name)
    {
        auto base = name.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
        return base.contains (".") ? base.fromLastOccurrenceOf (".", false, false).toLowerCase() : juce::String();
    }

    juce::String baseNameOf (const juce::String& path)
    {
        return path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
    }

    juce::String stemOf (const juce::String& path)
    {
        auto base = baseNameOf (path);
        return base.contains (".") ? base.upToLastOccurrenceOf (".", false, false) : base;
    }

    juce::String folderOf (const juce::String& path)
    {
        auto p = path.replaceCharacter ('\\', '/');
        return p.contains ("/") ? p.upToLastOccurrenceOf ("/", false, false) : juce::String();
    }

    std::string key (const juce::String& s)
    {
        return s.trim().toLowerCase().toStdString();
    }

    juce::AudioFormatManager& formatManager()
    {
        // AudioFormatManager is only read after construction; creating readers from it is thread-safe.
        struct Manager : juce::AudioFormatManager
        {
            Manager() { registerBasicFormats(); }
        };
        static Manager fm;
        return fm;
    }

    SampleDataPtr decodeWithJuce (std::unique_ptr<juce::InputStream> stream, const juce::String& ref, juce::String& error)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (std::move (stream)));
        if (reader == nullptr)
        {
            error = "unsupported or damaged audio file";
            return {};
        }

        auto frames = reader->lengthInSamples;
        if (frames <= 0)
        {
            error = "the audio file is empty";
            return {};
        }
        if (frames > (juce::int64) 48000 * 60 * 20)
        {
            error = "the audio file is too long (over 20 minutes)";
            return {};
        }

        auto data = std::make_shared<SampleData>();
        data->ref = ref;
        data->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
        auto chans = (int) juce::jmin<unsigned int> (2, reader->numChannels);
        data->allocate (chans, frames);

        float* dest[2] = { data->writePointer (0), chans > 1 ? data->writePointer (1) : nullptr };
        constexpr int blockSize = 1 << 16;
        juce::AudioBuffer<float> temp ((int) reader->numChannels, blockSize);
        for (juce::int64 pos = 0; pos < frames; pos += blockSize)
        {
            auto n = (int) juce::jmin<juce::int64> (blockSize, frames - pos);
            if (! reader->read (&temp, 0, n, pos, true, reader->numChannels > 1))
            {
                error = "could not decode audio data";
                return {};
            }
            for (int c = 0; c < chans; ++c)
                std::memcpy (dest[c] + pos, temp.getReadPointer (c), sizeof (float) * (size_t) n);
        }

        // WAV/AIFF sampler metadata: root note and loop points
        auto& meta = reader->metadataValues;
        if (meta.containsKey ("MidiUnityNote"))
        {
            auto note = meta["MidiUnityNote"].getIntValue();
            if (note > 0 && note < 128)
                data->rootNote = note;
        }
        if (meta["NumSampleLoops"].getIntValue() > 0)
        {
            auto ls = meta["Loop0Start"].getLargeIntValue();
            auto le = meta["Loop0End"].getLargeIntValue();
            if (le > ls && le <= frames)
            {
                data->loopStart = ls;
                data->loopEnd = le + 1;   // smpl loop end is inclusive
                data->loopEnabled = true;
            }
        }

        data->buildOverview();
        return data;
    }
}

juce::String bytesToText (const juce::MemoryBlock& data)
{
    auto* b = static_cast<const juce::uint8*> (data.getData());
    auto size = data.getSize();
    if (size == 0)
        return {};

    // UTF-16 (either byte order) or UTF-8 with BOM: JUCE handles these
    if (size >= 2 && ((b[0] == 0xff && b[1] == 0xfe) || (b[0] == 0xfe && b[1] == 0xff)))
        return juce::String::createStringFromData (b, (int) juce::jmin<size_t> (size, 0x7fffffff));

    size_t start = (size >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf) ? 3 : 0;
    auto* chars = reinterpret_cast<const char*> (b + start);
    auto len = (int) juce::jmin<size_t> (size - start, 0x7fffffff);

    // Stop at an embedded NUL like a C string would
    auto nul = std::find (chars, chars + len, '\0');
    len = (int) (nul - chars);

    if (juce::CharPointer_UTF8::isValidString (chars, len))
        return juce::String::fromUTF8 (chars, len);

    juce::String s;
    s.preallocateBytes ((size_t) len * 2);
    for (int i = 0; i < len; ++i)
        s += (juce::juce_wchar) (juce::uint8) chars[i];
    return s;
}

bool isAudioFileName (const juce::String& name)
{
    return audioExtensions().contains (extensionOf (name));
}

bool isProgramFileName (const juce::String& name)
{
    auto ext = extensionOf (name);
    return ext == "xpm" || ext == "pgm";
}

bool readRefBytes (const SampleRef& ref, juce::MemoryBlock& out, juce::String& error)
{
    if (ref.isInArchive())
    {
        auto zip = ZipReader::open (juce::File (ref.file));
        if (! zip->isValid())
        {
            error = zip->getError();
            return false;
        }
        auto idx = zip->indexOf (ref.entry, false);
        if (idx < 0)
            idx = zip->indexOf (ref.entry, true);
        if (idx < 0)
        {
            error = ref.entry + " is not in " + juce::File (ref.file).getFileName();
            return false;
        }
        return zip->readEntry (idx, out, error);
    }

    juce::File f (ref.file);
    if (! f.existsAsFile())
    {
        error = "file not found: " + ref.file;
        return false;
    }
    if (! f.loadFileAsData (out))
    {
        error = "could not read " + f.getFileName();
        return false;
    }
    return true;
}

SampleDataPtr loadSample (const SampleRef& ref, juce::String& error)
{
    if (ref.isEmpty())
    {
        error = "no sample";
        return {};
    }

    auto path = ref.isInArchive() ? ref.entry : ref.file;
    auto ext = extensionOf (path);

    if (! ref.isInArchive() && ext != "snd")
    {
        // Stream straight from disk: no need to hold the encoded file in memory
        juce::File f (ref.file);
        if (! f.existsAsFile())
        {
            error = "file not found";
            return {};
        }
        auto stream = std::make_unique<juce::FileInputStream> (f);
        if (! stream->openedOk())
        {
            error = "could not open file";
            return {};
        }
        return decodeWithJuce (std::move (stream), ref.toString(), error);
    }

    juce::MemoryBlock bytes;
    if (! readRefBytes (ref, bytes, error))
        return {};

    if (isMpcSnd (bytes.getData(), bytes.getSize()))
    {
        auto data = std::make_shared<SampleData>();
        data->ref = ref.toString();
        if (! decodeMpcSnd (bytes.getData(), bytes.getSize(), *data, error))
            return {};
        data->buildOverview();
        return data;
    }

    if (bytes.getSize() == 0)
    {
        error = "empty file";
        return {};
    }
    return decodeWithJuce (std::make_unique<juce::MemoryInputStream> (std::move (bytes)), ref.toString(), error);
}

//==============================================================================
juce::String ProgramLocation::displayName() const
{
    return entry.isNotEmpty() ? stemOf (entry) : file.getFileNameWithoutExtension();
}

ProgramLocation ProgramLocation::fromString (const juce::String& s)
{
    auto r = SampleRef::fromString (s);
    return { juce::File (r.file), r.entry };
}

juce::String ProgramLocation::folderKey() const
{
    return entry.isNotEmpty() ? folderOf (entry) : file.getParentDirectory().getFullPathName().replaceCharacter ('\\', '/');
}

//==============================================================================
void SampleIndex::add (const juce::String& path)
{
    auto idx = entries.size();
    entries.push_back ({ path, folderOf (path) });
    byStem[key (stemOf (path))].push_back (idx);
    byFileName[key (baseNameOf (path))].push_back (idx);
}

SampleIndex SampleIndex::forDirectory (const juce::File& root, int maxDepth)
{
    SampleIndex index;
    if (! root.isDirectory())
        return index;

    int count = 0;
    std::function<void (const juce::File&, int)> scan = [&] (const juce::File& dir, int depth)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            if (++count > 300000)
                return;
            auto f = entry.getFile();
            if (entry.isDirectory())
            {
                if (depth < maxDepth)
                    scan (f, depth + 1);
            }
            else if (isAudioFileName (f.getFileName()))
            {
                index.add (f.getFullPathName().replaceCharacter ('\\', '/'));
            }
        }
    };
    scan (root, 0);
    return index;
}

SampleIndex SampleIndex::forArchive (const ZipReader& zip)
{
    SampleIndex index;
    index.archive = true;
    index.archivePath = zip.getFile().getFullPathName();
    for (auto& e : zip.getEntries())
    {
        if (e.isDirectory() || ! isAudioFileName (e.name))
            continue;
        // Skip macOS resource forks and preview renders
        if (e.name.startsWith ("__MACOSX/") || baseNameOf (e.name).startsWith ("._"))
            continue;
        index.add (e.name);
    }
    return index;
}

void SampleIndex::merge (const SampleIndex& other)
{
    if (other.archive != archive)
        return;

    for (auto& item : other.entries)
    {
        bool known = false;
        auto it = byFileName.find (key (baseNameOf (item.path)));
        if (it != byFileName.end())
            for (auto i : it->second)
                known = known || entries[i].path == item.path;
        if (! known)
            add (item.path);
    }
}

const SampleIndex::Item* SampleIndex::best (const std::vector<size_t>& candidates, const juce::String& nearFolder) const
{
    if (candidates.empty())
        return nullptr;

    auto near = nearFolder.replaceCharacter ('\\', '/');
    const Item* bestItem = nullptr;
    int bestScore = std::numeric_limits<int>::max();

    for (auto i : candidates)
    {
        auto& item = entries[i];
        int score;
        if (item.folder.equalsIgnoreCase (near))
            score = 0;
        else if (near.isEmpty() || item.folder.startsWithIgnoreCase (near + "/"))
            score = 10 + item.folder.substring (near.length()).retainCharacters ("/").length();
        else
        {
            // Rank by how much of the path is shared
            int common = 0;
            auto a = item.folder.toLowerCase(), b = near.toLowerCase();
            while (common < a.length() && common < b.length() && a[common] == b[common])
                ++common;
            score = 1000 - common;
        }

        // Don't prefer MPC preview renders over the real sample
        if (item.path.containsIgnoreCase ("[Previews]") || item.path.containsIgnoreCase ("/Previews/"))
            score += 5000;

        if (score < bestScore)
        {
            bestScore = score;
            bestItem = &item;
        }
    }
    return bestItem;
}

SampleRef SampleIndex::find (const juce::String& name, const juce::String& fileHint, const juce::String& nearFolder) const
{
    auto makeRef = [this] (const Item* item) -> SampleRef
    {
        if (item == nullptr)
            return {};
        if (archive)
            return { archivePath, item->path };
        return { item->path, {} };
    };

    // 1. Explicit file name from the program (may be a full Windows/Mac path)
    if (fileHint.trim().isNotEmpty())
    {
        auto base = baseNameOf (fileHint.trim());
        auto it = byFileName.find (key (base));
        if (it != byFileName.end())
            return makeRef (best (it->second, nearFolder));
        auto st = byStem.find (key (stemOf (base)));
        if (st != byStem.end())
            return makeRef (best (st->second, nearFolder));
    }

    if (name.isEmpty())
        return {};

    // 2. Sample name (MPC names have no extension; some end with spaces, keys are trimmed)
    auto st = byStem.find (key (name));
    if (st != byStem.end())
        return makeRef (best (st->second, nearFolder));

    // 3. Name already includes an extension
    auto it = byFileName.find (key (name));
    if (it != byFileName.end())
        return makeRef (best (it->second, nearFolder));

    return {};
}

}
