#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace kaia
{

class PadGrid : public juce::Component,
                private juce::Timer
{
public:
    explicit PadGrid (KaiaProcessor& processor);
    ~PadGrid() override;

    void refresh();                        // kit or selection changed
    int padIndexAt (juce::Point<int> positionInGrid) const;
    void setDropHighlight (int padIndex);

    std::function<void (int pad)> onPadSelected;
    std::function<void (int pad, juce::Point<int> screenPos)> onPadMenu;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class PadButton;

    void timerCallback() override;
    int numBanks() const;
    void showBank (int bank);

    KaiaProcessor& processor;
    juce::OwnedArray<juce::TextButton> bankButtons;
    juce::OwnedArray<PadButton> pads;
    std::array<juce::uint32, kMaxPads> lastHits {};
    int dropHighlight = -1;
};

}
