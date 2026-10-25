#include "SettingsPanel.h"
#include "KineticScroller.h"

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
        if (e.mouseWasDraggedSinceMouseDown())
            return;
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
// Each pad colour theme as a small 4x4 pad grid with its name
class SettingsPanel::PadThemePicker : public juce::Component
{
public:
    std::function<void (int)> onPick;

    static constexpr int kCellW = 64, kCellH = 78;

    int heightForWidth (int width) const
    {
        auto cols = juce::jmax (1, width / kCellW);
        return ((int) padThemes().size() + cols - 1) / cols * kCellH;
    }

    void paint (juce::Graphics& g) override
    {
        auto& themes = padThemes();
        for (int i = 0; i < (int) themes.size(); ++i)
        {
            auto c = cell (i);
            auto grid = c.removeFromTop (kCellW).reduced (5).toFloat();
            bool chosen = i == Colours::padTheme;
            if (chosen)
            {
                g.setColour (Colours::text);
                g.drawRoundedRectangle (grid.expanded (3.0f), 6.0f, 2.0f);
            }

            auto pw = grid.getWidth() / 4.0f;
            for (int p = 0; p < kPadsPerBank; ++p)
            {
                auto col = p % 4, row = 3 - p / 4;
                auto r = juce::Rectangle<float> (grid.getX() + (float) col * pw, grid.getY() + (float) row * pw, pw, pw).reduced (1.0f);
                auto colour = padThemeColour (i, p);
                g.setColour (colour.isTransparent() ? Colours::padLoaded : colour);
                g.fillRoundedRectangle (r, 2.0f);
                if (colour.isTransparent())
                {
                    g.setColour (Colours::outline);
                    g.drawRoundedRectangle (r, 2.0f, 0.8f);
                }
            }

            g.setColour (chosen ? Colours::text : Colours::textDim);
            g.setFont (uiFont (11.0f, chosen));
            g.drawText (themes[(size_t) i].name, c, juce::Justification::centredTop);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        for (int i = 0; i < (int) padThemes().size(); ++i)
            if (cell (i).contains (e.getPosition()) && onPick)
                onPick (i);
    }

private:
    juce::Rectangle<int> cell (int i) const
    {
        auto cols = juce::jmax (1, getWidth() / kCellW);
        return { (i % cols) * kCellW, (i / cols) * kCellH, kCellW, kCellH };
    }
};

//==============================================================================
// Which MIDI notes play the 16 pads of bank A (or the pads with learned notes). Click a pad to
// change its notes.
class SettingsPanel::MapView : public juce::Component
{
public:
    explicit MapView (KaiaProcessor& p) : processor (p) {}

    std::function<void (int zone)> onCellClicked;
    int learningZone = -1;

    static constexpr int kCellHeight = 44;

    void paint (juce::Graphics& g) override
    {
        auto& map = processor.getTriggerMap();
        auto pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f);

