#include "Library.h"
#include "../Model/UserFiles.h"

namespace kaia
{

namespace
{
    bool isJunk (const juce::String& path)
    {
        auto base = path.fromLastOccurrenceOf ("/", false, false);
        return path.startsWith ("__MACOSX/") || path.contains ("/__MACOSX/") || base.startsWith ("._") || base.startsWith (".");
    }

    bool isXpn (const juce::File& f)        { return f.hasFileExtension ("xpn"); }

    juce::String titleFromExpansionXml (const juce::String& text)
    {
        if (auto xml = juce::XmlDocument::parse (text))
            for (auto* e : xml->getChildIterator())
                if (e->getTagName().equalsIgnoreCase ("title"))
                    return e->getAllSubText().trim();
        return {};
    }

    void sortPaths (juce::StringArray& paths)
    {
        std::sort (paths.begin(), paths.end(), [] (const juce::String& a, const juce::String& b)
        {
            auto fa = LibraryPack::folderOf (a), fb = LibraryPack::folderOf (b);
            if (fa != fb)
                return fa.compareNatural (fb) < 0;
            return a.compareNatural (b) < 0;
        });
    }

    struct Walker
    {
        juce::File root;
        const LibraryIndex* previous = nullptr;
        const std::function<bool (const juce::String&)>* keepGoing = nullptr;
        LibraryIndex* out = nullptr;
        int filesSeen = 0;
        bool cancelled = false;

        static constexpr int kMaxFiles = 400000;
        static constexpr int kMaxDepth = 12;

        bool poll (const juce::String& what)
        {
            if (cancelled || filesSeen > kMaxFiles)
                return false;
            if (keepGoing != nullptr && *keepGoing && ! (*keepGoing) (what))
                cancelled = true;
            return ! cancelled;
        }

        juce::String relative (const juce::File& f) const
        {
            auto r = f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
            return r == "." ? juce::String() : r;
        }

        void addArchive (const juce::File& f)
        {
            if (! poll (f.getFileName()))
                return;

            auto size = f.getSize();
            auto modified = f.getLastModificationTime().toMilliseconds();
            if (previous != nullptr)
                for (auto& p : previous->packs)
                    if (p.isArchive && p.file == f && p.fileSize == size && p.modified == modified)
                    {
                        auto copy = p;
                        copy.relativePath = relative (f);
                        out->packs.push_back (std::move (copy));
                        return;
                    }

            auto pack = Library::scanArchive (f);
            pack.relativePath = relative (f);
            if (! pack.programs.isEmpty() || ! pack.samples.isEmpty() || pack.isProtected)
                out->packs.push_back (std::move (pack));
        }

