#include "LibraryBrowser.h"

namespace kaia
{

//==============================================================================
class LibraryBrowser::Item : public juce::TreeViewItem
{
public:
    explicit Item (LibraryBrowser& b) : browser (b) {}

    int getItemHeight() const override { return 24; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        // Texts are measured once: painting runs for every visible row on every scroll step
        static const juce::Font plainFont = uiFont (13.5f), boldFont = uiFont (13.5f, true),
                                headingFont = uiFont (11.5f, true), detailFont = uiFont (11.5f);
        if (! cached)
        {
            cached = true;
            text = isHeading() ? getText().toUpperCase() : getText();
            detail = getDetail();
            if (detail.isNotEmpty())
                detailWidth = juce::GlyphArrangement::getStringWidthInt (detailFont, detail) + 8;
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
            g.setFont (detailFont);
            g.drawText (detail, r.removeFromRight (juce::jmin (r.getWidth() / 2, detailWidth)), juce::Justification::centredRight);
        }

        g.setColour (isDisabled() ? Colours::textDim : (isSelected() ? Colours::accent : (isHeading() ? Colours::textDim : Colours::text)));
        g.setFont (isHeading() ? headingFont : isBold() ? boldFont : plainFont);
        g.drawText (text, r, juce::Justification::centredLeft, true);
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showMenu();
    }

    void itemDoubleClicked (const juce::MouseEvent&) override { activate(); }

    virtual juce::String getText() const = 0;
    virtual juce::String getDetail() const   { return {}; }
    virtual bool isHeading() const           { return false; }
    virtual bool isBold() const              { return false; }
    virtual bool isDisabled() const          { return false; }
    virtual void activate()                  { setOpen (! isOpen()); }
    virtual void audition()                  {}
    virtual void showMenu()                  {}

    void itemSelectionChanged (bool nowSelected) override
    {
        if (! nowSelected || ! browser.autoPreview.getToggleState())
            return;
        // Clicks and touches play on release, so dragging the list to scroll stays silent
        if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            browser.pendingAudition = getItemIdentifierString();
        else
            audition();
    }

    // Touch screens on Linux send mouse events, so the list scrolls on any drag: only a drag
    // that starts sideways (towards the pads) picks the item up.
    static bool dragIsVertical()
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto delta = source.getScreenPosition() - source.getLastMouseDownPosition();
        return std::abs (delta.y) > std::abs (delta.x);
    }

    LibraryBrowser& browser;

private:
    bool cached = false;
    juce::String text, detail;
    int detailWidth = 0;
};

//==============================================================================
class LibraryBrowser::SampleItem : public Item
{
public:
    SampleItem (LibraryBrowser& b, std::shared_ptr<const LibraryIndex> idx, int pack, int sample, bool showPack)
        : Item (b), index (std::move (idx)), packIndex (pack), sampleIndex (sample), withPack (showPack) {}

    const LibraryPack& pack() const      { return index->packs[(size_t) packIndex]; }
    SampleRef ref() const                { return pack().sampleRef (sampleIndex); }

    juce::String getText() const override   { return LibraryPack::displayName (pack().samples[sampleIndex]); }
    juce::String getDetail() const override { return withPack ? pack().title : juce::String(); }
    bool mightContainSubItems() override    { return false; }
    juce::String getUniqueName() const override { return "s:" + ref().toString(); }

    juce::var getDragSourceDescription() override
    {
        if (dragIsVertical())
            return {};
        auto* o = new juce::DynamicObject();
        o->setProperty ("kaiaLibrary", "sample");
        o->setProperty ("ref", ref().toString());
        return juce::var (o);
    }

    void audition() override { browser.processor.previewSample (ref()); }
    void activate() override { browser.processor.assignSamplesToPad (browser.processor.selectedPad, std::vector<SampleRef> { ref() }); }

