#include "KitLoader.h"
#include "Parsers.h"
#include <thread>
#include <atomic>
#include <mutex>

namespace kaia
{

namespace
{
    juce::String extensionOf (const juce::String& name)
    {
        auto base = name.fromLastOccurrenceOf ("/", false, false);
        return base.contains (".") ? base.fromLastOccurrenceOf (".", false, false).toLowerCase() : juce::String();
    }

    juce::String stemOf (const juce::String& path)
    {
        auto base = path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
        return base.contains (".") ? base.upToLastOccurrenceOf (".", false, false) : base;
    }

    bool isJunkEntry (const juce::String& path)
    {
        auto base = path.fromLastOccurrenceOf ("/", false, false);
        return path.startsWith ("__MACOSX/") || base.startsWith ("._");
    }

    void readPackInfo (const juce::String& xmlText, PackInfo& info)
    {
        if (auto xml = juce::XmlDocument::parse (xmlText))
        {
            auto get = [&] (const char* tag)
            {
                if (auto* e = xml->getChildByName (tag))
                    return e->getAllSubText().trim();
                for (auto* e : xml->getChildIterator())
                    if (e->getTagName().equalsIgnoreCase (tag))
                        return e->getAllSubText().trim();
                return juce::String();
            };
            if (get ("title").isNotEmpty())
                info.title = get ("title");
            info.manufacturer = get ("manufacturer");
            info.description  = get ("description");
        }
    }

    juce::String imageNameFromExpansionXml (const juce::String& xmlText)
    {
        if (auto xml = juce::XmlDocument::parse (xmlText))
            for (auto* e : xml->getChildIterator())
                if (e->getTagName().equalsIgnoreCase ("img"))
                    return e->getAllSubText().trim();
        return {};
    }

    // Weak cache so switching between programs of one pack doesn't decode shared samples twice.
    std::mutex cacheLock;
    std::map<juce::String, std::weak_ptr<const SampleData>> sampleCache;

    SampleDataPtr cachedSample (const juce::String& key)
    {
        std::lock_guard<std::mutex> sl (cacheLock);
        auto it = sampleCache.find (key);
        if (it != sampleCache.end())
        {
            if (auto p = it->second.lock())
                return p;
            sampleCache.erase (it);
        }
        return {};
    }

    void storeSample (const juce::String& key, const SampleDataPtr& data)
    {
        std::lock_guard<std::mutex> sl (cacheLock);
        for (auto it = sampleCache.begin(); it != sampleCache.end();)
            it = it->second.expired() ? sampleCache.erase (it) : std::next (it);
        sampleCache[key] = data;
    }

    juce::String fileModKey (const SampleRef& ref)
    {
        juce::File f (ref.file);
        return ref.toString() + "@" + juce::String (f.getLastModificationTime().toMilliseconds());
    }

    int naturalCompare (const juce::String& a, const juce::String& b)
    {
        return a.compareNatural (b);
    }