        // A folder that holds programs: one pack with every program and sample below it
        void addProgramFolder (const juce::File& dir)
        {
            LibraryPack pack;
            pack.file = dir;
            pack.title = dir.getFileName();
            pack.relativePath = relative (dir);

            auto expansionXml = dir.getChildFile ("Expansion.xml");
            if (expansionXml.existsAsFile())
            {
                auto t = titleFromExpansionXml (expansionXml.loadFileAsString());
                if (t.isNotEmpty())
                    pack.title = t;
            }

            for (const auto& e : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
            {
                if (++filesSeen > kMaxFiles || ((filesSeen & 1023) == 0 && ! poll (pack.title)))
                    break;
                auto f = e.getFile();
                auto name = f.getFileName();
                auto rel = f.getRelativePathFrom (dir).replaceCharacter ('\\', '/');
                if (isJunk (rel))
                    continue;
                if (isProgramFileName (name))
                    pack.programs.add (rel);
                else if (isAudioFileName (name))
                    pack.samples.add (rel);
                else if (isXpn (f))
                    addArchive (f);
            }

            sortPaths (pack.programs);
            sortPaths (pack.samples);
            out->packs.push_back (std::move (pack));
        }

        void walk (const juce::File& dir, int depth)
        {
            if (depth > kMaxDepth || ! poll (relative (dir)))
                return;

            juce::Array<juce::File> files, dirs;
            for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
            {
                if (++filesSeen > kMaxFiles)
                    break;
                (e.isDirectory() ? dirs : files).add (e.getFile());
            }

            bool hasPrograms = false;
            for (auto& f : files)
                hasPrograms = hasPrograms || isProgramFileName (f.getFileName());

            if (hasPrograms)
            {
                addProgramFolder (dir);
                return;
            }

            LibraryPack samplePack;
            samplePack.file = dir;
            samplePack.title = dir == root ? dir.getFileName() : relative (dir);
            samplePack.relativePath = relative (dir);

            for (auto& f : files)
            {
                if (isXpn (f))
                    addArchive (f);
                else if (isAudioFileName (f.getFileName()) && ! isJunk (f.getFileName()))
                    samplePack.samples.add (f.getFileName());
            }
            if (! samplePack.samples.isEmpty())
            {
                sortPaths (samplePack.samples);
                out->packs.push_back (std::move (samplePack));
            }

            dirs.sort();
            for (auto& d : dirs)
                if (! isJunk (d.getFileName()))
                    walk (d, depth + 1);
        }
    };
}

//==============================================================================
ProgramLocation LibraryPack::programLocation (int i) const
{
    auto path = programs[i];
    if (isArchive)
        return { file, path };
    return { file.getChildFile (path), {} };
}

SampleRef LibraryPack::sampleRef (int i) const
{
    auto path = samples[i];
    if (isArchive)
        return { file.getFullPathName(), path };
    return { file.getChildFile (path).getFullPathName(), {} };
}

juce::String LibraryPack::displayName (const juce::String& path)
{
    auto base = path.fromLastOccurrenceOf ("/", false, false);
    return base.containsChar ('.') ? base.upToLastOccurrenceOf (".", false, false) : base;
}

juce::String LibraryPack::folderOf (const juce::String& path)
{
    return path.containsChar ('/') ? path.upToLastOccurrenceOf ("/", false, false) : juce::String();
}

int LibraryIndex::numPrograms() const
{
    int n = 0;
    for (auto& p : packs)
        n += p.programs.size();
    return n;
}

int LibraryIndex::numSamples() const
{
    int n = 0;
    for (auto& p : packs)
        n += p.samples.size();
    return n;
}

std::unique_ptr<juce::XmlElement> LibraryIndex::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("KaiaLibrary");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("root", root.getFullPathName());
    for (auto& p : packs)
    {
        auto* e = xml->createNewChildElement ("Pack");
        e->setAttribute ("file", p.file.getFullPathName());
        e->setAttribute ("title", p.title);
        e->setAttribute ("relative", p.relativePath);
        e->setAttribute ("archive", p.isArchive ? 1 : 0);
        e->setAttribute ("protected", p.isProtected ? 1 : 0);
        e->setAttribute ("size", juce::String (p.fileSize));
        e->setAttribute ("modified", juce::String (p.modified));
        e->createNewChildElement ("Programs")->addTextElement (p.programs.joinIntoString ("\n"));
        e->createNewChildElement ("Samples")->addTextElement (p.samples.joinIntoString ("\n"));
    }
    return xml;
}

LibraryIndex LibraryIndex::fromXml (const juce::XmlElement& xml)
{
    LibraryIndex index;
    if (! xml.hasTagName ("KaiaLibrary"))
        return index;
    index.root = juce::File (xml.getStringAttribute ("root"));
    for (auto* e : xml.getChildWithTagNameIterator ("Pack"))
    {
        auto path = e->getStringAttribute ("file");
        if (! juce::File::isAbsolutePath (path))
            continue;
        LibraryPack p;
        p.file = juce::File (path);
        p.title = e->getStringAttribute ("title");
        p.relativePath = e->getStringAttribute ("relative");
        p.isArchive = e->getIntAttribute ("archive") != 0;
        p.isProtected = e->getIntAttribute ("protected") != 0;
        p.fileSize = e->getStringAttribute ("size").getLargeIntValue();
        p.modified = e->getStringAttribute ("modified").getLargeIntValue();
        if (auto* pr = e->getChildByName ("Programs"))
            p.programs = juce::StringArray::fromLines (pr->getAllSubText());
        if (auto* s = e->getChildByName ("Samples"))
            p.samples = juce::StringArray::fromLines (s->getAllSubText());
        p.programs.removeEmptyStrings();
        p.samples.removeEmptyStrings();
        index.packs.push_back (std::move (p));
    }
    return index;
}

//==============================================================================
LibraryPack Library::scanArchive (const juce::File& xpn)
{
    LibraryPack pack;
    pack.file = xpn;
    pack.isArchive = true;
    pack.title = xpn.getFileNameWithoutExtension();
    pack.fileSize = xpn.getSize();
    pack.modified = xpn.getLastModificationTime().toMilliseconds();

    ZipReader zip (xpn);
    if (! zip.isValid())
        return pack;

    int encryptedPrograms = 0;
    for (int i = 0; i < (int) zip.getEntries().size(); ++i)
    {
        auto& e = zip.getEntries()[(size_t) i];
        if (e.isDirectory() || isJunk (e.name))
            continue;
        auto base = e.name.fromLastOccurrenceOf ("/", false, false);

        if (base.equalsIgnoreCase ("Expansion.xml"))
        {
            juce::MemoryBlock mb;
            juce::String err;
            if (zip.readEntry (i, mb, err))
            {
                auto t = titleFromExpansionXml (bytesToText (mb));
                if (t.isNotEmpty())
                    pack.title = t;
            }
        }
        else if (isProgramFileName (base))
        {
            if (e.encrypted)
                ++encryptedPrograms;
            else
                pack.programs.add (e.name);
        }
        else if (isAudioFileName (base) && ! e.encrypted)
        {
            pack.samples.add (e.name);
        }
    }

    pack.isProtected = pack.programs.isEmpty() && (encryptedPrograms > 0 || zip.hasEncryptedEntries());
    sortPaths (pack.programs);
    sortPaths (pack.samples);
    return pack;
}

LibraryIndex Library::scan (const juce::File& root, const LibraryIndex* previous, const std::function<bool (const juce::String&)>& keepGoing)
{
    LibraryIndex index;
    index.root = root;
    if (! root.isDirectory())
        return index;

    Walker w;
    w.root = root;
    w.previous = previous;
    w.keepGoing = &keepGoing;
    w.out = &index;
    w.walk (root, 0);

    std::stable_sort (index.packs.begin(), index.packs.end(), [] (const LibraryPack& a, const LibraryPack& b)
    {
        return a.title.compareNatural (b.title) < 0;
    });
    return index;
}

//==============================================================================
class LibraryService::ScanThread : public juce::Thread
{
public:
    explicit ScanThread (LibraryService& s) : juce::Thread ("Kaia library scan"), service (s) {}

