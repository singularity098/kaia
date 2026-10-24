#include "Parsers.h"
#include "SampleSource.h"

namespace kaia
{

//==============================================================================
float mpcEnvTimeMs (double v)
{
    // Logarithmic 1 ms .. 100 s, as used by the MPC software for envelope times.
    if (v <= 0.0)
        return 0.0f;
    return (float) (1.0 * std::exp (juce::jlimit (0.0, 1.0, v) * std::log (100000.0)));
}

float mpcCutoffHz (double v)
{
    // 0 -> ~26 Hz, 0.5 -> ~1.9 kHz, 1.0 -> fully open
    return (float) (880.0 * std::pow (2.0, (juce::jlimit (0.0, 1.0, v) * 140.0 - 57.0) / 12.0));
}

FilterType mpcFilterType (int t)
{
    switch (t)
    {
        case 0:                         return FilterType::Off;
        case 1: case 2:                 return FilterType::LP12;
        case 3: case 4: case 5:         return FilterType::LP24;
        case 6: case 7:                 return FilterType::HP12;
        case 8: case 9: case 10:        return FilterType::HP24;
        case 11:                        return FilterType::BP12;
        case 12: case 13: case 14:      return FilterType::BP24;
        case 15: case 16: case 17: case 18: return FilterType::Notch;
        case 29:                        return FilterType::LP24;   // "MPC3000" low-pass
        default:                        return FilterType::Off;    // formant/boost/model filters: leave the sound unfiltered
    }
}

namespace
{
//==============================================================================
// XML helpers
const juce::XmlElement* child (const juce::XmlElement* e, const char* name)
{
    if (e == nullptr)
        return nullptr;
    if (auto* c = e->getChildByName (name))
        return c;
    for (auto* c : e->getChildIterator())
        if (c->getTagName().equalsIgnoreCase (name))
            return c;
    return nullptr;
}

bool has (const juce::XmlElement* e, const char* name)
{
    auto* c = child (e, name);
    return c != nullptr && c->getAllSubText().trim().isNotEmpty();
}

juce::String text (const juce::XmlElement* e, const char* name, bool trim = true)
{
    auto* c = child (e, name);
    if (c == nullptr)
        return {};
    auto s = c->getAllSubText();
    return trim ? s.trim() : s;
}

double num (const juce::XmlElement* e, const char* name, double def)
{
    auto s = text (e, name);
    if (s.isEmpty())
        return def;
    if (s.equalsIgnoreCase ("true"))  return 1.0;
    if (s.equalsIgnoreCase ("false")) return 0.0;
    return s.getDoubleValue();
}

int inum (const juce::XmlElement* e, const char* name, int def)
{
    return (int) std::lround (num (e, name, def));
}

bool flag (const juce::XmlElement* e, const char* name, bool def)
{
    auto s = text (e, name);
    if (s.isEmpty())
        return def;
    return s.equalsIgnoreCase ("true") || s == "1";
}

float layerGainDb (double linear)
{
    if (linear <= 0.0)
        return -100.0f;
    return (float) juce::jlimit (-100.0, 12.0, 20.0 * std::log10 (linear));
}

int targetNote (int v) { return v > 0 && v < 128 ? v : -1; }

//==============================================================================
void readInstrumentXml (const juce::XmlElement* inst, Pad& pad, bool isKeygroup)
{
    pad.volumeDb  = mpcFaderToDb (num (inst, "Volume", 0.707946));
    pad.pan       = (float) juce::jlimit (-1.0, 1.0, num (inst, "Pan", 0.5) * 2.0 - 1.0);
    pad.mute      = flag (inst, "Mute", false);
    pad.solo      = flag (inst, "Solo", false);
    pad.tuneSemis = (float) (num (inst, "TuneCoarse", 0) + num (inst, "TuneFine", 0) / 100.0);
    pad.mono      = flag (inst, "Mono", false);
    auto poly     = inum (inst, "Polyphony", 0);
    pad.polyphony = (! pad.mono && poly > 1) ? poly : 0;

    switch (inum (inst, "ZonePlay", 1))
    {
        case 0:  pad.layerMode = LayerMode::Cycle; break;
        case 2:  pad.layerMode = LayerMode::Random; break;
        case 3:  pad.layerMode = LayerMode::Cycle; break;
        default: pad.layerMode = LayerMode::Velocity; break;
    }

    pad.muteGroup = juce::jlimit (0, 32, inum (inst, "MuteGroup", 0));
    for (int i = 0; i < 4; ++i)
    {
        auto mt = juce::String ("MuteTarget") + juce::String (i + 1);
        auto st = juce::String ("SimultTarget") + juce::String (i + 1);
        pad.muteTargets[(size_t) i]   = targetNote (inum (inst, mt.toRawUTF8(), 0));
        pad.simultTargets[(size_t) i] = targetNote (inum (inst, st.toRawUTF8(), 0));
    }

    if (has (inst, "TriggerMode"))
    {
        switch (inum (inst, "TriggerMode", 0))
        {
            case 0:  pad.trigger = TriggerMode::OneShot; break;
            case 1:  pad.trigger = TriggerMode::NoteOff; break;
            default: pad.trigger = TriggerMode::NoteOn; break;
        }
    }
    else
    {
        pad.trigger = flag (inst, "OneShot", ! isKeygroup) ? TriggerMode::OneShot : TriggerMode::NoteOn;
    }

    pad.filter      = mpcFilterType (inum (inst, "FilterType", 0));
    pad.cutoffHz    = mpcCutoffHz (num (inst, "Cutoff", 1.0));
    pad.resonance   = (float) juce::jlimit (0.0, 1.0, num (inst, "Resonance", 0.0));
    pad.velToCutoff = (float) juce::jlimit (0.0, 1.0, num (inst, "VelocityToFilter", 0.0));

    pad.env.adMode       = flag (inst, "VolumeADEnvelope", true);
    pad.env.decayFromEnd = flag (inst, "VolumeDecayType", true);
    pad.env.attackMs     = mpcEnvTimeMs (num (inst, "VolumeAttack", 0.0));
    pad.env.holdMs       = mpcEnvTimeMs (num (inst, "VolumeHold", 0.0));
    pad.env.decayMs      = mpcEnvTimeMs (num (inst, "VolumeDecay", 0.047244));
    pad.env.sustain      = (float) juce::jlimit (0.0, 1.0, num (inst, "VolumeSustain", 1.0));
    pad.env.releaseMs    = mpcEnvTimeMs (num (inst, "VolumeRelease", 0.0));
    pad.velToLevel       = (float) juce::jlimit (0.0, 1.0, num (inst, "VelocitySensitivity", 1.0));

    if (isKeygroup)
    {
        pad.noteLow  = juce::jlimit (0, 127, inum (inst, "LowNote", 0));
        pad.noteHigh = juce::jlimit (pad.noteLow, 127, inum (inst, "HighNote", 127));
    }

    auto ignoreBaseNote = flag (inst, "IgnoreBaseNote", false);
    bool anyKeyTrack = false;

    if (auto* layers = child (inst, "Layers"))
    {
        for (auto* le : layers->getChildWithTagNameIterator ("Layer"))
        {
            if ((int) pad.layers.size() >= kMaxLayers)
                break;

            auto sampleName = text (le, "SampleName", false);
            auto sampleFile = text (le, "SampleFile");
            if (sampleName.trim().isEmpty() && sampleFile.isEmpty())
                continue;
            if (! flag (le, "Active", true) || flag (le, "Mute", false))
                continue;

            Layer l;
            l.name      = sampleName.trim().isNotEmpty() ? sampleName : sampleFile;
            l.fileHint  = sampleFile;
            l.gainDb    = layerGainDb (num (le, "Volume", 1.0));
            l.pan       = (float) juce::jlimit (-1.0, 1.0, num (le, "Pan", 0.5) * 2.0 - 1.0);
            l.tuneSemis = has (le, "Pitch") ? (float) num (le, "Pitch", 0.0)
                                            : (float) (num (le, "TuneCoarse", 0) + num (le, "TuneFine", 0) / 100.0);
            l.velLow    = juce::jlimit (0, 127, inum (le, "VelStart", 0));
            l.velHigh   = juce::jlimit (0, 127, inum (le, "VelEnd", 127));
            if (l.velHigh < l.velLow)
                std::swap (l.velLow, l.velHigh);

            auto sliceStart = (juce::int64) num (le, "SliceStart", 0);
            auto sliceEnd   = (juce::int64) num (le, "SliceEnd", 0);
            auto sampStart  = (juce::int64) num (le, "SampleStart", 0);
            auto sampEnd    = (juce::int64) num (le, "SampleEnd", 0);
            if (sliceEnd > sliceStart && sliceStart >= 0)
            {
                l.start = sliceStart;
                l.end   = sliceEnd;
            }
            else if (sampEnd > sampStart && sampStart >= 0)
            {
                l.start = sampStart;
                l.end   = sampEnd;
            }
            auto offset = (juce::int64) num (le, "Offset", 0);
            if (offset > 0)
                l.start += offset;

            l.reverse = inum (le, "Direction", 0) > 0;

            auto sliceLoop = inum (le, "SliceLoop", 0);
            if (flag (le, "Loop", false))
            {
                l.loop      = true;
                l.loopStart = (juce::int64) num (le, "LoopStart", 0);
                l.loopEnd   = (juce::int64) num (le, "LoopEnd", 0);
            }
            else if (sliceLoop > 0)
            {
                l.loop      = true;
                l.loopStart = (juce::int64) num (le, "SliceLoopStart", 0);
                l.loopEnd   = l.end;
            }

            auto root = text (le, "RootNote");
            if (root.isNotEmpty() && root.getIntValue() > 0)
                l.rootNote = juce::jlimit (0, 127, root.getIntValue() - 1);   // stored as MIDI note + 1

            if (flag (le, "KeyTrack", false))
                anyKeyTrack = true;

            pad.layers.push_back (l);
        }
    }

    pad.keyTrack = isKeygroup && (! ignoreBaseNote || anyKeyTrack);
}

bool parseXmlXpm (const juce::String& content, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    auto doc = juce::XmlDocument::parse (content);
    if (doc == nullptr)
    {
        error = "The program file is not valid XML.";
        return false;
    }

    const juce::XmlElement* program = nullptr;
    juce::String fileVersion;
    if (doc->hasTagName ("MPCVObject"))
    {
        program = child (doc.get(), "Program");
        fileVersion = text (child (doc.get(), "Version"), "File_Version");
    }
    else if (doc->hasTagName ("Program"))
    {
        program = doc.get();
    }

    if (program == nullptr)
    {
        error = "No <Program> found. This is not an MPC program file.";
        return false;
    }

    auto type = program->getStringAttribute ("type", "Drum");
    bool isKeygroup = type.equalsIgnoreCase ("Keygroup");
    if (! isKeygroup && ! type.equalsIgnoreCase ("Drum"))
    {
        error = "This is an MPC \"" + type + "\" program. Only Drum and Keygroup programs contain samples.";
        return false;
    }

    kit.isKeygroupProgram = isKeygroup;
    kit.formatName = juce::String ("MPC ") + (isKeygroup ? "Keygroup" : "Drum") + " program (XPM"
                     + (fileVersion.isNotEmpty() ? " " + fileVersion : juce::String()) + ")";
    kit.name = text (program, "ProgramName");
    kit.volumeDb = mpcFaderToDb (num (program, "Volume", 0.707946));
    kit.tuneSemis = (float) (num (program, "TuneCoarse", 0) + num (program, "TuneFine", 0) / 100.0);
    if (has (program, "KeygroupPitchBendRange"))
        kit.pitchBendRange = (float) juce::jlimit (0.0, 48.0, std::round (num (program, "KeygroupPitchBendRange", 0.1667) * 12.0));

    auto* instruments = child (program, "Instruments");
    if (instruments == nullptr)
    {
        error = "The program has no instruments.";
        return false;
    }

    std::vector<const juce::XmlElement*> instList;
    int minNumber = std::numeric_limits<int>::max();
    for (auto* inst : instruments->getChildWithTagNameIterator ("Instrument"))
    {
        instList.push_back (inst);
        minNumber = juce::jmin (minNumber, inst->getIntAttribute ("number", 1));
    }
    bool zeroBased = minNumber == 0;

    if (isKeygroup)
    {
        auto numKeygroups = inum (program, "KeygroupNumKeygroups", (int) instList.size());
        kit.pads.clear();
        for (size_t i = 0; i < instList.size(); ++i)
        {
            auto number = instList[i]->getIntAttribute ("number", (int) i + 1) + (zeroBased ? 1 : 0);
            if (number > numKeygroups)
                continue;
            Pad pad;
            readInstrumentXml (instList[i], pad, true);
            pad.name = "Keygroup " + juce::String (number);
            if (pad.layers.empty())
                continue;
            kit.pads.push_back (std::move (pad));
        }
        if (kit.pads.empty())
        {
            error = "The keygroup program contains no samples.";
            return false;
        }
        return true;
    }

    kit.pads.resize (kMaxPads);
    for (int i = 0; i < kMaxPads; ++i)
        kit.pads[(size_t) i].note = defaultPadNote (i);

    for (auto* inst : instList)
    {
        auto index = inst->getIntAttribute ("number", 1) - (zeroBased ? 0 : 1);
        if (index < 0 || index >= kMaxPads)
            continue;
        auto& pad = kit.pads[(size_t) index];
        auto note = pad.note;
        readInstrumentXml (inst, pad, false);
        pad.note = note;
    }

    if (auto* noteMap = child (program, "PadNoteMap"))
    {
        int minPad = std::numeric_limits<int>::max();
        for (auto* pn : noteMap->getChildWithTagNameIterator ("PadNote"))
            minPad = juce::jmin (minPad, pn->getIntAttribute ("number", 1));
        bool padZeroBased = minPad == 0;

        for (auto* pn : noteMap->getChildWithTagNameIterator ("PadNote"))
        {
            auto index = pn->getIntAttribute ("number", 1) - (padZeroBased ? 0 : 1);
            auto note = inum (pn, "Note", -1);
            if (index >= 0 && index < kMaxPads && note >= 0 && note < 128)
                kit.pads[(size_t) index].note = note;
        }
    }

    bool any = false;
    for (auto& p : kit.pads)
        any = any || ! p.layers.empty();
    if (! any)
        warnings.add ("The drum program has no samples assigned.");
    return true;
}

//==============================================================================
// MPC 3.x programs: gzip-compressed text header + JSON
const juce::var& field (const juce::var& v, const char* name)
{
    if (auto* o = v.getDynamicObject())
        if (o->hasProperty (name))
            return o->getProperty (name);
    static juce::var none;
    return none;
}

double jnum (const juce::var& v, const char* name, double def)
{
    auto& f = field (v, name);
    if (f.isVoid() || f.isUndefined())
        return def;
    if (f.isObject() && field (f, "value0").isVoid() == false)
        return (double) field (f, "value0");
    return (double) f;
}

bool jbool (const juce::var& v, const char* name, bool def)
{
    auto& f = field (v, name);
    if (f.isVoid() || f.isUndefined())
        return def;
    if (f.isObject())
        return (bool) field (f, "value0");
    return (bool) f;
}

juce::String jstr (const juce::var& v, const char* name)
{
    auto& f = field (v, name);
    return f.isVoid() ? juce::String() : f.toString();
}

void readInstrumentJson (const juce::var& inst, Pad& pad, bool isKeygroup)
{
    auto& mix = field (inst, "mixable");
    pad.volumeDb  = mpcFaderToDb (jnum (mix, "volume", 0.707946));
    pad.pan       = (float) juce::jlimit (-1.0, 1.0, jnum (mix, "pan", 0.5) * 2.0 - 1.0);
    pad.mute      = jbool (mix, "mute", false);
    pad.solo      = jbool (mix, "solo", false);
    pad.tuneSemis = (float) (jnum (inst, "coarseTune", 0) + jnum (inst, "fineTune", 0) / 100.0);
    pad.mono      = jbool (inst, "monophonic", false);
    auto poly     = (int) jnum (inst, "polyphony", 0);
    pad.polyphony = (! pad.mono && poly > 1) ? poly : 0;

    auto zonePlay = (int) (field (inst, "zonePlay").isVoid() ? jnum (inst, "zonePlayTime", 1) : jnum (inst, "zonePlay", 1));
    pad.layerMode = zonePlay == 0 || zonePlay == 3 ? LayerMode::Cycle : zonePlay == 2 ? LayerMode::Random : LayerMode::Velocity;

    pad.muteGroup = juce::jlimit (0, 32, (int) jnum (inst, "whichMuteGroup", 0));
    auto& mt = field (inst, "muteTargets");
    auto& st = field (inst, "simultPlayTargets");
    for (int i = 0; i < 4; ++i)
    {
        auto k = ("value" + juce::String (i)).toStdString();
        pad.muteTargets[(size_t) i]   = targetNote ((int) jnum (mt, k.c_str(), 0));
        pad.simultTargets[(size_t) i] = targetNote ((int) jnum (st, k.c_str(), 0));
    }

    switch ((int) jnum (inst, "triggerMode", isKeygroup ? 2 : 0))
    {
        case 0:  pad.trigger = TriggerMode::OneShot; break;
        case 1:  pad.trigger = TriggerMode::NoteOff; break;
        default: pad.trigger = TriggerMode::NoteOn; break;
    }

    auto& synth = field (inst, "synthSection");
    auto& filter = field (field (synth, "filterData"), "value0");
    pad.filter      = mpcFilterType ((int) jnum (filter, "filterType", 0));
    pad.cutoffHz    = mpcCutoffHz (jnum (filter, "filterCutoff", 1.0));
    pad.resonance   = (float) juce::jlimit (0.0, 1.0, jnum (filter, "filterResonance", 0.0));
    pad.velToCutoff = (float) juce::jlimit (0.0, 1.0, jnum (filter, "filterVelocity", 0.0));

    auto& amp = field (synth, "ampEnvelope");
    if (! amp.isVoid())
    {
        pad.env.adMode       = jbool (amp, "AD", true);
        pad.env.decayFromEnd = jbool (amp, "DecayFromEnd", true);
        pad.env.attackMs     = mpcEnvTimeMs (jnum (amp, "Attack", 0.0));
        pad.env.holdMs       = mpcEnvTimeMs (jnum (amp, "Hold", 0.0));
        pad.env.decayMs      = mpcEnvTimeMs (jnum (amp, "Decay", 0.047244));
        pad.env.sustain      = (float) juce::jlimit (0.0, 1.0, jnum (amp, "Sustain", 1.0));
        pad.env.releaseMs    = mpcEnvTimeMs (jnum (amp, "Release", 0.0));
    }

    if (isKeygroup)
    {
        pad.noteLow  = juce::jlimit (0, 127, (int) jnum (inst, "lowNote", 0));
        pad.noteHigh = juce::jlimit (pad.noteLow, 127, (int) jnum (inst, "highNote", 127));
    }

    bool anyKeyTrack = false;
    if (auto* layers = field (inst, "layersv").getArray())
    {
        for (auto& lv : *layers)
        {
            if ((int) pad.layers.size() >= kMaxLayers)
                break;
            auto name = jstr (lv, "sampleName");
            auto file = jstr (lv, "sampleFile");
            if (name.trim().isEmpty() && file.trim().isEmpty())
                continue;
            if (! jbool (lv, "active", true) || jbool (lv, "mute", false))
                continue;

            Layer l;
            l.name     = name.trim().isNotEmpty() ? name : file;
            l.fileHint = file;
            auto& vol  = field (lv, "volume");
            l.gainDb   = layerGainDb (vol.isObject() ? jnum (vol, "gainCoefficient", 1.0) : jnum (lv, "volume", 1.0));
            l.pan      = (float) juce::jlimit (-1.0, 1.0, jnum (lv, "pan", 0.5) * 2.0 - 1.0);
            l.tuneSemis = (float) (jnum (lv, "coarseTune", 0) + jnum (lv, "fineTune", 0) / 100.0 + jnum (lv, "pitch", 0.0));
            l.velLow   = juce::jlimit (0, 127, (int) jnum (lv, "velocityStart", 0));
            l.velHigh  = juce::jlimit (0, 127, (int) jnum (lv, "velocityEnd", 127));
            if (l.velHigh < l.velLow)
                std::swap (l.velLow, l.velHigh);

            auto& slice = field (lv, "sliceInfo");
            auto sliceStart = (juce::int64) jnum (slice, "Start", 0);
            auto sliceEnd   = (juce::int64) jnum (slice, "End", 0);
            auto sampStart  = (juce::int64) jnum (lv, "sampleStart", 0);
            auto sampEnd    = (juce::int64) jnum (lv, "sampleEnd", 0);
            if (sliceEnd > sliceStart && sliceStart >= 0)       { l.start = sliceStart; l.end = sliceEnd; }
            else if (sampEnd > sampStart && sampStart >= 0)     { l.start = sampStart;  l.end = sampEnd; }
            auto offset = (juce::int64) jnum (lv, "offset", 0);
            if (offset > 0)
                l.start += offset;

            l.reverse = jnum (lv, "direction", 0) > 0;

            if (jbool (lv, "layerLoopModeOverridesSliceLoopMode", false))
            {
                if (jnum (lv, "loopMode", 0) > 0)
                {
                    l.loop = true;
                    l.loopStart = (juce::int64) jnum (lv, "loopStart", 0);
                    l.loopEnd   = (juce::int64) jnum (lv, "loopEnd", 0);
                }
            }
            else if (jnum (slice, "LoopMode", 0) > 0)
            {
                l.loop = true;
                l.loopStart = (juce::int64) jnum (slice, "LoopStart", 0);
                l.loopEnd   = sliceEnd;
            }

            auto root = (int) jnum (lv, "rootNote", 0);
            if (root > 0)
                l.rootNote = juce::jlimit (0, 127, root - 1);

            anyKeyTrack = anyKeyTrack || jbool (lv, "keyTrackEnable", false);
            pad.layers.push_back (l);
        }
    }

    pad.keyTrack = isKeygroup && (! jbool (inst, "ignoreBaseNote", false) || anyKeyTrack);
}

bool parseJsonXpm (const juce::MemoryBlock& data, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    juce::MemoryInputStream raw (data, false);
    juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock unzipped;
    gz.readIntoMemoryBlock (unzipped);
    auto content = bytesToText (unzipped);
    if (content.isEmpty())
    {
        error = "Could not decompress the program file.";
        return false;
    }

    // Header: "ACVS", version, data type, encoding, platform, then the JSON document
    auto lines = juce::StringArray::fromLines (content.substring (0, 512));
    if (lines.size() < 5 || lines[0].trim() != "ACVS")
    {
        error = "Unknown MPC program encoding.";
        return false;
    }
    if (lines[3].trim() != "json")
    {
        error = "Unsupported MPC program encoding (" + lines[3].trim() + ").";
        return false;
    }

    int pos = 0;
    for (int i = 0; i < 5; ++i)
        pos = content.indexOf (pos, "\n") + 1;

    auto root = juce::JSON::parse (content.substring (pos));
    auto& dataNode = field (root, "data");
    if (dataNode.isVoid())
    {
        error = "The MPC program contains no data.";
        return false;
    }

    auto dataType = lines[2].trim();
    const juce::var* program = &dataNode;
    if (dataType == "SerialisableTrackData" || ! field (dataNode, "program").isVoid())
        program = &field (dataNode, "program");

    if (dataType == "SerialisableProjectData")
    {
        error = "This is an MPC project, not a program. Export the program from the MPC first.";
        return false;
    }

    auto type = (int) jnum (*program, "type", 0);
    bool isKeygroup = type == 1;
    auto& drum = field (*program, "drum");
    auto* instruments = field (drum, "instruments").getArray();
    if (instruments == nullptr)
    {
        error = "This MPC program type has no samples.";
        return false;
    }

    kit.isKeygroupProgram = isKeygroup;
    kit.formatName = juce::String ("MPC ") + (isKeygroup ? "Keygroup" : "Drum") + " program (XPM " + lines[1].trim() + ")";
    kit.name = jstr (*program, "name");
    kit.volumeDb = mpcFaderToDb (jnum (field (*program, "mixable"), "volume", 0.707946));
    kit.tuneSemis = (float) (jnum (*program, "transpose", 0) + jnum (drum, "coarseTune", 0) + jnum (drum, "fineTune", 0) / 100.0);

    if (isKeygroup)
    {
        auto& kg = field (*program, "keygroup");
        auto numKeygroups = (int) jnum (kg, "numKeygroups", (double) instruments->size());
        kit.tuneSemis += (float) jnum (kg, "transpose", 0);
        auto bendUp = jnum (kg, "pitchBendPositiveRange", 0);
        if (bendUp > 0)
            kit.pitchBendRange = (float) bendUp;

        kit.pads.clear();
        for (int i = 0; i < instruments->size() && i < numKeygroups; ++i)
        {
            Pad pad;
            readInstrumentJson (instruments->getReference (i), pad, true);
            pad.name = "Keygroup " + juce::String (i + 1);
            if (! pad.layers.empty())
                kit.pads.push_back (std::move (pad));
        }
        if (kit.pads.empty())
        {
            error = "The keygroup program contains no samples.";
            return false;
        }
        return true;
    }

    kit.pads.resize (kMaxPads);
    auto& noteForPad = field (field (*program, "padNoteMap"), "noteForPad");
    for (int i = 0; i < kMaxPads; ++i)
    {
        auto& pad = kit.pads[(size_t) i];
        pad.note = defaultPadNote (i);
        auto k = ("value" + juce::String (i)).toStdString();
        if (! field (noteForPad, k.c_str()).isVoid())
            pad.note = juce::jlimit (0, 127, (int) jnum (noteForPad, k.c_str(), pad.note));
        if (i < instruments->size())
        {
            auto note = pad.note;
            readInstrumentJson (instruments->getReference (i), pad, false);
            pad.note = note;
        }
    }

    bool any = false;
    for (auto& p : kit.pads)
        any = any || ! p.layers.empty();
    if (! any)
        warnings.add ("The drum program has no samples assigned.");
    return true;
}

} // namespace

//==============================================================================
bool parseXpm (const juce::MemoryBlock& data, Kit& kit, juce::StringArray& warnings, juce::String& error)
{
    auto* bytes = static_cast<const juce::uint8*> (data.getData());
    if (data.getSize() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b)
        return parseJsonXpm (data, kit, warnings, error);

    auto content = bytesToText (data);
    if (! content.trimStart().startsWith ("<"))
    {
        error = "Unrecognised program file.";
        return false;
    }
    return parseXmlXpm (content, kit, warnings, error);
}

}