    // Resolves sample names using an index; returns the number of unresolved layers
    int resolveWith (Kit& kit, const SampleIndex& index, const juce::String& nearFolder, bool onlyMissing)
    {
        int unresolved = 0;
        for (auto& pad : kit.pads)
        {
            for (auto& layer : pad.layers)
            {
                if (onlyMissing && ! layer.missing)
                    continue;
                if (! onlyMissing && layer.hasSample())
                    continue;

                auto ref = index.find (layer.name, layer.fileHint, nearFolder);
                if (ref.isEmpty())
                {
                    layer.missing = true;
                    ++unresolved;
                }
                else
                {
                    layer.sample = ref;
                    layer.missing = false;
                }
            }
        }
        return unresolved;
    }
}

//==============================================================================
bool KitLoader::readPackArtwork (const juce::File& target, juce::MemoryBlock& image)
{
    // Expansion.xml names the cover; without one, an image file at the top of the pack
    // ("cover", "artwork" or "folder" preferred) is used.
    auto isImageName = [] (const juce::String& name)
    {
        auto ext = extensionOf (name);
        return ext == "jpg" || ext == "jpeg" || ext == "png";
    };
    auto preference = [] (const juce::String& base)
    {
        for (auto* word : { "cover", "artwork", "folder", "front" })
            if (base.containsIgnoreCase (word))
                return 2;
        return 1;
    };

    image.reset();
    if (target.existsAsFile() && extensionOf (target.getFileName()) == "xpn")
    {
        auto zip = ZipReader::open (target);
        if (! zip->isValid())
            return false;

        auto& entries = zip->getEntries();
        int best = -1, bestScore = 0;
        for (int i = 0; i < (int) entries.size(); ++i)
        {
            auto& e = entries[(size_t) i];
            if (e.isDirectory() || e.encrypted || isJunkEntry (e.name))
                continue;
            auto base = e.name.fromLastOccurrenceOf ("/", false, false);
            if (base.equalsIgnoreCase ("Expansion.xml"))
            {
                juce::MemoryBlock mb;
                juce::String err;
                auto name = zip->readEntry (i, mb, err) ? imageNameFromExpansionXml (bytesToText (mb)) : juce::String();
                auto idx = name.isNotEmpty() ? zip->indexOf (name) : -1;
                for (int j = 0; idx < 0 && name.isNotEmpty() && j < (int) entries.size(); ++j)
                    if (entries[(size_t) j].name.fromLastOccurrenceOf ("/", false, false).equalsIgnoreCase (name))
                        idx = j;
                if (idx >= 0)
                {
                    best = idx;
                    bestScore = 100;
                }
            }
            else if (isImageName (base) && ! e.name.containsChar ('/') && preference (base) > bestScore)
            {
                best = i;
                bestScore = preference (base);
            }
        }
        juce::String err;
        return best >= 0 && zip->readEntry (best, image, err) && image.getSize() > 0;
    }

    if (! target.isDirectory())
        return false;

    auto expansionXml = target.getChildFile ("Expansion.xml");
    if (expansionXml.existsAsFile())
    {
        auto name = imageNameFromExpansionXml (expansionXml.loadFileAsString());
        if (name.isNotEmpty() && target.getChildFile (name).loadFileAsData (image) && image.getSize() > 0)
            return true;
    }

    juce::File best;
    int bestScore = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (target, false, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
    {
        auto f = entry.getFile();
        if (isImageName (f.getFileName()) && preference (f.getFileName()) > bestScore)
        {
            best = f;
            bestScore = preference (f.getFileName());
        }
    }
    image.reset();
    return best != juce::File() && best.loadFileAsData (image) && image.getSize() > 0;
}

juce::String KitLoader::fileFilter()
{
    return "*.xpm;*.xpn;*.pgm;*.XPM;*.XPN;*.PGM;*.kaiapreset;*.wav;*.WAV;*.aif;*.aiff;*.flac;*.snd;*.SND";
}

std::vector<ProgramEntry> KitLoader::listPrograms (const juce::File& target, PackInfo* info, juce::String& error)
{
    std::vector<ProgramEntry> list;
    PackInfo localInfo;
    auto& pack = info != nullptr ? *info : localInfo;

    if (target.existsAsFile() && extensionOf (target.getFileName()) == "xpn")
    {
        auto zip = ZipReader::open (target);
        if (! zip->isValid())
        {
            error = zip->getError();
            return list;
        }

        pack.title = target.getFileNameWithoutExtension();
        int encryptedPrograms = 0;
        juce::String imageName;

        for (int i = 0; i < (int) zip->getEntries().size(); ++i)
        {
            auto& e = zip->getEntries()[(size_t) i];
            if (e.isDirectory() || isJunkEntry (e.name))
                continue;

            auto base = e.name.fromLastOccurrenceOf ("/", false, false);
            if (base.equalsIgnoreCase ("Expansion.xml"))
            {
                juce::MemoryBlock mb;
                juce::String err;
                if (zip->readEntry (i, mb, err))
                {
                    readPackInfo (bytesToText (mb), pack);
                    imageName = imageNameFromExpansionXml (bytesToText (mb));
                }
                continue;
            }

            if (isProgramFileName (e.name))
            {
                if (e.encrypted)
                {
                    ++encryptedPrograms;
                    continue;
                }
                ProgramEntry pe;
                pe.location = { target, e.name };
                pe.name = stemOf (e.name);
                pe.folder = e.name.contains ("/") ? e.name.upToLastOccurrenceOf ("/", false, false) : juce::String();
                list.push_back (pe);
            }
        }

        if (imageName.isNotEmpty())
        {
            auto idx = zip->indexOf (imageName);
            if (idx < 0)
                for (int i = 0; i < (int) zip->getEntries().size(); ++i)
                    if (zip->getEntries()[(size_t) i].name.fromLastOccurrenceOf ("/", false, false).equalsIgnoreCase (imageName))
                        idx = i;
            juce::String err;
            if (idx >= 0)
                zip->readEntry (idx, pack.image, err);
        }

        if (list.empty())
        {
            if (encryptedPrograms > 0 || zip->hasEncryptedEntries())
            {
                pack.protectedContent = true;
                error = "\"" + pack.title + "\" is copy-protected. Protected expansions can only be opened by Akai's own software.";
            }
            else
            {
                error = "No programs found in " + target.getFileName() + ".";
            }
        }
    }
    else if (target.isDirectory())
    {
        pack.title = target.getFileName();
        auto expansionXml = target.getChildFile ("Expansion.xml");
        if (expansionXml.existsAsFile())
        {
            auto text = expansionXml.loadFileAsString();
            readPackInfo (text, pack);
            auto img = imageNameFromExpansionXml (text);
            if (img.isNotEmpty())
                target.getChildFile (img).loadFileAsData (pack.image);
        }

        int count = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (target, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
        {
            if (++count > 200000 || list.size() > 5000)
                break;
            auto f = entry.getFile();
            auto ext = extensionOf (f.getFileName());
            if (ext == "xpm" || ext == "pgm" || ext == "xpn")
            {
                ProgramEntry pe;
                pe.location = { f, {} };
                pe.name = f.getFileNameWithoutExtension();
                pe.folder = f.getParentDirectory().getRelativePathFrom (target).replaceCharacter ('\\', '/');
                if (pe.folder == ".")
                    pe.folder = {};
                pe.isArchive = ext == "xpn";
                list.push_back (pe);
            }
        }
        if (list.empty())
            error = "No MPC programs found in " + target.getFileName() + ".";
    }
    else if (target.existsAsFile())
    {
        auto dir = target.getParentDirectory();
        pack.title = dir.getFileName();
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
        {
            auto f = entry.getFile();
            if (isProgramFileName (f.getFileName()))
            {
                ProgramEntry pe;
                pe.location = { f, {} };
                pe.name = f.getFileNameWithoutExtension();
                list.push_back (pe);
            }
        }
    }
    else
    {
        error = target.getFullPathName() + " does not exist.";
    }

    std::sort (list.begin(), list.end(), [] (const ProgramEntry& a, const ProgramEntry& b)
    {
        auto fc = naturalCompare (a.folder, b.folder);
        if (fc != 0)
            return fc < 0;
        return naturalCompare (a.name, b.name) < 0;
    });
    return list;
}

//==============================================================================
bool KitLoader::parseProgram (const ProgramLocation& location, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    juce::MemoryBlock data;
    std::shared_ptr<ZipReader> zip;

    if (location.inArchive())
    {
        zip = ZipReader::open (location.file);
        if (! zip->isValid())
        {
            error = zip->getError();
            return false;
        }
        auto idx = zip->indexOf (location.entry);
        if (idx < 0)
        {
            error = location.entry + " was not found in " + location.file.getFileName();
            return false;
        }
        if (! zip->readEntry (idx, data, error))
            return false;
    }
    else
    {
        if (! location.file.existsAsFile())
        {
            error = location.file.getFullPathName() + " does not exist.";
            return false;
        }
        if (! location.file.loadFileAsData (data))
        {
            error = "Could not read " + location.file.getFileName();
            return false;
        }
    }

    auto name = location.inArchive() ? location.entry : location.file.getFileName();
    auto ext = extensionOf (name);
    auto* bytes = static_cast<const juce::uint8*> (data.getData());

    bool ok;
    if (ext == "pgm" || (data.getSize() > 2 && bytes[0] == 0x07))
        ok = parsePgm (data, kit, warnings, error);
    else
        ok = parseXpm (data, kit, warnings, error);

    if (! ok)
        return false;

    kit.sourcePath = location.toString();
    if (kit.name.trim().isEmpty())
        kit.name = location.displayName();

    // Resolve sample names to files
    auto nearFolder = location.folderKey();
    int unresolved;
    if (zip != nullptr)
    {
        unresolved = resolveWith (kit, SampleIndex::forArchive (*zip), nearFolder, false);
    }
    else
    {
        auto dir = location.file.getParentDirectory();
        unresolved = resolveWith (kit, SampleIndex::forDirectory (dir, 4), nearFolder, false);

        // Programs are often kept in a "Programs" folder next to a "Samples" folder
        if (unresolved > 0 && dir.getParentDirectory() != dir)
            unresolved = resolveWith (kit, SampleIndex::forDirectory (dir.getParentDirectory(), 4), nearFolder, true);
    }

    if (unresolved > 0)
    {
        juce::StringArray names;
        for (auto& p : kit.pads)
            for (auto& l : p.layers)
                if (l.missing && names.size() < 6)
                    names.addIfNotAlreadyThere (l.name.trim());
        warnings.add (juce::String (unresolved) + " sample" + (unresolved == 1 ? "" : "s") + " not found: "
                      + names.joinIntoString (", ") + (unresolved > names.size() ? ", ..." : ""));
    }

    for (auto& p : kit.pads)
        for (auto& l : p.layers)
            l.fileHint = {};

    return true;
}

Kit KitLoader::kitFromAudioFiles (juce::Array<juce::File> files, const juce::String& name)
{
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });

    auto kit = Kit::makeEmpty();
    kit.name = name;
    kit.formatName = "Audio files";
    int pad = 0;
    for (auto& f : files)
    {
        if (pad >= kMaxPads)
            break;
        if (! isAudioFileName (f.getFileName()))
            continue;
        Layer l;
        l.sample = { f.getFullPathName(), {} };
        l.name = f.getFileNameWithoutExtension();
        kit.pads[(size_t) pad].layers.push_back (l);
        ++pad;
    }
    return kit;
}

bool KitLoader::loadSamples (Kit& kit, SampleMap& samples, juce::StringArray& warnings, const std::function<bool (float)>& progress)
{
    auto refs = kit.allSampleRefs();
    std::vector<SampleDataPtr> results (refs.size());
    std::vector<juce::String> errors (refs.size());

    std::atomic<size_t> next { 0 };
    std::atomic<size_t> done { 0 };
    std::atomic<bool> cancelled { false };

    auto worker = [&]
    {
        for (;;)
        {
            auto i = next.fetch_add (1);
            if (i >= refs.size() || cancelled.load())
                return;

            auto& ref = refs[i];
            auto key = fileModKey (ref);
            auto data = cachedSample (key);
            if (data == nullptr)
            {
                data = loadSample (ref, errors[i]);
                if (data != nullptr)
                    storeSample (key, data);
            }
            results[i] = data;
            done.fetch_add (1);
        }
    };

    auto numThreads = (int) juce::jlimit (1u, 8u, std::thread::hardware_concurrency());
    numThreads = juce::jmin (numThreads, (int) refs.size());
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t)
        threads.emplace_back (worker);

