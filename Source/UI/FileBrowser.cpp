#include "FileBrowser.h"

namespace kaia
{

namespace
{
    enum class FileKind { none, sample, program, expansion };

    FileKind kindOf (const juce::File& f)
    {
        auto name = f.getFileName();
        if (isAudioFileName (name))
            return FileKind::sample;
        if (isProgramFileName (name) || Presets::isPresetFile (f))
            return FileKind::program;
        if (f.hasFileExtension ("xpn"))
            return FileKind::expansion;
        return FileKind::none;
    }

    // Path components, the file name last: sorted like the tree shows them, at each level folders
    // first, then files, each in natural order ("Kick 2" before "Kick 10")
    bool displaysBefore (const juce::StringArray& a, const juce::StringArray& b)
    {
        for (int i = 0; i < a.size() && i < b.size(); ++i)
        {
            auto aIsFile = i == a.size() - 1, bIsFile = i == b.size() - 1;
            if (aIsFile != bIsFile)
                return bIsFile;
            if (auto c = a[i].compareNatural (b[i]); c != 0)
                return c < 0;
        }
        return false;
    }

    juce::StringArray filterWords (const juce::String& text)
    {
        auto words = juce::StringArray::fromTokens (text.trim(), " ", "\"");
        words.removeEmptyStrings();
        return words;
    }

    constexpr int kMaxListed = 300000;     // files gathered for the filter
    constexpr int kMaxResults = 1000;      // files shown for one filter
}

//==============================================================================
class FileBrowser::Item : public juce::TreeViewItem
{
public:
    Item (FileBrowser& b, juce::File f) : browser (b), file (std::move (f)) {}

    int getItemHeight() const override          { return 36; }
    juce::String getUniqueName() const override { return file.getFileName(); }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        auto& fonts = browser.fonts;
        if (! cached)
        {
            cached = true;
            text = getText();
            detail = getDetail();
            if (detail.isNotEmpty())
                detailWidth = juce::GlyphArrangement::getStringWidthInt (fonts.detail, detail) + 8;
        }

        auto area = juce::Rectangle<int> (width, height);
        if (isSelected())
        {
            g.setColour (Colours::accent.withAlpha (0.22f));
            g.fillRoundedRectangle (area.reduced (1, 1).toFloat(), 4.0f);
        }

        auto r = area.reduced (4, 0);
        if (detail.isNotEmpty())
        {
            g.setColour (Colours::textDim);
            g.setFont (fonts.detail);
            g.drawText (detail, r.removeFromRight (juce::jmin (r.getWidth() / 2, detailWidth)), juce::Justification::centredRight);
        }

        g.setColour (isSelected() ? Colours::accent : Colours::text);
        g.setFont (isBold() ? fonts.bold : fonts.plain);
        g.drawText (text, r, juce::Justification::centredLeft, true);
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        // Acts on release (FileBrowser::mouseUp), so dragging the list to scroll does nothing
        if (e.mods.isPopupMenu())
            showMenu();
        else
            browser.pendingClick = getItemIdentifierString();
    }

    void itemDoubleClicked (const juce::MouseEvent&) override { activate(); }

    void itemSelectionChanged (bool nowSelected) override
    {
        // Keyboard navigation plays what it lands on (mouse clicks are handled in clicked())
        if (nowSelected && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            playIfWanted();
    }

    virtual juce::String getText() const    { return file.getFileName(); }
    virtual juce::String getDetail() const  { return {}; }
    virtual bool isBold() const             { return false; }
    virtual void clicked()                  { if (mightContainSubItems()) setOpen (! isOpen()); }
    virtual void activate()                 {}
    virtual void audition()                 {}
    virtual void showMenu()                 {}

    void playIfWanted()
    {
        if (browser.autoPreview.getToggleState())
            audition();
    }

    FileBrowser& browser;
    const juce::File file;

private:
    bool cached = false;
    juce::String text, detail;
    int detailWidth = 0;
};

//==============================================================================
class FileBrowser::FileItem : public Item
{
public:
    FileItem (FileBrowser& b, juce::File f, FileKind k) : Item (b, std::move (f)), kind (k) {}

    bool mightContainSubItems() override    { return false; }
    juce::String getText() const override   { return kind == FileKind::sample ? file.getFileNameWithoutExtension() : file.getFileName(); }
    juce::String getDetail() const override
    {
        if (kind == FileKind::program)   return "program";
        if (kind == FileKind::expansion) return "expansion";
        return {};
    }
    bool isBold() const override            { return kind != FileKind::sample; }

    SampleRef ref() const                   { return { file.getFullPathName(), {} }; }

