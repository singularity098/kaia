#include "LibraryBrowser.h"

namespace kaia
{

//==============================================================================
class LibraryBrowser::Item : public juce::TreeViewItem
{
public:
    explicit Item (LibraryBrowser& b) : browser (b) {}

    int getItemHeight() const override { return 36; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        // Texts are measured once: painting runs for every visible row on every scroll step
        auto& fonts = browser.fonts;
        if (! cached)
        {
            cached = true;
            text = isHeading() ? getText().toUpperCase() : getText();
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

        g.setColour (isDisabled() ? Colours::textDim : (isSelected() ? Colours::accent : (isHeading() ? Colours::textDim : Colours::text)));
        g.setFont (isHeading() ? fonts.heading : isBold() ? fonts.bold : fonts.plain);
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
        if (dragIsVertical() || browser.scroller->pressIsScrolling())
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
        if (dragIsVertical() || browser.scroller->pressIsScrolling())
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
// Packs as cover-art tiles. Covers are read and scaled on background threads, only for tiles on
// screen; each tile is drawn once into an image at the screen's pixel scale, so scrolling only
// copies images.
class LibraryBrowser::TileView : public juce::Component
{
public:
    explicit TileView (LibraryBrowser& b) : browser (b)
    {
        viewport.setViewedComponent (&grid, false);
        viewport.setScrollBarsShown (false, false, false, false);
        addAndMakeVisible (viewport);
        bar.setAutoHide (true);
        addAndMakeVisible (bar);
        scroller = std::make_unique<KineticScroller> (viewport, viewport, &bar);
    }

    ~TileView() override
    {
        pool.removeAllJobs (true, 2000);
    }

    void setPacks (std::shared_ptr<const LibraryIndex> newIndex, std::vector<int> newPacks)
    {
        if (newIndex == index && newPacks == packs)
            return;
        ++shared->generation;        // queued cover loads refer to the old positions
        index = std::move (newIndex);
        for (auto& entry : artwork)
            entry.second.tile = {};     // redrawn with the current colours
        packs = std::move (newPacks);
        layout();
        grid.repaint();
    }

    void setColumns (int n)
    {
        columns = juce::jlimit (2, 4, n);
        layout();
        grid.repaint();
    }

    void setFling (float f)              { scroller->setFling (f); }
    void syncScrollBar()                 { scroller->syncScrollBar(); }
    bool isEmpty() const                 { return packs.empty(); }

    void resized() override
    {
        auto r = getLocalBounds();
        bar.setBounds (r.removeFromRight (10));
        r.removeFromRight (2);
        viewport.setBounds (r);
        layout();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::panel);
    }

private:
    static constexpr int kGap = 12, kCaption = 46;

    struct Art
    {
        enum State { unrequested, loading, ready, none };
        State state = unrequested;
        juce::Image source;                     // cover, at most kSourceSize pixels
        juce::Image tile;                       // the whole tile, at the screen's pixel scale
        int tileWidth = 0;
        float tileScale = 0.0f;
        juce::uint32 lastUsed = 0;
    };
    static constexpr int kSourceSize = 512;

    int tileWidth() const    { return juce::jmax (40, (grid.getWidth() - kGap * (columns - 1)) / columns); }
    int rowHeight() const    { return tileWidth() + kCaption + kGap; }

    void layout()
    {
        auto w = viewport.getMaximumVisibleWidth();
        auto rows = ((int) packs.size() + columns - 1) / columns;
        grid.setSize (w, juce::jmax (viewport.getHeight(), rows * rowHeight()));
        scroller->syncScrollBar();
    }

    const LibraryPack* packAt (int position) const
    {
        if (index == nullptr || position < 0 || position >= (int) packs.size())
            return nullptr;
        return &index->packs[(size_t) packs[(size_t) position]];
    }

    juce::Rectangle<int> tileBounds (int position) const
    {
        auto w = tileWidth();
        return { (position % columns) * (w + kGap), (position / columns) * rowHeight(), w, w + kCaption };
    }

    int positionAt (juce::Point<int> p) const
    {
        auto col = p.x / (tileWidth() + kGap);
        auto pos = (p.y / rowHeight()) * columns + col;
        return col < columns && tileBounds (pos).contains (p) && pos < (int) packs.size() ? pos : -1;
    }

    //==========================================================================
    class Grid : public juce::Component
    {
    public:
        explicit Grid (TileView& v) : view (v) { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Colours::panel);
            view.paintTiles (g);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            // Taps only: not the end of a scroll, nor the press that stopped a fling
            if (view.scroller->pressIsScrolling() || e.getDistanceFromDragStart() > 12 || e.mods.isPopupMenu())
                return;
            auto pos = view.positionAt (e.getMouseDownPosition());
            if (auto* pack = view.packAt (pos); pack != nullptr && ! pack->isProtected)
                view.browser.pickPack (view.packs[(size_t) pos]);
        }

    private:
        TileView& view;
    };

    void paintTiles (juce::Graphics& g)
    {
        auto clip = g.getClipBounds();
        auto rowH = rowHeight();
        auto first = juce::jmax (0, clip.getY() / rowH) * columns;
        auto last = juce::jmin ((int) packs.size() - 1, (clip.getBottom() / rowH + 1) * columns - 1);
        auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        auto current = browser.processor.getPackFile();
        ++paintCount;

        // Covers for the tiles on screen and a row either side
        auto visibleTop = viewport.getViewPositionY();
        shared->wantedFirst = juce::jmax (0, visibleTop / rowH - 1) * columns;
        shared->wantedLast = ((visibleTop + viewport.getViewHeight()) / rowH + 2) * columns - 1;

        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        for (int pos = first; pos <= last; ++pos)
        {
            auto* pack = packAt (pos);
            auto bounds = tileBounds (pos);
            if (pack == nullptr || ! bounds.intersects (clip))
                continue;

            auto& art = artFor (*pack, pos);
            art.lastUsed = paintCount;
            if (! art.tile.isValid() || art.tileWidth != bounds.getWidth() || ! juce::approximatelyEqual (art.tileScale, scale))
                renderTile (art, *pack, bounds.getWidth(), scale);
            g.drawImage (art.tile, bounds.toFloat());

            if (pack->file == current || pack->file == browser.packToReveal)
            {
                g.setColour (Colours::accent);
                g.drawRoundedRectangle (bounds.toFloat().reduced (1.0f), 6.0f, 2.0f);
            }
        }
        evictOldArt();
    }

    Art& artFor (const LibraryPack& pack, int position)
    {
        auto key = pack.file.getFullPathName();
        auto& art = artwork[key];
        if (art.state == Art::unrequested && ! pack.isProtected)
        {
            art.state = Art::loading;
            pool.addJob ([safe = juce::Component::SafePointer<TileView> (this), shared = shared, key, file = pack.file, position,
                          generation = shared->generation.load()]
            {
                // Scrolled past before its turn came: forget it, it is asked for again when seen
                if (shared->generation.load() != generation || position < shared->wantedFirst.load() || position > shared->wantedLast.load())
                {
                    finishLoad (safe, key, {}, Art::unrequested);
                    return;
                }
                juce::MemoryBlock data;
                juce::Image image;
                if (KitLoader::readPackArtwork (file, data))
                    image = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
                if (image.isValid() && juce::jmax (image.getWidth(), image.getHeight()) > kSourceSize)
                {
                    auto s = (float) kSourceSize / (float) juce::jmax (image.getWidth(), image.getHeight());
                    image = image.rescaled (juce::roundToInt ((float) image.getWidth() * s), juce::roundToInt ((float) image.getHeight() * s),
                                            juce::Graphics::highResamplingQuality);
                }
                finishLoad (safe, key, image, image.isValid() ? Art::ready : Art::none);
            });
        }
        return art;
    }

    static void finishLoad (juce::Component::SafePointer<TileView> safe, juce::String key, juce::Image image, Art::State state)
    {
        juce::MessageManager::callAsync ([safe, key, image, state]
        {
            if (safe == nullptr)
                return;
            auto it = safe->artwork.find (key);
            if (it == safe->artwork.end())
                return;
            it->second.state = state;
            it->second.source = image;
            it->second.tile = {};
            safe->grid.repaint();       // also asks again for covers skipped while still on screen
        });
    }

    void evictOldArt()
    {
        constexpr size_t kMaxCovers = 160;
        if (artwork.size() <= kMaxCovers)
            return;
        std::vector<std::pair<juce::uint32, juce::String>> ages;
        for (auto& [key, art] : artwork)
            if (art.state != Art::loading && art.lastUsed != paintCount)
                ages.push_back ({ art.lastUsed, key });
        std::sort (ages.begin(), ages.end());
        for (size_t i = 0; i < ages.size() && artwork.size() > kMaxCovers * 3 / 4; ++i)
            artwork.erase (ages[i].second);
    }

    void renderTile (Art& art, const LibraryPack& pack, int width, float scale)
    {
        auto height = width + kCaption;
        art.tileWidth = width;
        art.tileScale = scale;
        art.tile = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) width * scale)),
                                juce::jmax (1, juce::roundToInt ((float) height * scale)), false);
        juce::Graphics g (art.tile);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (Colours::panel);

        auto cover = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) width);
        juce::Path clip;
        clip.addRoundedRectangle (cover, 6.0f);
        g.setColour (Colours::panelRaised);
        g.fillPath (clip);

        if (art.source.isValid())
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (clip);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (art.source, cover, juce::RectanglePlacement::fillDestination);
        }
        else
        {
            // No cover: the pack's initials
            auto words = juce::StringArray::fromTokens (pack.title, " _-", "");
            words.removeEmptyStrings();
            juce::String initials;
            for (int i = 0; i < juce::jmin (2, words.size()); ++i)
                initials << words[i].substring (0, 1).toUpperCase();
            g.setColour (pack.isProtected ? Colours::textDim : Colours::accent.withAlpha (0.8f));
            g.setFont (uiFont ((float) width * 0.22f, true));
            g.drawText (initials, cover, juce::Justification::centred, false);
        }
        g.setColour (Colours::outline);
        g.strokePath (clip, juce::PathStrokeType (1.0f));

        auto caption = juce::Rectangle<int> (0, width + 4, width, kCaption - 4).reduced (2, 0);
        g.setColour (pack.isProtected ? Colours::textDim : Colours::text);
        g.setFont (uiFont (14.0f, true));
        g.drawText (pack.title, caption.removeFromTop (22), juce::Justification::centredLeft, true);

        juce::StringArray parts;
        if (pack.isProtected)
            parts.add ("copy-protected");
        if (! pack.programs.isEmpty()) parts.add (juce::String (pack.programs.size()) + (pack.programs.size() == 1 ? " program" : " programs"));
        if (! pack.samples.isEmpty())  parts.add (juce::String (pack.samples.size()) + (pack.samples.size() == 1 ? " sample" : " samples"));
        g.setColour (Colours::textDim);
        g.setFont (uiFont (12.0f));
        g.drawText (parts.joinIntoString (", "), caption.removeFromTop (18), juce::Justification::centredLeft, true);
    }

    LibraryBrowser& browser;
    Grid grid { *this };
    juce::Viewport viewport;
    juce::ScrollBar bar { true };
    std::unique_ptr<KineticScroller> scroller;

    std::shared_ptr<const LibraryIndex> index;
    std::vector<int> packs;                 // indices into index->packs, in display order
    int columns = 3;

    std::map<juce::String, Art> artwork;    // by pack path
    juce::uint32 paintCount = 0;
    struct Shared                           // read by the cover loading jobs
    {
        std::atomic<int> wantedFirst { 0 }, wantedLast { 0 }, generation { 0 };
    };
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    juce::ThreadPool pool { 2 };
};

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
    tree.setIndentSize (32);         // also the width of the open/close triangle's click area
    tree.setColour (juce::TreeView::backgroundColourId, Colours::panel);
    tree.setColour (juce::TreeView::linesColourId, Colours::outline);
    tree.setColour (juce::TreeView::selectedItemBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (tree);

    // Scrolling: drag anywhere (touch screens) with momentum, and the wheel glides instead of
    // jumping (KineticScroller). The tree's own scroll bars are hidden; scrollBar replaces them.
    tree.getViewport()->setScrollBarsShown (false, false, false, false);
    scrollBar.setAutoHide (true);
    addAndMakeVisible (scrollBar);
    scroller = std::make_unique<KineticScroller> (*tree.getViewport(), tree, &scrollBar);
    tree.addMouseListener (this, true);

    tiles = std::make_unique<TileView> (*this);
    addChildComponent (*tiles);
    tilesButton.setTooltip ("Show the packs as cover art, or as the list");
    tilesButton.onClick = [this] { setTileMode (! tileMode, true); };
    addAndMakeVisible (tilesButton);
    for (int n = 2; n <= 4; ++n)
    {
        auto* b = columnButtons.add (new juce::TextButton (juce::String (n)));
        b->setTooltip (juce::String (n) + " columns");
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x4b41);
        b->setConnectedEdges ((n > 2 ? juce::Button::ConnectedOnLeft : 0) | (n < 4 ? juce::Button::ConnectedOnRight : 0));
        b->onClick = [this, n] { setTileColumns (n, true); };
        addChildComponent (b);
    }

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

    {
        juce::SharedResourcePointer<SharedSettings> settings;
        setTileColumns (settings->get().getIntValue ("libraryColumns", 3), false);
        setTileMode (settings->get().getValue ("libraryView") == "tiles", false);
    }

    library->addChangeListener (this);
    refresh();
}

