#include "Kit.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace kaia
{

namespace
{
    // MPC1000/2500/MPC software default pad map for banks A-D (notes 35..98).
    constexpr int kBankABCD[64] = {
        37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53,   // A
        54, 69, 81, 80, 65, 66, 76, 77, 56, 62, 63, 64, 73, 74, 71, 39,   // B
        52, 57, 58, 59, 60, 61, 67, 68, 70, 72, 75, 78, 79, 35, 41, 50,   // C
        83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98    // D
    };

    juce::String b2s (bool b) { return b ? "1" : "0"; }
}

SampleRef SampleRef::fromString (const juce::String& s)
{
    SampleRef r;
    auto bar = s.lastIndexOfChar ('|');
    if (bar > 0)
    {
        r.file  = s.substring (0, bar);
        r.entry = s.substring (bar + 1);
    }
    else
    {
        r.file = s;
    }
    return r;
}

juce::String SampleRef::displayName() const
{
    auto path = entry.isNotEmpty() ? entry : file;
    auto name = path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
    return name.upToLastOccurrenceOf (".", false, false);
}

int defaultPadNote (int padIndex)
{
    if (padIndex >= 0 && padIndex < 64)
        return kBankABCD[padIndex];

    // Banks E-H: the notes not used by A-D, ascending (0..34 then 99..127).
    int idx = padIndex - 64;
    if (idx >= 0 && idx < 35)
        return idx;
    if (idx >= 35 && idx < 64)
        return 99 + (idx - 35);
    return -1;
}

juce::String padName (int padIndex)
{
    if (padIndex < 0)
        return "--";
    auto bank = (char) ('A' + padIndex / kPadsPerBank);
    auto num = padIndex % kPadsPerBank + 1;
    return juce::String::charToString (bank) + juce::String (num).paddedLeft ('0', 2);
}

juce::String noteName (int midiNote)
{
    if (midiNote < 0 || midiNote > 127)
        return "--";
    // MPC/Cubase-style octave numbering: note 60 = C3
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3) + " (" + juce::String (midiNote) + ")";
}

float mpcFaderToDb (double value)
{
    // The MPC stores levels as a fader position where gain = 2 * v^2:
    // 0.353 -> -12 dB, 0.707946 -> 0 dB, 1.0 -> +6 dB.
    if (value <= 0.0)
        return -100.0f;
    auto gain = 2.0 * value * value;
    return (float) juce::jlimit (-100.0, 12.0, 20.0 * std::log10 (gain));
}

double dbToGain (double db)
{
    return db <= -99.0 ? 0.0 : std::pow (10.0, db / 20.0);
}

Kit::Kit() = default;

Kit Kit::makeEmpty()
{
    Kit k;
    k.name = "Empty Kit";
    k.pads.resize (kMaxPads);
    for (int i = 0; i < kMaxPads; ++i)
        k.pads[(size_t) i].note = defaultPadNote (i);
    return k;
}

int Kit::countSamples() const
{
    int n = 0;
    for (auto& p : pads)
        for (auto& l : p.layers)
            if (l.hasSample() && ! l.missing)
                ++n;
    return n;
}

int Kit::countMissing() const
{
    int n = 0;
    for (auto& p : pads)
        for (auto& l : p.layers)
            if (l.missing)
                ++n;
    return n;
}

