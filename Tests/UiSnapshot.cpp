// Renders the real plugin editor to PNG files (for documentation and visual checks).
// Usage: KaiaUiSnapshot <program/pack/folder> <out dir> [pad index] [library folder]
// With a library folder it also shows the Files tab over it; KAIA_SNAPSHOT_FILTER=<text> adds a
// shot of the Files tab filtered by that text (files-filter.png).
// Set KAIA_CONFIG_DIR to keep it away from your own settings.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UI/PluginEditor.h"

using namespace kaia;

static void pump (int ms)
{
    auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

static void save (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, out);
    std::cout << "wrote " << f.getFullPathName() << "\n";
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
        return 2;

    juce::File target { juce::String (argv[1]) };
    juce::File outDir { juce::String (argv[2]) };
    int pad = argc > 3 ? juce::String (argv[3]).getIntValue() : 0;
    juce::File libraryDir { argc > 4 ? juce::String (argv[4]) : juce::String() };
    outDir.createDirectory();

    KaiaProcessor processor;
    processor.enableAllBuses();
    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    processor.openPack (target);
    pump (300);
    for (int i = 0; i < 300 && processor.isLoading(); ++i)
        pump (50);
    pump (200);
    std::cout << "status: " << processor.getStatusMessage() << "\n";

    processor.selectedPad = pad;
    processor.selectedBank = pad / kPadsPerBank;

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1120, 720);
    editor->setVisible (true);
    pump (300);

    // KAIA_SNAPSHOT_PAD_THEMES=1: one shot of the pads per colour theme (before any pad lights up)
    if (juce::SystemStats::getEnvironmentVariable ("KAIA_SNAPSHOT_PAD_THEMES", {}).isNotEmpty())
    {
        juce::Component* grid = nullptr;
        for (auto* c : editor->getChildren())
            if (dynamic_cast<PadGrid*> (c) != nullptr)
                grid = c;
        auto saved = Colours::padTheme;
        for (int i = 0; grid != nullptr && i < (int) padThemes().size(); ++i)
        {
            Colours::padTheme = i;
            editor->repaint();
            pump (120);
            auto img = editor->createComponentSnapshot (grid->getBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("padtheme-" + juce::String (i).paddedLeft ('0', 2) + "-" + padThemes()[(size_t) i].name + ".png");
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (img, out);
        }
        Colours::padTheme = saved;
        editor->repaint();
    }

    // Play a few pads in the background so some of them light up
    std::atomic<bool> running { true };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), 256);
        juce::MidiBuffer midi;
        int block = 0;
        std::vector<int> notes;
        for (auto& p : processor.getKit().pads)
            if (p.hasAnySample() && ! p.isKeygroup())
                notes.push_back (p.note);
        while (running)
        {
            midi.clear();
            if (! notes.empty() && block % 18 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[(size_t) (block / 18) % notes.size()], (juce::uint8) 115), 0);
            ++block;
            processor.processBlock (buffer, midi);
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
    });

    pump (900);
    save (*editor, outDir.getChildFile ("editor.png"));


    auto click = [&editor] (const juce::String& text)
    {
        for (auto* c : editor->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == text)
                    b->triggerClick();
        pump (400);
    };

    // Program browser view
    if (! processor.getPrograms().empty())
    {
        click ("Programs");
        save (*editor, outDir.getChildFile ("browser.png"));
    }

    // Library and settings
    if (libraryDir.isDirectory())
    {
        juce::SharedResourcePointer<LibraryService> library;
        library->setRoot (libraryDir);
        for (int i = 0; i < 100 && library->isScanning(); ++i)
            pump (50);
        click ("Library");
        // Open the first pack so the shot shows programs and samples
        std::function<juce::TreeView* (juce::Component&)> findTree = [&findTree] (juce::Component& c) -> juce::TreeView*
        {
            if (auto* t = dynamic_cast<juce::TreeView*> (&c))
                if (t->isVisible() && t->getParentComponent()->isVisible())
                    return t;
            for (auto* child : c.getChildren())
                if (auto* t = findTree (*child))
                    return t;
            return nullptr;
        };
        if (auto* tree = findTree (*editor))
            if (auto* rootItem = tree->getRootItem())
                for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                    if (auto* pack = rootItem->getSubItem (i); pack->getUniqueName().startsWith ("k:"))
                    {
                        pack->setOpen (true);       // opens its programs and samples too
                        break;
                    }
        pump (300);
        save (*editor, outDir.getChildFile ("library.png"));

        // KAIA_SNAPSHOT_BENCH=1: how long one frame of the scrolling list takes to paint
        if (auto* tree = findTree (*editor); tree != nullptr && juce::SystemStats::getEnvironmentVariable ("KAIA_SNAPSHOT_BENCH", {}) == "1")
        {
            auto* vp = tree->getViewport();
            for (auto scale : { 1.0f, 2.0f })
            {
                juce::Image frame (juce::Image::RGB, juce::roundToInt ((float) tree->getWidth() * scale), juce::roundToInt ((float) tree->getHeight() * scale), true);
                auto maxY = juce::jmax (1, vp->getViewedComponent()->getHeight() - vp->getViewHeight());
                auto start = juce::Time::getMillisecondCounterHiRes();
                constexpr int frames = 200;
                for (int i = 0; i < frames; ++i)
                {
                    vp->setViewPosition (0, (i * 7) % maxY);
                    juce::Graphics g (frame);
                    g.addTransform (juce::AffineTransform::scale (scale));
                    tree->paintEntireComponent (g, true);
                }
                std::cout << "library list: " << (juce::Time::getMillisecondCounterHiRes() - start) / frames
                          << " ms per frame at scale " << scale << "\n";
            }
            vp->setViewPosition (0, 0);
        }

        // The tile view (covers load in the background)
        std::function<juce::Button* (juce::Component&, const juce::String&)> findButton
            = [&findButton] (juce::Component& c, const juce::String& text) -> juce::Button*
        {
            if (auto* b = dynamic_cast<juce::Button*> (&c))
                if (b->getButtonText() == text)
                    return b;
            for (auto* child : c.getChildren())
                if (child->isVisible())
                    if (auto* b = findButton (*child, text))
                        return b;
            return nullptr;
        };
        // The favourites filter (stars come from favourites.txt in the config folder)
        if (auto* favourites = findButton (*editor, "Favourites"))
        {
            favourites->triggerClick();
            pump (200);
            save (*editor, outDir.getChildFile ("library-favourites.png"));
            favourites->triggerClick();
            pump (200);
        }

        if (auto* tilesButton = findButton (*editor, "Tiles"))
        {
            tilesButton->triggerClick();
            for (int i = 0; i < 5; ++i)
            {
                editor->createComponentSnapshot (editor->getLocalBounds());    // painting asks for the covers
                pump (300);
            }
            save (*editor, outDir.getChildFile ("library-tiles.png"));
            if (auto* b = findButton (*editor, "List"))
                b->triggerClick();
            pump (200);
        }

        // The Files tab over the same folder: the first folder open, then filtered
        FileBrowser::setRootFolder (libraryDir);
        click ("Files");
        if (auto* tree = findTree (*editor))
            if (auto* rootItem = tree->getRootItem(); rootItem != nullptr && rootItem->getNumSubItems() > 0)
                rootItem->getSubItem (0)->setOpen (true);
        pump (300);
        save (*editor, outDir.getChildFile ("files.png"));
        if (auto filter = juce::SystemStats::getEnvironmentVariable ("KAIA_SNAPSHOT_FILTER", {}); filter.isNotEmpty())
        {
            std::function<SearchBox* (juce::Component&)> findBox = [&findBox] (juce::Component& c) -> SearchBox*
            {
                if (auto* box = dynamic_cast<SearchBox*> (&c); box != nullptr && box->getParentComponent()->isVisible())
                    return box;
                for (auto* child : c.getChildren())
                    if (auto* box = findBox (*child))
                        return box;
                return nullptr;
            };
            if (auto* box = findBox (*editor))
            {
                box->setText (filter, true);
                pump (2000);
                save (*editor, outDir.getChildFile ("files-filter.png"));
                box->setText ({}, true);
                pump (300);
            }
        }
    }
    click ("Settings");
    save (*editor, outDir.getChildFile ("settings.png"));

    running = false;
    audio.join();
    editor.reset();
    processor.releaseResources();
    return 0;
}
