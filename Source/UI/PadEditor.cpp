#include "PadEditor.h"

namespace kaia
{

namespace
{
    juce::String fmtDb (double v)      { return v <= -59.95 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String fmtPan (double v)
    {
        auto p = juce::roundToInt (v * 100.0);
        return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
    }
    juce::String fmtSemis (double v)   { return (v > 0 ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) < 0.005 ? 0 : 2) + " st"; }
    juce::String fmtPercent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
    juce::String fmtMs (double v)      { return v < 1000.0 ? juce::String (v, v < 10.0 ? 1 : 0) + " ms" : juce::String (v / 1000.0, 2) + " s"; }
    juce::String fmtHz (double v)      { return v < 1000.0 ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0, 1) + " kHz"; }

    juce::NormalisableRange<double> skewed (double lo, double hi, double centre, double interval = 0.0)
    {
        juce::NormalisableRange<double> r (lo, hi, interval);
        r.setSkewForCentre (centre);
        return r;
    }

    void styleSmallSlider (juce::Slider& s, double lo, double hi, double interval, std::function<juce::String (double)> fmt)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (lo, hi, interval);
        s.setTextBoxIsEditable (true);
        s.textFromValueFunction = std::move (fmt);
        s.setVelocityBasedMode (false);
        s.setMouseDragSensitivity (200);
        s.setDoubleClickReturnValue (true, juce::jlimit (lo, hi, 0.0));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, Colours::text);
        s.updateText();
    }
}

//==============================================================================
Knob::Knob (const juce::String& c, juce::NormalisableRange<double> range, std::function<juce::String (double)> format)
    : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setNormalisableRange (range);
    slider.textFromValueFunction = std::move (format);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.onValueChange = [this] { if (onChange) onChange (slider.getValue()); };
    slider.setPopupMenuEnabled (false);
    slider.updateText();
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (14));
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, getWidth(), 16);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? Colours::textDim : Colours::textDim.withAlpha (0.4f));
    g.setFont (uiFont (11.5f));
    g.drawText (caption, getLocalBounds().removeFromTop (14), juce::Justification::centred);
}

LabelledCombo::LabelledCombo (const juce::String& c) : caption (c)
{
    addAndMakeVisible (box);
}

void LabelledCombo::resized()
{
    box.setBounds (getLocalBounds().withTrimmedTop (15));
}

void LabelledCombo::paint (juce::Graphics& g)
{
    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.5f));
    g.drawText (caption, getLocalBounds().removeFromTop (14).withTrimmedLeft (2), juce::Justification::centredLeft);
}

//==============================================================================
void WaveformView::setSample (SampleDataPtr data, juce::int64 s, juce::int64 e, bool rev, bool miss, const juce::String& n)
{
    sample = std::move (data);
    start = s;
    end = e;
    reverse = rev;
    missing = miss;
    name = n;
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::panelRaised.darker (0.25f));
    g.fillRoundedRectangle (r, 5.0f);

    if (missing)
    {
        g.setColour (Colours::error);
        g.setFont (uiFont (13.0f));
        g.drawText ("Sample missing: " + name, getLocalBounds(), juce::Justification::centred);
        return;
    }
    if (sample == nullptr || sample->overview.empty())
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (13.0f));
        g.drawText ("No sample", getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto& ov = sample->overview;
    auto inner = r.reduced (6.0f, 8.0f);
    auto mid = inner.getCentreY();
    auto half = inner.getHeight() * 0.5f;
    auto n = (int) ov.size();

    auto frameToX = [&] (juce::int64 f) { return inner.getX() + inner.getWidth() * (float) ((double) f / (double) juce::jmax<juce::int64> (1, sample->numFrames)); };
    auto s = start > 0 ? start : sample->defaultStart;
    auto e = end > 0 ? end : sample->effectiveEnd();
    auto x0 = frameToX (s), x1 = frameToX (e);

    juce::Path wave;
    for (int i = 0; i < n; ++i)
    {
        auto x = inner.getX() + inner.getWidth() * (float) i / (float) (n - 1);
        auto v = ov[(size_t) i] * half;
        wave.addRectangle (x, mid - v, juce::jmax (1.0f, inner.getWidth() / (float) n), juce::jmax (1.0f, 2.0f * v));
    }
    g.setColour (Colours::textDim.withAlpha (0.35f));
    g.fillPath (wave);

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()).toNearestInt());
    g.setColour (Colours::accent.withAlpha (0.85f));
    g.fillPath (wave);
    g.restoreState();

    g.setColour (Colours::text.withAlpha (0.6f));
    g.drawVerticalLine ((int) x0, r.getY() + 3, r.getBottom() - 3);
    g.drawVerticalLine ((int) x1, r.getY() + 3, r.getBottom() - 3);

    g.setFont (uiFont (11.0f));
    juce::String info = name + "  " + juce::String (sample->sampleRate / 1000.0, 1) + " kHz "
                      + (sample->numChannels > 1 ? "stereo" : "mono") + "  "
                      + juce::String ((double) (e - s) / sample->sampleRate, 2) + " s" + (reverse ? "  reversed" : "");
    auto textW = juce::GlyphArrangement::getStringWidthInt (uiFont (11.0f), info) + 12;
    auto textArea = juce::Rectangle<int> (6, 5, textW, 16);
    g.setColour (Colours::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (textArea.toFloat(), 3.0f);
    g.setColour (Colours::text);
    g.drawText (info, textArea, juce::Justification::centred);
}

