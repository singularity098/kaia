#pragma once

#include "LookAndFeel.h"

namespace kaia
{

// A one-line text field for searching and filtering, with an "x" at the right that clears it.
// Clearing (the button or Escape) sends onTextChange (asynchronously, like typing does); reset()
// clears it silently. Set other text with setText (text, true) so the button follows.
class SearchBox : public juce::TextEditor,
                  private juce::TextEditor::Listener
{
public:
    SearchBox()
    {
        setFont (uiFont (14.0f));
        setJustification (juce::Justification::centredLeft);
        setIndents (8, 0);
        setBorder ({ 1, 1, 1, kButtonWidth });
        onEscapeKey = [this] { setText ({}, true); };
        addListener (this);

        clearButton.setTooltip ("Clear");
        clearButton.setWantsKeyboardFocus (false);
        clearButton.setMouseCursor (juce::MouseCursor::NormalCursor);
        clearButton.onClick = [this] { setText ({}, true); };
        addChildComponent (clearButton);
    }

    ~SearchBox() override
    {
        removeListener (this);
    }

    void reset()
    {
        setText ({}, false);
        clearButton.setVisible (false);
    }

    void resized() override
    {
        juce::TextEditor::resized();
        clearButton.setBounds (getLocalBounds().removeFromRight (kButtonWidth));
        clearButton.toFront (false);
    }

private:
    static constexpr int kButtonWidth = 28;

    class ClearButton : public juce::Button
    {
    public:
        ClearButton() : juce::Button ("clear") {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().withSizeKeepingCentre (16.0f, 16.0f);
            g.setColour (down ? Colours::accent : over ? Colours::text.withAlpha (0.35f) : Colours::textDim.withAlpha (0.35f));
            g.fillEllipse (r);
            g.setColour (Colours::panel);
            auto x = r.reduced (5.0f);
            juce::Path p;
            p.addLineSegment ({ x.getTopLeft(), x.getBottomRight() }, 1.6f);
            p.addLineSegment ({ x.getTopRight(), x.getBottomLeft() }, 1.6f);
            g.fillPath (p);
        }
    };

    void textEditorTextChanged (juce::TextEditor&) override
    {
        clearButton.setVisible (! isEmpty());
    }

    ClearButton clearButton;
};

}
