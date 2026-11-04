// Slagg – automatiska tester. Körs lokalt och i CI (Linux).
//   SlaggTests [--wav-dir DIR] [--screenshot FILE]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Arrangement.h"
#include <iostream>
#include <map>
#include <set>

using namespace slagg;

namespace
{
int failures = 0;

void check (bool ok, const juce::String& rawWhat)
{
    // Literalerna i den här filen är UTF-8, men juce::String (const char*) läser dem tecken för tecken
    // (Latin-1). Packa tillbaka till bytes och läs som UTF-8 så att åäö blir rätt i loggen.
    std::string bytes;
    for (auto c : rawWhat)
        bytes.push_back ((char) (c < 256 ? c : '?'));
    const auto what = juce::String::fromUTF8 (bytes.data(), (int) bytes.size());
    if (! ok)
    {
        ++failures;
        std::cout << "  FEL: " << what << std::endl;
    }
}

std::unique_ptr<SlaggProcessor> makeProc()
{
    return std::unique_ptr<SlaggProcessor> (dynamic_cast<SlaggProcessor*> (createPluginFilter()));
}

void setParam (SlaggProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

struct PlayHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 128, loopStart = 4.0, loopEnd = 20.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setLoopPoints (LoopPoints { loopStart, loopEnd });
        return p;
    }
};

void writeWav (const juce::File& file, const juce::AudioBuffer<float>& b, double sr)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    if (out == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (out, opts))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct RenderResult { float peak = 0; float tailRms = 0; bool finite = true; };

// Spelar en ton (MIDI) och renderar main-utgången.
RenderResult renderVoice (SlaggProcessor& p, int voice, double sr, int block, double seconds, juce::AudioBuffer<float>* capture)
{
    p.setPlayHead (nullptr);
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    if (capture != nullptr)
        capture->setSize (2, total);
    RenderResult r;
    double tail = 0;
    int tailN = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (buf.getNumChannels(), n, false, false, true);
        midi.clear();
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, voiceInfo[(size_t) voice].midiNote, 1.0f), 0);
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                r.finite = r.finite && std::isfinite (d[i]);
                r.peak = std::max (r.peak, std::abs (d[i]));
                if (pos + i > total - (int) (0.3 * sr)) { tail += d[i] * d[i]; ++tailN; }
            }
            if (capture != nullptr)
                capture->copyFrom (ch, pos, buf, ch, 0, n);
        }
    }
    r.tailRms = (float) std::sqrt (tail / std::max (1, tailN));
    return r;
}

void testVoices (const juce::File& wavDir)
{
    std::cout << "Ljud:" << std::endl;
    auto p = makeProc();
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        juce::AudioBuffer<float> cap;
        const auto r = renderVoice (*p, v, 44100.0, 256, 6.0, wavDir.exists() ? &cap : nullptr);
        const auto r96 = renderVoice (*p, v, 96000.0, 333, 6.0, nullptr);
        const juce::String name (voiceInfo[(size_t) v].shortName);
        std::cout << "  " << name.paddedRight (' ', 11) << " peak " << juce::String (r.peak, 3) << std::endl;
        check (r.finite && r96.finite, name + ": NaN/Inf");
        check (r.peak <= 1.0f && r96.peak <= 1.0f, name + ": klipper");
        check (r.peak > 0.05f, name + ": för tyst (" + juce::String (r.peak) + ")");
        check (r.tailRms < 1.0e-4f && r96.tailRms < 1.0e-4f, name + ": klingar fortfarande efter 6 s");
        if (wavDir.exists())
            writeWav (wavDir.getChildFile (juce::String (v + 1).paddedLeft ('0', 2) + " " + name + ".wav"), cap, 44100.0);
    }

    // Som en stresstest: slumpade inställningar, alla ljud samtidigt, många slag.
    juce::Random rnd (99);
    float worst = 0;
    bool finite = true;
    for (int round = 0; round < 30; ++round)
    {
        for (auto* prm : p->getParameters())
            prm->setValueNotifyingHost (rnd.nextFloat());
        setParam (*p, pid::master, -6.0f);
        p->prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> buf (p->getTotalNumOutputChannels(), 128);
        juce::MidiBuffer midi;
        for (int b = 0; b < 200; ++b)
        {
            midi.clear();
            if (b % 7 == 0)
                for (const auto& info : voiceInfo)
                    midi.addEvent (juce::MidiMessage::noteOn (10, info.midiNote, rnd.nextFloat()), rnd.nextInt (128));
            p->processBlock (buf, midi);
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < 128; ++i)
                {
                    const float x = buf.getSample (ch, i);
                    finite = finite && std::isfinite (x);
                    worst = std::max (worst, std::abs (x));
                }
        }
    }
    check (finite, "stresstest: NaN/Inf");
    check (worst <= 1.0f, "stresstest: klipper (" + juce::String (worst) + ")");
    std::cout << "  stresstest 30 rundor ok, topp " << juce::String (worst, 3) << std::endl;
}

void testOutputs()
{
    std::cout << "Utgångar:" << std::endl;
    auto p = makeProc();
    check (p->getBusCount (false) == 1 + dsp::numVoices, "fel antal utgångar");

    auto energy = [] (const juce::AudioBuffer<float>& b, int firstCh)
    {
        double e = 0;
        for (int ch = firstCh; ch < firstCh + 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                e += b.getSample (ch, i) * b.getSample (ch, i);
        return e;
    };
    auto hit = [&] (int voice)
    {
        p->prepareToPlay (48000.0, 2048);
        juce::AudioBuffer<float> buf (p->getTotalNumOutputChannels(), 2048);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, voiceInfo[(size_t) voice].midiNote, 1.0f), 0);
        p->processBlock (buf, midi);
        return buf;
    };

    // Main som standard.
    auto a = hit (dsp::snare);
    check (energy (a, 0) > 1.0 && energy (a, 2 * (1 + dsp::snare)) < 1.0e-9, "virveln hamnar inte på Main som standard");

    // Egen utgång.
    setParam (*p, pid::id (dsp::snare, pid::sOut), 1.0f);
    auto b = hit (dsp::snare);
    check (energy (b, 0) < 1.0e-9, "virveln läcker till Main fast Out = Own");
    check (energy (b, 2 * (1 + dsp::snare)) > 1.0, "virveln hörs inte på sin egen utgång");

    // Choke: stängd hihat stryper den öppna.
    setParam (*p, pid::id (dsp::openHat, pid::sOut), 1.0f);
    setParam (*p, pid::id (dsp::openHat, pid::sDecay), 1.0f);
    p->prepareToPlay (48000.0, 4800);
    juce::AudioBuffer<float> buf (p->getTotalNumOutputChannels(), 4800);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 46, 1.0f), 0);
    p->processBlock (buf, midi);
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (10, 42, 1.0f), 0);
    p->processBlock (buf, midi);   // 100 ms in: stäng
    midi.clear();
    p->processBlock (buf, midi);   // 100 ms senare ska den öppna vara tyst
    check (energy (buf, 2 * (1 + dsp::openHat)) < 1.0e-6, "stängd hihat stryper inte den öppna");
    std::cout << "  Main/egen utgång och choke ok" << std::endl;
}