//==============================================================================
class PadEditor::LayerRow : public juce::Component
{
public:
    LayerRow (PadEditor& o, int i) : owner (o), index (i)
    {
        enable.onClick = [this] { auto on = enable.getToggleState(); owner.editLayer (index, [on] (Layer& l) { l.enabled = on; }); };
        enable.setTooltip ("Layer on/off");
        addAndMakeVisible (enable);

        name.setFont (uiFont (12.5f));
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        styleSmallSlider (velLo, 0, 127, 1, [] (double v) { return juce::String ((int) v); });
        styleSmallSlider (velHi, 0, 127, 1, [] (double v) { return juce::String ((int) v); });
        styleSmallSlider (gain, -60, 12, 0.1, [] (double v) { return v <= -59.95 ? juce::String ("-inf") : juce::String (v, 1); });
        styleSmallSlider (tune, -48, 48, 0.01, [] (double v) { return juce::String (v, std::abs (v - std::round (v)) < 0.005 ? 0 : 2); });
        styleSmallSlider (pan, -1, 1, 0.01, fmtPan);
        velHi.setDoubleClickReturnValue (true, 127);
        velLo.setTooltip ("Lowest velocity that plays this layer");
        velHi.setTooltip ("Highest velocity that plays this layer");
        gain.setTooltip ("Layer level (dB)");
        tune.setTooltip ("Layer tuning (semitones)");
        pan.setTooltip ("Layer pan");

        velLo.onValueChange = [this] { auto v = (int) velLo.getValue(); owner.editLayer (index, [v] (Layer& l) { l.velLow = v; l.velHigh = juce::jmax (l.velHigh, v); }); };
        velHi.onValueChange = [this] { auto v = (int) velHi.getValue(); owner.editLayer (index, [v] (Layer& l) { l.velHigh = v; l.velLow = juce::jmin (l.velLow, v); }); };
        gain.onValueChange  = [this] { auto v = (float) gain.getValue(); owner.editLayer (index, [v] (Layer& l) { l.gainDb = v; }); };
        tune.onValueChange  = [this] { auto v = (float) tune.getValue(); owner.editLayer (index, [v] (Layer& l) { l.tuneSemis = v; }); };
        pan.onValueChange   = [this] { auto v = (float) pan.getValue(); owner.editLayer (index, [v] (Layer& l) { l.pan = v; }); };

        for (auto* s : { &velLo, &velHi, &gain, &tune, &pan })
            addAndMakeVisible (s);

        reverse.setClickingTogglesState (true);
        reverse.setTooltip ("Play backwards");
        reverse.onClick = [this] { auto on = reverse.getToggleState(); owner.editLayer (index, [on] (Layer& l) { l.reverse = on; }); };
        addAndMakeVisible (reverse);

        remove.setTooltip ("Remove this layer");
        remove.onClick = [this]
        {
            auto layerToRemove = index;
            owner.edit ([layerToRemove] (Pad& p)
            {
                if (layerToRemove < (int) p.layers.size())
                    p.layers.erase (p.layers.begin() + layerToRemove);
            });
        };
        addAndMakeVisible (remove);
    }