        // Same layout as the pads: A01 bottom left
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            auto r = cell (i);
            bool learning = i == learningZone;
            auto themed = padThemeColour (Colours::padTheme, i);
            g.setColour (themed.isTransparent() ? Colours::panelRaised : themed);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            if (learning)
            {
                g.setColour (Colours::accent.withAlpha (0.15f + 0.25f * pulse));
                g.fillRoundedRectangle (r.toFloat(), 4.0f);
                g.setColour (Colours::accent);
                g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.5f);
            }
            else if (i == hoverZone && map.usesModule())
            {
                g.setColour (Colours::outline.brighter (0.4f));
                g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.0f);
            }

            auto notes = processor.getInputNotesForPad (i);
            juce::StringArray noteText;
            for (auto n : notes)
                noteText.add (juce::String (n));

            auto dim = themed.isTransparent() ? Colours::textDim : Colours::text.withAlpha (0.6f);
            auto inner = r.reduced (6, 3);
            auto top = inner.removeFromTop (17);
            g.setFont (uiFont (11.0f, true));
            g.setColour (Colours::accent);
            g.drawText (padName (i), top.removeFromLeft (30), juce::Justification::centredLeft);
            g.setColour (dim);
            g.setFont (uiFont (11.0f));
            if (map.usesModule())
                g.drawText (drumZoneName ((DrumZone) i), top, juce::Justification::centredRight, true);
            g.setColour (learning ? Colours::accent : notes.empty() ? dim : Colours::text);
            g.setFont (uiFont (12.0f, learning));
            g.drawText (learning ? juce::String ("Hit a drum...") : notes.empty() ? juce::String ("no note") : "note " + noteText.joinIntoString (", "),
                        inner, juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override  { setHover (zoneAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override     { setHover (-1); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        auto zone = zoneAt (e.getPosition());
        if (zone >= 0 && onCellClicked)
            onCellClicked (zone);
    }

private:
    juce::Rectangle<int> cell (int i) const
    {
        auto cw = getWidth() / 4;
        auto col = i % 4, row = 3 - i / 4;
        return juce::Rectangle<int> (col * cw, row * kCellHeight, cw, kCellHeight).reduced (2);
    }

    int zoneAt (juce::Point<int> p) const
    {
        for (int i = 0; i < kPadsPerBank; ++i)
            if (cell (i).contains (p))
                return i;
        return -1;
    }

    void setHover (int zone)
    {
        if (zone != hoverZone)
        {
            hoverZone = zone;
            repaint();
        }
    }

    KaiaProcessor& processor;
    int hoverZone = -1;
};

//==============================================================================
SettingsPanel::SettingsPanel (KaiaProcessor& p) : processor (p)
{
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);   // touch screens
    addAndMakeVisible (viewport);

    for (auto* text : { "Drum module", "Playback", "Library", "Highlight colour", "Pad colours", "Files" })
    {
        auto* l = headings.add (new juce::Label ({}, juce::String (text).toUpperCase()));
        l->setFont (uiFont (12.0f, true));
        l->setColour (juce::Label::textColourId, Colours::textDim);
        content.addAndMakeVisible (l);
    }

    // Drum module
    moduleBox.setTooltip ("Your electronic drum module. Its default notes are mapped to fixed pads: kick A01, snare A02, closed hi-hat A03, open hi-hat A08, ...");
    moduleBox.onChange = [this]
    {
        auto it = moduleIdForItem.find (moduleBox.getSelectedId());
        auto id = it != moduleIdForItem.end() ? it->second : juce::String();
        stopLearning();
        updateTriggers ([&] (TriggerMap& m) { m.selectModule (id); });
    };
    content.addAndMakeVisible (moduleBox);

    chokeToggle.onClick = [this] { updateTriggers ([this] (TriggerMap& m) { m.hiHatChoke = chokeToggle.getToggleState(); }); };
    content.addAndMakeVisible (chokeToggle);

    clearLearnedButton.setTooltip ("Remove every note assigned with MIDI Learn");
    clearLearnedButton.onClick = [this] { updateTriggers ([] (TriggerMap& m) { m.learned.clear(); }); };
    content.addAndMakeVisible (clearLearnedButton);

    saveMapButton.setTooltip ("Save this note map under a name of your own; it then appears in the drum module list");
    saveMapButton.onClick = [this] { saveCustomMap(); };
    deleteMapButton.onClick = [this] { deleteCustomMap(); };
    resetMapButton.setTooltip ("Undo your changes to this module's notes");
    resetMapButton.onClick = [this] { stopLearning(); updateTriggers ([] (TriggerMap& m) { m.resetToFactory(); }); };
    for (auto* b : { &saveMapButton, &deleteMapButton, &resetMapButton })
        content.addAndMakeVisible (b);

    for (auto* l : { &moduleInfo, &lastNoteLabel, &mapHint, &loopsInfo, &libraryPath, &configPath })
    {
        l->setFont (uiFont (13.0f));
        l->setColour (juce::Label::textColourId, Colours::textDim);
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (0.8f);
        content.addAndMakeVisible (l);
    }
    lastNoteLabel.setJustificationType (juce::Justification::centredLeft);
    mapHint.setJustificationType (juce::Justification::centredLeft);

    mapView = std::make_unique<MapView> (processor);
    mapView->onCellClicked = [this] (int zone) { showZoneMenu (zone); };
    content.addAndMakeVisible (*mapView);

    // Playback
    loopsToggle.setTooltip ("Some samples carry loop points. With this on, pads in note-on mode repeat them for as long as the note is held.");
    loopsToggle.onClick = [this] { processor.setPlayLoops (loopsToggle.getToggleState()); };
    content.addAndMakeVisible (loopsToggle);
    loopsInfo.setText ("Off by default: every hit plays its sample once. Turn on only for programs built around looped sounds "
                       "(pads, sustained instruments); drum kits with loop points otherwise keep repeating.",
                       juce::dontSendNotification);

    // Library
    chooseLibraryButton.onClick = [this] { if (onChooseLibraryFolder) onChooseLibraryFolder(); };
    rescanButton.onClick = [this] { library->rescan(); };
    content.addAndMakeVisible (chooseLibraryButton);
    content.addAndMakeVisible (rescanButton);

    flingLabel.setText ("Touch scrolling fling", juce::dontSendNotification);
    flingLabel.setFont (uiFont (13.0f));
    flingLabel.setColour (juce::Label::textColourId, Colours::text);
    content.addAndMakeVisible (flingLabel);
    flingSlider.setRange (0.0, 10.0, 1.0);
    flingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 22);
    flingSlider.setTooltip ("How far the library keeps gliding after you flick it: 0 stops as soon as you let go, 10 glides the furthest");
    flingSlider.onValueChange = [this]
    {
        juce::SharedResourcePointer<SharedSettings> settings;
        settings->get().setValue ("flingStrength", flingSlider.getValue() / 10.0);
        settings->get().saveIfNeeded();
    };
    content.addAndMakeVisible (flingSlider);

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

    padThemePicker = std::make_unique<PadThemePicker>();
    padThemePicker->onPick = [this] (int theme)
    {
        juce::SharedResourcePointer<SharedSettings> settings;
        settings->get().setValue ("padTheme", padThemes()[(size_t) theme].name);
        settings->get().saveIfNeeded();
        if (onPadThemeChanged)
            onPadThemeChanged();
    };
    content.addAndMakeVisible (*padThemePicker);

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
        startTimerHz (15);
    }
    else
    {
        stopLearning();
        stopTimer();
    }
}