    ~ScanThread() override
    {
        signalThreadShouldExit();
        wake.signal();
        stopThread (10000);
    }

    void request (const juce::File& root, bool loadCache)
    {
        {
            const juce::ScopedLock sl (requestLock);
            requestedRoot = root;
            requestLoadCache = requestLoadCache || loadCache;
            ++requestSerial;
        }
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        wake.signal();
    }

    void run() override
    {
        int handled = 0;
        while (! threadShouldExit())
        {
            juce::File root;
            bool loadCache;
            int serial;
            {
                const juce::ScopedLock sl (requestLock);
                root = requestedRoot;
                loadCache = requestLoadCache;
                serial = requestSerial;
                requestLoadCache = false;
            }

            if (serial == handled)
            {
                wake.wait (1000);
                continue;
            }
            handled = serial;
            service.scanning = true;

            std::shared_ptr<const LibraryIndex> previous = service.getIndex();
            if (loadCache)
            {
                if (auto xml = juce::parseXML (UserFiles::libraryCacheFile()))
                {
                    auto cached = std::make_shared<LibraryIndex> (LibraryIndex::fromXml (*xml));
                    if (cached->root == root)
                    {
                        previous = cached;
                        publish (cached, "Updating library...");
                    }
                }
            }

            juce::uint32 lastStatus = 0;
            auto keepGoing = [&] (const juce::String& what)
            {
                auto now = juce::Time::getMillisecondCounter();
                if (now - lastStatus > 250)
                {
                    lastStatus = now;
                    setStatus ("Scanning " + what);
                }
                const juce::ScopedLock sl (requestLock);
                return ! threadShouldExit() && requestSerial == serial;
            };

            auto result = std::make_shared<LibraryIndex> (Library::scan (root, previous.get(), keepGoing));
            bool superseded;
            {
                const juce::ScopedLock sl (requestLock);
                superseded = requestSerial != serial;
            }
            if (threadShouldExit())
                break;
            if (superseded)
                continue;

            // Cache for the next session (the config folder is the only place Kaia writes to)
            if (root.isDirectory())
            {
                UserFiles::configDirectory().createDirectory();
                result->toXml()->writeTo (UserFiles::libraryCacheFile());
            }

            juce::String summary;
            if (root == juce::File())
                summary = "No library folder set.";
            else if (! root.isDirectory())
                summary = root.getFullPathName() + " not found.";
            else
                summary = juce::String ((int) result->packs.size()) + " packs, " + juce::String (result->numPrograms())
                          + " programs, " + juce::String (result->numSamples()) + " samples";
            service.scanning = false;
            publish (result, summary);
        }
        service.scanning = false;
    }

private:
    void setStatus (const juce::String& s)
    {
        {
            const juce::ScopedLock sl (service.lock);
            service.status = s;
        }
        service.triggerAsyncUpdate();
    }