    void showMenu() override
    {
        auto target = browser.targetPadName();
        juce::PopupMenu m;
        m.addItem (1, "Play");
        m.addItem (2, "Put on pad " + target);
        m.addItem (3, "Add as layer to pad " + target);
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            auto& p = browser.processor;
            if (r == 1) p.previewSample (ref());
            if (r == 2) p.assignSamplesToPad (p.selectedPad, std::vector<SampleRef> { ref() });
            if (r == 3) p.assignSamplesToPad (p.selectedPad, std::vector<SampleRef> { ref() }, KaiaProcessor::AssignMode::Append);
        });
    }

private:
    std::shared_ptr<const LibraryIndex> index;
    int packIndex, sampleIndex;
    bool withPack;
};

//==============================================================================
class LibraryBrowser::PadItem : public Item
{
public:
    PadItem (LibraryBrowser& b, ProgramLocation loc, std::shared_ptr<const Kit> k, int pad)
        : Item (b), location (std::move (loc)), kit (std::move (k)), padIndex (pad) {}

    const Pad& pad() const { return kit->pads[(size_t) padIndex]; }

    juce::String getText() const override
    {
        auto& p = pad();
        auto name = p.name.isNotEmpty() ? p.name : p.layers.front().name.trim();
        if (name.isEmpty())
            name = p.layers.front().sample.displayName();
        return (p.isKeygroup() ? "KG " + juce::String (padIndex + 1) : padName (padIndex)) + "   " + name;
    }

    juce::String getDetail() const override
    {
        auto n = (int) pad().layers.size();
        juce::String d = n > 1 ? juce::String (n) + " layers" : juce::String();
        bool missing = false;
        for (auto& l : pad().layers)
            missing = missing || l.missing;
        return missing ? (d.isEmpty() ? "missing" : d + ", missing") : d;
    }

    bool mightContainSubItems() override        { return false; }
    juce::String getUniqueName() const override { return "p:" + location.toString() + "#" + juce::String (padIndex); }

    juce::var getDragSourceDescription() override
    {
        if (dragIsVertical())
            return {};
        auto* o = new juce::DynamicObject();
        o->setProperty ("kaiaLibrary", "pad");
        o->setProperty ("program", location.toString());
        o->setProperty ("pad", padIndex);
        return juce::var (o);
    }

    void audition() override { browser.processor.previewPad (pad()); }
    void activate() override { browser.processor.importPad (browser.processor.selectedPad, pad()); }

    void showMenu() override
    {
        juce::PopupMenu m;
        m.addItem (1, "Play");
        m.addItem (2, "Copy to pad " + browser.targetPadName());
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r == 1) audition();
            if (r == 2) activate();
        });
    }

private:
    ProgramLocation location;
    std::shared_ptr<const Kit> kit;
    int padIndex;
};

//==============================================================================
class LibraryBrowser::ProgramItem : public Item
{
public:
    ProgramItem (LibraryBrowser& b, std::shared_ptr<const LibraryIndex> idx, int pack, int program, bool showPack)
        : Item (b), index (std::move (idx)), packIndex (pack), programIndex (program), withPack (showPack) {}

    const LibraryPack& pack() const          { return index->packs[(size_t) packIndex]; }
    ProgramLocation location() const         { return pack().programLocation (programIndex); }

    juce::String getText() const override    { return LibraryPack::displayName (pack().programs[programIndex]); }
    juce::String getDetail() const override  { return withPack ? pack().title : juce::String(); }
    bool mightContainSubItems() override     { return true; }
    juce::String getUniqueName() const override { return "g:" + location().toString(); }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || getNumSubItems() > 0)
            return;
        auto kit = browser.programKit (location());
        if (kit == nullptr)
            return;
        for (int i = 0; i < (int) kit->pads.size(); ++i)
            if (! kit->pads[(size_t) i].layers.empty())
                addSubItem (new PadItem (browser, location(), kit, i));
    }

    void activate() override  { browser.processor.openPack (pack().file, location()); }

    void showMenu() override
    {
        juce::PopupMenu m;
        m.addItem (1, "Load program");
        m.addItem (2, "Show pads");
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r == 1) activate();
            if (r == 2) setOpen (true);
        });
    }