void SettingsPanel::timerCallback()
{
    auto last = processor.getEngine().getLastInputNote();

    // Learning a note for a pad of the map
    if (learningZone >= 0)
    {
        if ((last >> 8) != learnSequence)
        {
            auto zone = learningZone;
            auto note = (int) (last & 0x7f);
            stopLearning();
            updateTriggers ([&] (TriggerMap& m)
            {
                m.moveNoteToZone (note, zone);
                m.learned.erase (note);      // a MIDI-learned note would hide the change
            });
        }
        else
        {
            mapView->repaint();
        }
    }

    if (last == shownLastNote)
        return;
    shownLastNote = last;
    lastNoteLabel.setText (last == 0 ? juce::String ("Last note received: none yet. Hit a pad on your kit to check the connection.")
                                     : "Last note received: " + noteName ((int) (last & 0x7f)),
                           juce::dontSendNotification);
}

void SettingsPanel::stopLearning()
{
    if (learningZone < 0)
        return;
    learningZone = -1;
    mapView->learningZone = -1;
    mapView->repaint();
}

//==============================================================================
void SettingsPanel::rebuildModuleBox()
{
    moduleBox.clear (juce::dontSendNotification);
    moduleIdForItem.clear();

    moduleBox.addItem ("None: use each program's own note map", 1);
    moduleIdForItem[1] = {};

    juce::String brand;
    auto& modules = drumModules();
    for (int i = 0; i < (int) modules.size(); ++i)
    {
        auto& m = modules[(size_t) i];
        if (m.brand != brand)
        {
            brand = m.brand;
            moduleBox.addSectionHeading (brand);
        }
        moduleBox.addItem (m.name, i + 2);
        moduleIdForItem[i + 2] = m.id;
    }

    auto& map = processor.getTriggerMap();
    auto customs = CustomDrumMaps::list();
    bool currentListed = ! map.isCustom();
    if (! customs.empty() || ! currentListed)
        moduleBox.addSectionHeading ("My drum maps");
    int itemId = 1000;
    for (auto& m : customs)
    {
        currentListed = currentListed || m.id == map.moduleId;
        moduleBox.addItem (m.name, itemId);
        moduleIdForItem[itemId++] = m.id;
    }
    // A project's custom map that isn't saved on this computer
    if (! currentListed)
    {
        moduleBox.addItem (map.moduleName() + " (from the project)", itemId);
        moduleIdForItem[itemId] = map.moduleId;
    }

    for (auto& [item, id] : moduleIdForItem)
        if (id == map.moduleId)
            moduleBox.setSelectedId (item, juce::dontSendNotification);
}

