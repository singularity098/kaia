#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace kaia
{

// List of the programs in the open expansion / folder. Clicking (or arrow keys) loads a program.
class ProgramBrowser : public juce::Component,
                       private juce::ListBoxModel
{
public:
    explicit ProgramBrowser (KaiaProcessor& processor);

    void refresh();
    std::function<void()> onClose;

    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    struct Row
    {
        int program = -1;          // index into processor.getPrograms(), -1 for a folder heading
        juce::String text;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void returnKeyPressed (int lastRowSelected) override;
    void rebuildRows();

    KaiaProcessor& processor;
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton closeButton { "Done" };
    std::vector<Row> rows;
    bool updating = false;
    juce::Rectangle<int> headerArea;
};

}
