#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace kaia
{

// Rotary knob with a caption and a value read-out.
class Knob : public juce::Component
{
public:
    Knob (const juce::String& caption, juce::NormalisableRange<double> range, std::function<juce::String (double)> format);

    juce::Slider slider;
    std::function<void (double)> onChange;

    void setValue (double v)        { slider.setValue (v, juce::dontSendNotification); }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String caption;
};

// Combo box with a caption above it.
class LabelledCombo : public juce::Component
{
public:
    explicit LabelledCombo (const juce::String& caption);
    juce::ComboBox box;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String caption;
};

class WaveformView : public juce::Component
{
public:
    void setSample (SampleDataPtr data, juce::int64 start, juce::int64 end, bool reverse, bool missing, const juce::String& name);
    void paint (juce::Graphics&) override;

private:
    SampleDataPtr sample;
    juce::int64 start = 0, end = 0;
    bool reverse = false, missing = false;
    juce::String name;
};

// Plays the selected pad while held (a big, touch-friendly target under the waveform).
class PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("Play") {}
    std::function<void()> onPress, onRelease;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void buttonStateChanged() override;

private:
    bool wasDown = false;
};

class PadEditor : public juce::Component
{
public:
    explicit PadEditor (KaiaProcessor& processor);
    ~PadEditor() override;

    void refresh();
    int layerIndexAt (juce::Point<int> positionInEditor) const;   // for drag & drop onto a layer row

    std::function<void (int pad)> onLoadSampleRequest;            // "+ Layer" button
    std::function<void (int pad, int layer)> onReplaceLayerRequest;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class LayerRow;

    void edit (const std::function<void (Pad&)>& fn);
    void editLayer (int layer, const std::function<void (Layer&)>& fn);
    void selectLayer (int layer);
    void updateWaveform();

    KaiaProcessor& processor;

    juce::Label padLabel, nameLabel, rangeLabel, hintLabel;
    juce::ComboBox noteBox;
    juce::TextButton muteButton { "M" }, soloButton { "S" };

    Knob level, pan, tune, velLevel, attack, hold, decay, sustain, release;
    Knob cutoff, resonance, velCutoff;
    LabelledCombo trigger { "Trigger" }, layerMode { "Layers" }, envMode { "Envelope" },
                  choke { "Choke group" }, output { "Output" }, voiceMode { "Voices" }, filterType { "Filter" };

    juce::OwnedArray<LayerRow> layerRows;
    juce::TextButton addLayerButton { "+ Add layer..." }, clearButton { "Clear pad" };
    PlayButton playButton;
    int playingNote = -1;
    WaveformView waveform;

    int selectedLayer = 0;
    juce::Rectangle<int> layersHeaderArea, soundArea, filterArea;
};

}
