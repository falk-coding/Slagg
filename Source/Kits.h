#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Pattern.h"

// Fabrikskit: ljudinställningar + mönster. Nya kit läggs ALLTID sist (programnumren sparas i projekt).
namespace slagg
{
struct Kit
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;         // parameter-ID -> värde (riktiga enheter)
    std::array<juce::String, dsp::numVoices> steps {};          // se seq::Pattern::setVoiceFromString, tomt = tyst
    uint32_t seed = 1;
};

const std::vector<Kit>& getKits();

// Sätter alla parametrar till standard (utom Out = användarens utgångsval), sedan kitets värden och mönster.
void applyKit (juce::AudioProcessorValueTreeState& apvts, const Kit& kit, seq::Pattern& pattern);
} // namespace slagg