int countNotes (const juce::MidiFile& f, int track)
{
    int n = 0;
    if (track < f.getNumTracks())
        for (const auto* e : *f.getTrack (track))
            n += e->message.isNoteOn() ? 1 : 0;
    return n;
}

void testSequencer()
{
    std::cout << "Sekvenserare:" << std::endl;
    auto p = makeProc();
    PlayHead ph;
    const double sr = 48000.0;
    const int block = 256;
    p->setPlayHead (&ph);
    p->setRateAndBufferSizeDetails (sr, block);
    p->prepareToPlay (sr, block);

    // Polymeter: virvel med 3 steg, slag på steg 1.
    p->getPattern().setVoiceFromString (dsp::snare, "x..");
    setParam (*p, pid::id (dsp::snare, pid::sLen), 3.0f);
    setParam (*p, pid::swing, 0.4f);

    // Spela takt 1–4 (16 kvartsnoter).
    juce::AudioBuffer<float> buf (p->getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    std::array<uint32_t, dsp::numVoices> before {};
    for (int v = 0; v < dsp::numVoices; ++v) before[(size_t) v] = p->getHitCounter (v);
    const double ppqPerSample = ph.bpm / 60.0 / sr;
    const int total = (int) std::ceil (16.0 / ppqPerSample);
    ph.playing = true;
    for (int pos = 0; pos < total; pos += block)
    {
        ph.ppq = pos * ppqPerSample;
        midi.clear();
        p->processBlock (buf, midi);
    }

    // Exporten av samma område ska ha exakt lika många slag per ljud som spelades.
    const auto file = p->writeMidiExport (0.0, (double) (total / block + (total % block ? 1 : 0)) * block * ppqPerSample,
                                          0xFFFFFFFFu, "test-alla.mid");
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    check (in.openedOk() && mf.readFrom (in), "exporten kunde inte läsas");
    // Spår 0 = tempo, sedan ett spår per ljud som har slag (i ordning).
    int track = 1;
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        const auto played = (int) (p->getHitCounter (v) - before[(size_t) v]);
        if (played == 0)
            continue;
        const int exported = countNotes (mf, track);
        check (played == exported, juce::String (voiceInfo[(size_t) v].shortName) + ": spelade " + juce::String (played)
                                       + " men exporten har " + juce::String (exported));
        // Spårnamn.
        juce::String name;
        for (const auto* e : *mf.getTrack (track))
            if (e->message.isTrackNameEvent())
                name = e->message.getTextFromTextMetaEvent();
        check (name == voiceInfo[(size_t) v].shortName, "spåret heter " + name);
        ++track;
    }
    check (mf.getNumTracks() == track, "exporten har fel antal spår");
    // Sista blocket går en bit förbi takt 4, så slaget på takt 5:1 kommer med: 17 kickar.
    const auto kicks = p->getHitCounter (dsp::kick) - before[(size_t) dsp::kick];
    check (kicks == 17, "bastrumman slog " + juce::String (kicks) + " gånger, väntat 17");
    // 3-stegs virvel: steg 0, 3, 6 … 63 = 22 slag (+ steg 64 i sista blocket, 64 % 3 = 1 = tyst).
    const auto snares = p->getHitCounter (dsp::snare) - before[(size_t) dsp::snare];
    check (snares == 22, "polymetern för virveln: " + juce::String (snares) + " slag, väntat 22");

    // En enda ljud-export: ett spår (+ tempo), rätt not.
    const auto one = p->writeMidiExport (0.0, 16.0, 1u << dsp::kick, "test-kick.mid");
    juce::FileInputStream in2 (one);
    juce::MidiFile mf2;
    check (in2.openedOk() && mf2.readFrom (in2) && mf2.getNumTracks() == 2 && countNotes (mf2, 1) == 16, "kick-exporten är fel");
    bool rightNote = true;
    for (const auto* e : *mf2.getTrack (1))
        if (e->message.isNoteOn())
            rightNote = rightNote && e->message.getNoteNumber() == 36 && e->message.getVelocity() > 0;
    check (rightNote, "kick ska exporteras som not 36 med velocity");

    // Efter dragning slutar sekvensern spela ljudet – men exporten ger det fortfarande.
    p->voicesExported (1u << dsp::kick);
    check (! p->currentSettings().enabled[(size_t) dsp::kick], "sekvensern spelar fortfarande kicken efter dragning");
    std::vector<seq::Hit> hits;
    seq::generate (p->getPattern(), p->currentSettings(), 0.0, 4.0, 1u << dsp::kick, hits);
    check (hits.empty(), "avstängt ljud genereras ändå");
    const auto again = p->writeMidiExport (0.0, 16.0, 1u << dsp::kick, "test-kick2.mid");
    juce::FileInputStream in3 (again);
    juce::MidiFile mf3;
    check (in3.openedOk() && mf3.readFrom (in3) && countNotes (mf3, 1) == 16, "export av avstängt ljud blir tom");

    // Velocity följer med.
    p->getPattern().velocity[(size_t) dsp::clap][4] = 0.5f;
    const auto clapFile = p->writeMidiExport (0.0, 4.0, 1u << dsp::clap, "test-clap.mid");
    juce::FileInputStream in4 (clapFile);
    juce::MidiFile mf4;
    int vel = -1;
    if (in4.openedOk() && mf4.readFrom (in4))
        for (const auto* e : *mf4.getTrack (1))
            if (e->message.isNoteOn()) { vel = e->message.getVelocity(); break; }
    check (std::abs (vel - 64) <= 1, "velocity följer inte med i exporten (" + juce::String (vel) + ")");

    // Locators läses.
    const auto host = p->getHostInfo();
    check (host.hasLoop && std::abs (host.loopStart - 4.0) < 1e-9 && std::abs (host.loopEnd - 20.0) < 1e-9, "markörerna läses inte");

    p->setPlayHead (nullptr);
    std::cout << "  uppspelning = export, polymeter, velocity, spårnamn ok" << std::endl;
}

