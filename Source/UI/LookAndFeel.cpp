#include "LookAndFeel.h"

namespace kaia
{

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

KaiaLookAndFeel::KaiaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::TextButton::buttonColourId, Colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, Colours::panelRaised);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panelRaised);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::headerTextColourId, Colours::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, Colours::panelRaised);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, Colours::accent);
    setColour (juce::ListBox::backgroundColourId, Colours::panel);
    setColour (juce::ScrollBar::thumbColourId, Colours::outline.brighter (0.3f));
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelRaised);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::ToggleButton::textColourId, Colours::text);
    setColour (juce::ToggleButton::tickColourId, Colours::accent);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::outline);
    setColour (juce::ProgressBar::backgroundColourId, Colours::panelRaised);
    setColour (juce::ProgressBar::foregroundColourId, Colours::accent);
}

void KaiaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                            float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    auto lineW = juce::jmax (2.0f, radius * 0.16f);
    auto arcRadius = radius - lineW * 0.5f;
    auto enabled = slider.isEnabled();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::outline);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar knobs (pan, tune) fill from the centre
    auto isBipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    auto fromAngle = isBipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    auto toAngle = startAngle + pos * (endAngle - startAngle);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (fromAngle, toAngle), juce::jmax (fromAngle, toAngle), true);
    g.setColour (enabled ? Colours::accent : Colours::textDim);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto knobR = arcRadius - lineW * 1.4f;
    g.setColour (Colours::panelRaised);
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2);

    juce::Path pointer;
    auto pLen = knobR * 0.75f;
    pointer.addRoundedRectangle (-1.25f, -knobR + 2.0f, 2.5f, pLen * 0.55f, 1.0f);
    g.setColour (enabled ? Colours::text : Colours::textDim);
    g.fillPath (pointer, juce::AffineTransform::rotation (toAngle).translated (centre));
}

void KaiaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                            float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
    {
        auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (Colours::panelRaised);
        g.fillRoundedRectangle (r, 3.0f);
        auto isBipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        juce::Rectangle<float> fill;
        if (isBipolar)
        {
            auto zero = (float) slider.getPositionOfValue (0.0);
            fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zero, sliderPos), r.getY(), juce::jmax (zero, sliderPos), r.getBottom());
        }
        else
        {
            fill = r.withRight (sliderPos);
        }
        g.setColour (Colours::accent.withAlpha (slider.isEnabled() ? 0.35f : 0.15f));
        g.fillRoundedRectangle (fill, 3.0f);
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
}

void KaiaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                                bool isMouseOverButton, bool isButtonDown)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    auto c = button.getToggleState() ? Colours::accent : backgroundColour;
    if (isButtonDown)
        c = c.brighter (0.15f);
    else if (isMouseOverButton)
        c = c.brighter (0.07f);
    if (! button.isEnabled())
        c = c.withAlpha (0.5f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::outline.withAlpha (button.getToggleState() ? 0.0f : 1.0f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void KaiaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    auto r = button.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (juce::jmin (r.getHeight(), 18.0f)).reduced (2.0f).withSizeKeepingCentre (14.0f, 14.0f);
    g.setColour (button.getToggleState() ? Colours::accent : Colours::panelRaised);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (highlighted ? Colours::textDim : Colours::outline);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
        tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
        tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
        g.setColour (juce::Colours::black);
        g.strokePath (tick, juce::PathStrokeType (1.8f));
    }
    g.setColour (button.isEnabled() ? Colours::text : Colours::textDim);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (button.getButtonText(), r.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 1);
}

void KaiaLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Colours::accent : Colours::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    auto ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Colours::textDim);
    g.fillPath (arrow);
}

juce::Font KaiaLookAndFeel::getComboBoxFont (juce::ComboBox&)               { return uiFont (13.5f); }
juce::Font KaiaLookAndFeel::getTextButtonFont (juce::TextButton&, int h)    { return uiFont (juce::jmin (14.0f, (float) h * 0.55f)); }
juce::Font KaiaLookAndFeel::getLabelFont (juce::Label& l)                   { return l.getFont(); }

void KaiaLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void KaiaLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Colours::panelRaised);
    g.setColour (Colours::outline);
    g.drawRect (0, 0, width, height);
}

void KaiaLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto r = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (Colours::panelRaised);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (text, r.toNearestInt().reduced (8, 4), juce::Justification::centredLeft, 6);
}

juce::Label* KaiaLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (uiFont (12.0f));
    l->setColour (juce::Label::textColourId, Colours::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textWhenEditingColourId, Colours::text);
    l->setColour (juce::Label::backgroundWhenEditingColourId, Colours::panelRaised);
    l->setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
    return l;
}

}