LibraryBrowser::~LibraryBrowser()
{
    tree.removeMouseListener (this);
    scroller = nullptr;
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
        auto fling = KineticScroller::flingFromSettings();     // may have changed in Settings
        scroller->setFling (fling);
        tiles->setFling (fling);
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

    // Tiles: every pack whose title or path matches all the words
    {
        auto words = juce::StringArray::fromTokens (filter, " ", "\"");
        words.removeEmptyStrings();
        std::vector<int> packs;
        for (int i = 0; index != nullptr && i < (int) index->packs.size(); ++i)
        {
            auto& pack = index->packs[(size_t) i];
            bool ok = true;
            for (auto& w : words)
                ok = ok && (pack.title.containsIgnoreCase (w) || pack.relativePath.containsIgnoreCase (w));
            if (ok)
                packs.push_back (i);
        }
        tiles->setPacks (index, std::move (packs));
    }

    // What to tell the user
    juce::String text;
    if (library->getRoot() == juce::File())
        text = "Choose your library folder (Folder...) to browse all your expansions and samples here.";
    else if (filter.isNotEmpty() && (tileMode ? tiles->isEmpty() : root->getNumSubItems() == 0))
        text = "Nothing matches \"" + filter + "\".";
    else if (tileMode)
        text = "Tap a pack to load it. Switch to List to see its programs, pads and samples.";
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
    buttons.removeFromRight (6);
    tilesButton.setBounds (buttons.removeFromRight (60));

    r.removeFromTop (6);
    auto row = r.removeFromTop (28);
    auto right = row.removeFromRight (118);
    autoPreview.setBounds (right);
    auto columnsArea = right.withSizeKeepingCentre (3 * 34, 26).withRightX (right.getRight());
    for (auto* b : columnButtons)
        b->setBounds (columnsArea.removeFromLeft (34));
    row.removeFromRight (8);
    search.setBounds (row);
    r.removeFromTop (6);
    hint.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);
    tiles->setBounds (r);
    scrollBar.setBounds (r.removeFromRight (10));
    r.removeFromRight (2);
    tree.setBounds (r);
    scroller->syncScrollBar();
}

