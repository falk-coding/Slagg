#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace slagg;

juce::AudioProcessor::BusesProperties SlaggProcessor::makeBuses()
{
    {
        // Main (stereo mix) + en stereoutgång per ljud. Ett ljud går till sin egen utgång när "Out" = Own
        // och värden har aktiverat utgången; annars till Main.
        auto b = juce::AudioProcessor::BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (const auto& info : voiceInfo)
            b = b.withOutput (info.shortName, juce::AudioChannelSet::stereo(), true);
        return b;
    }
}

SlaggProcessor::SlaggProcessor()
    : AudioProcessor (makeBuses()),
      // Tillståndets typnamn "Slagg" får aldrig ändras (sparade projekt).
      apvts (*this, nullptr, "Slagg", createParameterLayout())
{
    params.bind (apvts);
    events.reserve (512);
    hits.reserve (512);
}

SlaggProcessor::~SlaggProcessor()
{
    for (auto& l : liveSamples)
        l.store (nullptr);
}

void SlaggProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    uint32_t seed = 0x51A66u;
    for (int v = 0; v < dsp::numVoices; ++v)
        voices[(size_t) v].prepare ((dsp::VoiceType) v, (float) sr, seed += 0x9E3779B9u);
    rumble.prepare ((float) sr);
    masterGain.reset (sr, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.master->load(), -60.0f));
    reset();
}

void SlaggProcessor::reset()
{
    for (auto& v : voices)
        v.reset();
    rumble.reset();
    hostWasPlaying = false;
    currentPpq = -1.0;
}

bool SlaggProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty();
}

seq::Settings SlaggProcessor::currentSettings() const
{
    seq::Settings s;
    s.stepPpq = rateToPpq ((int) params.rate->load());
    s.swing = params.swing->load();
    s.humanize = params.humanize->load();
    s.evolve = params.evolve->load();
    s.fills = params.fills->load();
    s.phraseBars = phraseBars ((int) params.phrase->load());
    s.barPpq = hostTsNum.load() * 4.0 / juce::jmax (1, hostTsDen.load());
    // Låtform: live räknas den från DAW:ns markörer (utan markörer: från takt 1, utan slut).
    s.arrForm = (int) params.arrForm->load();
    s.arrStart = hostHasLoop.load() ? hostLoopStart.load() : 0.0;
    s.arrEnd = hostHasLoop.load() ? hostLoopEnd.load() : 0.0;
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        s.length[(size_t) v] = (int) params.voice[(size_t) v].len->load();
        s.enabled[(size_t) v] = params.voice[(size_t) v].seq->load() > 0.5f;
    }
    return s;
}

SlaggProcessor::HostInfo SlaggProcessor::getHostInfo() const
{
    HostInfo h;
    h.hasLoop = hostHasLoop.load();
    h.loopStart = hostLoopStart.load();
    h.loopEnd = hostLoopEnd.load();
    h.timeSigNum = hostTsNum.load();
    h.timeSigDen = hostTsDen.load();
    h.bpm = hostBpm.load();
    return h;
}

void SlaggProcessor::audition (int voice, float velocity)
{
    if (juce::isPositiveAndBelow (voice, dsp::numVoices))
        pendingAudition[(size_t) voice] = velocity;
}

void SlaggProcessor::triggerVoice (int v, float velocity)
{
    if (params.voice[(size_t) v].mute->load() > 0.5f)
        return;
    voices[(size_t) v].trigger (velocity, params.voiceParams (v));
    if (v == dsp::closedHat)
        voices[(size_t) dsp::openHat].choke();   // stängd hihat stryper den öppna
    if (v == dsp::kick)
        rumble.duckTrigger ((float) (60.0 / hostBpm.load()) * 0.5f);   // mullret sväller tillbaka på en halv taktslag
    hitCounter[(size_t) v].fetch_add (1);
}

juce::File SlaggProcessor::writeMidiExport (double fromPpq, double toPpq, uint32_t voiceMask, const juce::String& fileName) const
{
    const auto host = getHostInfo();
    auto settings = currentSettings();
    settings.arrStart = fromPpq;   // dragningen: låtformen sträcks över just det dragna området
    settings.arrEnd = toPpq;
    const auto midi = seq::renderMidiFile (pattern, settings, fromPpq, toPpq, voiceMask,
                                           host.bpm, host.timeSigNum, host.timeSigDen);
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Slagg");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName (fileName));
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! midi.writeTo (out, 1))
        return {};
    out.flush();
    return file;
}

