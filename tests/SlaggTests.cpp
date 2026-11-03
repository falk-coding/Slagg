// Slagg – automatiska tester. Körs lokalt och i CI (Linux).
//   SlaggTests [--wav-dir DIR] [--screenshot FILE]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>

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
    double ppq = 0, bpm = 128;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setLoopPoints (LoopPoints { 4.0, 20.0 });
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
    testState();
    if (shot != juce::File())
        screenshot (shot);

    std::cout << (failures == 0 ? "ALLA TESTER OK" : juce::String (failures) + " FEL") << std::endl;
    return failures == 0 ? 0 : 1;
}