    while (done.load() < refs.size() && ! cancelled.load())
    {
        if (progress && ! progress ((float) done.load() / (float) juce::jmax<size_t> (1, refs.size())))
            cancelled = true;
        std::this_thread::sleep_for (std::chrono::milliseconds (15));
        bool allIdle = next.load() >= refs.size() && done.load() >= refs.size();
        if (allIdle)
            break;
    }

    for (auto& t : threads)
        t.join();

    if (cancelled)
        return false;

    juce::StringArray failed;
    for (size_t i = 0; i < refs.size(); ++i)
    {
        if (results[i] != nullptr)
            samples[refs[i].toString()] = results[i];
        else
            failed.add (refs[i].displayName() + " (" + errors[i] + ")");
    }

    for (auto& pad : kit.pads)
        for (auto& layer : pad.layers)
            if (layer.hasSample() && samples.find (layer.sample.toString()) == samples.end())
                layer.missing = true;

    if (! failed.isEmpty())
    {
        juce::StringArray shortList;
        for (int i = 0; i < juce::jmin (4, failed.size()); ++i)
            shortList.add (failed[i]);
        warnings.add (juce::String (failed.size()) + " sample" + (failed.size() == 1 ? "" : "s")
                      + " could not be loaded: " + shortList.joinIntoString ("; ") + (failed.size() > 4 ? "; ..." : ""));
    }

