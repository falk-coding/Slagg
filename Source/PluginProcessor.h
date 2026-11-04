#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Pattern.h"
#include "Kits.h"
#include "Rumble.h"
#include "SampleLayer.h"

class SlaggProcessor : public juce::AudioProcessor
{
public:
    SlaggProcessor();
    ~SlaggProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Slagg"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return (int) slagg::getKits().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    slagg::seq::Pattern& getPattern() noexcept { return pattern; }
    slagg::seq::Settings currentSettings() const;
    // Bumpas när kit eller sparat läge laddas, så att GUI:t kan rita om mönstret.
    std::atomic<int> programChangeCounter { 0 };
    // Nytt frö till slumpen (probability/humanize): samma mönster, ny variant.
    void reroll();

    // Spela ett ljud direkt (GUI: klick på namnet). Trådsäkert – körs i nästa block.
    void audition (int voice, float velocity = 0.9f);

    struct HostInfo
    {
        bool hasLoop = false;
        double loopStart = 0, loopEnd = 0;
        int timeSigNum = 4, timeSigDen = 4;
        double bpm = 120;
    };
    HostInfo getHostInfo() const;

    // Spelhuvudet i PPQ (< 0 = står still) och när varje ljud senast slog (för lamporna).
    double getCurrentPpq() const noexcept { return currentPpq.load(); }
    uint32_t getHitCounter (int voice) const noexcept { return hitCounter[(size_t) voice].load(); }

    // MIDI-fil för [fromPpq, toPpq) med ljuden i voiceMask (ett spår per ljud). Returnerar en temporär fil.
    juce::File writeMidiExport (double fromPpq, double toPpq, uint32_t voiceMask, const juce::String& fileName) const;
    // Efter dragning: sekvensern slutar spela de ljuden (annars låter det dubbelt när klippet spelar).
    void voicesExported (uint32_t voiceMask);

    // Sample-lager (meddelandetråden). Ladda/ta bort byter atomärt – ljudtråden allokerar eller väntar aldrig.
    bool loadSample (int voice, const juce::File& file, juce::String& error);
    void clearSample (int voice);
    slagg::SamplePtr getSample (int voice) const;
    // Bumpas när ett sample laddas/tas bort (GUI:t ritar om).
    std::atomic<int> sampleChangeCounter { 0 };
    // Frigör utbytta sample när ljudtråden garanterat släppt dem (anropas från GUI-timern och vid byte).
    void collectGarbage();

private:
    static BusesProperties makeBuses();
    void triggerVoice (int voice, float velocity);
    void installSample (int voice, slagg::SamplePtr data);

    slagg::ParamPointers params;
    std::array<slagg::dsp::DrumVoice, slagg::dsp::numVoices> voices;
    slagg::seq::Pattern pattern;
    slagg::dsp::Rumble rumble;
    int currentProgram = 0;
    std::vector<slagg::seq::Hit> hits;

    struct Event { int sample; int voice; float velocity; };
    std::vector<Event> events;

    double sampleRate = 44100.0;
    bool hostWasPlaying = false;
    double expectedNextPpq = 0;
    juce::SmoothedValue<float> masterGain;

    std::atomic<double> currentPpq { -1.0 };
    std::array<std::atomic<uint32_t>, slagg::dsp::numVoices> hitCounter {};
    std::array<std::atomic<float>, slagg::dsp::numVoices> pendingAudition {};

    // Samples: ägs av meddelandetråden (under sampleLock); ljudtråden läser bara råpekarna.
    mutable juce::CriticalSection sampleLock;
    std::array<slagg::SamplePtr, slagg::dsp::numVoices> samples;
    std::array<std::atomic<const slagg::dsp::SampleBuffer*>, slagg::dsp::numVoices> liveSamples {};
    struct Retired { slagg::SamplePtr data; uint64_t stamp; };
    std::vector<Retired> retired;
    std::atomic<uint64_t> blocksDone { 0 };

    std::atomic<bool> hostHasLoop { false };
    std::atomic<double> hostLoopStart { 0 }, hostLoopEnd { 0 }, hostBpm { 120 };
    std::atomic<int> hostTsNum { 4 }, hostTsDen { 4 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlaggProcessor)
};