void SlaggProcessor::setCurrentProgram (int index)
{
    const auto& kits = getKits();
    if (! juce::isPositiveAndBelow (index, (int) kits.size()))
        return;
    currentProgram = index;
    applyKit (apvts, kits[(size_t) index], pattern);
    ++programChangeCounter;
}

const juce::String SlaggProcessor::getProgramName (int index)
{
    const auto& kits = getKits();
    return juce::isPositiveAndBelow (index, (int) kits.size()) ? kits[(size_t) index].name : juce::String();
}

void SlaggProcessor::reroll()
{
    pattern.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7FFFFFFF) | 1u;
    ++programChangeCounter;
}

void SlaggProcessor::voicesExported (uint32_t voiceMask)
{
    for (int v = 0; v < dsp::numVoices; ++v)
        if ((voiceMask & (1u << v)) != 0)
            if (auto* p = apvts.getParameter (pid::id (v, pid::sSeq)))
                p->setValueNotifyingHost (0.0f);
}

//==============================================================================
void SlaggProcessor::installSample (int v, SamplePtr data)
{
    if (! juce::isPositiveAndBelow (v, dsp::numVoices))
        return;
    {
        const juce::ScopedLock sl (sampleLock);
        auto old = std::move (samples[(size_t) v]);
        samples[(size_t) v] = std::move (data);
        liveSamples[(size_t) v].store (samples[(size_t) v] != nullptr ? &samples[(size_t) v]->buffer : nullptr,
                                      std::memory_order_release);
        // Det gamla kan fortfarande läsas av ett pågående block – frigörs först när två block till är klara.
        if (old != nullptr)
            retired.push_back ({ std::move (old), blocksDone.load() });
    }
    ++sampleChangeCounter;
    collectGarbage();
}

void SlaggProcessor::collectGarbage()
{
    const juce::ScopedLock sl (sampleLock);
    const auto done = blocksDone.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [done] (const Retired& r) { return done >= r.stamp + 2; }),
                   retired.end());
}

