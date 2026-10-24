#pragma once

#include "LookAndFeel.h"
#include "PadGrid.h"
#include "PadEditor.h"
#include "ProgramBrowser.h"

namespace kaia
{

class KaiaEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit KaiaEditor (KaiaProcessor&);
    ~KaiaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshAll();
    void showOpenMenu();
    void showRoutingMenu();
    void showPadMenu (int pad, juce::Point<int> screenPos);
    void chooseFiles (bool folders);
    void chooseSamplesForPad (int pad, KaiaProcessor::AssignMode mode, int layer);
    void chooseRelinkFolder();
    void setBrowserVisible (bool visible);
    void addRecent (const juce::File& f);
    juce::StringArray getRecents();
    juce::File lastFolder();

    KaiaProcessor& processor;
    KaiaLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton openButton { "Open" }, browseButton { "Programs" }, prevButton { "<" }, nextButton { ">" };
    juce::TextButton panicButton { "Stop" }, relinkButton { "Relink..." }, routingButton { "Outputs" };
    juce::Label kitLabel, formatLabel, statusLabel, voicesLabel;
    juce::Slider masterSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment masterAttachment;

    PadGrid padGrid;
    PadEditor padEditor;
    ProgramBrowser browser;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<Pad> clipboard;
    juce::ComponentBoundsConstrainer constrainer;

    juce::Rectangle<int> headerArea, footerArea, progressArea;
    float shownProgress = 0.0f;
    bool wasLoading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaiaEditor)
};

}
