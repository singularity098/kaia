#include "PluginEditor.h"

namespace kaia
{

KaiaEditor::KaiaEditor (KaiaProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      masterAttachment (p.getParameters(), "master", masterSlider),
      padGrid (p),
      padEditor (p),
      browser (p),
      library (p),
      settingsPanel (p)
{
    setLookAndFeel (&lookAndFeel);

    openButton.setTooltip ("Open an MPC program (.xpm, .pgm), an expansion (.xpn), a folder, or audio files");
    openButton.onClick = [this] { showOpenMenu(); };
    browseButton.setTooltip ("Show the programs of the open expansion or folder");
    browseButton.onClick = [this] { togglePanel (Panel::Programs); };
    libraryButton.setTooltip ("Browse your whole library: presets, expansions, programs, pads and samples");
    libraryButton.onClick = [this] { togglePanel (Panel::Library); };
    settingsButton.setTooltip ("Drum module, library folder, highlight colour");
    settingsButton.onClick = [this] { togglePanel (Panel::Settings); };
    prevButton.setTooltip ("Previous program");
    nextButton.setTooltip ("Next program");
    prevButton.onClick = [this] { processor.stepProgram (-1); };
    nextButton.onClick = [this] { processor.stepProgram (1); };
    panicButton.setTooltip ("Stop all sounds");
    panicButton.onClick = [this] { processor.getEngine().allSoundOff(); };
    relinkButton.setTooltip ("Find missing samples in a folder you choose");
    relinkButton.onClick = [this] { chooseRelinkFolder(); };
    routingButton.setTooltip ("Route pads to the plugin's aux outputs for multi-track mixing");
    routingButton.onClick = [this] { showRoutingMenu(); };

    for (auto* b : { &openButton, &browseButton, &libraryButton, &prevButton, &nextButton, &panicButton, &relinkButton, &routingButton, &settingsButton })
        addAndMakeVisible (b);

    kitLabel.setFont (uiFont (19.0f, true));
    kitLabel.setColour (juce::Label::textColourId, Colours::text);
    kitLabel.setMinimumHorizontalScale (0.6f);
    formatLabel.setFont (uiFont (12.0f));
    formatLabel.setColour (juce::Label::textColourId, Colours::textDim);
    statusLabel.setFont (uiFont (13.0f));
    statusLabel.setMinimumHorizontalScale (0.7f);
    voicesLabel.setFont (uiFont (12.0f));
    voicesLabel.setColour (juce::Label::textColourId, Colours::textDim);
    voicesLabel.setJustificationType (juce::Justification::centredRight);
    for (auto* l : { &kitLabel, &formatLabel, &statusLabel, &voicesLabel })
        addAndMakeVisible (l);

    coverButton.setTooltip ("Show the programs of this expansion");
    coverButton.onClick = [this] { togglePanel (Panel::Programs); };
    addChildComponent (coverButton);

    githubLink.setFont (uiFont (12.0f), false, juce::Justification::centredRight);
    githubLink.setColour (juce::HyperlinkButton::textColourId, Colours::textDim);
    githubLink.setTooltip ("Kaia on GitHub: github.com/singularity098/kaia");
    addAndMakeVisible (githubLink);

    masterSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    masterSlider.setDoubleClickReturnValue (true, 0.0);
    masterSlider.setTooltip ("Master volume");
    masterSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    masterSlider.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (masterSlider);

    padGrid.onPadSelected = [this] (int pad)
    {
        processor.selectedPad = pad;
        padGrid.refresh();
        padEditor.refresh();
    };
    padGrid.onPadMenu = [this] (int pad, juce::Point<int> pos) { showPadMenu (pad, pos); };
    padGrid.isInterestedInDrag = [] (const juce::var& d) { return LibraryBrowser::isLibraryDrag (d); };
    padGrid.onDrop = [this] (int pad, const juce::var& d, bool asLayer)
    {
        processor.selectedPad = pad;
        library.dropOnPad (d, pad, asLayer);
        padGrid.refresh();
    };
    addAndMakeVisible (padGrid);

    padEditor.onLoadSampleRequest = [this] (int pad) { chooseSamplesForPad (pad, KaiaProcessor::AssignMode::Append, 0); };
    padEditor.onReplaceLayerRequest = [this] (int pad, int layer) { chooseSamplesForPad (pad, KaiaProcessor::AssignMode::ReplaceLayer, layer); };
    addAndMakeVisible (padEditor);

    browser.onClose = [this] { showPanel (Panel::PadEditor); };
    addChildComponent (browser);
    library.onClose = [this] { showPanel (Panel::PadEditor); };
    library.onChooseFolder = [this] { chooseLibraryFolder(); };
    addChildComponent (library);
    settingsPanel.onClose = [this] { showPanel (Panel::PadEditor); };
    settingsPanel.onChooseLibraryFolder = [this] { chooseLibraryFolder(); };
    settingsPanel.onAccentChanged = [this] { accentChanged(); };
    settingsPanel.onPadThemeChanged = [this] { padThemeChanged(); };
    addChildComponent (settingsPanel);
    settings().addChangeListener (this);

    processor.addChangeListener (this);

    constrainer.setMinimumSize (900, 600);
    constrainer.setMaximumSize (2400, 1600);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (1120, 720);

    refreshAll();
    startTimerHz (15);
}

KaiaEditor::~KaiaEditor()
{
    processor.cancelMidiLearn();
    settings().removeChangeListener (this);
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void KaiaEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // Settings changed (maybe by another instance): pick up a new highlight colour
    if (source == &settings())
    {
        if (savedAccentColour() != Colours::accent)
            accentChanged();
        if (savedPadTheme() != Colours::padTheme)
            padThemeChanged();
        return;
    }
    refreshAll();
}

void KaiaEditor::accentChanged()
{
    setAccentColour (savedAccentColour());
    lookAndFeel.applyColours();
    sendLookAndFeelChange();
    refreshAll();
}

void KaiaEditor::padThemeChanged()
{
    Colours::padTheme = savedPadTheme();
    padGrid.refresh();
    settingsPanel.refresh();
}

void KaiaEditor::updateCover()
{
    // Decoding the artwork only when the pack changes
    auto& info = processor.getPackInfo();
    auto key = processor.getPackFile().getFullPathName() + ":" + juce::String ((juce::int64) info.image.getSize());
    if (key == shownCoverKey)
        return;
    shownCoverKey = key;

    auto image = info.image.isEmpty() ? juce::Image()
                                      : juce::ImageFileFormat::loadFrom (info.image.getData(), info.image.getSize());
    if (image.isValid())
        coverButton.setImages (false, true, true, image, 1.0f, {}, image, 0.85f, {}, image, 0.7f, {});
    coverButton.setVisible (image.isValid());
}

void KaiaEditor::refreshAll()
{
    updateCover();
    auto& kit = processor.getKit();
    kitLabel.setText (kit.name.isNotEmpty() ? kit.name : "Untitled", juce::dontSendNotification);

    juce::String info = kit.formatName;
    auto& programs = processor.getPrograms();
    if (! programs.empty() && processor.getCurrentProgramIndex() >= 0)
        info << (info.isNotEmpty() ? "   " : "") << (processor.getCurrentProgramIndex() + 1) << " / " << (int) programs.size()
             << "  in  " << processor.getPackInfo().title;
    formatLabel.setText (info, juce::dontSendNotification);

    statusLabel.setText (processor.getStatusMessage(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, processor.lastLoadFailed() ? Colours::error : Colours::textDim);
    statusLabel.setTooltip (processor.getWarnings().joinIntoString ("\n"));

    prevButton.setEnabled (programs.size() > 1);
    nextButton.setEnabled (programs.size() > 1);
    browseButton.setEnabled (! programs.empty());
    relinkButton.setVisible (kit.countMissing() > 0);

    padGrid.refresh();
    padEditor.refresh();
    if (browser.isVisible())
        browser.refresh();
    resized();
    repaint();
}

void KaiaEditor::timerCallback()
{
    auto& engine = processor.getEngine();
    auto voices = engine.getActiveVoiceCount();
    voicesLabel.setText (juce::String (voices) + (voices == 1 ? " voice" : " voices"), juce::dontSendNotification);

    // Incoming MIDI notes: the header shows the last one and flashes on every hit
    auto lastNote = engine.getLastInputNote();
    if (lastNote != shownInputNote)
    {
        shownInputNote = lastNote;
        midiFlash = 1.0f;
        repaint (midiArea);
    }
    else if (midiFlash > 0.0f)
    {
        midiFlash = midiFlash < 0.05f ? 0.0f : midiFlash * 0.7f;
        repaint (midiArea);
    }

    auto loading = processor.isLoading();
    auto progress = loading ? processor.getLoadProgress() : 0.0f;
    if (loading != wasLoading || std::abs (progress - shownProgress) > 0.01f)
    {
        wasLoading = loading;
        shownProgress = progress;
        repaint (progressArea);
    }
}

//==============================================================================
static constexpr int kLogoWidth = 196;     // the logo at the left of the header

void KaiaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    g.setColour (Colours::panel);
    g.fillRect (headerArea);
    g.fillRect (footerArea);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (headerArea.getBottom(), 0.0f, (float) getWidth());
    g.drawHorizontalLine (footerArea.getY(), 0.0f, (float) getWidth());

    // Logo: a small 2x2 pad block and the name
    auto logo = headerArea.withWidth (kLogoWidth).reduced (16, 0);
    {
        auto icon = logo.removeFromLeft (30).withSizeKeepingCentre (30, 30).toFloat();
        auto cell = icon.getWidth() / 2.0f;
        for (int i = 0; i < 4; ++i)
        {
            auto r = juce::Rectangle<float> (icon.getX() + (float) (i % 2) * cell, icon.getY() + (float) (i / 2) * cell, cell, cell).reduced (2.0f);
            g.setColour (i == 1 ? Colours::text : Colours::accent);
            g.fillRoundedRectangle (r, 3.0f);
        }
        logo.removeFromLeft (12);
    }
    g.setFont (uiFont (27.0f, true));
    g.setColour (Colours::text);
    g.drawText ("Kaia", logo, juce::Justification::centredLeft);

    if (wasLoading)
    {
        auto r = progressArea.toFloat();
        g.setColour (Colours::panelRaised);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (6.0f, r.getWidth() * shownProgress)), 3.0f);
    }

