#pragma once

#include "LookAndFeel.h"
#include "KineticScroller.h"
#include "SearchBox.h"
#include "../PluginProcessor.h"

namespace kaia
{

// The Files tab: a plain file explorer over one folder (setting "filesFolder"), shown as it is on
// disk. Nothing is indexed or added to the library. Click a folder to open it, a sample to hear it,
// a program or expansion to load it; double-click a sample (or drag it onto a pad) to use it. The
// filter lists the files whose name or folder path contains every word typed (the folder is read
// once, on a background thread, each time the tab is shown). Nothing here writes to the folder.
class FileBrowser : public juce::Component,
                    private juce::Timer
{
public:
    explicit FileBrowser (KaiaProcessor& processor);
    ~FileBrowser() override;

    static juce::File rootFolder();                     // the setting, else the library folder, else home
    static void setRootFolder (const juce::File& folder);

    void refresh();                                     // the folder setting may have changed
    std::function<void()> onClose;
    std::function<void()> onChooseFolder;

    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Item;
    class FolderItem;
    class FileItem;

    // Every wanted file below the root (for the filter), sorted as the tree shows them
    struct Listing
    {
        struct Entry
        {
            juce::File file;
            juce::String folder;       // parent folder relative to the root, '/'-separated, "" at the root
        };
        juce::File root;
        std::vector<Entry> entries;
        bool truncated = false;
    };

    void timerCallback() override;
    void rebuildTree();
    void buildFilteredTree (const juce::StringArray& words);
    void startListing();
    void updateHint();
    juce::String targetPadName() const;

    KaiaProcessor& processor;
    juce::File root;

    SearchBox filter;
    juce::TreeView tree;
    std::unique_ptr<FolderItem> rootItem;
    bool rootItemShowsFilter = false;
    std::unique_ptr<juce::XmlElement> savedOpenness;    // the folders open before filtering
    juce::TextButton closeButton { "Done" }, folderButton { "Folder..." };
    juce::ToggleButton autoPreview { "Click to hear" };
    juce::Label hint;
    juce::ScrollBar scrollBar { true };
    std::unique_ptr<KineticScroller> scroller;

    std::shared_ptr<const Listing> listing;
    std::shared_ptr<std::atomic<bool>> listingCancel;   // the walk in progress, if any
    juce::ThreadPool pool { 1 };
    int shownResults = 0;
    bool resultsCapped = false;

    juce::String pendingClick;             // item clicked, acted on when the click is released
    int hintPad = -1;
    juce::Rectangle<int> headerArea;

    struct Fonts
    {
        juce::Font plain = uiFont (18.0f), bold = uiFont (18.0f, true), detail = uiFont (14.0f);
    } fonts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileBrowser)
};

}