void testGroove()
{
    std::cout << "Groove (probability, ratchet, nudge, humanize):" << std::endl;
    seq::Pattern pat;
    pat.clear();
    seq::Settings st;

    // 66 % sannolikhet över 4000 steg: ska hamna nära 66 %.
    pat.setVoiceFromString (dsp::closedHat, "x?66");
    st.length[(size_t) dsp::closedHat] = 1;
    std::vector<seq::Hit> a, b;
    seq::generate (pat, st, 0.0, 1000.0, 1u << dsp::closedHat, a);
    const double ratio = (double) a.size() / 4000.0;
    check (ratio > 0.62 && ratio < 0.70, "probability 66 % gav " + juce::String (ratio * 100.0, 1) + " %");

    // Deterministiskt: samma område två gånger = samma slag. Uppdelat i block = samma som i ett svep.
    seq::generate (pat, st, 0.0, 1000.0, 1u << dsp::closedHat, b);
    check (a.size() == b.size(), "probability är inte deterministisk");
    std::vector<seq::Hit> pieces;
    for (double t = 0.0; t < 1000.0; t += 0.37)
        seq::generate (pat, st, t, std::min (1000.0, t + 0.37), 1u << dsp::closedHat, pieces);
    check (pieces.size() == a.size(), "probability skiljer sig när det genereras i block");

    // Reroll = nytt frö = annat utfall.
    pat.seed = 777;
    std::vector<seq::Hit> c;
    seq::generate (pat, st, 0.0, 1000.0, 1u << dsp::closedHat, c);
    bool differs = c.size() != a.size();
    for (size_t i = 0; ! differs && i < c.size(); ++i)
        differs = std::abs (c[i].ppq - a[i].ppq) > 1e-9;
    check (differs, "nytt frö ger samma slumputfall");

    // Ratchet 3: tre slag inom steget, avtagande styrka.
    pat.clear();
    pat.setVoiceFromString (dsp::snare, "X*3 . . .");
    st.length[(size_t) dsp::snare] = 4;
    std::vector<seq::Hit> r;
    seq::generate (pat, st, 0.0, 1.0, 1u << dsp::snare, r);
    check (r.size() == 3 && std::abs (r[1].ppq - 0.25 / 3.0) < 1e-9 && r[1].velocity < r[0].velocity, "ratchet 3 fungerar inte");

    // Nudge +20 %: slaget kommer 0,2 steg senare.
    pat.clear();
    pat.setVoiceFromString (dsp::rim, ". X>20 . .");
    st.length[(size_t) dsp::rim] = 4;
    std::vector<seq::Hit> n;
    seq::generate (pat, st, 0.0, 1.0, 1u << dsp::rim, n);
    check (n.size() == 1 && std::abs (n[0].ppq - (0.25 + 0.05)) < 1e-9, "nudge flyttar inte slaget rätt");

    // Nudge bakåt över en blockgräns: slaget får bara komma en gång.
    pat.setVoiceFromString (dsp::rim, "X<30 . . .");
    std::vector<seq::Hit> m;
    for (double t = -1.0; t < 4.0; t += 0.1)
        seq::generate (pat, st, t, t + 0.1, 1u << dsp::rim, m);
    check (m.size() == 5, "nudge bakåt dubblerar eller tappar slag (" + juce::String ((int) m.size()) + ")");

    // Humanize ändrar tid och styrka, men inte antalet slag.
    pat.setVoiceFromString (dsp::rim, "X X X X");
    st.humanize = 1.0f;
    std::vector<seq::Hit> h;
    seq::generate (pat, st, 0.0, 4.0, 1u << dsp::rim, h);
    bool moved = false, velVaries = false;
    for (const auto& x : h)
    {
        const double rel = x.ppq / 0.25;
        moved = moved || std::abs (rel - std::round (rel)) > 1e-6;
        velVaries = velVaries || std::abs (x.velocity - 1.0f) > 1e-3f;
    }
    // Slagen vid kanterna (0 och 4) kan glida in i eller ut ur fönstret – därför 15–17.
    check (h.size() >= 15 && h.size() <= 17 && moved && velVaries,
           "humanize gör inget (" + juce::String ((int) h.size()) + " slag)");

    // Sparat mönster behåller probability/ratchet/nudge/frö; gammalt mönster (v0.1) får standardvärden.
    pat.clear();
    pat.setVoiceFromString (dsp::tomLow, "x?40*2>10");
    pat.seed = 4242;
    seq::Pattern back;
    back.fromTree (pat.toTree());
    const auto lo = (size_t) dsp::tomLow;
    check (std::abs (back.probability[lo][0].load() - 0.4f) < 0.01f && back.ratchet[lo][0].load() == 2
               && std::abs (back.nudge[lo][0].load() - 0.1f) < 0.01f && back.seed.load() == 4242u,
           "probability/ratchet/nudge/frö sparas inte");
    auto old = pat.toTree();
    for (int i = 0; i < old.getNumChildren(); ++i)
        for (auto* key : { "prob", "rat", "nudge" })
            old.getChild (i).removeProperty (key, nullptr);
    back.fromTree (old);
    check (back.probability[lo][0].load() > 0.99f && back.ratchet[lo][0].load() == 1, "gammalt mönster får fel standardvärden");
    std::cout << "  probability " << juce::String (ratio * 100.0, 1) << " % (66 %), ratchet, nudge, humanize, sparning ok" << std::endl;
}

// Spelar med transport och returnerar topp, rms och slag per ljud.
struct TransportResult { float peak = 0; double rms = 0; bool finite = true; std::array<uint32_t, dsp::numVoices> hits {}; double end = 0; };
TransportResult runTransport (SlaggProcessor& p, double bars, double bpm, juce::AudioBuffer<float>* capture = nullptr,
                              double loopStart = 4.0, double loopEnd = 20.0)
{
    PlayHead ph;
    ph.bpm = bpm;
    ph.loopStart = loopStart;
    ph.loopEnd = loopEnd;
    const double sr = 48000.0;
    const int block = 512;
    p.setPlayHead (&ph);
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    std::array<uint32_t, dsp::numVoices> before {};
    for (int v = 0; v < dsp::numVoices; ++v) before[(size_t) v] = p.getHitCounter (v);
    const double pps = bpm / 60.0 / sr;
    const int total = (int) std::ceil (bars * 4.0 / pps / block) * block;
    if (capture != nullptr) capture->setSize (2, total);
    TransportResult r;
    double sum = 0;
    ph.playing = true;
    for (int pos = 0; pos < total; pos += block)
    {
        ph.ppq = pos * pps;
        midi.clear();
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample (ch, i);
                r.finite = r.finite && std::isfinite (x);
                r.peak = std::max (r.peak, std::abs (x));
                sum += x * x;
            }
            if (capture != nullptr) capture->copyFrom (ch, pos, buf, ch, 0, block);
        }
    }
    r.rms = std::sqrt (sum / (2.0 * total));
    r.end = total * pps;
    for (int v = 0; v < dsp::numVoices; ++v) r.hits[(size_t) v] = p.getHitCounter (v) - before[(size_t) v];
    p.setPlayHead (nullptr);
    return r;
}

