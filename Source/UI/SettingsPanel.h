#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace kaia
{

// Settings shared by all Kaia instances: drum module (trigger map, editable and saveable as custom
// maps), playback, library and Files tab folders, highlight and pad colours, and where Kaia keeps its files.
class SettingsPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit SettingsPanel (KaiaProcessor& processor);
    ~SettingsPanel() override;

    void refresh();
    std::function<void()> onClose;
    std::function<void()> onChooseLibraryFolder;
    std::function<void()> onChooseFilesFolder;
    std::function<void()> onAccentChanged;
    std::function<void()> onPadThemeChanged;

    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    class Swatches;
    class PadThemePicker;
    class MapView;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateTriggers (const std::function<void (TriggerMap&)>& fn);
    void rebuildModuleBox();
    void showZoneMenu (int zone);
    void typeZoneNotes (int zone);
    void saveCustomMap();
    void deleteCustomMap();
    void stopLearning();

    KaiaProcessor& processor;
    juce::SharedResourcePointer<LibraryService> library;

    juce::Viewport viewport;
    juce::Component content;

    juce::TextButton closeButton { "Done" };
    juce::ComboBox moduleBox;
    std::map<int, juce::String> moduleIdForItem;
    juce::ToggleButton chokeToggle { "Hi-hat pads choke each other (closed and pedal cut the open hi-hat)" };
    juce::TextButton clearLearnedButton { "Forget learned notes" };
    juce::TextButton saveMapButton { "Save as custom map..." }, deleteMapButton { "Delete custom map" }, resetMapButton { "Restore factory notes" };
    juce::Label moduleInfo, lastNoteLabel, mapHint;
    std::unique_ptr<MapView> mapView;
    int learningZone = -1;
    juce::uint32 learnSequence = 0;

    juce::ToggleButton loopsToggle { "Play sample loops on note-on pads" };
    juce::Label loopsInfo;

    juce::Label libraryPath;
    juce::TextButton chooseLibraryButton { "Choose folder..." }, rescanButton { "Rescan" };
    juce::Label filesPath;
    juce::TextButton chooseFilesButton { "Choose folder..." };
    juce::Label flingLabel;
    juce::Slider flingSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<Swatches> swatches;
    std::unique_ptr<PadThemePicker> padThemePicker;
    juce::Label configPath;
    juce::TextButton showConfigButton { "Show" };

    std::unique_ptr<juce::AlertWindow> dialog;
    juce::OwnedArray<juce::Label> headings;
    juce::uint32 shownLastNote = 0;
};

}
