// Renders the real plugin editor to PNG files (for documentation and visual checks).
// Usage: KaiaUiSnapshot <program/pack/folder> <out dir> [pad index] [library folder]
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
                if (auto* heading = rootItem->getSubItem (rootItem->getNumSubItems() - 1))
                    if (auto* pack = heading->getSubItem (0))
                    {
                        pack->setOpen (true);
                        for (int i = 0; i < pack->getNumSubItems(); ++i)
                            pack->getSubItem (i)->setOpen (true);
                    }
        pump (300);
        save (*editor, outDir.getChildFile ("library.png"));
    }
    click ("Settings");
    save (*editor, outDir.getChildFile ("settings.png"));

    running = false;
    audio.join();
    editor.reset();
    processor.releaseResources();
    return 0;
}