void testKits (const juce::File& wavDir)
{
    std::cout << "Kit:" << std::endl;
    auto p = makeProc();
    for (int k = 0; k < p->getNumPrograms(); ++k)
    {
        p->setCurrentProgram (k);
        const auto name = p->getProgramName (k);
        juce::AudioBuffer<float> cap;
        const auto r = runTransport (*p, 8.0, 130.0, wavDir.exists() ? &cap : nullptr);
        std::cout << "  " << juce::String (k + 1).paddedLeft ('0', 2) << " " << name.paddedRight (' ', 18)
                  << " peak " << juce::String (r.peak, 3) << "  rms " << juce::String (r.rms, 4) << std::endl;
        check (r.finite, name + ": NaN/Inf");
        check (r.peak <= 0.98f, name + ": klipper");
        check (r.rms > 0.01, name + ": för tyst");
        check (r.peak < 0.97f, name + ": går för hårt in i begränsaren (topp " + juce::String (r.peak, 3) + ")");

        // Exporten av samma område ska ha exakt lika många slag per ljud – även med probability/ratchet/humanize.
        const auto file = p->writeMidiExport (0.0, r.end, 0xFFFFFFFFu, "test-kit.mid");
        juce::FileInputStream in (file);
        juce::MidiFile mf;
        check (in.openedOk() && mf.readFrom (in), name + ": exporten kan inte läsas");
        int track = 1;
        for (int v = 0; v < dsp::numVoices; ++v)
        {
            if (r.hits[(size_t) v] == 0)
                continue;
            const int exported = countNotes (mf, track++);
            check ((int) r.hits[(size_t) v] == exported, name + " / " + voiceInfo[(size_t) v].shortName + ": spelade "
                       + juce::String ((int) r.hits[(size_t) v]) + ", exporten har " + juce::String (exported));
        }
        if (wavDir.exists())
            writeWav (wavDir.getChildFile ("kit " + juce::String (k + 1).paddedLeft ('0', 2) + " " + name + ".wav"), cap, 48000.0);
    }
    check (p->getNumPrograms() >= 12, "för få kit");

    // Kit behåller användarens utgångsval.
    setParam (*p, pid::id (dsp::kick, pid::sOut), 1.0f);
    p->setCurrentProgram (1);
    check (p->apvts.getRawParameterValue (pid::id (dsp::kick, pid::sOut))->load() > 0.5f, "kit nollställer utgångsvalet");
}

void testSongs()
{
    std::cout << "Song-kit (5 minuter mellan markörerna):" << std::endl;
    auto p = makeProc();
    int songs = 0;
    for (int k = 0; k < p->getNumPrograms(); ++k)
    {
        if (! p->getProgramName (k).startsWith ("Song"))
            continue;
        ++songs;
        p->setCurrentProgram (k);
        const auto name = p->getProgramName (k);
        // 5 minuter i 130 BPM = 650 taktslag = 162,5 takter.
        const double end = 650.0;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto file = p->writeMidiExport (0.0, end, 0xFFFFFFFFu, "test-song.mid");
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        juce::FileInputStream in (file);
        juce::MidiFile mf;
        if (! (in.openedOk() && mf.readFrom (in)))
        {
            check (false, name + ": exporten kan inte läsas");
            continue;
        }
        const double tpq = mf.getTimeFormat();
        const int bars = (int) (end / 4.0);
        std::map<int, std::vector<int>> perBar;          // not -> antal slag per takt
        std::map<int, std::set<juce::String>> shapes;    // not -> olika "utseenden" av en takt
        std::map<int, std::vector<juce::String>> barShape;
        for (int t = 1; t < mf.getNumTracks(); ++t)
            for (const auto* e : *mf.getTrack (t))
                if (e->message.isNoteOn())
                {
                    const int note = e->message.getNoteNumber();
                    const double ppq = e->message.getTimeStamp() / tpq;
                    const int bar = (int) (ppq / 4.0);
                    if (bar >= bars) continue;
                    auto& v = perBar[note];
                    auto& sh = barShape[note];
                    if (v.empty()) { v.assign ((size_t) bars, 0); sh.assign ((size_t) bars, {}); }
                    ++v[(size_t) bar];
                    sh[(size_t) bar] << juce::roundToInt ((ppq - bar * 4.0) * 4.0) << ",";
                }
        for (auto& [note, sh] : barShape)
            for (auto& x : sh) shapes[note].insert (x);

        // Kicken bär låten: fyra slag i varje takt (alla Song-kit har rak kick, utom Night Shuffle som har brutet mönster).
        const auto& kicks = perBar[36];
        int emptyKickBars = 0;
        for (int c : kicks) emptyKickBars += c == 0 ? 1 : 0;
        check (! kicks.empty() && emptyKickBars == 0, name + ": kicken försvinner i " + juce::String (emptyKickBars) + " takter");

        // Hattarna (stängd 42) ska variera: många olika takter och varierande täthet.
        int minH = 999, maxH = 0;
        for (int c : perBar[42]) { minH = std::min (minH, c); maxH = std::max (maxH, c); }
        const auto distinct = (int) shapes[42].size();
        check (distinct >= 25, name + ": för statiskt – bara " + juce::String (distinct) + " olika hihat-takter av " + juce::String (bars));
        check (maxH - minH >= 3, name + ": hihat-tätheten varierar för lite (" + juce::String (minH) + "–" + juce::String (maxH) + ")");

        // Fills och cymbal på nya fraser.
        int fillHits = 0;
        for (int n : { 41, 45, 48, 38 })
            for (size_t b = 0; b < perBar[n].size(); ++b)
                fillHits += perBar[n][b];
        const int crashes = [&] { int c = 0; for (int x : perBar[49]) c += x; return c; }();
        check (fillHits > 0 && crashes > 0, name + ": inga fills/cymbaler");
        check (ms < 2000.0, name + ": exporten tar för lång tid (" + juce::String (ms, 0) + " ms)");

        std::cout << "  " << name.paddedRight (' ', 24) << " hihat-takter: " << distinct << " olika av " << bars
                  << ", " << minH << "–" << maxH << " slag/takt, cymbaler " << crashes << ", " << juce::String (ms, 0) << " ms" << std::endl;
    }
    check (songs == 10, "väntade 10 Song-kit, hittade " + juce::String (songs));
}

