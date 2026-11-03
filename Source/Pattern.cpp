#include "Pattern.h"
#include "Parameters.h"
#include <algorithm>
#include <cmath>

namespace slagg::seq
{
namespace
{
    int posMod (int64_t n, int m) noexcept
    {
        const auto r = (int) (n % m);
        return r < 0 ? r + m : r;
    }
}

void Pattern::clearVoice (int v)
{
    for (int i = 0; i < maxSteps; ++i)
    {
        on[(size_t) v][(size_t) i] = false;
        velocity[(size_t) v][(size_t) i] = 0.8f;
    }
}

void Pattern::clear()
{
    for (int v = 0; v < dsp::numVoices; ++v)
        clearVoice (v);
}

void Pattern::setVoiceFromString (int v, const juce::String& steps)
{
    clearVoice (v);
    for (int i = 0; i < std::min (steps.length(), maxSteps); ++i)
    {
        const auto c = steps[i];
        if (c == 'x' || c == 'o')
        {
            on[(size_t) v][(size_t) i] = true;
            velocity[(size_t) v][(size_t) i] = c == 'x' ? 1.0f : 0.6f;
        }
    }
}

void Pattern::setDefault()
{
    clear();
    setVoiceFromString (dsp::kick,      "x...x...x...x...");
    setVoiceFromString (dsp::clap,      "....x.......x...");
    setVoiceFromString (dsp::closedHat, "o.x.o.x.o.x.o.xo");
    setVoiceFromString (dsp::openHat,   "..............x.");
}

juce::ValueTree Pattern::toTree() const
{
    juce::ValueTree t ("Pattern");
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        juce::String sOn, sVel;
        for (int i = 0; i < maxSteps; ++i)
        {
            sOn << (on[(size_t) v][(size_t) i].load() ? '1' : '0');
            sVel << juce::roundToInt (velocity[(size_t) v][(size_t) i].load() * 100.0f) << ' ';
        }
        juce::ValueTree vt ("Voice");
        vt.setProperty ("id", voiceInfo[(size_t) v].prefix, nullptr);
        vt.setProperty ("on", sOn, nullptr);
        vt.setProperty ("vel", sVel.trim(), nullptr);
        t.appendChild (vt, nullptr);
    }
    return t;
}

void Pattern::fromTree (const juce::ValueTree& t)
{
    clear();
    if (! t.isValid())
        return;
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        const auto vt = t.getChildWithProperty ("id", voiceInfo[(size_t) v].prefix);
        if (! vt.isValid())
            continue;
        const auto sOn = vt.getProperty ("on").toString();
        const auto vels = juce::StringArray::fromTokens (vt.getProperty ("vel").toString(), " ", "");
        for (int i = 0; i < maxSteps; ++i)
        {
            if (i < sOn.length()) on[(size_t) v][(size_t) i] = sOn[i] == '1';
            if (i < vels.size())  velocity[(size_t) v][(size_t) i] = juce::jlimit (0.05f, 1.0f, vels[i].getFloatValue() / 100.0f);
        }
    }
}

//==============================================================================
double stepStart (int64_t n, const Settings& s) noexcept
{
    const double swing = (n % 2 != 0) ? (double) s.swing * s.stepPpq / 3.0 : 0.0;
    return (double) n * s.stepPpq + swing;
}

void generate (const Pattern& p, const Settings& s, double fromPpq, double toPpq, uint32_t voiceMask,
               std::vector<Hit>& out)
{
    if (s.stepPpq <= 0.0 || toPpq <= fromPpq)
        return;

    const auto first = (int64_t) std::floor (fromPpq / s.stepPpq) - 1;
    const auto last  = (int64_t) std::floor (toPpq / s.stepPpq) + 1;
    const auto start = out.size();

    for (int64_t n = first; n <= last; ++n)
    {
        const double t = stepStart (n, s);
        if (t < fromPpq || t >= toPpq)
            continue;
        for (int v = 0; v < dsp::numVoices; ++v)
        {
            if ((voiceMask & (1u << v)) == 0 || ! s.enabled[(size_t) v])
                continue;
            const auto i = (size_t) posMod (n, std::clamp (s.length[(size_t) v], 1, maxSteps));
            if (p.on[(size_t) v][i].load())
                out.push_back ({ t, v, p.velocity[(size_t) v][i].load() });
        }
    }
    std::stable_sort (out.begin() + (long) start, out.end(), [] (const Hit& a, const Hit& b) { return a.ppq < b.ppq; });
}

juce::MidiFile renderMidiFile (const Pattern& p, const Settings& s, double fromPpq, double toPpq, uint32_t voiceMask,
                               double bpm, int timeSigNum, int timeSigDen)
{
    constexpr int tpq = 960;
    constexpr double noteLen = 0.125;   // 1/32
    auto ticks = [fromPpq] (double ppq) { return std::round ((ppq - fromPpq) * tpq); };

    juce::MidiFile file;
    file.setTicksPerQuarterNote (tpq);

    // Spår 0: tempo och taktart.
    juce::MidiMessageSequence conductor;
    auto tempo = juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / std::max (1.0, bpm)));
    tempo.setTimeStamp (0);
    conductor.addEvent (tempo);
    auto sig = juce::MidiMessage::timeSignatureMetaEvent (timeSigNum, timeSigDen);
    sig.setTimeStamp (0);
    conductor.addEvent (sig);
    auto endC = juce::MidiMessage::endOfTrack();
    endC.setTimeStamp (ticks (toPpq));
    conductor.addEvent (endC);
    file.addTrack (conductor);

    // Dragningen ska alltid ge det man ser – även ljud som sekvensern stängt av efter en tidigare dragning.
    auto exportSettings = s;
    exportSettings.enabled.fill (true);

    for (int v = 0; v < dsp::numVoices; ++v)
    {
        if ((voiceMask & (1u << v)) == 0)
            continue;
        std::vector<Hit> hits;
        generate (p, exportSettings, fromPpq, toPpq, 1u << v, hits);
        if (hits.empty())
            continue;

        juce::MidiMessageSequence track;
        auto name = juce::MidiMessage::textMetaEvent (3, voiceInfo[(size_t) v].shortName);   // spårnamn
        name.setTimeStamp (0);
        track.addEvent (name);
        const int note = voiceInfo[(size_t) v].midiNote;
        for (const auto& h : hits)
        {
            auto on = juce::MidiMessage::noteOn (10, note, h.velocity);
            on.setTimeStamp (ticks (h.ppq));
            auto off = juce::MidiMessage::noteOff (10, note);
            off.setTimeStamp (ticks (std::min (h.ppq + noteLen, toPpq)));
            track.addEvent (on);
            track.addEvent (off);
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp (ticks (toPpq));
        track.addEvent (end);
        track.updateMatchedPairs();
        file.addTrack (track);
    }
    return file;
}
} // namespace slagg::seq
