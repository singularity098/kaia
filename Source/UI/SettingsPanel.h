#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace kaia
{

// Settings shared by all Kaia instances: drum module (trigger map), library folder, highlight
// colour, and where Kaia keeps its files.
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
    std::function<void()> onAccentChanged;

    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    class Swatches;
    class MapView;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateTriggers (const std::function<void (TriggerMap&)>& fn);

    KaiaProcessor& processor;
    juce::SharedResourcePointer<LibraryService> library;

    juce::Viewport viewport;
    juce::Component content;

    juce::TextButton closeButton { "Done" };
    juce::ComboBox moduleBox;
    juce::ToggleButton chokeToggle { "Hi-hat pads choke each other (closed and pedal cut the open hi-hat)" };
    juce::TextButton clearLearnedButton { "Forget learned notes" };
    juce::Label moduleInfo, lastNoteLabel;
    std::unique_ptr<MapView> mapView;

    juce::Label libraryPath;
    juce::TextButton chooseLibraryButton { "Choose folder..." }, rescanButton { "Rescan" };
    std::unique_ptr<Swatches> swatches;
    juce::Label configPath;
    juce::TextButton showConfigButton { "Show" };

    juce::OwnedArray<juce::Label> headings;
    juce::uint32 shownLastNote = 0;
};

}