private:
    std::shared_ptr<const LibraryIndex> index;
    int packIndex, programIndex;
    bool withPack;
};

//==============================================================================
// A heading that lists programs or samples of a pack (optionally of one sub folder)
class LibraryBrowser::GroupItem : public Item
{
public:
    enum Kind { Programs, Samples, SampleFolder };

    GroupItem (LibraryBrowser& b, std::shared_ptr<const LibraryIndex> idx, int pack, Kind k, juce::String folderName = {})
        : Item (b), index (std::move (idx)), packIndex (pack), kind (k), folder (std::move (folderName)) {}

    const LibraryPack& pack() const { return index->packs[(size_t) packIndex]; }

    juce::String getText() const override
    {
        if (kind == SampleFolder)
            return folder;
        return kind == Programs ? "Programs" : "Samples";
    }

    juce::String getDetail() const override
    {
        if (kind == Programs) return juce::String (pack().programs.size());
        if (kind == Samples)  return juce::String (pack().samples.size());
        int n = 0;
        for (auto& s : pack().samples)
            if (LibraryPack::folderOf (s) == folder)
                ++n;
        return juce::String (n);      // computed once per item (see Item::paintItem)
    }

    bool isHeading() const override             { return kind != SampleFolder; }
    bool mightContainSubItems() override        { return true; }
    bool canBeSelected() const override         { return kind == SampleFolder; }
    juce::String getUniqueName() const override { return juce::String ((int) kind) + ":" + pack().file.getFullPathName() + ":" + folder; }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || getNumSubItems() > 0)
            return;

        auto& p = pack();
        if (kind == Programs)
        {
            for (int i = 0; i < p.programs.size(); ++i)
                addSubItem (new ProgramItem (browser, index, packIndex, i, false));
            return;
        }

        // Samples: sub folders first (when the pack has more than one), then loose files
        juce::StringArray folders;
        for (auto& s : p.samples)
            folders.addIfNotAlreadyThere (LibraryPack::folderOf (s));

        if (kind == Samples && folders.size() > 1)
        {
            for (auto& f : folders)
                if (f.isNotEmpty())
                    addSubItem (new GroupItem (browser, index, packIndex, SampleFolder, f));
            for (int i = 0; i < p.samples.size(); ++i)
                if (LibraryPack::folderOf (p.samples[i]).isEmpty())
                    addSubItem (new SampleItem (browser, index, packIndex, i, false));
            return;
        }

        for (int i = 0; i < p.samples.size(); ++i)
            if (kind == Samples || LibraryPack::folderOf (p.samples[i]) == folder)
                addSubItem (new SampleItem (browser, index, packIndex, i, false));
    }

private:
    std::shared_ptr<const LibraryIndex> index;
    int packIndex;
    Kind kind;
    juce::String folder;
};

//==============================================================================
class LibraryBrowser::PackItem : public Item
{
public:
    PackItem (LibraryBrowser& b, std::shared_ptr<const LibraryIndex> idx, int pack)
        : Item (b), index (std::move (idx)), packIndex (pack) {}

    const LibraryPack& pack() const { return index->packs[(size_t) packIndex]; }