void testArrangement()
{
    std::cout << "Låtform (arrangemang):" << std::endl;
    using falk::arr::Part;
    // Klassisk form över 160 takter: intro 16, uppbyggnad 16, huvuddel 32, break 32 (buildup sista 8), ... outro 16.
    auto at = [] (int bar) { return falk::arr::sectionAt (falk::arr::classic, bar * 4.0, 0.0, 640.0, 4.0); };
    check (at (0).part == Part::intro && at (16).part == Part::build && at (32).part == Part::main
               && at (64).part == Part::breakdown && at (88).part == Part::buildup && at (96).part == Part::main
               && at (96).drop && ! at (32).drop && at (144).part == Part::outro && at (159).part == Part::outro,
           "låtformens delar hamnar fel");

    auto p = makeProc();
    p->setCurrentProgram (13);   // Song - Bunker Night
    setParam (*p, pid::arrForm, (float) falk::arr::classic);
    const auto file = p->writeMidiExport (0.0, 640.0, 0xFFFFFFFFu, "test-arr.mid");
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    check (in.openedOk() && mf.readFrom (in), "arrangemangsexporten kan inte läsas");
    const double tpq = mf.getTimeFormat();

    std::map<juce::String, std::vector<double>> tracks;   // spårnamn -> note on (ppq)
    juce::StringArray markerNames;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::String name;
        for (const auto* e : *mf.getTrack (t))
        {
            if (e->message.isTrackNameEvent()) name = e->message.getTextFromTextMetaEvent();
            if (e->message.isTextMetaEvent() && e->message.getMetaEventType() == 6) markerNames.add (e->message.getTextFromTextMetaEvent());
            if (e->message.isNoteOn()) tracks[name].push_back (e->message.getTimeStamp() / tpq);
        }
    }
    auto countIn = [&] (const juce::String& track, int barFrom, int barTo)
    {
        int c = 0;
        for (double x : tracks[track]) c += (x >= barFrom * 4.0 && x < barTo * 4.0) ? 1 : 0;
        return c;
    };
    int drumsInBreak = 0, introOther = 0;
    for (const auto& info : voiceInfo)
    {
        const juce::String n (info.shortName);
        drumsInBreak += countIn (n, 64, 88) + (n == "Snare" ? 0 : countIn (n, 88, 96));
        if (n != "Kick" && n != "Closed hat") introOther += countIn (n, 0, 16);
    }
    check (drumsInBreak == 0, "trummor spelar i breaket (" + juce::String (drumsInBreak) + " slag)");
    check (introOther == 0, "andra ljud än kick/hihat i introt");
    check (countIn ("Kick", 32, 64) >= 120, "huvuddelen saknar kick");
    const int rollStart = countIn ("Snare", 88, 89), rollEnd = countIn ("Snare", 94, 95);
    check (rollStart > 0 && rollEnd > rollStart * 3, "virvelrullningen accelererar inte (" + juce::String (rollStart) + " -> " + juce::String (rollEnd) + ")");
    bool crashOnDrop = false;
    for (double x : tracks["Cymbal"]) crashOnDrop = crashOnDrop || std::abs (x - 96.0 * 4.0) < 1e-6;
    check (crashOnDrop, "ingen cymbal på dropet");
    check (countIn ("Riser", 88, 96) == 1 && countIn ("Impact", 96, 97) == 1, "Riser/Impact-spåren saknas eller hamnar fel");
    // Sektioner-spåret: en ton per del, rätt tonhöjd (break = F3 = 53 på takt 65, drop = A3 = 57 på takt 97).
    bool breakNote = false, dropNote = false;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::String name;
        for (const auto* e : *mf.getTrack (t))
        {
            if (e->message.isTrackNameEvent()) name = e->message.getTextFromTextMetaEvent();
            if (name == "Sektioner" && e->message.isNoteOn())
            {
                const double x = e->message.getTimeStamp() / tpq;
                breakNote = breakNote || (std::abs (x - 64.0 * 4.0) < 1e-6 && e->message.getNoteNumber() == 53);
                dropNote = dropNote || (std::abs (x - 96.0 * 4.0) < 1e-6 && e->message.getNoteNumber() == 57);
            }
        }
    }
    check (breakNote && dropNote, "Sektioner-spåret saknas eller har fel toner");
    check (markerNames.contains ("Intro") && markerNames.contains ("Break") && markerNames.contains ("Drop")
               && markerNames.contains ("Outro"), "markörerna saknas");

    // Live = export: spela 104 takter med DAW-markörerna över hela låten (0–640) och jämför slag per ljud.
    std::array<uint32_t, dsp::numVoices> before {};
    const auto r = runTransport (*p, 104.0, 130.0, nullptr, 0.0, 640.0);
    juce::ignoreUnused (before);
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        int exported = 0;
        for (double x : tracks[voiceInfo[(size_t) v].shortName]) exported += x < r.end - 1e-9 ? 1 : 0;
        check ((int) r.hits[(size_t) v] == exported, juce::String (voiceInfo[(size_t) v].shortName) + ": live "
                   + juce::String ((int) r.hits[(size_t) v]) + " men exporten " + juce::String (exported) + " med låtform");
    }
    check (r.finite && r.peak <= 0.98f, "låtformen ger klipp/NaN");
    std::cout << "  break utan trummor, rullning " << rollStart << " -> " << rollEnd << " slag/takt, cymbal på drop, Riser/Impact, "
              << markerNames.size() << " markörer, live = export" << std::endl;
}

void testRumble()
{
    std::cout << "Muller (rumble):" << std::endl;
    // Bara kick, fyra på golvet. Energi under 250 Hz i andra halvan av varje taktslag (mellan kickarna).
    auto measure = [] (float amount)
    {
        auto p = makeProc();
        p->getPattern().clear();
        p->getPattern().setVoiceFromString (dsp::kick, "X . . .");
        setParam (*p, pid::id (dsp::kick, pid::sLen), 4.0f);
        setParam (*p, pid::id (dsp::kick, pid::sDecay), 0.2f);
        setParam (*p, pid::rumbleAmt, amount);
        juce::AudioBuffer<float> cap;
        const auto r = runTransport (*p, 4.0, 120.0, &cap);
        // 120 BPM, 48 kHz: ett taktslag = 24000 sampel. Mät 2:a halvan av slag 4–16.
        double between = 0;
        for (int beat = 3; beat < 16; ++beat)
            for (int i = 0; i < 12000; ++i)
            {
                const float x = cap.getSample (0, beat * 24000 + 12000 + i);
                between += x * x;
            }
        return std::make_tuple (between, r.peak, r.finite);
    };
    const auto [offE, offPeak, offFinite] = measure (0.0f);
    const auto [onE, onPeak, onFinite] = measure (0.8f);
    std::cout << "  energi mellan kickarna: av " << juce::String (offE, 4) << ", på " << juce::String (onE, 4) << std::endl;
    check (onFinite && offFinite, "mullret ger NaN/Inf");
    check (onE > offE * 20.0 + 1.0, "mullret hörs inte mellan kickarna");
    check (onPeak <= 0.98f, "mullret klipper");
}

