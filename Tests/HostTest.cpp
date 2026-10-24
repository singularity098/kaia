// Loads the *built* plugin binary (VST3 or LV2) the way a DAW does, feeds it a kit and MIDI,
// and checks the audio. Optionally opens the editor for a while so it can be screenshotted.
//
// Usage: KaiaHostTest <plugin path> <program file> <output dir> [--editor-seconds N]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Formats/KitLoader.h"

using namespace kaia;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "\n"; } else std::cout << "  ok: " << msg << "\n"; } while (0)

static juce::MemoryBlock makeState (const Kit& kit)
{
    juce::XmlElement state ("KaiaState");
    state.setAttribute ("version", 1);
    state.addChildElement (kit.toXml().release());
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (state, mb);
    return mb;
}

// A DAW stores a plugin's state inside a format-specific envelope; build that envelope like a host would.
static juce::MemoryBlock wrapForHost (juce::AudioPluginInstance& p, const juce::MemoryBlock& raw)
{
    auto format = p.getPluginDescription().pluginFormatName;
    if (format == "VST3")
    {
        juce::XmlElement x ("VST3PluginState");
        x.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (x, mb);
        return mb;
    }
    if (format == "LV2")
    {
        // Take the plugin's current state (Turtle text) and swap the encoded blob of the state property
        juce::MemoryBlock current;
        p.getStateInformation (current);
        auto text = current.toString();
        auto replacement = raw.toBase64Encoding();
        int pos = 0;
        while ((pos = text.indexOfChar (pos, '"')) >= 0)
        {
            auto end = text.indexOfChar (pos + 1, '"');
            if (end < 0)
                break;
            auto literal = text.substring (pos + 1, end);
            juce::MemoryBlock decoded;
            if (literal.containsChar ('.') && literal.length() > 16 && decoded.fromBase64Encoding (literal)
                 && juce::AudioProcessor::getXmlFromBinary (decoded.getData(), (int) decoded.getSize()) != nullptr)
            {
                text = text.substring (0, pos + 1) + replacement + text.substring (end);
                break;
            }
            pos = end + 1;
        }
        return juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8() + 1);
    }
    return raw;
}

static void setKit (juce::AudioPluginInstance& p, const juce::MemoryBlock& raw)
{
    auto wrapped = wrapForHost (p, raw);
    p.setStateInformation (wrapped.getData(), (int) wrapped.getSize());
}

struct Hit { int sample; int note; int velocity; };

static juce::AudioBuffer<float> renderHits (juce::AudioPluginInstance& p, const std::vector<Hit>& hits, int totalSamples, int block)
{
    auto channels = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> out (channels, totalSamples);
    out.clear();
    juce::AudioBuffer<float> buf (channels, block);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < totalSamples; pos += block)
    {
        auto n = juce::jmin (block, totalSamples - pos);
        buf.setSize (channels, n, false, false, true);
        buf.clear();
        midi.clear();
        for (auto& h : hits)
            if (h.sample >= pos && h.sample < pos + n)
                midi.addEvent (h.velocity > 0 ? juce::MidiMessage::noteOn (1, h.note, (juce::uint8) h.velocity)
                                              : juce::MidiMessage::noteOff (1, h.note), h.sample - pos);
        p.processBlock (buf, midi);
        for (int c = 0; c < channels; ++c)
            out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}

