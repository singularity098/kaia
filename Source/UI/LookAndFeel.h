#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kaia
{

namespace Colours
{
    const juce::Colour background   { 0xff15171b };
    const juce::Colour panel        { 0xff1d2026 };
    const juce::Colour panelRaised  { 0xff262a31 };
    const juce::Colour outline      { 0xff31353d };
    const juce::Colour text         { 0xffe8e6e1 };
    const juce::Colour textDim      { 0xff8d9099 };
    const juce::Colour accent       { 0xffff7a3d };   // warm orange
    const juce::Colour accentDim    { 0xff8a4526 };
    const juce::Colour good         { 0xff58c49a };
    const juce::Colour warning      { 0xffe8b04a };
    const juce::Colour error        { 0xffe8625a };
    const juce::Colour padEmpty     { 0xff23262c };
    const juce::Colour padLoaded    { 0xff2d3139 };
}

class KaiaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KaiaLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSliderOutline (juce::Graphics&, int, int, int, int, juce::Slider::SliderStyle, juce::Slider&) override {}
};

juce::Font uiFont (float height, bool bold = false);

}