void testState()
{
    std::cout << "Spara/ladda:" << std::endl;
    auto a = makeProc();
    a->getPattern().setVoiceFromString (dsp::tomMid, "x.o.x...");
    a->getPattern().velocity[(size_t) dsp::tomMid][2] = 0.33f;
    setParam (*a, pid::id (dsp::kick, pid::sTune), -7.0f);
    juce::MemoryBlock mb;
    a->getStateInformation (mb);
    auto b = makeProc();
    b->setStateInformation (mb.getData(), (int) mb.getSize());
    check (b->getPattern().on[(size_t) dsp::tomMid][2].load() && std::abs (b->getPattern().velocity[(size_t) dsp::tomMid][2].load() - 0.33f) < 0.01f,
           "mönstret sparas inte");
    check (std::abs (b->apvts.getRawParameterValue (pid::id (dsp::kick, pid::sTune))->load() + 7.0f) < 0.01f, "parametrar sparas inte");

    // Som pluginval: slumpade värden, spara, ändra, ladda.
    juce::Random rnd (1234);
    for (auto* p : a->getParameters()) p->setValueNotifyingHost (rnd.nextFloat());
    std::vector<float> saved;
    for (auto* p : a->getParameters()) saved.push_back (p->getValue());
    a->getStateInformation (mb);
    for (auto* p : a->getParameters()) p->setValueNotifyingHost (rnd.nextFloat());
    a->setStateInformation (mb.getData(), (int) mb.getSize());
    for (size_t i = 0; i < saved.size(); ++i)
    {
        auto* p = a->getParameters()[(int) i];
        check (std::abs (p->getValue() - saved[i]) < 0.02f, p->getName (32) + " återställs inte");
    }
    std::cout << "  " << a->getParameters().size() << " parametrar kontrollerade" << std::endl;
}

//==============================================================================
// Sample-lager: testfil = 1 s, första halvan 440 Hz, andra halvan 880 Hz (sinus, 0,8).
juce::File makeTestSample (const juce::File& dir, double sr)
{
    const int n = (int) sr;
    juce::AudioBuffer<float> b (1, n);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        ph += (i < n / 2 ? 440.0 : 880.0) / sr;
        b.setSample (0, i, 0.8f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * ph));
    }
    auto f = dir.getChildFile ("Slagg test " + juce::String ((int) sr) + ".wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (1).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (out, opts))
        w->writeFromAudioSampleBuffer (b, 0, n);
    return f;
}

// Ett slag (MIDI) och sedan `seconds` sekunder; returnerar main L, eller utgång `bus` L.
std::vector<float> renderHit (SlaggProcessor& p, int voice, double sr, int block, double seconds, float velocity,
                              int bus = 0, int chokeVoice = -1, double chokeAt = 0)
{
    p.setPlayHead (nullptr);
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    const int total = (int) (seconds * sr);
    std::vector<float> out ((size_t) total, 0.0f);
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    const int chokeSample = (int) (chokeAt * sr);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (buf.getNumChannels(), n, false, false, true);
        midi.clear();
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, voiceInfo[(size_t) voice].midiNote, velocity), 0);
        if (chokeVoice >= 0 && chokeSample >= pos && chokeSample < pos + n)
            midi.addEvent (juce::MidiMessage::noteOn (10, voiceInfo[(size_t) chokeVoice].midiNote, 1.0f), chokeSample - pos);
        p.processBlock (buf, midi);
        const float* d = buf.getReadPointer (2 * bus);
        std::copy (d, d + n, out.begin() + pos);
    }
    return out;
}

// Frekvens ur nollgenomgångar i [from, to) sekunder.
double zeroCrossFreq (const std::vector<float>& x, double sr, double from, double to)
{
    int crossings = 0;
    const int a = (int) (from * sr), b = std::min ((int) x.size(), (int) (to * sr));
    for (int i = a + 1; i < b; ++i)
        if ((x[(size_t) i - 1] < 0.0f) != (x[(size_t) i] < 0.0f))
            ++crossings;
    return crossings / 2.0 / (to - from);
}

double rmsOf (const std::vector<float>& x, double sr, double from, double to)
{
    double e = 0;
    const int a = (int) (from * sr), b = std::min ((int) x.size(), (int) (to * sr));
    for (int i = a; i < b; ++i)
        e += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (e / std::max (1, b - a));
}

float peakOf (const std::vector<float>& x)
{
    float m = 0;
    for (auto v : x) m = std::max (m, std::abs (v));
    return m;
}