    // Last MIDI note received
    {
        auto r = midiArea.toFloat();
        g.setColour (Colours::panelRaised.interpolatedWith (Colours::accent, 0.45f * midiFlash));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (midiFlash > 0.0f ? Colours::accent : Colours::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

        auto inner = midiArea.reduced (8, 4);
        if (inner.getWidth() > 96)
        {
            auto label = inner.removeFromLeft (30);
            g.setColour (Colours::textDim);
            g.setFont (uiFont (10.0f, true));
            g.drawText ("MIDI", label.removeFromTop (label.getHeight() / 2), juce::Justification::bottomLeft);
            g.drawText ("NOTE", label, juce::Justification::topLeft);
        }

        bool any = shownInputNote != 0;
        auto note = (int) (shownInputNote & 0x7f);
        auto numberArea = inner.removeFromLeft (38);
        g.setColour (any ? Colours::text : Colours::textDim);
        g.setFont (uiFont (22.0f, true));
        g.drawText (any ? juce::String (note) : juce::String ("--"), numberArea, juce::Justification::centred);

        g.setFont (uiFont (11.0f));
        g.setColour (Colours::textDim);
        if (any)
        {
            g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3), inner.removeFromTop (inner.getHeight() / 2),
                        juce::Justification::bottomRight);
            g.drawText ("vel " + juce::String (processor.getEngine().getLastInputVelocity()), inner, juce::Justification::topRight);
        }
    }

    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText ("MASTER", masterSlider.getBounds().translated (0, 0).withY (headerArea.getBottom() - 14).withHeight (12),
                juce::Justification::centred);
}

void KaiaEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (64);
    footerArea = r.removeFromBottom (32);

    auto h = headerArea.reduced (12, 10);
    h.removeFromLeft (kLogoWidth - 20);
    auto nav = [&h] (juce::Component& c, int w) { c.setBounds (h.removeFromLeft (w).withSizeKeepingCentre (w, 38)); };
    nav (openButton, 76);
    h.removeFromLeft (6);
    nav (browseButton, 92);
    h.removeFromLeft (6);
    nav (libraryButton, 80);
    h.removeFromLeft (6);
    nav (prevButton, 34);
    h.removeFromLeft (4);
    nav (nextButton, 34);
    h.removeFromLeft (14);

    auto narrow = h.getWidth() < 520;     // near the minimum editor width
    githubLink.setBounds (h.removeFromRight (48).withTrimmedBottom (h.getHeight() / 2));
    h.removeFromRight (8);
    masterSlider.setBounds (h.removeFromRight (52).withTrimmedBottom (10).expanded (0, 6));
    h.removeFromRight (8);
    panicButton.setBounds (h.removeFromRight (58).reduced (0, 4));
    h.removeFromRight (10);
    midiArea = h.removeFromRight (narrow ? 104 : 124).reduced (0, 1);
    h.removeFromRight (12);

    if (coverButton.isVisible() && h.getWidth() >= 220)
    {
        coverButton.setBounds (h.removeFromLeft (h.getHeight() + 6).expanded (0, 4).withTrimmedRight (6));
        h.removeFromLeft (8);
    }
    else
    {
        coverButton.setBounds ({});
    }
    kitLabel.setBounds (h.removeFromTop (22));
    formatLabel.setBounds (h);

    auto f = footerArea.reduced (12, 5);
    routingButton.setBounds (f.removeFromRight (84));
    f.removeFromRight (6);
    settingsButton.setBounds (f.removeFromRight (84));
    f.removeFromRight (6);
    if (relinkButton.isVisible())
    {
        relinkButton.setBounds (f.removeFromRight (84));
        f.removeFromRight (6);
    }
    progressArea = f.removeFromRight (120).reduced (0, 7);
    f.removeFromRight (10);
    voicesLabel.setBounds (f.removeFromRight (70));
    f.removeFromRight (10);
    statusLabel.setBounds (f);

    auto body = r.reduced (12, 12);
    auto gridW = juce::jmin ((int) (body.getWidth() * 0.44), body.getHeight() - 38 + 0);
    auto left = body.removeFromLeft (juce::jmax (360, gridW));
    padGrid.setBounds (left);
    body.removeFromLeft (14);
    padEditor.setBounds (body);
    browser.setBounds (body);
    library.setBounds (body);
    settingsPanel.setBounds (body);
}