    juce::var getDragSourceDescription() override
    {
        // Touch screens: only a drag that starts sideways (towards the pads) picks the sample up
        if (kind != FileKind::sample || browser.scroller->pressIsScrolling())
            return {};
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto delta = source.getScreenPosition() - source.getLastMouseDownPosition();
        if (std::abs (delta.y) > std::abs (delta.x))
            return {};
        auto* o = new juce::DynamicObject();
        o->setProperty ("kaiaLibrary", "sample");      // dropped on a pad like a library sample
        o->setProperty ("ref", ref().toString());
        return juce::var (o);
    }

    void clicked() override
    {
        if (kind == FileKind::sample)
            playIfWanted();
        else
            activate();
    }

    void activate() override
    {
        auto& p = browser.processor;
        if (kind == FileKind::sample)
            p.assignSamplesToPad (p.selectedPad, std::vector<SampleRef> { ref() });
        else
            p.openPack (file);
    }

    void itemDoubleClicked (const juce::MouseEvent&) override
    {
        if (kind == FileKind::sample)
            activate();                 // programs were loaded by the first click
    }

    void audition() override
    {
        if (kind == FileKind::sample)
            browser.processor.previewSample (ref());
    }

    void showMenu() override
    {
        juce::PopupMenu m;
        auto target = browser.targetPadName();
        if (kind == FileKind::sample)
        {
            m.addItem (1, "Play");
            m.addItem (2, "Put on pad " + target);
            m.addItem (3, "Add as layer to pad " + target);
        }
        else
        {
            m.addItem (4, kind == FileKind::expansion ? "Open expansion" : "Load program");
        }
        m.addItem (5, "Show in file manager");
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            auto& p = browser.processor;
            if (r == 1) p.previewSample (ref());
            if (r == 2 || r == 4) activate();
            if (r == 3) p.assignSamplesToPad (p.selectedPad, std::vector<SampleRef> { ref() }, KaiaProcessor::AssignMode::Append);
            if (r == 5) file.revealToUser();
        });
    }

private:
    FileKind kind;
};

//==============================================================================
// A folder. Opening it lists it from disk, unless it was filled with filter results.
class FileBrowser::FolderItem : public Item
{
public:
    FolderItem (FileBrowser& b, juce::File f, bool filled = false) : Item (b, std::move (f)), filledByFilter (filled) {}

    bool mightContainSubItems() override { return true; }
    bool canBeSelected() const override  { return true; }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || filledByFilter || listed)
            return;
        listed = true;
        struct Child { juce::File file; juce::StringArray key; };
        std::vector<Child> children;
        for (auto& e : juce::RangedDirectoryIterator (file, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
            if (e.isDirectory() || kindOf (e.getFile()) != FileKind::none)
                children.push_back ({ e.getFile(), e.isDirectory() ? juce::StringArray (e.getFile().getFileName(), juce::String())   // a folder sorts first
                                                                   : juce::StringArray (e.getFile().getFileName()) });
        std::sort (children.begin(), children.end(), [] (const Child& a, const Child& b) { return displaysBefore (a.key, b.key); });
        for (auto& c : children)
        {
            if (c.key.size() > 1)
                addSubItem (new FolderItem (browser, c.file));
            else
                addSubItem (new FileItem (browser, c.file, kindOf (c.file)));
        }
    }

    void showMenu() override
    {
        juce::PopupMenu m;
        m.addItem (1, isOpen() ? "Close" : "Open");
        m.addItem (2, "Load as kit (its programs or samples)");
        m.addItem (3, "Show in file manager");
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r == 1) setOpen (! isOpen());
            if (r == 2) browser.processor.openPack (file);
            if (r == 3) file.revealToUser();
        });
    }

private:
    bool filledByFilter, listed = false;
};

