#include "PadGrid.h"

namespace kaia
{

//==============================================================================
class PadGrid::PadButton : public juce::Component
{
public:
    PadButton (PadGrid& g, int slot) : grid (g), slotInBank (slot) {}

    int padIndex = 0;
    float flash = 0.0f;
    int voices = 0;
    bool selected = false;
    bool dropTarget = false;

    void paint (juce::Graphics& g) override
    {
        auto& kit = grid.processor.getKit();
        const Pad* pad = padIndex < (int) kit.pads.size() ? &kit.pads[(size_t) padIndex] : nullptr;
        bool loaded = pad != nullptr && ! pad->layers.empty();
        bool missing = false;
        if (pad != nullptr)
            for (auto& l : pad->layers)
                missing = missing || l.missing;

        auto r = getLocalBounds().toFloat().reduced (3.0f);
        auto base = loaded ? Colours::padLoaded : Colours::padEmpty;
        if (pad != nullptr && pad->mute)
            base = base.darker (0.3f);
        g.setColour (base);
        g.fillRoundedRectangle (r, 6.0f);

        // Hit flash and sounding glow
        if (flash > 0.01f)
        {
            g.setColour (Colours::accent.withAlpha (0.55f * flash));
            g.fillRoundedRectangle (r, 6.0f);
        }
        if (voices > 0)
        {
            g.setColour (Colours::accent.withAlpha (0.9f));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - 3.0f).reduced (8.0f, 0.0f), 1.5f);
        }

        g.setColour (dropTarget ? Colours::good : selected ? Colours::accent : Colours::outline);
        g.drawRoundedRectangle (r, 6.0f, dropTarget || selected ? 2.0f : 1.0f);

        auto inner = r.reduced (8.0f, 6.0f).toNearestInt();
        g.setFont (uiFont (11.5f, true));
        g.setColour (selected ? Colours::accent : Colours::textDim);
        g.drawText (pad != nullptr && pad->isKeygroup() ? "KG " + juce::String (padIndex + 1) : padName (padIndex),
                    inner.removeFromTop (14), juce::Justification::topLeft);

        // Note / key range, top right
        if (pad != nullptr)
        {
            juce::String noteText;
            if (pad->isKeygroup())
                noteText = juce::MidiMessage::getMidiNoteName (pad->noteLow, true, true, 3) + "-" + juce::MidiMessage::getMidiNoteName (pad->noteHigh, true, true, 3);
            else if (pad->note >= 0)
                noteText = juce::MidiMessage::getMidiNoteName (pad->note, true, true, 3);
            g.setFont (uiFont (11.0f));
            g.setColour (Colours::textDim);
            g.drawText (noteText, getLocalBounds().reduced (11, 9).removeFromTop (14), juce::Justification::topRight);
        }

        if (! loaded)
            return;

        // Name in the middle
        auto name = pad->name.isNotEmpty() && ! pad->isKeygroup() ? pad->name : pad->layers.front().name.trim();
        if (name.isEmpty())
            name = pad->layers.front().sample.displayName();
        g.setFont (uiFont (juce::jlimit (11.0f, 15.0f, (float) getHeight() * 0.13f)));
        g.setColour (missing ? Colours::error : Colours::text);
        auto nameArea = inner.withTrimmedBottom (14);
        g.drawFittedText (name, nameArea, juce::Justification::centred, 3, 0.8f);

        // Footer: layer dots, choke group, mute/solo
        auto footer = inner.removeFromBottom (12);
        int n = (int) pad->layers.size();
        for (int i = 0; i < n; ++i)
        {
            auto dot = juce::Rectangle<float> ((float) footer.getX() + (float) i * 8.0f, (float) footer.getCentreY() - 2.5f, 5.0f, 5.0f);
            g.setColour (pad->layers[(size_t) i].missing ? Colours::error : Colours::textDim);
            g.fillEllipse (dot);
        }

        juce::String tags;
        if (pad->muteGroup > 0) tags << "G" << pad->muteGroup << " ";
        if (pad->output > 0)    tags << "A" << pad->output << " ";
        if (pad->mute)          tags << "M ";
        if (pad->solo)          tags << "S ";
        if (pad->layerMode == LayerMode::Cycle && n > 1)  tags << "RR ";
        if (pad->layerMode == LayerMode::Random && n > 1) tags << "RND ";
        g.setFont (uiFont (10.5f, true));
        g.setColour (Colours::textDim);
        g.drawText (tags.trim(), footer, juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (grid.onPadSelected)
            grid.onPadSelected (padIndex);

        if (e.mods.isPopupMenu())
        {
            if (grid.onPadMenu)
                grid.onPadMenu (padIndex, e.getScreenPosition());
            return;
        }

        auto& kit = grid.processor.getKit();
        if (padIndex >= (int) kit.pads.size())
            return;
        auto& pad = kit.pads[(size_t) padIndex];
        auditionNote = pad.isKeygroup() ? juce::jlimit (pad.noteLow, pad.noteHigh, 60) : pad.note;
        if (auditionNote < 0)
            return;

        // Velocity from where the pad is hit: top = hardest
        auto y = 1.0f - juce::jlimit (0.0f, 1.0f, (float) e.y / (float) juce::jmax (1, getHeight()));
        auto velocity = juce::jlimit (1, 127, (int) std::round (40.0f + y * 87.0f));
        grid.processor.getEngine().auditionNoteOn (auditionNote, velocity);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (auditionNote >= 0)
            grid.processor.getEngine().auditionNoteOff (auditionNote);
        auditionNote = -1;
    }

private:
    PadGrid& grid;
    int slotInBank;
    int auditionNote = -1;
};