void LibraryBrowser::setTileMode (bool shouldShowTiles, bool save)
{
    tileMode = shouldShowTiles;
    tilesButton.setButtonText (tileMode ? "List" : "Tiles");
    tiles->setVisible (tileMode);
    tree.setVisible (! tileMode);
    scrollBar.setVisible (! tileMode);
    autoPreview.setVisible (! tileMode);
    for (auto* b : columnButtons)
        b->setVisible (tileMode);
    search.setTextToShowWhenEmpty (tileMode ? "Search packs" : "Search programs and samples", Colours::textDim);
    scroller->stop();

    if (save)
    {
        juce::SharedResourcePointer<SharedSettings> settings;
        settings->get().setValue ("libraryView", tileMode ? "tiles" : "list");
        settings->get().saveIfNeeded();
        rebuildTree();
    }
    if (! tileMode && packToReveal != juce::File())
    {
        if (search.getText().isNotEmpty())
        {
            search.clear();         // the search results don't show packs
            rebuildTree();
        }
        revealPack (std::exchange (packToReveal, juce::File()));
    }
    repaint();
}

void LibraryBrowser::setTileColumns (int columns, bool save)
{
    tileColumns = juce::jlimit (2, 4, columns);
    columnButtons[tileColumns - 2]->setToggleState (true, juce::dontSendNotification);
    tiles->setColumns (tileColumns);
    if (save)
    {
        juce::SharedResourcePointer<SharedSettings> settings;
        settings->get().setValue ("libraryColumns", tileColumns);
        settings->get().saveIfNeeded();
    }
}