//==============================================================================
void KaiaEditor::showPanel (Panel panel)
{
    currentPanel = panel;
    padEditor.setVisible (panel == Panel::PadEditor);
    browser.setVisible (panel == Panel::Programs);
    library.setVisible (panel == Panel::Library);
    settingsPanel.setVisible (panel == Panel::Settings);
    browseButton.setToggleState (panel == Panel::Programs, juce::dontSendNotification);
    libraryButton.setToggleState (panel == Panel::Library, juce::dontSendNotification);
    settingsButton.setToggleState (panel == Panel::Settings, juce::dontSendNotification);
}

void KaiaEditor::chooseLibraryFolder()
{
    juce::SharedResourcePointer<LibraryService> service;
    auto start = service->getRoot().isDirectory() ? service->getRoot() : lastFolder();
    chooser = std::make_unique<juce::FileChooser> ("Choose your library folder (expansions, program folders, samples)", start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        auto dir = fc.getResult();
        if (! dir.isDirectory())
            return;
        juce::SharedResourcePointer<LibraryService> lib;
        lib->setRoot (dir);
        showPanel (Panel::Library);
    });
}

void KaiaEditor::savePresetDialog()
{
    presetDialog = std::make_unique<juce::AlertWindow> ("Save preset",
                                                        "Saved in " + UserFiles::presetsDirectory().getFullPathName()
                                                        + ".\nThe preset refers to the samples where they are; nothing in your library is changed.",
                                                        juce::MessageBoxIconType::NoIcon, this);
    presetDialog->addTextEditor ("name", processor.getKit().name, "Name");
    presetDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        auto name = presetDialog != nullptr ? presetDialog->getTextEditorContents ("name").trim() : juce::String();
        presetDialog.reset();
        if (result != 1 || name.isEmpty())
            return;

        auto save = [this, name]
        {
            juce::String error;
            processor.savePreset (name, error);
            if (library.isVisible())
                library.refresh();
        };

        if (! Presets::fileFor (name).existsAsFile())
        {
            save();
            return;
        }
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Replace preset",
                                                                    "A preset called \"" + name + "\" already exists. Replace it?",
                                                                    "Replace", "Cancel", this);
        juce::AlertWindow::showAsync (options, [save, safe = juce::Component::SafePointer<KaiaEditor> (this)] (int r)
        {
            if (r == 1 && safe != nullptr)
                save();
        });
    }), false);
}