void SettingsPanel::showZoneMenu (int zone)
{
    auto& map = processor.getTriggerMap();
    juce::PopupMenu m;
    m.addSectionHeader (padName (zone) + "   " + drumZoneName ((DrumZone) zone));
    if (! map.usesModule())
    {
        m.addItem (-1, "Choose a drum module above to edit its notes", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mapView.get()));
        return;
    }

    m.addItem (1, "Learn: hit the drum that should play this pad");
    m.addItem (2, "Type notes...");
    auto notes = map.notesForZone (zone);
    if (! notes.empty())
    {
        m.addSeparator();
        for (auto n : notes)
            m.addItem (100 + n, "Remove note " + juce::String (n));
        if (notes.size() > 1)
            m.addItem (3, "Remove all notes");
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mapView.get()),
                     [this, zone, safe = juce::Component::SafePointer<SettingsPanel> (this)] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        if (r == 1)
        {
            learningZone = zone;
            learnSequence = processor.getEngine().getLastInputNote() >> 8;
            mapView->learningZone = zone;
            mapView->repaint();
        }
        else if (r == 2)
        {
            typeZoneNotes (zone);
        }
        else if (r == 3)
        {
            updateTriggers ([zone] (TriggerMap& t) { t.setZoneNotes (zone, {}); });
        }
        else if (r >= 100)
        {
            updateTriggers ([zone, note = r - 100] (TriggerMap& t) { t.removeNote (note, zone); });
        }
    });
}

void SettingsPanel::typeZoneNotes (int zone)
{
    stopLearning();
    juce::StringArray current;
    for (auto n : processor.getTriggerMap().notesForZone (zone))
        current.add (juce::String (n));

    dialog = std::make_unique<juce::AlertWindow> ("Notes for " + padName (zone) + " (" + drumZoneName ((DrumZone) zone) + ")",
                                                  "MIDI note numbers 0-127, separated by commas. A note can play several pads: "
                                                  "type it on each of them.",
                                                  juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("notes", current.joinIntoString (", "), "Notes");
    dialog->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, zone] (int result)
    {
        auto text = dialog != nullptr ? dialog->getTextEditorContents ("notes") : juce::String();
        dialog.reset();
        if (result != 1)
            return;
        std::vector<int> notes;
        for (auto& token : juce::StringArray::fromTokens (text, ", ;", {}))
            if (token.trim().containsOnly ("0123456789") && token.trim().isNotEmpty())
                if (auto n = token.trim().getIntValue(); n >= 0 && n < 128)
                    notes.push_back (n);
        updateTriggers ([&] (TriggerMap& t) { t.setZoneNotes (zone, notes); });
    }));
}

void SettingsPanel::saveCustomMap()
{
    stopLearning();
    auto& map = processor.getTriggerMap();
    auto suggested = map.isCustom() ? map.moduleName() : "My " + map.moduleName();

    dialog = std::make_unique<juce::AlertWindow> ("Save drum map",
                                                  "The map is saved in " + UserFiles::drumMapsDirectory().getFullPathName()
                                                  + " and appears under \"My drum maps\" in the module list.",
                                                  juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", suggested, "Name");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        auto name = dialog != nullptr ? dialog->getTextEditorContents ("name").trim() : juce::String();
        dialog.reset();
        if (result != 1 || name.isEmpty())
            return;

        juce::String error;
        if (! CustomDrumMaps::save (name, processor.getTriggerMap().notes, error))
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, "Save drum map", error), nullptr);
            return;
        }
        updateTriggers ([&] (TriggerMap& t) { t.moduleId = CustomDrumMaps::kIdPrefix + name; });
        refresh();
    }));
}

void SettingsPanel::deleteCustomMap()
{
    auto name = processor.getTriggerMap().moduleName();
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Delete drum map",
                                                                "Delete the drum map \"" + name + "\"?", "Delete", "Cancel");
    juce::AlertWindow::showAsync (options, [this, name, safe = juce::Component::SafePointer<SettingsPanel> (this)] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;
        CustomDrumMaps::remove (name);
        updateTriggers ([] (TriggerMap& t) { t.selectModule ({}); });
        refresh();
    });
}