    juce::String getText() const override    { return pack().title; }
    bool isBold() const override             { return true; }
    bool isDisabled() const override         { return pack().isProtected; }
    juce::String getDetail() const override
    {
        auto& p = pack();
        if (p.isProtected)
            return "copy-protected";
        juce::StringArray parts;
        if (! p.programs.isEmpty()) parts.add (juce::String (p.programs.size()) + " prog");
        if (! p.samples.isEmpty())  parts.add (juce::String (p.samples.size()) + " smp");
        return parts.joinIntoString ("  ");
    }
    bool mightContainSubItems() override     { return ! pack().isProtected; }
    juce::String getUniqueName() const override { return "k:" + pack().file.getFullPathName(); }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || getNumSubItems() > 0)
            return;
        if (! pack().programs.isEmpty())
            addSubItem (new GroupItem (browser, index, packIndex, GroupItem::Programs));
        if (! pack().samples.isEmpty())
            addSubItem (new GroupItem (browser, index, packIndex, GroupItem::Samples));
        if (getNumSubItems() == 1)
            getSubItem (0)->setOpen (true);
    }

    void showMenu() override
    {
        juce::PopupMenu m;
        m.addItem (1, "Open in Programs list", ! pack().programs.isEmpty());
        m.addItem (2, "Show in file manager");
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r == 1) browser.processor.openPack (pack().file);
            if (r == 2) pack().file.revealToUser();
        });
    }

private:
    std::shared_ptr<const LibraryIndex> index;
    int packIndex;
};

//==============================================================================
class LibraryBrowser::PresetItem : public Item
{
public:
    PresetItem (LibraryBrowser& b, Presets::Info i) : Item (b), info (std::move (i)) {}

    juce::String getText() const override       { return info.name; }
    bool mightContainSubItems() override        { return false; }
    juce::String getUniqueName() const override { return "r:" + info.file.getFullPathName(); }
    void activate() override                    { browser.processor.loadPreset (info.file); }

    void showMenu() override
    {
        juce::PopupMenu m;
        m.addItem (1, "Load preset");
        m.addItem (2, "Delete preset");
        m.addItem (3, "Show in file manager");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, file = info.file, b = &browser] (int r)
        {
            if (r == 1) b->processor.loadPreset (file);
            if (r == 3) file.revealToUser();
            if (r == 2)
            {
                auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Delete preset",
                                                                            "Delete the preset \"" + file.getFileNameWithoutExtension() + "\"?\n"
                                                                            "Only the preset file is removed, never any samples.",
                                                                            "Delete", "Cancel");
                juce::AlertWindow::showAsync (options, [file, safe = juce::Component::SafePointer<LibraryBrowser> (b)] (int result)
                {
                    if (result == 1 && Presets::remove (file) && safe != nullptr)
                        safe->refresh();
                });
            }
        });
    }

private:
    Presets::Info info;
};

//==============================================================================
class LibraryBrowser::RootItem : public juce::TreeViewItem
{
public:
    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return "root"; }
};

namespace
{
    // Heading that groups a flat list (presets, search results)
    class Heading : public juce::TreeViewItem
    {
    public:
        explicit Heading (juce::String t) : text (std::move (t)) {}
        bool mightContainSubItems() override        { return true; }
        bool canBeSelected() const override         { return false; }
        int getItemHeight() const override          { return 26; }
        juce::String getUniqueName() const override { return "h:" + text; }
        void paintItem (juce::Graphics& g, int width, int height) override
        {
            g.setColour (Colours::textDim);
            g.setFont (uiFont (11.5f, true));
            g.drawText (text.toUpperCase(), 4, 0, width - 8, height, juce::Justification::centredLeft);
        }
    private:
        juce::String text;
    };
}

