#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace kaia
{

// Browses the library folder: presets, expansions and folders, their programs (and the pads of
// each program) and their samples. Click to audition, double-click to use, drag onto a pad.
// Nothing here writes to the library.
class LibraryBrowser : public juce::Component,
                       private juce::ChangeListener
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

private:
    class Item;
    class RootItem;
    class PackItem;
    class GroupItem;
    class ProgramItem;
    class PadItem;
    class SampleItem;
    class PresetItem;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildTree();
    std::shared_ptr<const Kit> programKit (const ProgramLocation& location);   // parsed lazily, cached
    juce::String targetPadName() const;

    KaiaProcessor& processor;
    juce::SharedResourcePointer<LibraryService> library;
    std::shared_ptr<const LibraryIndex> index;

    juce::TextEditor search;
    juce::TreeView tree;
    std::unique_ptr<RootItem> root;
    juce::TextButton closeButton { "Done" }, rescanButton { "Rescan" }, folderButton { "Folder..." };
    juce::ToggleButton autoPreview { "Click to hear" };
    juce::Label hint;

    std::map<juce::String, std::shared_ptr<const Kit>> parsedPrograms;
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryBrowser)
};

}