    void update (const Layer& l, bool isSelected)
    {
        selected = isSelected;
        enable.setToggleState (l.enabled, juce::dontSendNotification);
        auto text = l.name.trim().isNotEmpty() ? l.name.trim() : l.sample.displayName();
        name.setText (text, juce::dontSendNotification);
        name.setColour (juce::Label::textColourId, l.missing ? Colours::error : (l.enabled ? Colours::text : Colours::textDim));
        name.setTooltip (l.missing ? "Missing: " + l.sample.toString() : l.sample.toString());
        velLo.setValue (l.velLow, juce::dontSendNotification);
        velHi.setValue (l.velHigh, juce::dontSendNotification);
        gain.setValue (juce::jmax (-60.0f, l.gainDb), juce::dontSendNotification);
        tune.setValue (l.tuneSemis, juce::dontSendNotification);
        pan.setValue (l.pan, juce::dontSendNotification);
        reverse.setToggleState (l.reverse, juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 2);
        r.removeFromLeft (16);   // index
        enable.setBounds (r.removeFromLeft (22));
        remove.setBounds (r.removeFromRight (22));
        r.removeFromRight (3);
        reverse.setBounds (r.removeFromRight (34));
        r.removeFromRight (3);
        pan.setBounds (r.removeFromRight (46));
        r.removeFromRight (3);
        tune.setBounds (r.removeFromRight (50));
        r.removeFromRight (3);
        gain.setBounds (r.removeFromRight (50));
        r.removeFromRight (3);
        velHi.setBounds (r.removeFromRight (36));
        r.removeFromRight (3);
        velLo.setBounds (r.removeFromRight (36));
        r.removeFromRight (4);
        name.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
        g.setColour (selected ? Colours::accent.withAlpha (0.12f) : Colours::panelRaised.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colours::textDim);
        g.setFont (uiFont (11.0f, true));
        g.drawText (juce::String (index + 1), getLocalBounds().withWidth (18), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override  { owner.selectLayer (index); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (owner.onReplaceLayerRequest)
            owner.onReplaceLayerRequest (owner.processor.selectedPad, index);
    }

private:
    PadEditor& owner;
    int index;
    bool selected = false;
    juce::ToggleButton enable;
    juce::Label name;
    juce::Slider velLo, velHi, gain, tune, pan;
    juce::TextButton reverse { "Rev" }, remove { "x" };
};

//==============================================================================
PadEditor::PadEditor (KaiaProcessor& p)
    : processor (p),
      level ("Level", skewed (-60.0, 12.0, -12.0, 0.1), fmtDb),
      pan ("Pan", juce::NormalisableRange<double> (-1.0, 1.0, 0.01), fmtPan),
      tune ("Tune", juce::NormalisableRange<double> (-36.0, 36.0, 0.01), fmtSemis),
      velLevel ("Vel > Level", juce::NormalisableRange<double> (0.0, 1.0, 0.01), fmtPercent),
      attack ("Attack", skewed (0.0, 5000.0, 150.0, 0.1), fmtMs),
      hold ("Hold", skewed (0.0, 5000.0, 150.0, 0.1), fmtMs),
      decay ("Decay", skewed (0.0, 10000.0, 400.0, 0.1), fmtMs),
      sustain ("Sustain", juce::NormalisableRange<double> (0.0, 1.0, 0.01), fmtPercent),
      release ("Release", skewed (0.0, 10000.0, 400.0, 0.1), fmtMs),
      cutoff ("Cutoff", skewed (20.0, 20000.0, 1000.0, 1.0), fmtHz),
      resonance ("Resonance", juce::NormalisableRange<double> (0.0, 1.0, 0.01), fmtPercent),
      velCutoff ("Vel > Cutoff", juce::NormalisableRange<double> (0.0, 1.0, 0.01), fmtPercent)
{
    padLabel.setFont (uiFont (20.0f, true));
    padLabel.setColour (juce::Label::textColourId, Colours::accent);
    addAndMakeVisible (padLabel);

    nameLabel.setFont (uiFont (15.0f));
    nameLabel.setEditable (false, true);
    nameLabel.setTooltip ("Double-click to rename the pad");
    nameLabel.onTextChange = [this] { auto t = nameLabel.getText(); edit ([t] (Pad& pad) { pad.name = t; }); };
    addAndMakeVisible (nameLabel);

    rangeLabel.setFont (uiFont (13.0f));
    rangeLabel.setColour (juce::Label::textColourId, Colours::textDim);
    rangeLabel.setJustificationType (juce::Justification::centredRight);
    addChildComponent (rangeLabel);

    for (int n = 0; n < 128; ++n)
        noteBox.addItem (noteName (n), n + 1);
    noteBox.setTooltip ("MIDI note that plays this pad");
    noteBox.onChange = [this] { auto n = noteBox.getSelectedId() - 1; edit ([n] (Pad& pad) { pad.note = n; }); };
    addAndMakeVisible (noteBox);

    muteButton.setClickingTogglesState (true);
    soloButton.setClickingTogglesState (true);
    muteButton.setTooltip ("Mute pad");
    soloButton.setTooltip ("Solo pad");
    muteButton.onClick = [this] { auto on = muteButton.getToggleState(); edit ([on] (Pad& pad) { pad.mute = on; }); };
    soloButton.onClick = [this] { auto on = soloButton.getToggleState(); edit ([on] (Pad& pad) { pad.solo = on; }); };
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    level.onChange     = [this] (double v) { edit ([v] (Pad& pad) { pad.volumeDb = v <= -59.95 ? -100.0f : (float) v; }); };
    pan.onChange       = [this] (double v) { edit ([v] (Pad& pad) { pad.pan = (float) v; }); };
    tune.onChange      = [this] (double v) { edit ([v] (Pad& pad) { pad.tuneSemis = (float) v; }); };
    velLevel.onChange  = [this] (double v) { edit ([v] (Pad& pad) { pad.velToLevel = (float) v; }); };
    attack.onChange    = [this] (double v) { edit ([v] (Pad& pad) { pad.env.attackMs = (float) v; }); };
    hold.onChange      = [this] (double v) { edit ([v] (Pad& pad) { pad.env.holdMs = (float) v; }); };
    decay.onChange     = [this] (double v) { edit ([v] (Pad& pad) { pad.env.decayMs = (float) v; }); };
    sustain.onChange   = [this] (double v) { edit ([v] (Pad& pad) { pad.env.sustain = (float) v; }); };
    release.onChange   = [this] (double v) { edit ([v] (Pad& pad) { pad.env.releaseMs = (float) v; }); };
    cutoff.onChange    = [this] (double v) { edit ([v] (Pad& pad) { pad.cutoffHz = (float) v; }); };
    resonance.onChange = [this] (double v) { edit ([v] (Pad& pad) { pad.resonance = (float) v; }); };
    velCutoff.onChange = [this] (double v) { edit ([v] (Pad& pad) { pad.velToCutoff = (float) v; }); };

    level.slider.setDoubleClickReturnValue (true, 0.0);
    pan.slider.setDoubleClickReturnValue (true, 0.0);
    tune.slider.setDoubleClickReturnValue (true, 0.0);
    velLevel.slider.setDoubleClickReturnValue (true, 1.0);
    cutoff.slider.setDoubleClickReturnValue (true, 20000.0);
    resonance.slider.setDoubleClickReturnValue (true, 0.0);

    for (auto* k : { &level, &pan, &tune, &velLevel, &attack, &hold, &decay, &sustain, &release, &cutoff, &resonance, &velCutoff })
        addAndMakeVisible (k);

    trigger.box.addItemList ({ "One shot", "Note on", "Note off" }, 1);
    trigger.box.setTooltip ("One shot: plays to the end. Note on: stops when the note is released. Note off: plays when the note is released.");
    trigger.box.onChange = [this] { auto i = trigger.box.getSelectedItemIndex(); edit ([i] (Pad& pad) { pad.trigger = (TriggerMode) i; }); };

    layerMode.box.addItemList ({ "Velocity", "Cycle", "Random" }, 1);
    layerMode.box.onChange = [this] { auto i = layerMode.box.getSelectedItemIndex(); edit ([i] (Pad& pad) { pad.layerMode = (LayerMode) i; }); };

    envMode.box.addItemList ({ "Tail fade", "Decay", "AHDSR" }, 1);
    envMode.box.setTooltip ("Tail fade (MPC \"decay from end\"): the sample plays fully and Decay fades out its tail.\nDecay (MPC \"decay from start\"): the sound fades out over Decay right after the attack. Use it to shorten hits.\nAHDSR: classic envelope with sustain; Release starts when the note ends.");
    envMode.box.onChange = [this]
    {
        auto i = envMode.box.getSelectedItemIndex();
        edit ([i] (Pad& pad) { pad.env.adMode = i < 2; pad.env.decayFromEnd = i == 0; });
    };

    choke.box.addItem ("Off", 1);
    for (int g = 1; g <= 32; ++g)
        choke.box.addItem ("Group " + juce::String (g), g + 1);
    choke.box.setTooltip ("Pads in the same group cut each other off (e.g. open and closed hi-hat)");
    choke.box.onChange = [this] { auto g = choke.box.getSelectedId() - 1; edit ([g] (Pad& pad) { pad.muteGroup = g; }); };

    output.box.addItem ("Main", 1);
    for (int o = 1; o <= kNumAuxOutputs; ++o)
        output.box.addItem ("Aux " + juce::String (o), o + 1);
    output.box.setTooltip ("Plugin output for this pad (enable the aux outputs in your DAW for multi-out mixing)");
    output.box.onChange = [this] { auto o = output.box.getSelectedId() - 1; edit ([o] (Pad& pad) { pad.output = o; }); };

    voiceMode.box.addItemList ({ "Poly", "Mono" }, 1);
    layerMode.box.setTooltip ("Velocity: every layer whose velocity range matches plays (stacking). Cycle: one layer per hit, in turn (round robin). Random: one random layer per hit.");
    voiceMode.box.setTooltip ("Mono: a new hit cuts the previous one on this pad");
    voiceMode.box.onChange = [this] { auto mono = voiceMode.box.getSelectedItemIndex() == 1; edit ([mono] (Pad& pad) { pad.mono = mono; }); };

    filterType.box.addItemList ({ "Off", "Low-pass 12 dB", "Low-pass 24 dB", "High-pass 12 dB", "High-pass 24 dB", "Band-pass 12 dB", "Band-pass 24 dB", "Notch" }, 1);
    filterType.box.onChange = [this] { auto i = filterType.box.getSelectedItemIndex(); edit ([i] (Pad& pad) { pad.filter = (FilterType) i; }); };

    for (auto* c : { &trigger, &layerMode, &envMode, &choke, &output, &voiceMode, &filterType })
        addAndMakeVisible (c);

    for (int i = 0; i < kMaxLayers; ++i)
        addChildComponent (layerRows.add (new LayerRow (*this, i)));

    addLayerButton.setTooltip ("Add samples as layers of this pad (or drop audio files on a pad)");
    addLayerButton.onClick = [this] { if (onLoadSampleRequest) onLoadSampleRequest (processor.selectedPad); };
    clearButton.onClick = [this] { edit ([] (Pad& pad) { auto note = pad.note; pad = Pad(); pad.note = note; }); };
    addAndMakeVisible (addLayerButton);
    addAndMakeVisible (clearButton);

    hintLabel.setText ("Empty pad. Drop audio files here or on a pad, or use \"+ Add layer\".", juce::dontSendNotification);
    hintLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setColour (juce::Label::textColourId, Colours::textDim);
    hintLabel.setFont (uiFont (13.0f));
    addChildComponent (hintLabel);

    addAndMakeVisible (waveform);
    refresh();
}

PadEditor::~PadEditor() = default;

void PadEditor::edit (const std::function<void (Pad&)>& fn)
{
    processor.editPad (processor.selectedPad, fn);
}

void PadEditor::editLayer (int layer, const std::function<void (Layer&)>& fn)
{
    processor.editPad (processor.selectedPad, [layer, fn] (Pad& p)
    {
        if (layer >= 0 && layer < (int) p.layers.size())
            fn (p.layers[(size_t) layer]);
    });
}

void PadEditor::selectLayer (int layer)
{
    selectedLayer = layer;
    refresh();
}

int PadEditor::layerIndexAt (juce::Point<int> pos) const
{
    for (int i = 0; i < layerRows.size(); ++i)
        if (layerRows[i]->isVisible() && layerRows[i]->getBounds().contains (pos))
            return i;
    return -1;
}

void PadEditor::updateWaveform()
{
    auto& kit = processor.getKit();
    if (processor.selectedPad >= (int) kit.pads.size())
    {
        waveform.setSample (nullptr, 0, 0, false, false, {});
        return;
    }
    auto& pad = kit.pads[(size_t) processor.selectedPad];
    if (pad.layers.empty())
    {
        waveform.setSample (nullptr, 0, 0, false, false, {});
        return;
    }
    auto& l = pad.layers[(size_t) juce::jlimit (0, (int) pad.layers.size() - 1, selectedLayer)];
    waveform.setSample (processor.getSample (l.sample), l.start, l.end, l.reverse, l.missing, l.name.trim());
}

void PadEditor::refresh()
{
    auto& kit = processor.getKit();
    auto index = processor.selectedPad;
    bool valid = index >= 0 && index < (int) kit.pads.size();
    static const Pad emptyPad;
    auto& pad = valid ? kit.pads[(size_t) index] : emptyPad;

    padLabel.setText (pad.isKeygroup() ? "KG " + juce::String (index + 1) : padName (index), juce::dontSendNotification);
    auto displayName = pad.name.isNotEmpty() ? pad.name : (pad.layers.empty() ? juce::String ("(empty)") : pad.layers.front().name.trim());
    nameLabel.setText (displayName, juce::dontSendNotification);

    noteBox.setVisible (! pad.isKeygroup());
    rangeLabel.setVisible (pad.isKeygroup());
    if (pad.isKeygroup())
        rangeLabel.setText ("Keys " + noteName (pad.noteLow) + " - " + noteName (pad.noteHigh), juce::dontSendNotification);
    else
        noteBox.setSelectedId (pad.note + 1, juce::dontSendNotification);

    muteButton.setToggleState (pad.mute, juce::dontSendNotification);
    soloButton.setToggleState (pad.solo, juce::dontSendNotification);

    level.setValue (juce::jmax (-60.0f, pad.volumeDb));
    pan.setValue (pad.pan);
    tune.setValue (pad.tuneSemis);
    velLevel.setValue (pad.velToLevel);
    attack.setValue (pad.env.attackMs);
    hold.setValue (pad.env.holdMs);
    decay.setValue (pad.env.decayMs);
    sustain.setValue (pad.env.sustain);
    release.setValue (pad.env.releaseMs);
    sustain.setEnabled (! pad.env.adMode);
    release.setEnabled (! pad.env.adMode || pad.trigger == TriggerMode::NoteOn);
    cutoff.setValue (pad.cutoffHz);
    resonance.setValue (pad.resonance);
    velCutoff.setValue (pad.velToCutoff);
    for (auto* k : { &cutoff, &resonance, &velCutoff })
        k->setEnabled (pad.filter != FilterType::Off);

    trigger.box.setSelectedItemIndex ((int) pad.trigger, juce::dontSendNotification);
    layerMode.box.setSelectedItemIndex ((int) pad.layerMode, juce::dontSendNotification);
    envMode.box.setSelectedItemIndex (pad.env.adMode ? (pad.env.decayFromEnd ? 0 : 1) : 2, juce::dontSendNotification);
    choke.box.setSelectedId (pad.muteGroup + 1, juce::dontSendNotification);
    output.box.setSelectedId (pad.output + 1, juce::dontSendNotification);
    voiceMode.box.setSelectedItemIndex (pad.mono ? 1 : 0, juce::dontSendNotification);
    filterType.box.setSelectedItemIndex ((int) pad.filter, juce::dontSendNotification);

    auto numLayers = (int) pad.layers.size();
    selectedLayer = juce::jlimit (0, juce::jmax (0, numLayers - 1), selectedLayer);
    for (int i = 0; i < layerRows.size(); ++i)
    {
        layerRows[i]->setVisible (i < numLayers);
        if (i < numLayers)
            layerRows[i]->update (pad.layers[(size_t) i], i == selectedLayer);
    }
    hintLabel.setVisible (numLayers == 0);
    addLayerButton.setEnabled (valid && numLayers < kMaxLayers);
    clearButton.setEnabled (valid && numLayers > 0);

    for (auto* c : getChildren())
        if (c != &hintLabel && c != &addLayerButton && c != &padLabel && c != &noteBox && c != &waveform)
            c->setAlpha (valid ? 1.0f : 0.5f);

    updateWaveform();
    resized();
    repaint();
}

void PadEditor::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    auto header = r.removeFromTop (32);
    padLabel.setBounds (header.removeFromLeft (64));
    soloButton.setBounds (header.removeFromRight (28).reduced (0, 3));
    header.removeFromRight (4);
    muteButton.setBounds (header.removeFromRight (28).reduced (0, 3));
    header.removeFromRight (10);
    noteBox.setBounds (header.removeFromRight (120).reduced (0, 3));
    rangeLabel.setBounds (noteBox.getBounds().withLeft (noteBox.getX() - 80));
    nameLabel.setBounds (header.reduced (4, 0));

    r.removeFromTop (8);
    soundArea = r.removeFromTop (88);
    {
        auto row = soundArea.withTrimmedTop (4);
        Knob* knobs[] = { &level, &pan, &tune, &velLevel, &attack, &hold, &decay, &sustain, &release };
        auto w = row.getWidth() / 9;
        for (auto* k : knobs)
            k->setBounds (row.removeFromLeft (w));
    }

    r.removeFromTop (8);
    {
        auto row = r.removeFromTop (40);
        LabelledCombo* combos[] = { &trigger, &layerMode, &envMode, &choke, &output, &voiceMode };
        auto w = row.getWidth() / 6;
        for (auto* c : combos)
            c->setBounds (row.removeFromLeft (w).reduced (3, 0));
    }

    r.removeFromTop (10);
    filterArea = r.removeFromTop (80);
    {
        filterType.setBounds (juce::Rectangle<int> (filterArea.getX() + 3, filterArea.getY() + 20, 144, 40));
        auto row = filterArea.withTrimmedLeft (160);
        auto w = juce::jmin (90, row.getWidth() / 3);
        for (auto* k : { &cutoff, &resonance, &velCutoff })
            k->setBounds (row.removeFromLeft (w));
    }

    r.removeFromTop (8);
    layersHeaderArea = r.removeFromTop (18);
    auto buttons = r.removeFromBottom (26);
    clearButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (6);
    addLayerButton.setBounds (buttons.removeFromRight (130));
    r.removeFromBottom (6);

    // Layer rows from the top, the waveform takes what's left (at least 70 px)
    int visibleRows = 0;
    for (auto* row : layerRows)
        visibleRows += row->isVisible() ? 1 : 0;
    auto rowsArea = r.removeFromTop (juce::jmin (r.getHeight() - 76, juce::jmax (1, visibleRows) * 24));
    hintLabel.setBounds (rowsArea);
    for (int i = 0; i < layerRows.size(); ++i)
        layerRows[i]->setBounds (rowsArea.removeFromTop (24));
    r.removeFromTop (6);
    waveform.setBounds (r);
}

void PadEditor::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Column headings for the layer table
    auto h = layersHeaderArea.reduced (2, 0);
    g.setFont (uiFont (11.0f, true));
    g.setColour (Colours::textDim);
    g.drawText ("LAYERS", h.removeFromLeft (120).withTrimmedLeft (16), juce::Justification::centredLeft);
    h.removeFromRight (22 + 3 + 34 + 3);
    g.drawText ("PAN", h.removeFromRight (46), juce::Justification::centred);
    h.removeFromRight (3);
    g.drawText ("TUNE", h.removeFromRight (50), juce::Justification::centred);
    h.removeFromRight (3);
    g.drawText ("DB", h.removeFromRight (50), juce::Justification::centred);
    h.removeFromRight (3);
    g.drawText ("HI", h.removeFromRight (36), juce::Justification::centred);
    h.removeFromRight (3);
    g.drawText ("VEL LO", h.removeFromRight (40), juce::Justification::centred);

    g.setColour (Colours::outline);
    g.drawHorizontalLine (filterArea.getY() - 5, (float) filterArea.getX(), (float) filterArea.getRight());
    g.drawHorizontalLine (layersHeaderArea.getY() - 5, (float) layersHeaderArea.getX(), (float) layersHeaderArea.getRight());
}

}
