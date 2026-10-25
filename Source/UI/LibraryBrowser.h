#pragma once

#include "LookAndFeel.h"
#include "KineticScroller.h"
#include "../PluginProcessor.h"

namespace kaia
{

// Browses the library folder: presets, expansions and folders, their programs (and the pads of
// each program) and their samples. Click to audition, double-click to use, drag onto a pad.
// The tile view shows the packs as cover art (2-4 columns); a tap loads the pack, which is
// then open in the list. Nothing here writes to the library.
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
    void setTileMode (bool tiles, bool save);
    void setTileColumns (int columns, bool save);
    void pickPack (int packIndex);         // a tap on a tile
    void revealPack (const juce::File& pack);
    std::shared_ptr<const Kit> programKit (const ProgramLocation& location);   // parsed lazily, cached
    juce::String targetPadName() const;

    KaiaProcessor& processor;
    juce::SharedResourcePointer<LibraryService> library;
    std::shared_ptr<const LibraryIndex> index;

    juce::TextEditor search;
    juce::TreeView tree;
    std::unique_ptr<RootItem> root;
    juce::TextButton closeButton { "Done" }, rescanButton { "Rescan" }, folderButton { "Folder..." };
    juce::TextButton tilesButton { "Tiles" };
    juce::OwnedArray<juce::TextButton> columnButtons;     // 2, 3, 4 columns
    juce::ToggleButton autoPreview { "Click to hear" };
    juce::Label hint;

    juce::ScrollBar scrollBar { true };
    std::unique_ptr<KineticScroller> scroller;
    std::unique_ptr<TileView> tiles;
    bool tileMode = false;
    int tileColumns = 3;
    juce::File packToReveal;               // picked in the tile view: open it in the list
    juce::File shownPackFile;
    juce::String pendingAudition;          // item to play when the click is released

    struct Fonts
    {
        juce::Font plain = uiFont (18.0f), bold = uiFont (18.0f, true), heading = uiFont (13.0f, true), detail = uiFont (14.0f);
    } fonts;

    std::map<juce::String, std::shared_ptr<const Kit>> parsedPrograms;
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryBrowser)
};

}