std::vector<SampleRef> Kit::allSampleRefs() const
{
    std::vector<SampleRef> refs;
    for (auto& p : pads)
        for (auto& l : p.layers)
            if (l.hasSample() && std::find (refs.begin(), refs.end(), l.sample) == refs.end())
                refs.push_back (l.sample);
    return refs;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Kit::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("Kit");
    xml->setAttribute ("name", name);
    xml->setAttribute ("source", sourcePath);
    xml->setAttribute ("format", formatName);
    xml->setAttribute ("keygroup", b2s (isKeygroupProgram));
    xml->setAttribute ("volumeDb", volumeDb);
    xml->setAttribute ("tune", tuneSemis);
    xml->setAttribute ("bendRange", pitchBendRange);

    for (size_t i = 0; i < pads.size(); ++i)
    {
        auto& p = pads[i];
        auto* pe = xml->createNewChildElement ("Pad");
        pe->setAttribute ("index", (int) i);
        pe->setAttribute ("name", p.name);
        pe->setAttribute ("note", p.note);
        pe->setAttribute ("noteLow", p.noteLow);
        pe->setAttribute ("noteHigh", p.noteHigh);
        pe->setAttribute ("keyTrack", b2s (p.keyTrack));
        pe->setAttribute ("root", p.rootNote);
        pe->setAttribute ("layerMode", (int) p.layerMode);
        pe->setAttribute ("trigger", (int) p.trigger);
        pe->setAttribute ("volumeDb", p.volumeDb);
        pe->setAttribute ("pan", p.pan);
        pe->setAttribute ("tune", p.tuneSemis);
        pe->setAttribute ("velToLevel", p.velToLevel);
        pe->setAttribute ("envAD", b2s (p.env.adMode));
        pe->setAttribute ("decayFromEnd", b2s (p.env.decayFromEnd));
        pe->setAttribute ("attack", p.env.attackMs);
        pe->setAttribute ("hold", p.env.holdMs);
        pe->setAttribute ("decay", p.env.decayMs);
        pe->setAttribute ("sustain", p.env.sustain);
        pe->setAttribute ("release", p.env.releaseMs);
        pe->setAttribute ("filter", (int) p.filter);
        pe->setAttribute ("cutoff", p.cutoffHz);
        pe->setAttribute ("resonance", p.resonance);
        pe->setAttribute ("velToCutoff", p.velToCutoff);
        pe->setAttribute ("muteGroup", p.muteGroup);
        juce::StringArray mt, st;
        for (auto t : p.muteTargets) mt.add (juce::String (t));
        for (auto t : p.simultTargets) st.add (juce::String (t));
        pe->setAttribute ("muteTargets", mt.joinIntoString (","));
        pe->setAttribute ("simultTargets", st.joinIntoString (","));
        pe->setAttribute ("mono", b2s (p.mono));
        pe->setAttribute ("polyphony", p.polyphony);
        pe->setAttribute ("output", p.output);
        pe->setAttribute ("mute", b2s (p.mute));
        pe->setAttribute ("solo", b2s (p.solo));

        for (auto& l : p.layers)
        {
            auto* le = pe->createNewChildElement ("Layer");
            le->setAttribute ("sample", l.sample.toString());
            le->setAttribute ("name", l.name);
            le->setAttribute ("enabled", b2s (l.enabled));
            le->setAttribute ("gainDb", l.gainDb);
            le->setAttribute ("pan", l.pan);
            le->setAttribute ("tune", l.tuneSemis);
            le->setAttribute ("velLow", l.velLow);
            le->setAttribute ("velHigh", l.velHigh);
            le->setAttribute ("start", juce::String (l.start));
            le->setAttribute ("end", juce::String (l.end));
            le->setAttribute ("reverse", b2s (l.reverse));
            le->setAttribute ("loop", b2s (l.loop));
            le->setAttribute ("loopStart", juce::String (l.loopStart));
            le->setAttribute ("loopEnd", juce::String (l.loopEnd));
            le->setAttribute ("root", l.rootNote);
        }
    }
    return xml;
}