void LibraryBrowser::pickPack (int packIndex)
{
    if (index == nullptr || packIndex < 0 || packIndex >= (int) index->packs.size())
        return;
    auto& pack = index->packs[(size_t) packIndex];
    processor.openPack (pack.file);
    packToReveal = pack.file;           // shown open when the list comes back
    tiles->repaint();
}

void LibraryBrowser::revealPack (const juce::File& packFile)
{
    // The pack in the list (when the search shows it): open, selected and scrolled to
    if (root == nullptr)
        return;
    for (int h = 0; h < root->getNumSubItems(); ++h)
    {
        auto* heading = root->getSubItem (h);
        for (int i = 0; i < heading->getNumSubItems(); ++i)
        {
            auto* item = heading->getSubItem (i);
            if (item->getUniqueName() != "k:" + packFile.getFullPathName())
                continue;
            item->setOpen (true);
            item->setSelected (true, true, juce::dontSendNotification);
            // Positions are worked out asynchronously after opening
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LibraryBrowser> (this), id = item->getItemIdentifierString()]
            {
                if (safe == nullptr)
                    return;
                if (auto* it = safe->tree.findItemFromIdentifierString (id))
                    safe->scroller->scrollTo (it->getItemPosition (false).getY() - 8);
            });
            return;
        }
    }
}

//==============================================================================
void LibraryBrowser::mouseUp (const juce::MouseEvent& e)
{
    // Releasing a click plays the item; the end of a scroll (or a touch that stopped one) doesn't
    auto pending = std::exchange (pendingAudition, juce::String());
    if (pending.isNotEmpty() && ! scroller->pressIsScrolling() && e.getDistanceFromDragStart() < 12)
        if (auto* item = dynamic_cast<Item*> (tree.findItemFromIdentifierString (pending)))
            item->audition();
}

void LibraryBrowser::timerCallback()
{
    // Keeps the scroll bars in step with opening folders and so on
    if (! scroller->isMoving())
        scroller->syncScrollBar();
    tiles->syncScrollBar();
    if (tileMode && processor.getPackFile() != shownPackFile)
    {
        shownPackFile = processor.getPackFile();     // the loaded pack's tile is highlighted
        tiles->repaint();
    }
}

void LibraryBrowser::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    auto h = headerArea;
    h.removeFromRight (300);
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
