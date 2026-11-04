#include "Pattern.h"
#include "Parameters.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace slagg::seq
{
namespace
{
    int posMod (int64_t n, int m) noexcept
    {
        const auto r = (int) (n % m);
        return r < 0 ? r + m : r;
    }

    uint64_t splitmix (uint64_t x) noexcept
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    // Deterministiskt "slumptal" 0..1 för (frö, ljud, steg, syfte).
    double hash01 (uint32_t seed, int voice, int64_t step, uint32_t salt) noexcept
    {
        const auto h = splitmix (splitmix ((uint64_t) seed * 0x100000001B3ull ^ (uint64_t) (uint32_t) (voice + 1) * 0x9E37ull ^ salt)
                                 ^ (uint64_t) step);
        return (double) (h >> 11) * (1.0 / 9007199254740992.0);
    }

    template <typename T>
    juce::String join (const Grid<T>& g, int v, std::function<juce::String (T)> f)
    {
        juce::String s;
        for (int i = 0; i < maxSteps; ++i)
            s << f (g[(size_t) v][(size_t) i].load()) << ' ';
        return s.trim();
    }
}

void Pattern::clearVoice (int v)
{
    for (size_t i = 0; i < (size_t) maxSteps; ++i)
    {
        on[(size_t) v][i] = false;
        velocity[(size_t) v][i] = 0.8f;
        probability[(size_t) v][i] = 1.0f;
        ratchet[(size_t) v][i] = 1;
        nudge[(size_t) v][i] = 0.0f;
    }
}

void Pattern::clear()
{
    for (int v = 0; v < dsp::numVoices; ++v)
        clearVoice (v);
}

void Pattern::setVoiceFromString (int v, const juce::String& text)
{
    clearVoice (v);
    juce::StringArray tokens;
    if (text.containsAnyOf (" ?*<>"))
    {
        tokens = juce::StringArray::fromTokens (text, " ", "");
        tokens.removeEmptyStrings();
    }
    else
    {
        for (int i = 0; i < text.length(); ++i)
            tokens.add (juce::String::charToString (text[i]));
    }

    for (int i = 0; i < std::min (tokens.size(), maxSteps); ++i)
    {
        const auto t = tokens[i];
        const auto c = t[0];
        float vel = 0.0f;
        if (c == 'X') vel = 1.0f;
        else if (c == 'x') vel = 0.78f;
        else if (c == 'o') vel = 0.55f;
        else if (c == '-') vel = 0.32f;
        if (vel <= 0.0f)
            continue;

        const auto idx = (size_t) i;
        on[(size_t) v][idx] = true;
        velocity[(size_t) v][idx] = vel;
        if (const int q = t.indexOfChar ('?'); q >= 0)
            probability[(size_t) v][idx] = juce::jlimit (0.0f, 1.0f, t.substring (q + 1).getFloatValue() / 100.0f);
        if (const int r = t.indexOfChar ('*'); r >= 0)
            ratchet[(size_t) v][idx] = juce::jlimit (1, maxRatchet, t.substring (r + 1).getIntValue());
        if (const int n = t.indexOfChar ('>'); n >= 0)
            nudge[(size_t) v][idx] = juce::jlimit (-0.5f, 0.5f, t.substring (n + 1).getFloatValue() / 100.0f);
        if (const int n = t.indexOfChar ('<'); n >= 0)
            nudge[(size_t) v][idx] = juce::jlimit (-0.5f, 0.5f, -t.substring (n + 1).getFloatValue() / 100.0f);
    }
}

void Pattern::setDefault()
{
    clear();
    seed = 1;
    setVoiceFromString (dsp::kick,      "X . . . X . . . X . . . X . . .");
    setVoiceFromString (dsp::clap,      ". . . . X . . . . . . . X . . .");
    setVoiceFromString (dsp::closedHat, "o . x . o . x . o . x . o . x o?50");
    setVoiceFromString (dsp::openHat,   ". . . . . . . . . . . . . . x .");
}

juce::ValueTree Pattern::toTree() const
{
    juce::ValueTree t ("Pattern");
    t.setProperty ("seed", (int) seed.load(), nullptr);
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        juce::String sOn;
        for (int i = 0; i < maxSteps; ++i)
            sOn << (on[(size_t) v][(size_t) i].load() ? '1' : '0');
        auto pct = [] (float x) { return juce::String (juce::roundToInt (x * 100.0f)); };
        juce::ValueTree vt ("Voice");
        vt.setProperty ("id", voiceInfo[(size_t) v].prefix, nullptr);
        vt.setProperty ("on", sOn, nullptr);
        vt.setProperty ("vel", join<float> (velocity, v, pct), nullptr);
        vt.setProperty ("prob", join<float> (probability, v, pct), nullptr);
        vt.setProperty ("rat", join<int> (ratchet, v, [] (int x) { return juce::String (x); }), nullptr);
        vt.setProperty ("nudge", join<float> (nudge, v, pct), nullptr);
        t.appendChild (vt, nullptr);
    }
    return t;
}

