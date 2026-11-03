#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <vector>
#include "Voices.h"

// Mönster och sekvenserare. All tid räknas i PPQ (kvartsnoter från låtens början), så att samma
// mönster alltid hamnar på samma ställe och MIDI-exporten blir identisk med uppspelningen.
namespace slagg::seq
{
inline constexpr int maxSteps = 32;

struct Pattern
{
    // Atomics: GUI skriver, ljudtråden läser.
    std::array<std::array<std::atomic<bool>, maxSteps>, dsp::numVoices> on;
    std::array<std::array<std::atomic<float>, maxSteps>, dsp::numVoices> velocity;   // 0.05..1

    Pattern() { setDefault(); }

    void clear();
    void clearVoice (int voice);
    void setDefault();      // ett enkelt technomönster att starta från
    // Textformat per ljud: "x" = slag (velocity 100 %), "o" = svagare (60 %), "." = tyst. Ex: "x...x...x...x..."
    void setVoiceFromString (int voice, const juce::String& steps);

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& tree);
};

// Det som gäller just nu (läses en gång per block / per export).
struct Settings
{
    double stepPpq = 0.25;
    float swing = 0.0f;                           // 0..1, fördröjer vartannat steg upp till 1/3 steg
    std::array<int, dsp::numVoices> length {};    // 1..32 per ljud
    std::array<bool, dsp::numVoices> enabled {};  // spelar sekvensern ljudet?

    Settings() { length.fill (16); enabled.fill (true); }
};

struct Hit
{
    double ppq = 0;
    int voice = 0;
    float velocity = 1;
};

double stepStart (int64_t step, const Settings& s) noexcept;

// Alla slag med start i [fromPpq, toPpq), sorterade i tid. voiceMask: vilka ljud (bit per ljud).
void generate (const Pattern& p, const Settings& s, double fromPpq, double toPpq, uint32_t voiceMask,
               std::vector<Hit>& out);

// MIDI-fil (960 tick/kvartsnot), tid 0 = fromPpq. Ett spår per ljud (typ 1-fil med tempospår först).
// Bara ljud i voiceMask som har slag tas med. Tonlängd: 1/32.
juce::MidiFile renderMidiFile (const Pattern& p, const Settings& s, double fromPpq, double toPpq, uint32_t voiceMask,
                               double bpm, int timeSigNum, int timeSigDen);
} // namespace slagg::seq
