#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <vector>
#include "Voices.h"

// Mönster och sekvenserare. All tid räknas i PPQ (kvartsnoter från låtens början), så att samma
// mönster alltid hamnar på samma ställe och MIDI-exporten blir identisk med uppspelningen.
// Slumpen (probability, humanize) är deterministisk: den beror på ljud, stegets position i låten och
// mönstrets frö. Samma takt låter alltid likadant – tills man trycker Reroll (nytt frö).
namespace slagg::seq
{
inline constexpr int maxSteps = 32;
inline constexpr int maxRatchet = 4;

template <typename T>
using Grid = std::array<std::array<std::atomic<T>, maxSteps>, dsp::numVoices>;

struct Pattern
{
    // Atomics: GUI skriver, ljudtråden läser.
    Grid<bool> on;
    Grid<float> velocity;      // 0.05..1
    Grid<float> probability;   // 0..1 (1 = spelar alltid)
    Grid<int> ratchet;         // 1..4 slag inom steget
    Grid<float> nudge;         // -0.5..0.5 av ett steg (negativt = tidigare)
    std::atomic<uint32_t> seed { 1 };

    Pattern() { setDefault(); }

    void clear();
    void clearVoice (int voice);
    void setDefault();      // ett enkelt technomönster att starta från

    // Ett ljuds steg som text. Antingen ett tecken per steg ("x...x...") eller tokens med mellanslag:
    //   "."  tyst          "X" 100 %   "x" 78 %   "o" 55 %   "-" 32 % (spökslag)
    //   Tillägg: "?66" = 66 % sannolikhet, "*3" = ratchet 3, ">12" / "<8" = 12 % senare / 8 % tidigare.
    //   Ex: "X . x?66 . o>12 . X*2 . "
    void setVoiceFromString (int voice, const juce::String& steps);

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& tree);
};

// Det som gäller just nu (läses en gång per block / per export).
struct Settings
{
    double stepPpq = 0.25;
    float swing = 0.0f;                           // 0..1, fördröjer vartannat steg upp till 1/3 steg
    float humanize = 0.0f;                        // 0..1, små variationer i tid och styrka
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
// Bara ljud i voiceMask som har slag tas med. Tonlängd: 1/32 (kortare vid ratchet).
juce::MidiFile renderMidiFile (const Pattern& p, const Settings& s, double fromPpq, double toPpq, uint32_t voiceMask,
                               double bpm, int timeSigNum, int timeSigDen);
} // namespace slagg::seq