void Pattern::fromTree (const juce::ValueTree& t)
{
    clear();
    if (! t.isValid())
        return;
    seed = (uint32_t) (int) t.getProperty ("seed", 1);
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        const auto vt = t.getChildWithProperty ("id", voiceInfo[(size_t) v].prefix);
        if (! vt.isValid())
            continue;
        const auto sOn = vt.getProperty ("on").toString();
        auto list = [&vt] (const char* key) { return juce::StringArray::fromTokens (vt.getProperty (key).toString(), " ", ""); };
        const auto vels = list ("vel"), probs = list ("prob"), rats = list ("rat"), nudges = list ("nudge");
        for (int i = 0; i < maxSteps; ++i)
        {
            const auto idx = (size_t) i;
            if (i < sOn.length())    on[(size_t) v][idx] = sOn[i] == '1';
            if (i < vels.size())     velocity[(size_t) v][idx] = juce::jlimit (0.05f, 1.0f, vels[i].getFloatValue() / 100.0f);
            // Saknas i sparat läge från v0.1 -> standardvärden (spelar alltid, ett slag, i takt).
            if (i < probs.size())    probability[(size_t) v][idx] = juce::jlimit (0.0f, 1.0f, probs[i].getFloatValue() / 100.0f);
            if (i < rats.size())     ratchet[(size_t) v][idx] = juce::jlimit (1, maxRatchet, rats[i].getIntValue());
            if (i < nudges.size())   nudge[(size_t) v][idx] = juce::jlimit (-0.5f, 0.5f, nudges[i].getFloatValue() / 100.0f);
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

    // Marginal: nudge/humanize kan flytta ett slag upp till ett halvt steg åt båda håll.
    const auto first = (int64_t) std::floor (fromPpq / s.stepPpq) - 2;
    const auto last  = (int64_t) std::floor (toPpq / s.stepPpq) + 2;
    const auto start = out.size();
    const uint32_t seed = p.seed.load();
    const double h = std::clamp ((double) s.humanize, 0.0, 1.0);

    for (int64_t n = first; n <= last; ++n)
    {
        for (int v = 0; v < dsp::numVoices; ++v)
        {
            if ((voiceMask & (1u << v)) == 0 || ! s.enabled[(size_t) v])
                continue;
            const auto i = (size_t) posMod (n, std::clamp (s.length[(size_t) v], 1, maxSteps));
            if (! p.on[(size_t) v][i].load())
                continue;

            const float prob = p.probability[(size_t) v][i].load();
            if (prob < 1.0f && hash01 (seed, v, n, 0x50524F42u) >= (double) prob)
                continue;

            double vel = p.velocity[(size_t) v][i].load();
            double t0 = stepStart (n, s) + (double) p.nudge[(size_t) v][i].load() * s.stepPpq;
            if (h > 0.0)
            {
                vel += (hash01 (seed, v, n, 0x56454C00u) - 0.5) * 2.0 * h * 0.25;
                t0 += (hash01 (seed, v, n, 0x54494D45u) - 0.5) * 2.0 * h * 0.06 * s.stepPpq;
            }
            vel = std::clamp (vel, 0.05, 1.0);

            const int r = std::clamp (p.ratchet[(size_t) v][i].load(), 1, maxRatchet);
            for (int k = 0; k < r; ++k)
            {
                const double t = t0 + (double) k * s.stepPpq / (double) r;
                if (t < fromPpq || t >= toPpq)
                    continue;
                out.push_back ({ t, v, (float) std::max (0.05, vel * std::pow (0.82, (double) k)) });
            }
        }
    }
    std::stable_sort (out.begin() + (long) start, out.end(), [] (const Hit& a, const Hit& b) { return a.ppq < b.ppq; });
}

juce::MidiFile renderMidiFile (const Pattern& p, const Settings& s, double fromPpq, double toPpq, uint32_t voiceMask,
                               double bpm, int timeSigNum, int timeSigDen)
{
    constexpr int tpq = 960;
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
    const double maxLen = 0.125;   // 1/32

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
        for (size_t k = 0; k < hits.size(); ++k)
        {
            const auto& h = hits[k];
            // Tonen slutar före nästa slag (ratchet) och senast vid exportområdets slut.
            double end = std::min (h.ppq + maxLen, toPpq);
            if (k + 1 < hits.size())
                end = std::min (end, hits[k + 1].ppq - 1.0 / tpq);
            end = std::max (end, h.ppq + 1.0 / tpq);
            auto on = juce::MidiMessage::noteOn (10, note, h.velocity);
            on.setTimeStamp (ticks (h.ppq));
            auto off = juce::MidiMessage::noteOff (10, note);
            off.setTimeStamp (ticks (end));
            track.addEvent (on);
            track.addEvent (off);
        }
        auto endT = juce::MidiMessage::endOfTrack();
        endT.setTimeStamp (ticks (toPpq));
        track.addEvent (endT);
        track.updateMatchedPairs();
        file.addTrack (track);
    }
    return file;
}
} // namespace slagg::seq