//==============================================================================
FileBrowser::FileBrowser (KaiaProcessor& p) : processor (p)
{
    filter.setTextToShowWhenEmpty ("Filter by file or folder name", Colours::textDim);
    filter.onTextChange = [this] { rebuildTree(); };
    addAndMakeVisible (filter);

    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setIndentSize (32);
    tree.setColour (juce::TreeView::backgroundColourId, Colours::panel);
    tree.setColour (juce::TreeView::linesColourId, Colours::outline);
    tree.setColour (juce::TreeView::selectedItemBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (tree);

    // Scrolling as in the library (KineticScroller); scrollBar replaces the tree's own
    tree.getViewport()->setScrollBarsShown (false, false, false, false);
    scrollBar.setAutoHide (true);
    addAndMakeVisible (scrollBar);
    scroller = std::make_unique<KineticScroller> (*tree.getViewport(), tree, &scrollBar);
    tree.addMouseListener (this, true);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    folderButton.setTooltip ("Choose the folder this tab starts in");
    folderButton.onClick = [this] { if (onChooseFolder) onChooseFolder(); };
    addAndMakeVisible (closeButton);
    addAndMakeVisible (folderButton);

    autoPreview.setToggleState (true, juce::dontSendNotification);
    autoPreview.setTooltip ("Play samples when you select them");
    addAndMakeVisible (autoPreview);

    hint.setFont (uiFont (12.0f));
    hint.setColour (juce::Label::textColourId, Colours::textDim);
    hint.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (hint);
}

FileBrowser::~FileBrowser()
{
    if (listingCancel != nullptr)
        listingCancel->store (true);
    pool.removeAllJobs (true, 4000);
    tree.removeMouseListener (this);
    scroller = nullptr;
    tree.setRootItem (nullptr);
}

juce::File FileBrowser::rootFolder()
{
    juce::SharedResourcePointer<SharedSettings> settings;
    juce::File dir (settings->get().getValue ("filesFolder"));
    if (dir.isDirectory())
        return dir;
    juce::SharedResourcePointer<LibraryService> library;
    if (library->getRoot().isDirectory())
        return library->getRoot();
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

void FileBrowser::setRootFolder (const juce::File& folder)
{
    juce::SharedResourcePointer<SharedSettings> settings;
    settings->get().setValue ("filesFolder", folder.getFullPathName());
    settings->get().saveIfNeeded();
}

juce::String FileBrowser::targetPadName() const
{
    return padName (processor.selectedPad);
}

void FileBrowser::visibilityChanged()
{
    if (isVisible())
    {
        scroller->setFling (KineticScroller::flingFromSettings());
        listing = nullptr;          // read the folder again: files may have come and gone
        refresh();
        startTimerHz (20);
    }
    else
    {
        stopTimer();
        processor.stopPreview();
        if (listingCancel != nullptr)
            listingCancel->store (true);
    }
}

void FileBrowser::refresh()
{
    auto dir = rootFolder();
    if (dir != root)
    {
        root = dir;
        listing = nullptr;
        if (listingCancel != nullptr)
            listingCancel->store (true);
        tree.setRootItem (nullptr);
        rootItem = nullptr;         // nothing of the old folder's openness applies
        savedOpenness = nullptr;
    }
    rebuildTree();
    repaint();
}

void FileBrowser::rebuildTree()
{
    auto words = filterWords (filter.getText());
    auto filtering = ! words.isEmpty();

    // Browsing: folders that were open stay open (and are listed again from disk), also across a
    // filter
    std::unique_ptr<juce::XmlElement> openness;
    if (rootItem != nullptr && ! rootItemShowsFilter)
        openness = tree.getOpennessState (true);
    if (filtering)
    {
        if (openness != nullptr)
            savedOpenness = std::move (openness);
    }
    else if (openness == nullptr)
    {
        openness = std::move (savedOpenness);
    }

    tree.setRootItem (nullptr);
    rootItem = std::make_unique<FolderItem> (*this, root, filtering);
    rootItemShowsFilter = filtering;

    if (filtering)
    {
        buildFilteredTree (words);
        tree.setRootItem (rootItem.get());
        rootItem->setOpen (true);
    }
    else
    {
        tree.setRootItem (rootItem.get());
        rootItem->setOpen (true);
        if (openness != nullptr)
            tree.restoreOpennessState (*openness, true);
    }
    scroller->syncScrollBar();
    updateHint();
}

void FileBrowser::buildFilteredTree (const juce::StringArray& words)
{
    shownResults = 0;
    resultsCapped = false;
    if (listing == nullptr || listing->root != root)
    {
        startListing();             // rebuilds the tree when the folder has been read
        return;
    }

    // The matching files under their folders, all open
    std::map<juce::String, FolderItem*> folders { { juce::String(), rootItem.get() } };
    std::function<FolderItem* (const juce::String&)> folderFor = [&] (const juce::String& path) -> FolderItem*
    {
        if (auto it = folders.find (path); it != folders.end())
            return it->second;
        auto slash = path.lastIndexOfChar ('/');
        auto* parent = folderFor (slash < 0 ? juce::String() : path.substring (0, slash));
        auto* item = new FolderItem (*this, root.getChildFile (path), true);
        parent->addSubItem (item);
        item->setOpen (true);
        return folders[path] = item;
    };

    for (auto& entry : listing->entries)
    {
        auto name = entry.file.getFileName();
        bool ok = true;
        for (auto& w : words)
            ok = ok && (name.containsIgnoreCase (w) || entry.folder.containsIgnoreCase (w));
        if (! ok)
            continue;
        if (shownResults == kMaxResults)
        {
            resultsCapped = true;
            break;
        }
        ++shownResults;
        folderFor (entry.folder)->addSubItem (new FileItem (*this, entry.file, kindOf (entry.file)));
    }
}

void FileBrowser::startListing()
{
    if (listingCancel != nullptr && ! listingCancel->load())
        return;                     // already reading this folder

    auto cancel = std::make_shared<std::atomic<bool>> (false);
    listingCancel = cancel;
    pool.addJob ([safe = juce::Component::SafePointer<FileBrowser> (this), dir = root, cancel]
    {
        auto result = std::make_shared<Listing>();
        result->root = dir;
        std::vector<std::pair<juce::StringArray, size_t>> keys;     // path components, entry

        for (auto& e : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles,
                                                      juce::File::FollowSymlinks::noCycles))
        {
            if (cancel->load())
                return;
            auto f = e.getFile();
            if (kindOf (f) == FileKind::none)
                continue;
            if ((int) result->entries.size() == kMaxListed)
            {
                result->truncated = true;
                break;
            }
            auto folder = f.getParentDirectory() == dir ? juce::String()
                                                        : f.getParentDirectory().getRelativePathFrom (dir).replaceCharacter ('\\', '/');
            auto key = juce::StringArray::fromTokens (folder, "/", "");
            key.removeEmptyStrings();
            key.add (f.getFileName());
            keys.push_back ({ std::move (key), result->entries.size() });
            result->entries.push_back ({ f, folder });
        }

        std::sort (keys.begin(), keys.end(), [] (const auto& a, const auto& b) { return displaysBefore (a.first, b.first); });
        std::vector<Listing::Entry> sorted;
        sorted.reserve (keys.size());
        for (auto& k : keys)
            sorted.push_back (std::move (result->entries[k.second]));
        result->entries = std::move (sorted);

        juce::MessageManager::callAsync ([safe, result, cancel]
        {
            if (safe == nullptr || cancel->load())
                return;
            cancel->store (true);           // finished
            if (result->root != safe->root)
                return;
            safe->listing = result;
            safe->rebuildTree();
        });
    });
    updateHint();
}

void FileBrowser::updateHint()
{
    hintPad = processor.selectedPad;
    auto words = filterWords (filter.getText());
    juce::String text;
    if (! root.isDirectory())
        text = "The folder " + root.getFullPathName() + " can't be found. Choose another one (Folder...).";
    else if (! words.isEmpty() && (listing == nullptr || listing->root != root))
        text = "Searching " + root.getFullPathName() + "...";
    else if (! words.isEmpty() && shownResults == 0)
        text = "Nothing matches \"" + filter.getText().trim() + "\".";
    else if (resultsCapped || (! words.isEmpty() && listing->truncated))
        text = "Showing the first " + juce::String (shownResults) + " matches. Type more to narrow it down.";
    else
        text = "Click a folder to open it, a sample to hear it, a program to load it. Double-click a sample (or drag it "
               "onto a pad) to put it on pad " + targetPadName() + ".";
    hint.setText (text, juce::dontSendNotification);
}

void FileBrowser::resized()
{
    auto r = getLocalBounds().reduced (12);
    headerArea = r.removeFromTop (52);
    auto buttons = headerArea.withHeight (26).withTrimmedTop (2);
    closeButton.setBounds (buttons.removeFromRight (64));
    buttons.removeFromRight (6);
    folderButton.setBounds (buttons.removeFromRight (76));

    r.removeFromTop (6);
    auto row = r.removeFromTop (28);
    autoPreview.setBounds (row.removeFromRight (118));
    row.removeFromRight (8);
    filter.setBounds (row);
    r.removeFromTop (6);
    hint.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);
    scrollBar.setBounds (r.removeFromRight (10));
    r.removeFromRight (2);
    tree.setBounds (r);
    scroller->syncScrollBar();
}

//==============================================================================
void FileBrowser::mouseUp (const juce::MouseEvent& e)
{
    // Single clicks act on release; not the second click of a double-click, nor the end of a scroll
    auto pending = std::exchange (pendingClick, juce::String());
    if (pending.isEmpty() || scroller->pressIsScrolling() || e.getDistanceFromDragStart() >= 12
        || e.getNumberOfClicks() > 1)
        return;
    if (auto* item = dynamic_cast<Item*> (tree.findItemFromIdentifierString (pending)))
        item->clicked();
}

void FileBrowser::timerCallback()
{
    if (! scroller->isMoving())
        scroller->syncScrollBar();     // folders opened and closed
    if (processor.selectedPad != hintPad)
        updateHint();
}

void FileBrowser::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    auto h = headerArea;
    h.removeFromRight (160);
    g.setColour (Colours::text);
    g.setFont (uiFont (17.0f, true));
    g.drawText ("Files", h.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (12.5f));
    g.drawText (root.getFullPathName(), h.removeFromTop (18), juce::Justification::centredLeft, true);
}

}