Kit Kit::fromXml (const juce::XmlElement& xml)
{
    Kit k;
    k.name              = xml.getStringAttribute ("name");
    k.sourcePath        = xml.getStringAttribute ("source");
    k.formatName        = xml.getStringAttribute ("format");
    k.isKeygroupProgram = xml.getBoolAttribute ("keygroup");
    k.volumeDb          = (float) xml.getDoubleAttribute ("volumeDb", 0.0);
    k.tuneSemis         = (float) xml.getDoubleAttribute ("tune", 0.0);
    k.pitchBendRange    = (float) xml.getDoubleAttribute ("bendRange", 2.0);

    if (! k.isKeygroupProgram)
    {
        k.pads.resize (kMaxPads);
        for (int i = 0; i < kMaxPads; ++i)
            k.pads[(size_t) i].note = defaultPadNote (i);
    }

    auto parseTargets = [] (const juce::String& s, std::array<int, 4>& out)
    {
        auto toks = juce::StringArray::fromTokens (s, ",", "");
        for (int i = 0; i < 4; ++i)
            out[(size_t) i] = i < toks.size() ? toks[i].getIntValue() : -1;
    };

    for (auto* pe : xml.getChildWithTagNameIterator ("Pad"))
    {
        auto index = pe->getIntAttribute ("index", -1);
        if (index < 0 || index >= 4096)
            continue;
        if ((size_t) index >= k.pads.size())
            k.pads.resize ((size_t) index + 1);

        auto& p = k.pads[(size_t) index];
        p.name        = pe->getStringAttribute ("name");
        p.note        = pe->getIntAttribute ("note", p.note);
        p.noteLow     = pe->getIntAttribute ("noteLow", -1);
        p.noteHigh    = pe->getIntAttribute ("noteHigh", -1);
        p.keyTrack    = pe->getBoolAttribute ("keyTrack");
        p.rootNote    = pe->getIntAttribute ("root", 60);
        p.layerMode   = (LayerMode) juce::jlimit (0, 2, pe->getIntAttribute ("layerMode"));
        p.trigger     = (TriggerMode) juce::jlimit (0, 2, pe->getIntAttribute ("trigger"));
        p.volumeDb    = (float) pe->getDoubleAttribute ("volumeDb");
        p.pan         = (float) pe->getDoubleAttribute ("pan");
        p.tuneSemis   = (float) pe->getDoubleAttribute ("tune");
        p.velToLevel  = (float) pe->getDoubleAttribute ("velToLevel", 1.0);
        p.env.adMode       = pe->getBoolAttribute ("envAD", true);
        p.env.decayFromEnd = pe->getBoolAttribute ("decayFromEnd", true);
        p.env.attackMs     = (float) pe->getDoubleAttribute ("attack");
        p.env.holdMs       = (float) pe->getDoubleAttribute ("hold");
        p.env.decayMs      = (float) pe->getDoubleAttribute ("decay", 2.0);
        p.env.sustain      = (float) pe->getDoubleAttribute ("sustain", 1.0);
        p.env.releaseMs    = (float) pe->getDoubleAttribute ("release", 2.0);
        p.filter      = (FilterType) juce::jlimit (0, 7, pe->getIntAttribute ("filter"));
        p.cutoffHz    = (float) pe->getDoubleAttribute ("cutoff", 20000.0);
        p.resonance   = (float) pe->getDoubleAttribute ("resonance");
        p.velToCutoff = (float) pe->getDoubleAttribute ("velToCutoff");
        p.muteGroup   = pe->getIntAttribute ("muteGroup");
        parseTargets (pe->getStringAttribute ("muteTargets"), p.muteTargets);
        parseTargets (pe->getStringAttribute ("simultTargets"), p.simultTargets);
        p.mono        = pe->getBoolAttribute ("mono");
        p.polyphony   = pe->getIntAttribute ("polyphony");
        p.output      = juce::jlimit (0, kNumAuxOutputs, pe->getIntAttribute ("output"));
        p.mute        = pe->getBoolAttribute ("mute");
        p.solo        = pe->getBoolAttribute ("solo");

        p.layers.clear();
        for (auto* le : pe->getChildWithTagNameIterator ("Layer"))
        {
            if ((int) p.layers.size() >= kMaxLayers)
                break;
            Layer l;
            l.sample    = SampleRef::fromString (le->getStringAttribute ("sample"));
            l.name      = le->getStringAttribute ("name");
            l.enabled   = le->getBoolAttribute ("enabled", true);
            l.gainDb    = (float) le->getDoubleAttribute ("gainDb");
            l.pan       = (float) le->getDoubleAttribute ("pan");
            l.tuneSemis = (float) le->getDoubleAttribute ("tune");
            l.velLow    = le->getIntAttribute ("velLow", 0);
            l.velHigh   = le->getIntAttribute ("velHigh", 127);
            l.start     = le->getStringAttribute ("start").getLargeIntValue();
            l.end       = le->getStringAttribute ("end").getLargeIntValue();
            l.reverse   = le->getBoolAttribute ("reverse");
            l.loop      = le->getBoolAttribute ("loop");
            l.loopStart = le->getStringAttribute ("loopStart").getLargeIntValue();
            l.loopEnd   = le->getStringAttribute ("loopEnd").getLargeIntValue();
            l.rootNote  = le->getIntAttribute ("root", -1);
            p.layers.push_back (l);
        }
    }
    return k;
}

}