juce::File KaiaEditor::lastFolder()
{
    juce::File f (settings().getValue ("lastFolder"));
    return f.isDirectory() ? f : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

juce::StringArray KaiaEditor::getRecents()
{
    auto list = juce::StringArray::fromLines (settings().getValue ("recent"));
    list.removeEmptyStrings();
    return list;
}

void KaiaEditor::addRecent (const juce::File& f)
{
    auto list = getRecents();
    list.removeString (f.getFullPathName());
    list.insert (0, f.getFullPathName());
    while (list.size() > 12)
        list.remove (list.size() - 1);
    settings().setValue ("recent", list.joinIntoString ("\n"));
    settings().setValue ("lastFolder", (f.isDirectory() ? f : f.getParentDirectory()).getFullPathName());
    settings().saveIfNeeded();
}

void KaiaEditor::showOpenMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Open program, expansion or samples...");
    menu.addItem (2, "Open folder...");
    menu.addSeparator();

    auto recents = getRecents();
    juce::PopupMenu recentMenu;
    for (int i = 0; i < recents.size(); ++i)
    {
        juce::File f (recents[i]);
        recentMenu.addItem (100 + i, f.getFileName() + "   (" + f.getParentDirectory().getFullPathName() + ")", f.exists());
    }
    menu.addSubMenu ("Recent", recentMenu, ! recents.isEmpty());

    auto presets = Presets::list();
    juce::PopupMenu presetMenu;
    for (int i = 0; i < (int) presets.size(); ++i)
        presetMenu.addItem (1000 + i, presets[(size_t) i].name);
    menu.addSubMenu ("Presets", presetMenu, ! presets.empty());
    menu.addSeparator();
    menu.addItem (4, "Save preset...");
    menu.addItem (3, "New empty kit");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (openButton), [this, recents, presets] (int result)
    {
        if (result == 1) chooseFiles (false);
        else if (result == 2) chooseFiles (true);
        else if (result == 3) processor.newEmptyKit();
        else if (result == 4) savePresetDialog();
        else if (result >= 1000 && result - 1000 < (int) presets.size())
            processor.loadPreset (presets[(size_t) (result - 1000)].file);
        else if (result >= 100 && result - 100 < recents.size())
        {
            juce::File f (recents[result - 100]);
            addRecent (f);
            processor.openPack (f);
        }
    });
}

void KaiaEditor::chooseFiles (bool folders)
{
    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | (folders ? juce::FileBrowserComponent::canSelectDirectories
                                 : juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems);

    chooser = std::make_unique<juce::FileChooser> (folders ? "Open a folder of MPC programs or samples" : "Open an MPC program, expansion or samples",
                                                   lastFolder(), folders ? juce::String() : KitLoader::fileFilter());
    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        auto results = fc.getResults();
        if (results.isEmpty())
            return;
        addRecent (results[0]);
        if (results.size() == 1)
            processor.openPack (results[0]);
        else
            processor.loadAudioFiles (results);
    });
}

void KaiaEditor::chooseSamplesForPad (int pad, KaiaProcessor::AssignMode mode, int layer)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose samples", lastFolder(), "*.wav;*.WAV;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.snd;*.SND");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                      | (mode == KaiaProcessor::AssignMode::ReplaceLayer ? 0 : juce::FileBrowserComponent::canSelectMultipleItems);
    chooser->launchAsync (chooserFlags, [this, pad, mode, layer] (const juce::FileChooser& fc)
    {
        auto results = fc.getResults();
        if (results.isEmpty())
            return;
        settings().setValue ("lastFolder", results[0].getParentDirectory().getFullPathName());
        processor.assignSamplesToPad (pad, results, mode, layer);
    });
}

void KaiaEditor::chooseRelinkFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder that contains the missing samples", lastFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().isDirectory())
            processor.relinkMissingSamples (fc.getResult());
    });
}

void KaiaEditor::showRoutingMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Pad outputs");
    menu.addItem (1, "All pads to Main");
    menu.addItem (2, "Pads 1-16 of each bank to Aux 1-16");
    menu.addItem (3, "Each loaded pad to its own Aux output");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (routingButton), [this] (int result)
    {
        if (result > 0)
            processor.routeOutputs (result - 1);
    });
}