    void publish (std::shared_ptr<const LibraryIndex> idx, const juce::String& s)
    {
        {
            const juce::ScopedLock sl (service.lock);
            service.index = std::move (idx);
            service.status = s;
        }
        service.triggerAsyncUpdate();
    }

    LibraryService& service;
    juce::CriticalSection requestLock;
    juce::File requestedRoot;
    bool requestLoadCache = false;
    int requestSerial = 0;
    juce::WaitableEvent wake;
};

LibraryService::LibraryService()
{
    juce::SharedResourcePointer<SharedSettings> settings;
    auto path = settings->get().getValue ("libraryFolder");
    root = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    index = std::make_shared<LibraryIndex>();
    status = root == juce::File() ? "No library folder set." : "Not scanned yet.";
    thread = std::make_unique<ScanThread> (*this);
}

LibraryService::~LibraryService()
{
    cancelPendingUpdate();
    thread.reset();
}

juce::File LibraryService::getRoot() const
{
    const juce::ScopedLock sl (lock);
    return root;
}

void LibraryService::setRoot (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        root = folder;
    }
    juce::SharedResourcePointer<SharedSettings> settings;
    settings->get().setValue ("libraryFolder", folder.getFullPathName());
    settings->get().saveIfNeeded();
    started = true;
    startScan (false);
}

void LibraryService::rescan()
{
    started = true;
    startScan (false);
}

void LibraryService::ensureScanned()
{
    if (started)
        return;
    started = true;
    startScan (true);
}

void LibraryService::startScan (bool loadCacheFirst)
{
    scanning = true;
    thread->request (getRoot(), loadCacheFirst);
    sendChangeMessage();
}

std::shared_ptr<const LibraryIndex> LibraryService::getIndex() const
{
    const juce::ScopedLock sl (lock);
    return index;
}

juce::String LibraryService::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void LibraryService::handleAsyncUpdate()
{
    sendChangeMessage();
}

}
