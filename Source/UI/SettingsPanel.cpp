#include "SettingsPanel.h"

namespace kaia
{

//==============================================================================
class SettingsPanel::Swatches : public juce::Component,
                                public juce::TooltipClient
{
public:
    std::function<void (juce::Colour)> onPick;

    void paint (juce::Graphics& g) override
    {
        auto& options = accentOptions();
        for (int i = 0; i < (int) options.size(); ++i)
        {
            auto r = cell (i).toFloat().reduced (3.0f);
            juce::Colour c (options[(size_t) i].argb);
            g.setColour (c);
            g.fillRoundedRectangle (r, 5.0f);
            if (c == Colours::accent)
            {
                g.setColour (Colours::text);
                g.drawRoundedRectangle (r.expanded (2.0f), 6.0f, 2.0f);
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto& options = accentOptions();
        for (int i = 0; i < (int) options.size(); ++i)
            if (cell (i).contains (e.getPosition()) && onPick)
                onPick (juce::Colour (options[(size_t) i].argb));
    }

    juce::String getTooltip() override
    {
        auto p = getMouseXYRelative();
        auto& options = accentOptions();
        for (int i = 0; i < (int) options.size(); ++i)
            if (cell (i).contains (p))
                return options[(size_t) i].name;
        return {};
    }

private:
    juce::Rectangle<int> cell (int i) const { return { i * 38, 0, 38, 34 }; }
};

//==============================================================================
// Which MIDI notes play the 16 pads of bank A (or the pads with learned notes)
class SettingsPanel::MapView : public juce::Component
{
public:
    explicit MapView (KaiaProcessor& p) : processor (p) {}

    void paint (juce::Graphics& g) override
    {
        auto& map = processor.getTriggerMap();
        auto cols = 4;
        auto cw = getWidth() / cols;
        auto ch = 40;
        // Same layout as the pads: A01 bottom left
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            auto col = i % 4, row = 3 - i / 4;
            auto r = juce::Rectangle<int> (col * cw, row * ch, cw, ch).reduced (2);
            g.setColour (Colours::panelRaised);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);

            auto notes = processor.getInputNotesForPad (i);
            juce::StringArray noteText;
            for (auto n : notes)
                noteText.add (juce::String (n));

            auto inner = r.reduced (6, 3);
            auto top = inner.removeFromTop (16);
            g.setFont (uiFont (11.0f, true));
            g.setColour (Colours::accent);
            g.drawText (padName (i), top.removeFromLeft (30), juce::Justification::centredLeft);
            g.setColour (Colours::textDim);
            g.setFont (uiFont (11.0f));
            if (map.usesModule())
                g.drawText (drumZoneName ((DrumZone) i), top, juce::Justification::centredRight, true);
            g.setColour (notes.empty() ? Colours::textDim : Colours::text);
            g.drawText (notes.empty() ? juce::String ("no note") : "note " + noteText.joinIntoString (", "), inner, juce::Justification::centredLeft, true);
        }
    }

private:
    KaiaProcessor& processor;
};

//==============================================================================
SettingsPanel::SettingsPanel (KaiaProcessor& p) : processor (p)
{
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    for (auto* text : { "Drum module", "Library", "Highlight colour", "Files" })
    {
        auto* l = headings.add (new juce::Label ({}, juce::String (text).toUpperCase()));
        l->setFont (uiFont (12.0f, true));
        l->setColour (juce::Label::textColourId, Colours::textDim);
        content.addAndMakeVisible (l);
    }

    // Drum module
    moduleBox.addItem ("None: use each program's own note map", 1);
    juce::String brand;
    auto& modules = drumModules();
    for (int i = 0; i < (int) modules.size(); ++i)
    {
        if (modules[(size_t) i].brand != brand)
        {
            brand = modules[(size_t) i].brand;
            moduleBox.addSectionHeading (brand);
        }
        moduleBox.addItem (modules[(size_t) i].name, i + 2);
    }
    moduleBox.setTooltip ("Your electronic drum module. Its default notes are mapped to fixed pads: kick A01, snare A02, closed hi-hat A03, open hi-hat A08, ...");
    moduleBox.onChange = [this]
    {
        auto id = moduleBox.getSelectedId();
        auto& mods = drumModules();
        updateTriggers ([&] (TriggerMap& m)
        {
            m.moduleId = id >= 2 && id - 2 < (int) mods.size() ? mods[(size_t) (id - 2)].id : juce::String();
        });
    };
    content.addAndMakeVisible (moduleBox);

    chokeToggle.onClick = [this] { updateTriggers ([this] (TriggerMap& m) { m.hiHatChoke = chokeToggle.getToggleState(); }); };
    content.addAndMakeVisible (chokeToggle);

    clearLearnedButton.setTooltip ("Remove every note assigned with MIDI Learn");
    clearLearnedButton.onClick = [this] { updateTriggers ([] (TriggerMap& m) { m.learned.clear(); }); };
    content.addAndMakeVisible (clearLearnedButton);

    for (auto* l : { &moduleInfo, &lastNoteLabel, &libraryPath, &configPath })
    {
        l->setFont (uiFont (13.0f));
        l->setColour (juce::Label::textColourId, Colours::textDim);
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (0.8f);
        content.addAndMakeVisible (l);
    }
    lastNoteLabel.setJustificationType (juce::Justification::centredLeft);

    mapView = std::make_unique<MapView> (processor);
    content.addAndMakeVisible (*mapView);

    // Library
    chooseLibraryButton.onClick = [this] { if (onChooseLibraryFolder) onChooseLibraryFolder(); };
    rescanButton.onClick = [this] { library->rescan(); };
    content.addAndMakeVisible (chooseLibraryButton);
    content.addAndMakeVisible (rescanButton);

    // Colours
    swatches = std::make_unique<Swatches>();
    swatches->onPick = [this] (juce::Colour c)
    {
        juce::SharedResourcePointer<SharedSettings> settings;
        settings->get().setValue ("accent", c.toDisplayString (false));
        settings->get().saveIfNeeded();
        if (onAccentChanged)
            onAccentChanged();
    };
    content.addAndMakeVisible (*swatches);

    // Files
    showConfigButton.onClick = [] { UserFiles::configDirectory().createDirectory(); UserFiles::configDirectory().revealToUser(); };
    content.addAndMakeVisible (showConfigButton);

    library->addChangeListener (this);
    processor.addChangeListener (this);
    refresh();
}

SettingsPanel::~SettingsPanel()
{
    library->removeChangeListener (this);
    processor.removeChangeListener (this);
}

void SettingsPanel::updateTriggers (const std::function<void (TriggerMap&)>& fn)
{
    auto map = processor.getTriggerMap();
    fn (map);
    processor.setTriggerMap (map);
}

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (isVisible())
        refresh();
}