void KaiaEditor::showPadMenu (int pad, juce::Point<int>)
{
    auto& kit = processor.getKit();
    if (pad < 0 || pad >= (int) kit.pads.size())
        return;
    bool hasLayers = ! kit.pads[(size_t) pad].layers.empty();

    juce::PopupMenu menu;
    menu.addSectionHeader (kit.pads[(size_t) pad].isKeygroup() ? "Keygroup " + juce::String (pad + 1) : "Pad " + padName (pad));
    menu.addItem (1, "Load sample(s)...");
    menu.addItem (2, "Add layer(s)...", (int) kit.pads[(size_t) pad].layers.size() < kMaxLayers);
    menu.addSeparator();
    menu.addItem (3, "Copy pad", hasLayers);
    menu.addItem (4, "Paste pad", clipboard != nullptr);
    menu.addItem (5, "Clear pad", hasLayers);
    menu.addSeparator();
    bool keygroups = kit.isKeygroupProgram;
    if (processor.getMidiLearnPad() == pad)
        menu.addItem (6, "Cancel MIDI Learn");
    else
        menu.addItem (6, "MIDI Learn (hit a drum to assign it)", ! keygroups);
    auto& learned = processor.getTriggerMap().learned;
    bool hasLearned = std::any_of (learned.begin(), learned.end(), [pad] (const auto& kv) { return kv.second == pad; });
    menu.addItem (7, "Forget learned notes", hasLearned);

    menu.showMenuAsync (juce::PopupMenu::Options(), [this, pad] (int result)
    {
        switch (result)
        {
            case 6:
                if (processor.getMidiLearnPad() == pad)
                    processor.cancelMidiLearn();
                else
                    processor.startMidiLearn (pad);
                padGrid.refresh();
                break;
            case 7:
            {
                auto map = processor.getTriggerMap();
                map.forgetPad (pad);
                processor.setTriggerMap (map);
                break;
            }
            case 1: chooseSamplesForPad (pad, KaiaProcessor::AssignMode::Replace, 0); break;
            case 2: chooseSamplesForPad (pad, KaiaProcessor::AssignMode::Append, 0); break;
            case 3: clipboard = std::make_unique<Pad> (processor.getKit().pads[(size_t) pad]); break;
            case 4:
                if (clipboard != nullptr)
                {
                    auto copy = *clipboard;
                    processor.editPad (pad, [copy] (Pad& p)
                    {
                        auto note = p.note, lo = p.noteLow, hi = p.noteHigh;
                        p = copy;
                        if (! copy.isKeygroup())
                            p.note = note;
                        else
                        {
                            p.noteLow = lo;
                            p.noteHigh = hi;
                        }
                    });
                }
                break;
            case 5: processor.editPad (pad, [] (Pad& p) { auto note = p.note; auto lo = p.noteLow, hi = p.noteHigh; p = Pad(); p.note = note; p.noteLow = lo; p.noteHigh = hi; }); break;
            default: break;
        }
    });
}

//==============================================================================
bool KaiaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.isDirectory() || isAudioFileName (file.getFileName()) || isProgramFileName (file.getFileName())
             || file.hasFileExtension ("xpn") || Presets::isPresetFile (file))
            return true;
    }
    return false;
}

void KaiaEditor::fileDragMove (const juce::StringArray& files, int x, int y)
{
    bool audioOnly = true;
    for (auto& f : files)
        audioOnly = audioOnly && isAudioFileName (f);

    auto posInGrid = padGrid.getLocalPoint (this, juce::Point<int> (x, y));
    padGrid.setDropHighlight (audioOnly ? padGrid.padIndexAt (posInGrid) : -1);
}

void KaiaEditor::fileDragExit (const juce::StringArray&)
{
    padGrid.setDropHighlight (-1);
}

void KaiaEditor::filesDropped (const juce::StringArray& paths, int x, int y)
{
    padGrid.setDropHighlight (-1);

    juce::Array<juce::File> files;
    bool audioOnly = true;
    for (auto& p : paths)
    {
        files.add (juce::File (p));
        audioOnly = audioOnly && isAudioFileName (p);
    }
    if (files.isEmpty())
        return;

    if (audioOnly)
    {
        // Onto a pad: assign. Onto a layer row: replace that layer. Elsewhere on the editor: the selected pad.
        auto padIndex = padGrid.padIndexAt (padGrid.getLocalPoint (this, juce::Point<int> (x, y)));
        if (padIndex >= 0)
        {
            processor.selectedPad = padIndex;
            processor.assignSamplesToPad (padIndex, files);
            return;
        }

        if (padEditor.isVisible() && padEditor.getBounds().contains (x, y))
        {
            auto layer = padEditor.layerIndexAt (padEditor.getLocalPoint (this, juce::Point<int> (x, y)));
            if (layer >= 0)
                processor.assignSamplesToPad (processor.selectedPad, files, KaiaProcessor::AssignMode::ReplaceLayer, layer);
            else
                processor.assignSamplesToPad (processor.selectedPad, files, KaiaProcessor::AssignMode::Append);
            return;
        }

        // Dropped on the header/background: make a new kit from the files
        processor.loadAudioFiles (files);
        return;
    }

    addRecent (files[0]);
    processor.openPack (files[0]);
}

}