bool SlaggProcessor::loadSample (int v, const juce::File& file, juce::String& error)
{
    auto s = loadSampleFile (file, error);
    if (s == nullptr)
        return false;
    installSample (v, std::move (s));
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

void SlaggProcessor::clearSample (int v)
{
    installSample (v, nullptr);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

SamplePtr SlaggProcessor::getSample (int v) const
{
    const juce::ScopedLock sl (sampleLock);
    return juce::isPositiveAndBelow (v, dsp::numVoices) ? samples[(size_t) v] : nullptr;
}

//==============================================================================
void SlaggProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    events.clear();

    // Transport från DAW:en.
    bool playing = false;
    juce::Optional<double> ppq;
    double bpm = hostBpm.load();
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition();
            bpm = pos->getBpm().orFallback (bpm);
            if (auto loop = pos->getLoopPoints(); loop.hasValue() && loop->ppqEnd > loop->ppqStart)
            {
                hostLoopStart = loop->ppqStart;
                hostLoopEnd = loop->ppqEnd;
                hostHasLoop = true;
            }
            if (auto ts = pos->getTimeSignature())
            {
                hostTsNum = juce::jmax (1, ts->numerator);
                hostTsDen = juce::jmax (1, ts->denominator);
            }
        }
    }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    hostBpm = bpm;

    // Sekvensern.
    if (playing && ppq.hasValue())
    {
        const double ppqPerSample = bpm / 60.0 / sampleRate;
        const double start = *ppq;
        const double end = start + numSamples * ppqPerSample;
        const bool continuous = hostWasPlaying && std::abs (start - expectedNextPpq) < 1.0e-3;
        // Kant i kant med förra blocket: ett slag precis på gränsen spelas varken dubbelt eller tappas.
        const double genStart = continuous ? expectedNextPpq : start;
        expectedNextPpq = end;

        hits.clear();
        seq::generate (pattern, currentSettings(), genStart, end, 0xFFFFFFFFu, hits);
        for (const auto& h : hits)
            events.push_back ({ juce::jlimit (0, numSamples - 1, (int) std::floor ((h.ppq - start) / ppqPerSample)),
                                h.voice, h.velocity });
        currentPpq = start;
    }
    else
    {
        currentPpq = -1.0;
    }
    hostWasPlaying = playing;

    // MIDI in: GM-trumnoterna spelar ljuden.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOn())
            continue;
        for (int v = 0; v < dsp::numVoices; ++v)
            if (voiceInfo[(size_t) v].midiNote == m.getNoteNumber())
                events.push_back ({ meta.samplePosition, v, m.getFloatVelocity() });
    }
    midi.clear();

    // Klick i GUI:t.
    for (int v = 0; v < dsp::numVoices; ++v)
        if (const float a = pendingAudition[(size_t) v].exchange (0.0f); a > 0.0f)
            events.push_back ({ 0, v, a });

    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });

    // Parametrar en gång per block.
    std::array<dsp::VoiceParams, dsp::numVoices> vp;
    std::array<float*, dsp::numVoices * 2> dest {};
    float* mainL = buffer.getWritePointer (0);
    float* mainR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : mainL;
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        vp[(size_t) v] = params.voiceParams (v);
        voices[(size_t) v].setSample (liveSamples[(size_t) v].load (std::memory_order_acquire));
        voices[(size_t) v].setup (vp[(size_t) v]);

        // Egen utgång om ljudet ställts på "Own" och värden har den aktiv, annars Main.
        float* l = mainL;
        float* r = mainR;
        if (params.voice[(size_t) v].out->load() > 0.5f)
        {
            auto bus = getBusBuffer (buffer, false, v + 1);
            if (bus.getNumChannels() >= 2)
            {
                l = bus.getWritePointer (0);
                r = bus.getWritePointer (1);
            }
        }
        dest[(size_t) (2 * v)] = l;
        dest[(size_t) (2 * v + 1)] = r;
    }

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (params.master->load(), -60.0f));

    dsp::RumbleParams rp;
    rp.amount = params.rumbleAmt->load();
    rp.decay = params.rumbleDecay->load();
    rp.tone = params.rumbleTone->load();
    rp.drive = params.rumbleDrive->load();
    rp.duck = params.rumbleDuck->load();

    std::array<float, dsp::numVoices> panL {}, panR {};
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        const float a = (vp[(size_t) v].pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        panL[(size_t) v] = std::cos (a) * 1.41421356f;
        panR[(size_t) v] = std::sin (a) * 1.41421356f;
    }

    size_t next = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        while (next < events.size() && events[next].sample <= i)
        {
            triggerVoice (events[next].voice, events[next].velocity);
            ++next;
        }
        const float g = masterGain.getNextValue();
        float kickDry = 0.0f;
        for (int v = 0; v < dsp::numVoices; ++v)
        {
            auto& voice = voices[(size_t) v];
            if (! voice.isActive())
                continue;
            const float raw = voice.process();
            if (v == dsp::kick)
                kickDry = raw;
            const float x = raw * g;
            dest[(size_t) (2 * v)][i] += x * panL[(size_t) v];
            dest[(size_t) (2 * v + 1)][i] += x * panR[(size_t) v];
        }
        // Mullret följer kickens utgång (Main eller kickens egen).
        const float r = rumble.process (kickDry, rp) * g;
        dest[0][i] += r;
        dest[1][i] += r;
    }

    // Mjuk begränsning så att inget klipper hårt.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = std::tanh (d[i]) * 0.98f;   // aldrig över 0,98
    }
    blocksDone.fetch_add (1, std::memory_order_release);
}

juce::AudioProcessorEditor* SlaggProcessor::createEditor()
{
    return new SlaggEditor (*this);
}

void SlaggProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", SLAGG_VERSION, nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.removeChild (state.getChildWithName ("Pattern"), nullptr);
    state.appendChild (pattern.toTree(), nullptr);
    // Sample-lagren: sökväg + inbäddad ljuddata (projektet fungerar på en annan dator).
    state.removeChild (state.getChildWithName ("Samples"), nullptr);
    {
        juce::ValueTree st ("Samples");
        const juce::ScopedLock sl (sampleLock);
        for (int v = 0; v < dsp::numVoices; ++v)
            if (samples[(size_t) v] != nullptr)
                st.appendChild (sampleToTree (*samples[(size_t) v], v), nullptr);
        state.appendChild (st, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SlaggProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            const auto pt = tree.getChildWithName ("Pattern");
            if (pt.isValid())
                pattern.fromTree (pt);
            tree.removeChild (pt, nullptr);
            // Samples: saknas noden (äldre projekt) = inga samples.
            const auto st = tree.getChildWithName ("Samples");
            std::array<SamplePtr, dsp::numVoices> loaded {};
            for (const auto& c : st)
            {
                const int v = c.getProperty ("voice", -1);
                if (juce::isPositiveAndBelow (v, dsp::numVoices))
                    loaded[(size_t) v] = sampleFromTree (c);
            }
            tree.removeChild (st, nullptr);
            for (int v = 0; v < dsp::numVoices; ++v)
                if (loaded[(size_t) v] != getSample (v))
                    installSample (v, loaded[(size_t) v]);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
            apvts.replaceState (tree);
            ++programChangeCounter;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SlaggProcessor();
}