static double rms (const juce::AudioBuffer<float>& b, int channel, int start, int len)
{
    if (channel >= b.getNumChannels())
        return 0.0;
    return b.getRMSLevel (channel, juce::jlimit (0, b.getNumSamples() - 1, start), juce::jmin (len, b.getNumSamples() - start));
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate, int channels = 2)
{
    f.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.release(), rate, (unsigned int) channels, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 4)
    {
        std::cout << "usage: KaiaHostTest <plugin> <program> <outdir> [--editor-seconds N]\n";
        return 2;
    }
    juce::File pluginFile { juce::String (argv[1]) };
    juce::File programFile { juce::String (argv[2]) };
    juce::File outDir { juce::String (argv[3]) };
    outDir.createDirectory();
    double editorSeconds = 0;
    for (int i = 4; i < argc; ++i)
        if (juce::String (argv[i]) == "--editor-seconds" && i + 1 < argc)
            editorSeconds = juce::String (argv[++i]).getDoubleValue();

    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : formats.getFormats())
        if (f->fileMightContainThisPluginType (pluginFile.getFullPathName()))
            f->findAllTypesForFile (types, pluginFile.getFullPathName());

    if (types.isEmpty())
    {
        std::cout << "FAIL: no plugin found in " << pluginFile.getFullPathName() << "\n";
        return 1;
    }

    auto& desc = *types[0];
    std::cout << "Loaded description: " << desc.name << " (" << desc.pluginFormatName << "), "
              << desc.numOutputChannels << " outputs, instrument=" << (desc.isInstrument ? "yes" : "no") << "\n";

    const double rate = 48000.0;
    const int block = 512;
    juce::String error;
    auto plugin = formats.createPluginInstance (desc, rate, block, error);
    if (plugin == nullptr)
    {
        std::cout << "FAIL: could not instantiate: " << error << "\n";
        return 1;
    }

    std::cout << "Initial layout: " << plugin->getTotalNumOutputChannels() << " output channels; buses:";
    for (int i = 0; i < plugin->getBusCount (false); ++i)
        std::cout << " " << plugin->getBus (false, i)->getName() << "=" << plugin->getBus (false, i)->getNumberOfChannels();
    std::cout << "\n";
    CHECK (plugin->acceptsMidi(), "plugin accepts MIDI");
    CHECK (plugin->getBusCount (false) >= 1, "has output buses (" << plugin->getBusCount (false) << ")");

    // The kit, loaded by our own loader and handed to the plugin as saved state (like a DAW project)
    auto loaded = KitLoader::loadProgram ({ programFile, {} });
    if (! loaded.ok)
    {
        std::cout << "FAIL: could not load " << programFile.getFullPathName() << ": " << loaded.error << "\n";
        return 1;
    }
    std::cout << "Kit: " << loaded.kit.name << " (" << loaded.kit.countSamples() << " samples)\n";

    plugin->setRateAndBufferSizeDetails (rate, block);
    plugin->prepareToPlay (rate, block);
    auto state = makeState (loaded.kit);
    setKit (*plugin, state);

    std::vector<Hit> hits;
    int t = block * 4;
    for (auto& pad : loaded.kit.pads)
        if (pad.hasAnySample())
        {
            auto note = pad.isKeygroup() ? (pad.noteLow + pad.noteHigh) / 2 : pad.note;
            hits.push_back ({ t, note, 110 });
            hits.push_back ({ t + (int) (0.1 * rate), note, 0 });
            t += (int) (0.35 * rate);
        }
    auto total = t + (int) rate;

    auto out = renderHits (*plugin, hits, total, block);
    int silent = 0;
    for (auto& h : hits)
        if (h.velocity > 0 && rms (out, 0, h.sample, 4096) + rms (out, 1, h.sample, 4096) < 1e-4)
            ++silent;
    CHECK (silent == 0, "every pad sounds through the plugin binary (" << silent << " silent of " << (int) hits.size() / 2 << ")");

    float peak = out.getMagnitude (0, out.getNumSamples());
    CHECK (std::isfinite (peak) && peak > 0.05f && peak < 4.0f, "sane output level (peak " << peak << ")");
    writeWav (outDir.getChildFile (desc.pluginFormatName + "_render.wav"), out, rate);

    // State round trip into a fresh instance must sound identical
    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    CHECK (saved.getSize() > 100, "plugin saves its state (" << (int) saved.getSize() << " bytes)");
    {
        auto second = formats.createPluginInstance (desc, rate, block, error);
        second->setRateAndBufferSizeDetails (rate, block);
        second->prepareToPlay (rate, block);
        second->setStateInformation (saved.getData(), (int) saved.getSize());
        auto out2 = renderHits (*second, hits, total, block);
        double diff = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
                diff = juce::jmax (diff, (double) std::abs (out.getSample (c, i) - out2.getSample (c, i)));
        CHECK (diff < 1e-5, "restored instance renders identically (max diff " << diff << ")");
        second->releaseResources();
    }

    // Multi-out: route the first pad to Aux 2 with all buses enabled
    if (plugin->getBusCount (false) > 2)
    {
        plugin->releaseResources();
        auto layout = plugin->getBusesLayout();
        for (auto& bus : layout.outputBuses)
            bus = juce::AudioChannelSet::stereo();
        auto ok = plugin->setBusesLayout (layout);
        CHECK (ok, "all " << plugin->getBusCount (false) << " output buses can be enabled (" << plugin->getTotalNumOutputChannels() << " channels)");

        auto kit = loaded.kit;
        int firstPad = -1;
        for (int i = 0; i < (int) kit.pads.size(); ++i)
            if (kit.pads[(size_t) i].hasAnySample() && firstPad < 0)
                firstPad = i;
        kit.pads[(size_t) firstPad].output = 2;
        auto st = makeState (kit);
        plugin->setRateAndBufferSizeDetails (rate, block);
        plugin->prepareToPlay (rate, block);
        setKit (*plugin, st);

        auto& fp = kit.pads[(size_t) firstPad];
        auto note = fp.isKeygroup() ? (fp.noteLow + fp.noteHigh) / 2 : fp.note;
        auto mo = renderHits (*plugin, { { block, note, 120 } }, (int) (0.5 * rate), block);
        // Aux 2 is bus index 2 -> channels 4 and 5
        auto aux = rms (mo, 4, block, 4096) + rms (mo, 5, block, 4096);
        auto main = rms (mo, 0, block, 4096) + rms (mo, 1, block, 4096);
        CHECK (aux > 1e-3 && main < 1e-6, "pad routed to Aux 2 comes out of channels 5/6 only (aux " << aux << ", main " << main << ")");
    }

    // Editor
    if (editorSeconds > 0)
    {
        plugin->releaseResources();
        auto st = makeState (loaded.kit);
        plugin->setRateAndBufferSizeDetails (rate, block);
        plugin->prepareToPlay (rate, block);
        setKit (*plugin, st);

        auto* editor = plugin->createEditorIfNeeded();
        CHECK (editor != nullptr, "editor opens");
        if (editor != nullptr)
        {
            juce::DocumentWindow window ("Kaia", juce::Colours::black, 0);
            window.setUsingNativeTitleBar (false);
            window.setTitleBarHeight (0);
            window.setContentNonOwned (editor, true);
            window.setTopLeftPosition (0, 0);
            window.setVisible (true);

            // Keep audio running in the background so the pads light up
            std::atomic<bool> running { true };
            std::thread audio ([&]
            {
                juce::AudioBuffer<float> b (plugin->getTotalNumOutputChannels(), block);
                juce::MidiBuffer m;
                int counter = 0;
                while (running)
                {
                    b.clear();
                    m.clear();
                    if (counter % 20 == 0 && ! hits.empty())
                    {
                        auto& h = hits[(size_t) ((counter / 20 * 2) % (int) hits.size())];
                        m.addEvent (juce::MidiMessage::noteOn (1, h.note, (juce::uint8) 120), 0);
                    }
                    ++counter;
                    plugin->processBlock (b, m);
                    std::this_thread::sleep_for (std::chrono::milliseconds (10));
                }
            });

            auto end = juce::Time::getMillisecondCounter() + (juce::uint32) (editorSeconds * 1000.0);
            while (juce::Time::getMillisecondCounter() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            running = false;
            audio.join();
            window.clearContentComponent();
            delete editor;
        }
    }

    plugin->releaseResources();
    plugin.reset();
    std::cout << (failures == 0 ? "ALL HOST CHECKS PASSED\n" : "HOST CHECKS FAILED\n");
    return failures == 0 ? 0 : 1;
}