    if (progress)
        progress (1.0f);
    return true;
}

LoadResult KitLoader::loadProgram (const ProgramLocation& location, const std::function<bool (float)>& progress)
{
    LoadResult r;
    if (! parseProgram (location, r.kit, r.warnings, r.error))
        return r;
    if (! loadSamples (r.kit, r.samples, r.warnings, progress))
    {
        r.error = "Loading cancelled.";
        return r;
    }
    r.ok = true;
    return r;
}

LoadResult KitLoader::reloadKit (Kit kit, const std::function<bool (float)>& progress)
{
    LoadResult r;
    for (auto& p : kit.pads)
        for (auto& l : p.layers)
            l.missing = false;
    r.kit = std::move (kit);
    if (! loadSamples (r.kit, r.samples, r.warnings, progress))
    {
        r.error = "Loading cancelled.";
        return r;
    }
    r.ok = true;
    return r;
}

int KitLoader::relinkMissing (Kit& kit, const juce::File& folder)
{
    auto index = SampleIndex::forDirectory (folder, 6);
    int fixed = 0;
    for (auto& pad : kit.pads)
    {
        for (auto& layer : pad.layers)
        {
            if (! layer.missing)
                continue;
            auto name = layer.name.isNotEmpty() ? layer.name : layer.sample.displayName();
            auto fileName = layer.sample.isEmpty() ? juce::String() : layer.sample.toString().fromLastOccurrenceOf ("/", false, false);
            auto ref = index.find (name, fileName, folder.getFullPathName());
            if (! ref.isEmpty())
            {
                layer.sample = ref;
                layer.missing = false;
                ++fixed;
            }
        }
    }
    return fixed;
}

}