//==============================================================================
LibraryBrowser::LibraryBrowser (KaiaProcessor& p) : processor (p)
{
    search.setTextToShowWhenEmpty ("Search programs and samples", Colours::textDim);
    search.setFont (uiFont (14.0f));
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (8, 0);
    search.onTextChange = [this] { rebuildTree(); };
    search.onEscapeKey = [this] { search.clear(); rebuildTree(); };
    addAndMakeVisible (search);

    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setIndentSize (14);
    tree.setColour (juce::TreeView::backgroundColourId, Colours::panel);
    tree.setColour (juce::TreeView::linesColourId, Colours::outline);
    tree.setColour (juce::TreeView::selectedItemBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (tree);

    // Scrolling: drag anywhere (touch screens), and the wheel glides instead of jumping. The tree's
    // own scroll bars are hidden so wheel events reach mouseWheelMove(); scrollBar replaces them.
    if (auto* vp = tree.getViewport())
    {
        vp->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        vp->setScrollBarsShown (false, false, false, false);
    }
    scrollBar.setAutoHide (true);
    scrollBar.addListener (this);
    addAndMakeVisible (scrollBar);
    tree.addMouseListener (this, true);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    rescanButton.setTooltip ("Scan the library folder again");
    rescanButton.onClick = [this] { library->rescan(); };
    folderButton.setTooltip ("Choose the folder that holds your expansions and samples");
    folderButton.onClick = [this] { if (onChooseFolder) onChooseFolder(); };
    for (auto* b : { &closeButton, &rescanButton, &folderButton })
        addAndMakeVisible (b);

    autoPreview.setToggleState (true, juce::dontSendNotification);
    autoPreview.setTooltip ("Play samples and pads when you select them");
    addAndMakeVisible (autoPreview);

    hint.setFont (uiFont (12.0f));
    hint.setColour (juce::Label::textColourId, Colours::textDim);
    hint.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (hint);

    library->addChangeListener (this);
    refresh();
}

LibraryBrowser::~LibraryBrowser()
{
    tree.removeMouseListener (this);
    scrollBar.removeListener (this);
    library->removeChangeListener (this);
    tree.setRootItem (nullptr);
}

juce::String LibraryBrowser::targetPadName() const
{
    return padName (processor.selectedPad);
}

void LibraryBrowser::visibilityChanged()
{
    if (isVisible())
    {
        library->ensureScanned();
        refresh();
        startTimerHz (20);
    }
    else
    {
        stopTimer();
        processor.stopPreview();
    }
}

void LibraryBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (library->getIndex() != index)
        refresh();
    else
        repaint (headerArea);
}

void LibraryBrowser::refresh()
{
    index = library->getIndex();
    parsedPrograms.clear();
    rebuildTree();
    repaint();
}

void LibraryBrowser::rebuildTree()
{
    auto openness = root != nullptr ? tree.getOpennessState (true) : nullptr;
    tree.setRootItem (nullptr);
    root = std::make_unique<RootItem>();

    auto filter = search.getText().trim();
    auto presets = Presets::list();

    if (filter.isEmpty())
    {
        if (! presets.empty())
        {
            auto* h = new Heading ("My presets");
            for (auto& pr : presets)
                h->addSubItem (new PresetItem (*this, pr));
            root->addSubItem (h);
        }
        if (index != nullptr && ! index->packs.empty())
        {
            auto* h = new Heading ("Library");
            for (int i = 0; i < (int) index->packs.size(); ++i)
                h->addSubItem (new PackItem (*this, index, i));
            root->addSubItem (h);
        }
    }
    else
    {
        // Flat search results: every word must match (name, pack title or folder)
        auto words = juce::StringArray::fromTokens (filter, " ", "\"");
        words.removeEmptyStrings();
        auto matches = [&words] (const juce::String& a, const juce::String& b)
        {
            for (auto& w : words)
                if (! a.containsIgnoreCase (w) && ! b.containsIgnoreCase (w))
                    return false;
            return true;
        };

        constexpr int kMaxResults = 400;
        auto* presetHeading = new Heading ("Presets");
        for (auto& pr : presets)
            if (matches (pr.name, {}))
                presetHeading->addSubItem (new PresetItem (*this, pr));

        auto* programHeading = new Heading ("Programs");
        auto* sampleHeading = new Heading ("Samples");
        if (index != nullptr)
        {
            for (int pi = 0; pi < (int) index->packs.size(); ++pi)
            {
                auto& pack = index->packs[(size_t) pi];
                for (int i = 0; i < pack.programs.size() && programHeading->getNumSubItems() < kMaxResults; ++i)
                    if (matches (pack.programs[i], pack.title))
                        programHeading->addSubItem (new ProgramItem (*this, index, pi, i, true));
                for (int i = 0; i < pack.samples.size() && sampleHeading->getNumSubItems() < kMaxResults; ++i)
                    if (matches (pack.samples[i], pack.title))
                        sampleHeading->addSubItem (new SampleItem (*this, index, pi, i, true));
            }
        }

        for (auto* h : { presetHeading, programHeading, sampleHeading })
        {
            if (h->getNumSubItems() == 0)
            {
                delete h;
                continue;
            }
            root->addSubItem (h);
        }
    }

    tree.setRootItem (root.get());
    root->setOpen (true);
    if (openness != nullptr && filter.isEmpty())
        tree.restoreOpennessState (*openness, true);
    for (int i = 0; i < root->getNumSubItems(); ++i)
        root->getSubItem (i)->setOpen (true);     // headings are always open

    // What to tell the user
    juce::String text;
    if (library->getRoot() == juce::File())
        text = "Choose your library folder (Folder...) to browse all your expansions and samples here.";
    else if (filter.isNotEmpty() && root->getNumSubItems() == 0)
        text = "Nothing matches \"" + filter + "\".";
    else
        text = "Click to hear. Double-click (or drag onto a pad) to use it on pad " + targetPadName() + ".";
    hint.setText (text, juce::dontSendNotification);
}

