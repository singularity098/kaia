#include "ProgramBrowser.h"

namespace kaia
{

ProgramBrowser::ProgramBrowser (KaiaProcessor& p) : processor (p)
{
    search.setTextToShowWhenEmpty ("Search programs", Colours::textDim);
    search.setFont (uiFont (14.0f));
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (8, 0);
    search.onTextChange = [this] { rebuildRows(); };
    search.onEscapeKey = [this] { search.clear(); rebuildRows(); };
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, Colours::panel);
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    refresh();
}

void ProgramBrowser::refresh()
{
    auto& info = processor.getPackInfo();
    cover = info.image.isEmpty() ? juce::Image()
                                 : juce::ImageFileFormat::loadFrom (info.image.getData(), info.image.getSize());
    rebuildRows();
    repaint();
}

void ProgramBrowser::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        list.grabKeyboardFocus();
    }
}

void ProgramBrowser::rebuildRows()
{
    rows.clear();
    auto& programs = processor.getPrograms();
    auto filter = search.getText().trim();
    juce::String currentFolder = "\x01";

    for (int i = 0; i < (int) programs.size(); ++i)
    {
        auto& entry = programs[(size_t) i];
        if (filter.isNotEmpty() && ! entry.name.containsIgnoreCase (filter) && ! entry.folder.containsIgnoreCase (filter))
            continue;

        if (entry.folder != currentFolder)
        {
            currentFolder = entry.folder;
            if (currentFolder.isNotEmpty())
                rows.push_back ({ -1, currentFolder });
        }
        rows.push_back ({ i, entry.isArchive ? entry.name + "  (expansion)" : entry.name });
    }

    updating = true;
    list.updateContent();
    list.deselectAllRows();
    for (int r = 0; r < (int) rows.size(); ++r)
        if (rows[(size_t) r].program == processor.getCurrentProgramIndex() && rows[(size_t) r].program >= 0)
        {
            list.selectRow (r, false, true);
            list.scrollToEnsureRowIsOnscreen (r);
        }
    updating = false;
    list.repaint();
}

int ProgramBrowser::getNumRows()
{
    return (int) rows.size();
}

void ProgramBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) rows.size())
        return;
    auto& r = rows[(size_t) row];
    auto area = juce::Rectangle<int> (width, height);

    if (r.program < 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (11.5f, true));
        g.drawText (r.text.toUpperCase(), area.reduced (10, 0), juce::Justification::bottomLeft);
        return;
    }

    bool current = r.program == processor.getCurrentProgramIndex();
    if (selected || current)
    {
        g.setColour (current ? Colours::accent.withAlpha (0.22f) : Colours::panelRaised);
        g.fillRoundedRectangle (area.reduced (4, 1).toFloat(), 4.0f);
    }
    g.setColour (current ? Colours::accent : Colours::text);
    g.setFont (uiFont (14.0f));
    g.drawText (r.text, area.reduced (16, 0), juce::Justification::centredLeft);
}

void ProgramBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].program >= 0)
        processor.loadProgramAt (rows[(size_t) row].program);
}

void ProgramBrowser::selectedRowsChanged (int row)
{
    if (updating)
        return;
    // Arrow-key browsing: load as the selection moves (archives open on click/return only)
    if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].program >= 0
         && rows[(size_t) row].program != processor.getCurrentProgramIndex()
         && ! processor.getPrograms()[(size_t) rows[(size_t) row].program].isArchive
         && juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() == false)
        processor.loadProgramAt (rows[(size_t) row].program);
}

void ProgramBrowser::returnKeyPressed (int row)
{
    if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].program >= 0)
        processor.loadProgramAt (rows[(size_t) row].program);
}

void ProgramBrowser::resized()
{
    auto r = getLocalBounds().reduced (12);
    headerArea = r.removeFromTop (72);
    closeButton.setBounds (headerArea.getRight() - 64, headerArea.getY() + 4, 64, 26);
    r.removeFromTop (6);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    list.setBounds (r);
}

void ProgramBrowser::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    auto& info = processor.getPackInfo();
    auto h = headerArea;
    if (cover.isValid())
    {
        auto c = h.removeFromLeft (72);
        g.drawImage (cover, c.toFloat(), juce::RectanglePlacement::centred);
        h.removeFromLeft (10);
    }
    h.removeFromRight (72);

    g.setColour (Colours::text);
    g.setFont (uiFont (17.0f, true));
    g.drawText (info.title.isNotEmpty() ? info.title : juce::String ("No pack open"), h.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (13.0f));
    if (info.manufacturer.isNotEmpty())
        g.drawText (info.manufacturer, h.removeFromTop (18), juce::Justification::centredLeft);
    int count = 0;
    for (auto& p : processor.getPrograms())
        if (! p.isArchive)
            ++count;
    g.drawText (juce::String (count) + " program" + (count == 1 ? "" : "s") + "   " + processor.getPackFile().getFullPathName(),
                h.removeFromTop (18), juce::Justification::centredLeft);
}

}