void SettingsPanel::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        startTimerHz (10);
    }
    else
    {
        stopTimer();
    }
}

void SettingsPanel::timerCallback()
{
    auto last = processor.getEngine().getLastInputNote();
    if (last == shownLastNote)
        return;
    shownLastNote = last;
    lastNoteLabel.setText (last == 0 ? juce::String ("Last note received: none yet. Hit a pad on your kit to check the connection.")
                                     : "Last note received: " + noteName ((int) (last & 0x7f)),
                           juce::dontSendNotification);
}

void SettingsPanel::refresh()
{
    auto& map = processor.getTriggerMap();
    auto& mods = drumModules();
    int id = 1;
    for (int i = 0; i < (int) mods.size(); ++i)
        if (mods[(size_t) i].id == map.moduleId)
            id = i + 2;
    moduleBox.setSelectedId (id, juce::dontSendNotification);
    chokeToggle.setToggleState (map.hiHatChoke, juce::dontSendNotification);
    chokeToggle.setVisible (map.usesModule());
    clearLearnedButton.setEnabled (! map.learned.empty());
    clearLearnedButton.setButtonText (map.learned.empty() ? juce::String ("No learned notes")
                                                          : "Forget " + juce::String ((int) map.learned.size()) + " learned note" + (map.learned.size() == 1 ? "" : "s"));

    juce::String info;
    if (map.usesModule())
        info = "Kits play from " + map.module()->name + " with the layout below. Build kits with the kick on A01, snare on A02, "
               "closed hi-hat on A03 and so on (the Library makes that easy). Notes not listed are ignored. "
               "If a drum plays the wrong pad, right-click the pad and choose MIDI Learn. Keygroup programs keep their own notes.";
    else
        info = "Pads play from the notes stored in each program (MPC layout). Choose your drum module to map its drums to "
               "fixed pads instead. Right-click any pad and choose MIDI Learn to assign a note by hitting it.";
    moduleInfo.setText (info, juce::dontSendNotification);
    shownLastNote = ~0u;
    timerCallback();

    auto root = library->getRoot();
    libraryPath.setText ((root == juce::File() ? juce::String ("No library folder set. Choose the folder that holds your expansions (.xpn), program folders and samples.")
                                               : root.getFullPathName() + "\n" + library->getStatus())
                         + "\nKaia only reads from the library: files there are never changed.",
                         juce::dontSendNotification);
    rescanButton.setEnabled (root != juce::File());

    configPath.setText ("Settings and presets: " + UserFiles::configDirectory().getFullPathName()
                        + "\nPresets are saved in " + UserFiles::presetsDirectory().getFullPathName(), juce::dontSendNotification);

    mapView->repaint();
    swatches->repaint();
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    closeButton.setBounds (top.removeFromRight (64).withHeight (26));
    r.removeFromTop (4);
    viewport.setBounds (r);

    auto w = r.getWidth() - viewport.getScrollBarThickness() - 4;
    int y = 0;
    auto row = [&] (int h) { auto b = juce::Rectangle<int> (0, y, w, h); y += h; return b; };

    headings[0]->setBounds (row (22));
    moduleBox.setBounds (row (28).withWidth (juce::jmin (w, 420)));
    y += 6;
    moduleInfo.setBounds (row (54));
    y += 4;
    mapView->setBounds (row (162).withWidth (juce::jmin (w, 520)));
    y += 6;
    chokeToggle.setBounds (row (24));
    y += 4;
    auto learnRow = row (26);
    clearLearnedButton.setBounds (learnRow.removeFromLeft (170));
    learnRow.removeFromLeft (12);
    lastNoteLabel.setBounds (learnRow);

    y += 18;
    headings[1]->setBounds (row (22));
    libraryPath.setBounds (row (52));
    auto libButtons = row (26);
    chooseLibraryButton.setBounds (libButtons.removeFromLeft (130));
    libButtons.removeFromLeft (6);
    rescanButton.setBounds (libButtons.removeFromLeft (80));

    y += 18;
    headings[2]->setBounds (row (22));
    swatches->setBounds (row (34).withWidth ((int) accentOptions().size() * 38));

    y += 18;
    headings[3]->setBounds (row (22));
    auto cfg = row (38);
    showConfigButton.setBounds (cfg.removeFromRight (60).withHeight (26));
    configPath.setBounds (cfg);
    y += 8;

    content.setSize (w, y);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (17.0f, true));
    g.drawText ("Settings", getLocalBounds().reduced (12).removeFromTop (26), juce::Justification::centredLeft);
}

}