void testSamples()
{
    std::cout << "Sample-lager:" << std::endl;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SlaggSampleTest");
    dir.createDirectory();
    const auto wav44 = makeTestSample (dir, 44100.0);
    const auto wav96 = makeTestSample (dir, 96000.0);
    const int v = dsp::snare;

    // Blend 0 = exakt samma ljud som utan sample.
    {
        auto a = makeProc();
        auto b = makeProc();
        juce::String err;
        check (b->loadSample (v, wav44, err), "testsamplet laddas inte: " + err);
        bool same = true;
        for (int voice = 0; voice < dsp::numVoices; ++voice)
        {
            juce::String e2;
            b->loadSample (voice, wav44, e2);
            const auto x = renderHit (*a, voice, 44100.0, 256, 1.0, 0.9f);
            const auto y = renderHit (*b, voice, 44100.0, 256, 1.0, 0.9f);
            same = same && x == y;
        }
        check (same, "Blend 0 med sample laddat låter inte exakt som utan sample");
    }

    // Tonhöjd, S.Tune, S.Start, S.Decay, velocity – i 44,1 och 96 kHz, med sample i både 44,1 och 96 kHz.
    for (const double sr : { 44100.0, 96000.0 })
        for (const auto& file : { wav44, wav96 })
        {
            const juce::String tag = " (" + juce::String (sr / 1000.0, 1) + " kHz, fil " + file.getFileNameWithoutExtension().fromLastOccurrenceOf (" ", false, false) + ")";
            auto p = makeProc();
            juce::String err;
            check (p->loadSample (v, file, err), "sample laddas inte" + tag);
            setParam (*p, pid::id (v, pid::sBlend), 1.0f);
            const int block = sr > 50000.0 ? 333 : 256;

            const auto base = renderHit (*p, v, sr, block, 1.2, 1.0f);
            const double f = zeroCrossFreq (base, sr, 0.05, 0.45);
            check (std::abs (f - 440.0) < 4.0, "samplet spelar " + juce::String (f, 1) + " Hz, väntat 440" + tag);
            check (zeroCrossFreq (base, sr, 0.55, 0.95) > 860.0, "andra halvan av samplet hörs inte" + tag);

            setParam (*p, pid::id (v, pid::sSTune), 12.0f);
            const auto up = renderHit (*p, v, sr, block, 0.5, 1.0f);
            const double fu = zeroCrossFreq (up, sr, 0.03, 0.23);
            check (std::abs (fu - 880.0) < 8.0, "S.Tune +12 ger " + juce::String (fu, 1) + " Hz, väntat 880" + tag);
            setParam (*p, pid::id (v, pid::sSTune), 0.0f);

            setParam (*p, pid::id (v, pid::sSStart), 0.5f);
            const auto st = renderHit (*p, v, sr, block, 0.5, 1.0f);
            const double fs = zeroCrossFreq (st, sr, 0.03, 0.4);
            check (std::abs (fs - 880.0) < 8.0, "S.Start 50 % hoppar inte in (" + juce::String (fs, 1) + " Hz)" + tag);
            check (rmsOf (st, sr, 0.6, 0.9) < 1.0e-4, "S.Start: samplet spelar för länge" + tag);
            setParam (*p, pid::id (v, pid::sSStart), 0.0f);

            setParam (*p, pid::id (v, pid::sSDecay), 0.3f);
            const auto dc = renderHit (*p, v, sr, block, 1.0, 1.0f);
            check (rmsOf (dc, sr, 0.0, 0.04) > 0.1, "S.Decay 30 %: början saknas" + tag);
            check (rmsOf (dc, sr, 0.15, 0.9) < 1.0e-4, "S.Decay 30 % kortar inte samplet" + tag);
            check (rmsOf (base, sr, 0.15, 0.9) > 0.1, "S.Decay 100 % spelar inte hela samplet" + tag);
            setParam (*p, pid::id (v, pid::sSDecay), 1.0f);

            const float loud = peakOf (renderHit (*p, v, sr, block, 0.2, 1.0f));
            const float soft = peakOf (renderHit (*p, v, sr, block, 0.2, 0.5f));
            const float ratio = soft / std::max (1.0e-6f, loud);
            check (ratio > 0.5f && ratio < 0.8f, "velocity skalar inte samplet (" + juce::String (ratio, 2) + ")" + tag);
        }

    // Samma kedja som det analoga: Cutoff, egen utgång, choke.
    {
        auto p = makeProc();
        juce::String err;
        p->loadSample (v, wav44, err);
        setParam (*p, pid::id (v, pid::sBlend), 1.0f);
        const auto open = renderHit (*p, v, 44100.0, 256, 0.4, 1.0f);
        setParam (*p, pid::id (v, pid::sCutoff), 100.0f);
        const auto closed = renderHit (*p, v, 44100.0, 256, 0.4, 1.0f);
        check (rmsOf (closed, 44100.0, 0.05, 0.35) < rmsOf (open, 44100.0, 0.05, 0.35) * 0.3, "Cutoff påverkar inte samplet");
        setParam (*p, pid::id (v, pid::sCutoff), 20000.0f);

        setParam (*p, pid::id (v, pid::sOut), 1.0f);
        const auto mainOut = renderHit (*p, v, 44100.0, 256, 0.4, 1.0f, 0);
        const auto ownOut = renderHit (*p, v, 44100.0, 256, 0.4, 1.0f, 1 + v);
        check (peakOf (mainOut) < 1.0e-6f && peakOf (ownOut) > 0.1f, "samplet följer inte Out = Own");

        // Öppen hihat med långt sample stryps av den stängda.
        p->loadSample (dsp::openHat, wav44, err);
        setParam (*p, pid::id (dsp::openHat, pid::sBlend), 1.0f);
        setParam (*p, pid::id (dsp::openHat, pid::sOut), 1.0f);
        const auto ringing = renderHit (*p, dsp::openHat, 44100.0, 256, 0.8, 1.0f, 1 + dsp::openHat);
        const auto choked = renderHit (*p, dsp::openHat, 44100.0, 256, 0.8, 1.0f, 1 + dsp::openHat, dsp::closedHat, 0.2);
        check (rmsOf (ringing, 44100.0, 0.4, 0.7) > 0.05, "öppen hihat-sample klingar inte");
        check (rmsOf (choked, 44100.0, 0.4, 0.7) < 1.0e-5, "stängd hihat stryper inte den öppnas sample");

        // Sekvensern spelar samplet (DAW-transport).
        PlayHead ph;
        ph.playing = true;
        p->setPlayHead (&ph);
        p->prepareToPlay (44100.0, 512);
        p->getPattern().clear();
        p->getPattern().setVoiceFromString (v, "x...x...x...x...");
        juce::AudioBuffer<float> buf (p->getTotalNumOutputChannels(), 512);
        juce::MidiBuffer midi;
        float seqPeak = 0;
        for (int b = 0; b < 40; ++b)
        {
            ph.ppq = b * 512 * (ph.bpm / 60.0 / 44100.0);
            p->processBlock (buf, midi);
            seqPeak = std::max (seqPeak, buf.getMagnitude (2 * (1 + v), 0, 512));
        }
        p->setPlayHead (nullptr);
        check (seqPeak > 0.1f, "sekvensern spelar inte samplet");
    }

    // Ta bort: tyst igen, och byte mitt i ett slag kraschar inte.
    {
        auto p = makeProc();
        juce::String err;
        p->loadSample (v, wav44, err);
        setParam (*p, pid::id (v, pid::sBlend), 1.0f);
        p->prepareToPlay (44100.0, 256);
        juce::AudioBuffer<float> buf (p->getTotalNumOutputChannels(), 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, voiceInfo[(size_t) v].midiNote, 1.0f), 0);
        p->processBlock (buf, midi);
        midi.clear();
        p->loadSample (v, wav96, err);   // byts medan det spelar
        for (int b = 0; b < 20; ++b) p->processBlock (buf, midi);
        p->clearSample (v);
        for (int b = 0; b < 4; ++b) p->processBlock (buf, midi);
        p->collectGarbage();
        check (p->getSample (v) == nullptr, "samplet tas inte bort");
        const auto after = renderHit (*p, v, 44100.0, 256, 0.6, 1.0f);
        check (rmsOf (after, 44100.0, 0.0, 0.6) < 1.0e-6, "Blend 1 utan sample ska vara tyst");
    }

    // Sparat läge: samplet följer med inbäddat även när filen är borta.
    {
        auto tmp = dir.getChildFile ("flyttas bort.wav");
        wav44.copyFileTo (tmp);
        auto a = makeProc();
        juce::String err;
        a->loadSample (dsp::tomLow, tmp, err);
        setParam (*a, pid::id (dsp::tomLow, pid::sBlend), 1.0f);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        tmp.deleteFile();
        auto b = makeProc();
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        const auto s = b->getSample (dsp::tomLow);
        check (s != nullptr && s->name == "flyttas bort.wav", "inbäddat sample saknas efter laddning");
        check (s != nullptr && s->buffer.data.size() == 44100 && std::abs (s->buffer.rate - 44100.0) < 0.5, "inbäddat sample har fel längd/frekvens");
        check (b->getSample (dsp::kick) == nullptr, "andra ljud fick sample");
        const auto x = renderHit (*b, dsp::tomLow, 48000.0, 480, 0.5, 1.0f);
        check (std::abs (zeroCrossFreq (x, 48000.0, 0.05, 0.45) - 440.0) < 4.0, "inbäddat sample spelar fel");
        std::cout << "  sparat läge med sample: " << juce::String ((int) mb.getSize() / 1024) << " kB" << std::endl;

        // Ladda ett projekt utan sample: samplet försvinner.
        auto c = makeProc();
        juce::MemoryBlock empty;
        c->getStateInformation (empty);
        b->setStateInformation (empty.getData(), (int) empty.getSize());
        check (b->getSample (dsp::tomLow) == nullptr, "sample finns kvar efter laddning av projekt utan sample");

        // Kit: Blend nollställs men användarens sample behålls.
        a->setCurrentProgram (3);
        check (a->getSample (dsp::tomLow) != nullptr, "kit tar bort användarens sample");
        check (a->apvts.getRawParameterValue (pid::id (dsp::tomLow, pid::sBlend))->load() < 1.0e-6f, "kit nollställer inte Blend");
    }

    // Stresstest och pluginval-liknande slumpvärden med sample på alla ljud.
    {
        auto p = makeProc();
        juce::String err;
        for (int voice = 0; voice < dsp::numVoices; ++voice)
            p->loadSample (voice, voice % 2 == 0 ? wav44 : wav96, err);
        juce::Random rnd (7);
        float worst = 0;
        bool finite = true;
        for (int round = 0; round < 30; ++round)
        {
            for (auto* prm : p->getParameters())
                prm->setValueNotifyingHost (rnd.nextFloat());
            setParam (*p, pid::master, -6.0f);
            p->prepareToPlay (48000.0, 128);
            juce::AudioBuffer<float> buf (p->getTotalNumOutputChannels(), 128);
            juce::MidiBuffer midi;
            for (int b = 0; b < 200; ++b)
            {
                midi.clear();
                if (b % 5 == 0)
                    for (const auto& info : voiceInfo)
                        midi.addEvent (juce::MidiMessage::noteOn (10, info.midiNote, rnd.nextFloat()), rnd.nextInt (128));
                p->processBlock (buf, midi);
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    for (int i = 0; i < 128; ++i)
                    {
                        const float x = buf.getSample (ch, i);
                        finite = finite && std::isfinite (x);
                        worst = std::max (worst, std::abs (x));
                    }
            }
        }
        check (finite, "sample-stresstest: NaN/Inf");
        check (worst <= 1.0f, "sample-stresstest: klipper");

        std::vector<float> saved;
        for (auto* prm : p->getParameters()) saved.push_back (prm->getValue());
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        for (auto* prm : p->getParameters()) prm->setValueNotifyingHost (rnd.nextFloat());
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        bool restored = true;
        for (size_t i = 0; i < saved.size(); ++i)
            restored = restored && std::abs (p->getParameters()[(int) i]->getValue() - saved[i]) < 0.02f;
        check (restored, "parametrar återställs inte med samples i projektet");
        int withSample = 0;
        for (int voice = 0; voice < dsp::numVoices; ++voice)
            withSample += p->getSample (voice) != nullptr ? 1 : 0;
        check (withSample == dsp::numVoices, "alla samples kom inte tillbaka");
        std::cout << "  stresstest med 10 samples ok, topp " << juce::String (worst, 3)
                  << ", sparat läge " << juce::String ((int) mb.getSize() / 1024) << " kB" << std::endl;
    }
    dir.deleteRecursively();
}

