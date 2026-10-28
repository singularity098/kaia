#pragma once

#include "LookAndFeel.h"
#include "KineticScroller.h"
#include "SearchBox.h"
#include "../PluginProcessor.h"

namespace kaia
{

// Browses the library folder: presets, expansions and folders, their programs (and the pads of
// each program) and their samples. Click to open, audition or load, double-click to use, drag
// onto a pad. Programs and samples can be starred (favourites, with a filter), and programs that
// come with a preview render ("[Previews]" in MPC expansions) get a play/stop button. The tile view shows the packs as cover art (2-4 columns); a tap loads the pack,
// which is then open in the list. Nothing here writes to the library.
class LibraryBrowser : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit LibraryBrowser (KaiaProcessor& processor);
    ~LibraryBrowser() override;

    void refresh();                        // presets or library changed
    std::function<void()> onClose;
    std::function<void()> onChooseFolder;

    // Drag descriptions created by the tree, resolved when dropped on a pad
    static bool isLibraryDrag (const juce::var& description);
    bool dropOnPad (const juce::var& description, int padIndex, bool asLayer);

    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Item;
    class RootItem;
    class HeadingItem;
    class PackItem;
    class GroupItem;
    class ProgramItem;
    class PadItem;
    class SampleItem;
    class PresetItem;
    class TileView;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildTree();
    void updateHint();
    void setTileMode (bool tiles, bool save);
    void setTileColumns (int columns, bool save);
    void pickPack (int packIndex);         // a tap on a tile
    void setAllOpen (bool open);           // the collapse / expand all button
    void setFavouritesOnly (bool only);
    bool isFavourite (const juce::String& key) const   { return favourites.count (key) > 0; }
    void toggleFavourite (const juce::String& key);
    int previewSampleFor (int packIndex, int programIndex);  // the program's preview render, -1 if none
    void togglePreview (const juce::String& key, const SampleRef& preview);
    void updatePreviewState();
    static juce::String favouriteKey (const char* kind, const LibraryPack& pack, const juce::String& path);
    void revealPack (const juce::File& pack);
    std::shared_ptr<const Kit> programKit (const ProgramLocation& location);   // parsed lazily, cached
    juce::String targetPadName() const;

    KaiaProcessor& processor;
    juce::SharedResourcePointer<LibraryService> library;
    std::shared_ptr<const LibraryIndex> index;

    SearchBox search;
    juce::TreeView tree;
    std::unique_ptr<RootItem> root;
    juce::TextButton closeButton { "Done" }, rescanButton { "Rescan" }, folderButton { "Folder..." };
    juce::TextButton tilesButton { "Tiles" };
    juce::OwnedArray<juce::TextButton> columnButtons;     // 2, 3, 4 columns
    juce::ToggleButton autoPreview { "Click to hear" };
    juce::ShapeButton favouritesButton { "Favourites", {}, {}, {} };
    juce::TextButton collapseButton { "Expand all" };
    juce::Label hint;

    juce::ScrollBar scrollBar { true };
    std::unique_ptr<KineticScroller> scroller;
    std::unique_ptr<TileView> tiles;
    bool tileMode = false;
    int tileColumns = 3;
    juce::File packToReveal;               // picked in the tile view: open it in the list
    juce::File shownPackFile;
    juce::String pendingClick;             // item clicked, acted on when the click is released
    int pendingButton = 0;                 // ...and the row button it was on (Item::Button)
    int hintPad = -1;                      // pad named in the hint
    std::map<juce::String, bool> headingOpen;   // headings opened or closed (kept across rebuilds)

    std::set<juce::String> favourites;     // favouriteKey()s, saved in favourites.txt
    bool favouritesOnly = false;

    // The program preview playing (its ProgramItem shows a stop button): set on click, cleared
    // when the engine stops sounding it
    juce::String previewKey;
    juce::uint32 previewIdAtClick = 0, previewSoundId = 0, previewClickTime = 0;
    std::map<int, std::map<juce::String, int>> previewSamples;   // per pack: program name -> preview sample

    struct Fonts
    {
        juce::Font plain = uiFont (18.0f), bold = uiFont (18.0f, true), heading = uiFont (13.0f, true), detail = uiFont (14.0f);
    } fonts;

    std::map<juce::String, std::shared_ptr<const Kit>> parsedPrograms;
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryBrowser)
};

}