//==============================================================================
void SettingsPanel::refresh()
{
    auto& map = processor.getTriggerMap();
    rebuildModuleBox();
    chokeToggle.setToggleState (map.hiHatChoke, juce::dontSendNotification);
    chokeToggle.setVisible (map.usesModule());
    clearLearnedButton.setEnabled (! map.learned.empty());
    clearLearnedButton.setButtonText (map.learned.empty() ? juce::String ("No learned notes")
                                                          : "Forget " + juce::String ((int) map.learned.size()) + " learned note" + (map.learned.size() == 1 ? "" : "s"));

    saveMapButton.setVisible (map.usesModule());
    deleteMapButton.setVisible (map.isCustom());
    resetMapButton.setVisible (map.isEdited());
    mapHint.setText (map.usesModule() ? (map.isEdited() ? "Edited: save it as your own map to keep it alongside the factory one."
                                                        : "Click a pad to change its notes.")
                                      : juce::String(),
                     juce::dontSendNotification);

    juce::String info;
    if (map.usesModule())
        info = "Kits play from " + map.moduleName() + (map.isEdited() ? " (edited)" : "") + " with the layout below. "
               "Build kits with the kick on A01, snare on A02, closed hi-hat on A03 and so on (the Library makes that easy). "
               "Notes not listed are ignored. Click a pad below to change which notes play it. Keygroup programs keep their own notes.";
    else
        info = "Pads play from the notes stored in each program (MPC layout). Choose your drum module to map its drums to "
               "fixed pads instead. Right-click any pad and choose MIDI Learn to assign a note by hitting it.";
    moduleInfo.setText (info, juce::dontSendNotification);
    shownLastNote = ~0u;
    timerCallback();

    loopsToggle.setToggleState (processor.getPlayLoops(), juce::dontSendNotification);

    auto root = library->getRoot();
    libraryPath.setText ((root == juce::File() ? juce::String ("No library folder set. Choose the folder that holds your expansions (.xpn), program folders and samples.")
                                               : root.getFullPathName() + "\n" + library->getStatus())
                         + "\nKaia only reads from the library: files there are never changed.",
                         juce::dontSendNotification);
    rescanButton.setEnabled (root != juce::File());
    flingSlider.setValue (std::round (KineticScroller::flingFromSettings() * 10.0f), juce::dontSendNotification);

    configPath.setText ("Settings and presets: " + UserFiles::configDirectory().getFullPathName()
                        + "\nPresets are saved in " + UserFiles::presetsDirectory().getFullPathName(), juce::dontSendNotification);

    mapView->repaint();
    swatches->repaint();
    padThemePicker->repaint();
    resized();
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
    moduleInfo.setBounds (row (58));
    y += 4;
    mapView->setBounds (row (MapView::kCellHeight * 4).withWidth (juce::jmin (w, 560)));
    y += 6;
    if (saveMapButton.isVisible())
    {
        auto mapRow = row (26);
        for (auto* b : { &saveMapButton, &resetMapButton, &deleteMapButton })
            if (b->isVisible())
            {
                b->setBounds (mapRow.removeFromLeft (b == &saveMapButton ? 160 : 150));
                mapRow.removeFromLeft (8);
            }
        mapHint.setBounds (mapRow);
        y += 6;
    }
    if (chokeToggle.isVisible())
    {
        chokeToggle.setBounds (row (24));
        y += 4;
    }
    auto learnRow = row (26);
    clearLearnedButton.setBounds (learnRow.removeFromLeft (170));
    learnRow.removeFromLeft (12);
    lastNoteLabel.setBounds (learnRow);

    y += 18;
    headings[1]->setBounds (row (22));
    loopsToggle.setBounds (row (24));
    loopsInfo.setBounds (row (44));

    y += 18;
    headings[2]->setBounds (row (22));
    libraryPath.setBounds (row (60));
    y += 4;
    auto libButtons = row (26);
    chooseLibraryButton.setBounds (libButtons.removeFromLeft (130));
    libButtons.removeFromLeft (6);
    rescanButton.setBounds (libButtons.removeFromLeft (80));
    y += 8;
    auto flingRow = row (26);
    flingLabel.setBounds (flingRow.removeFromLeft (160));
    flingSlider.setBounds (flingRow.withWidth (juce::jmin (flingRow.getWidth(), 300)));

    y += 18;
    headings[3]->setBounds (row (22));
    swatches->setBounds (row (34).withWidth ((int) accentOptions().size() * 38));

    y += 18;
    headings[4]->setBounds (row (22));
    auto pickerW = juce::jmin (w, 8 * PadThemePicker::kCellW);
    padThemePicker->setBounds (row (padThemePicker->heightForWidth (pickerW)).withWidth (pickerW));

    y += 18;
    headings[5]->setBounds (row (22));
    auto cfg = row (58);
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