void screenshot (const juce::File& file)
{
    auto p = makeProc();
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    if (auto* e = dynamic_cast<SlaggEditor*> (ed.get()))
        e->refreshNow();
    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    check (out.openedOk() && png.writeImageToStream (img, out), "kunde inte spara skärmdump");
    std::cout << "Skärmdump: " << file.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")" << std::endl;

    // Ett Song-kit: probability-prickar, ratchet-streck, Låt-sektionen och 32-stegs hattar syns.
    p->setCurrentProgram (13);
    if (auto* e = dynamic_cast<SlaggEditor*> (ed.get()))
        e->refreshNow();
    const auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    const auto file2 = file.getSiblingFile (file.getFileNameWithoutExtension() + "-kit.png");
    file2.deleteFile();
    juce::FileOutputStream out2 (file2);
    check (out2.openedOk() && png.writeImageToStream (img2, out2), "kunde inte spara skärmdump 2");

    // Sample-sidan: tre ljud med sample (vågform, start, uttoning), resten tom dragruta.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SlaggShot");
    dir.createDirectory();
    const auto wav = makeTestSample (dir, 44100.0);
    auto kickWav = dir.getChildFile ("Kick 01.wav");
    {
        // Ett kickliknande sample (sjunkande ton) så att vågformen ser ut som en trumma.
        juce::AudioBuffer<float> b (1, 22050);
        double ph = 0;
        for (int i = 0; i < 22050; ++i)
        {
            const double t = i / 44100.0;
            ph += (50.0 + 200.0 * std::exp (-t * 30.0)) / 44100.0;
            b.setSample (0, i, (float) (0.9 * std::exp (-t * 7.0) * std::sin (2.0 * juce::MathConstants<double>::pi * ph)));
        }
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> o (kickWav.createOutputStream().release());
        if (auto w = fmt.createWriterFor (o, juce::AudioFormatWriterOptions().withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (24)))
            w->writeFromAudioSampleBuffer (b, 0, 22050);
    }
    juce::String err;
    p->loadSample (dsp::kick, kickWav, err);
    p->loadSample (dsp::snare, wav, err);
    p->loadSample (dsp::clap, wav, err);
    setParam (*p, pid::id (dsp::kick, pid::sBlend), 0.6f);
    setParam (*p, pid::id (dsp::snare, pid::sBlend), 1.0f);
    setParam (*p, pid::id (dsp::snare, pid::sSStart), 0.2f);
    setParam (*p, pid::id (dsp::snare, pid::sSDecay), 0.6f);
    if (auto* e = dynamic_cast<SlaggEditor*> (ed.get()))
    {
        e->showSamplePages (true);
        e->refreshNow();
    }
    const auto img3 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    const auto file3 = file.getSiblingFile (file.getFileNameWithoutExtension() + "-sample.png");
    file3.deleteFile();
    juce::FileOutputStream out3 (file3);
    check (out3.openedOk() && png.writeImageToStream (img3, out3), "kunde inte spara skärmdump 3");
    // Närbild av ljudremsorna i dubbel storlek (läsbarhet).
    const auto img4 = ed->createComponentSnapshot (juce::Rectangle<int> (0, 0, ed->getWidth() / 2, 470), true, 2.0f);
    const auto file4 = file.getSiblingFile (file.getFileNameWithoutExtension() + "-sample-zoom.png");
    file4.deleteFile();
    juce::FileOutputStream out4 (file4);
    check (out4.openedOk() && png.writeImageToStream (img4, out4), "kunde inte spara skärmdump 4");
    dir.deleteRecursively();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File wavDir, shot;
    for (int i = 1; i + 1 < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--wav-dir")    wavDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--screenshot") shot = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
    }
    if (wavDir != juce::File())
        wavDir.createDirectory();

    testVoices (wavDir);
    testOutputs();
    testSequencer();
    testGroove();
    testKits (wavDir);
    testSongs();
    testArrangement();
    testRumble();
    testState();
    testSamples();
    if (shot != juce::File())
        screenshot (shot);

    std::cout << (failures == 0 ? "ALLA TESTER OK" : juce::String (failures) + " FEL") << std::endl;
    return failures == 0 ? 0 : 1;
}
