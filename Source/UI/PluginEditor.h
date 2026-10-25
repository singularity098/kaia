#pragma once

#include "LookAndFeel.h"
#include "PadGrid.h"
#include "PadEditor.h"
#include "ProgramBrowser.h"
#include "LibraryBrowser.h"
#include "SettingsPanel.h"

namespace kaia
{

class KaiaEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       public juce::DragAndDropContainer,
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
    enum class Panel { PadEditor, Programs, Library, Settings };
    void showPanel (Panel panel);
    void togglePanel (Panel panel)   { showPanel (currentPanel == panel ? Panel::PadEditor : panel); }
    void chooseLibraryFolder();
    void savePresetDialog();
    void accentChanged();
    void addRecent (const juce::File& f);
    juce::StringArray getRecents();
    juce::File lastFolder();

    KaiaProcessor& processor;
    KaiaLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::Drawable> wordmark;      // "KAIA" in the header logo

    juce::TextButton openButton { "Open" }, browseButton { "Programs" }, prevButton { "<" }, nextButton { ">" };
    juce::TextButton panicButton { "Stop" }, relinkButton { "Relink..." }, routingButton { "Outputs" };
    juce::TextButton libraryButton { "Library" }, settingsButton { "Settings" };
    juce::Label kitLabel, formatLabel, statusLabel, voicesLabel;
    juce::ImageButton coverButton;                 // the open expansion's artwork
    juce::String shownCoverKey;
    juce::HyperlinkButton logoLink { {}, juce::URL ("https://github.com/singularity098/kaia") };   // over the painted logo
    juce::Slider masterSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment masterAttachment;

    PadGrid padGrid;
    PadEditor padEditor;
    ProgramBrowser browser;
    LibraryBrowser library;
    SettingsPanel settingsPanel;
    Panel currentPanel = Panel::PadEditor;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::SharedResourcePointer<SharedSettings> sharedSettings;
    juce::PropertiesFile& settings() { return sharedSettings->get(); }
    std::unique_ptr<juce::AlertWindow> presetDialog;
    std::unique_ptr<Pad> clipboard;
    juce::ComponentBoundsConstrainer constrainer;

    void updateCover();
    void padThemeChanged();

    juce::Rectangle<int> headerArea, footerArea, progressArea, midiArea;
    juce::uint32 shownInputNote = 0;     // last MIDI note-on shown in the header
    float midiFlash = 0.0f;
    float shownProgress = 0.0f;
    bool wasLoading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaiaEditor)
};

}