//==============================================================================
PadGrid::PadGrid (KaiaProcessor& p) : processor (p)
{
    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* btn = bankButtons.add (new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b))));
        btn->setClickingTogglesState (false);
        btn->setTooltip ("Pad bank " + btn->getButtonText());
        btn->onClick = [this, b] { showBank (b); };
        addAndMakeVisible (btn);
    }

    for (int i = 0; i < kPadsPerBank; ++i)
        addAndMakeVisible (pads.add (new PadButton (*this, i)));

    for (int i = 0; i < kMaxPads; ++i)
        lastHits[(size_t) i] = processor.getEngine().getPadHitCount (i);

    refresh();
    startTimerHz (30);
}

PadGrid::~PadGrid() { stopTimer(); }

int PadGrid::numBanks() const
{
    auto& kit = processor.getKit();
    if (kit.isKeygroupProgram)
        return juce::jlimit (1, kNumBanks, ((int) kit.pads.size() + kPadsPerBank - 1) / kPadsPerBank);
    return kNumBanks;
}

void PadGrid::showBank (int bank)
{
    processor.selectedBank = juce::jlimit (0, numBanks() - 1, bank);
    refresh();
}

void PadGrid::refresh()
{
    auto& kit = processor.getKit();
    auto banks = numBanks();
    processor.selectedBank = juce::jlimit (0, banks - 1, processor.selectedBank);

    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* btn = bankButtons[b];
        btn->setVisible (b < banks);
        btn->setToggleState (b == processor.selectedBank, juce::dontSendNotification);

        bool hasSamples = false;
        for (int i = b * kPadsPerBank; i < (b + 1) * kPadsPerBank && i < (int) kit.pads.size(); ++i)
            hasSamples = hasSamples || ! kit.pads[(size_t) i].layers.empty();
        btn->setColour (juce::TextButton::textColourOffId, hasSamples ? Colours::text : Colours::textDim.withAlpha (0.6f));
    }

    for (int i = 0; i < kPadsPerBank; ++i)
    {
        auto* pad = pads[i];
        pad->padIndex = processor.selectedBank * kPadsPerBank + i;
        pad->selected = pad->padIndex == processor.selectedPad;
        pad->dropTarget = pad->padIndex == dropHighlight;
        pad->setVisible (pad->padIndex < (int) kit.pads.size() || ! kit.isKeygroupProgram);
        pad->repaint();
    }
    resized();
}

void PadGrid::setDropHighlight (int padIndex)
{
    if (dropHighlight == padIndex)
        return;
    dropHighlight = padIndex;
    for (auto* p : pads)
    {
        p->dropTarget = p->padIndex == dropHighlight;
        p->repaint();
    }
}

int PadGrid::padIndexAt (juce::Point<int> pos) const
{
    for (auto* p : pads)
        if (p->isVisible() && p->getBounds().contains (pos))
            return p->padIndex;
    return -1;
}

void PadGrid::resized()
{
    auto r = getLocalBounds();
    auto bankRow = r.removeFromTop (30);
    auto banks = numBanks();
    auto bw = juce::jmin (44, bankRow.getWidth() / juce::jmax (1, kNumBanks));
    for (int b = 0; b < kNumBanks; ++b)
        if (b < banks)
            bankButtons[b]->setBounds (bankRow.removeFromLeft (bw).reduced (2, 0));

    r.removeFromTop (8);
    auto side = juce::jmin (r.getWidth(), r.getHeight());
    auto grid = r.withSizeKeepingCentre (side, side).withY (r.getY());
    auto cell = side / 4;

    // MPC layout: pad 1 bottom-left, pad 16 top-right
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        auto col = i % 4;
        auto row = 3 - i / 4;
        pads[i]->setBounds (grid.getX() + col * cell, grid.getY() + row * cell, cell, cell);
    }
}

void PadGrid::paint (juce::Graphics&) {}

void PadGrid::timerCallback()
{
    auto& engine = processor.getEngine();
    for (auto* p : pads)
    {
        auto idx = p->padIndex;
        if (idx < 0 || idx >= kMaxPads)
            continue;

        auto hits = engine.getPadHitCount (idx);
        auto voices = engine.getPadVoiceCount (idx);
        bool dirty = false;

        if (hits != lastHits[(size_t) idx])
        {
            lastHits[(size_t) idx] = hits;
            p->flash = 1.0f;
            dirty = true;
        }
        else if (p->flash > 0.0f)
        {
            p->flash = p->flash < 0.02f ? 0.0f : p->flash * 0.78f;
            dirty = true;
        }

        if (voices != p->voices)
        {
            p->voices = voices;
            dirty = true;
        }

        if (dirty)
            p->repaint();
    }

    // Keep hit counters of other banks in sync so switching banks doesn't flash old hits
    for (int i = 0; i < kMaxPads; ++i)
    {
        auto bank = i / kPadsPerBank;
        if (bank != processor.selectedBank)
            lastHits[(size_t) i] = engine.getPadHitCount (i);
    }
}

}