std::shared_ptr<const Kit> LibraryBrowser::programKit (const ProgramLocation& location)
{
    auto key = location.toString();
    auto it = parsedPrograms.find (key);
    if (it != parsedPrograms.end())
        return it->second;

    auto kit = std::make_shared<Kit>();
    juce::StringArray warnings;
    juce::String error;
    if (! KitLoader::parseProgram (location, *kit, warnings, error))
        kit = nullptr;
    parsedPrograms[key] = kit;
    return kit;
}

bool LibraryBrowser::isLibraryDrag (const juce::var& d)
{
    return d.getProperty ("kaiaLibrary", {}).toString().isNotEmpty();
}

bool LibraryBrowser::dropOnPad (const juce::var& d, int padIndex, bool asLayer)
{
    auto kind = d.getProperty ("kaiaLibrary", {}).toString();
    if (kind == "sample")
    {
        processor.assignSamplesToPad (padIndex, std::vector<SampleRef> { SampleRef::fromString (d.getProperty ("ref", {}).toString()) },
                                      asLayer ? KaiaProcessor::AssignMode::Append : KaiaProcessor::AssignMode::Replace);
        return true;
    }
    if (kind == "pad")
    {
        auto kit = programKit (ProgramLocation::fromString (d.getProperty ("program", {}).toString()));
        auto src = (int) d.getProperty ("pad", -1);
        if (kit == nullptr || src < 0 || src >= (int) kit->pads.size())
            return false;
        processor.importPad (padIndex, kit->pads[(size_t) src]);
        return true;
    }
    return false;
}

void LibraryBrowser::resized()
{
    auto r = getLocalBounds().reduced (12);
    headerArea = r.removeFromTop (52);
    auto buttons = headerArea.withHeight (26).withTrimmedTop (2);
    closeButton.setBounds (buttons.removeFromRight (64));
    buttons.removeFromRight (6);
    rescanButton.setBounds (buttons.removeFromRight (72));
    buttons.removeFromRight (6);
    folderButton.setBounds (buttons.removeFromRight (76));

    r.removeFromTop (6);
    auto row = r.removeFromTop (28);
    autoPreview.setBounds (row.removeFromRight (118));
    row.removeFromRight (8);
    search.setBounds (row);
    r.removeFromTop (6);
    hint.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);
    scrollBar.setBounds (r.removeFromRight (10));
    r.removeFromRight (2);
    tree.setBounds (r);
    syncScrollBar();
}

//==============================================================================
void LibraryBrowser::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Arrives from the tree (its scroll bars are hidden, so its viewport passes wheel events up);
    // the copy delivered to the mouse listener on the tree is ignored.
    // The tree's item components pass each event up twice (as children and to their listener)
    auto* vp = tree.getViewport();
    if (e.eventComponent != this || vp == nullptr || e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isAltDown()
         || std::exchange (lastWheelTime, e.eventTime) == e.eventTime)
        return;

    auto maxY = juce::jmax (0, vp->getViewedComponent()->getHeight() - vp->getViewHeight());
    auto delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 14.0f * 18.0f;   // about two rows per wheel notch
    if (std::abs (delta) < 0.01f)
        return;

    if (wheel.isSmooth)
    {
        // Touchpads already send small, smooth steps
        scrollTarget = juce::jlimit (0.0, (double) maxY, (double) vp->getViewPositionY() - delta);
        scrollPosition = scrollTarget;
        vp->setViewPosition (vp->getViewPositionX(), juce::roundToInt (scrollTarget));
        syncScrollBar();
        return;
    }

    // Mouse wheel notches: glide to the new position over a few frames
    if (! scrollAnimating)
        scrollPosition = scrollTarget = vp->getViewPositionY();
    scrollTarget = juce::jlimit (0.0, (double) maxY, scrollTarget - delta);
    if (! scrollAnimating)
    {
        scrollAnimating = true;
        scrollAnimator.startTimerHz (60);
    }
}

void LibraryBrowser::animateScroll()
{
    auto* vp = tree.getViewport();
    if (vp == nullptr || std::abs (scrollTarget - scrollPosition) < 0.5)
    {
        if (vp != nullptr)
            vp->setViewPosition (vp->getViewPositionX(), juce::roundToInt (scrollTarget));
        scrollAnimating = false;
        scrollAnimator.stopTimer();
        syncScrollBar();
        return;
    }
    scrollPosition += (scrollTarget - scrollPosition) * 0.32;
    vp->setViewPosition (vp->getViewPositionX(), juce::roundToInt (scrollPosition));
    syncScrollBar();
}

void LibraryBrowser::syncScrollBar()
{
    auto* vp = tree.getViewport();
    if (vp == nullptr || vp->getViewedComponent() == nullptr)
        return;
    // (ScrollBar notifies its listeners asynchronously, so changes made here must not notify)
    scrollBar.setRangeLimits (0.0, (double) vp->getViewedComponent()->getHeight(), juce::dontSendNotification);
    scrollBar.setCurrentRange ((double) vp->getViewPositionY(), (double) vp->getViewHeight(), juce::dontSendNotification);
    scrollBar.setSingleStepSize (24.0);
}

void LibraryBrowser::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    if (auto* vp = tree.getViewport())
    {
        scrollAnimating = false;
        scrollAnimator.stopTimer();
        scrollPosition = scrollTarget = newRangeStart;
        vp->setViewPosition (vp->getViewPositionX(), juce::roundToInt (newRangeStart));
    }
}

void LibraryBrowser::mouseDrag (const juce::MouseEvent&)
{
    // Dragging the list to scroll
    scrollAnimating = false;
    scrollAnimator.stopTimer();
    syncScrollBar();
}

void LibraryBrowser::mouseUp (const juce::MouseEvent& e)
{
    auto pending = std::exchange (pendingAudition, juce::String());
    if (pending.isNotEmpty() && ! e.mouseWasDraggedSinceMouseDown())
        if (auto* item = dynamic_cast<Item*> (tree.findItemFromIdentifierString (pending)))
            item->audition();
}

void LibraryBrowser::timerCallback()
{
    // Keeps the scroll bar in step with drag-scrolling momentum, opening folders and so on
    syncScrollBar();
}

void LibraryBrowser::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    auto h = headerArea;
    h.removeFromRight (230);
    g.setColour (Colours::text);
    g.setFont (uiFont (17.0f, true));
    g.drawText ("Library", h.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (12.5f));
    auto rootDir = library->getRoot();
    auto status = library->getStatus();
    g.drawText ((rootDir == juce::File() ? juce::String() : rootDir.getFullPathName() + "   ") + status,
                h.removeFromTop (18), juce::Justification::centredLeft, true);
}

}
