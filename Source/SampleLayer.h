#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include "Voices.h"

// Sample-lager per ljud: läsa in WAV/AIFF/FLAC, spara inbäddat i projektet (FLAC 24 bit, base64).
// Allt här körs i meddelandetråden (eller där värden laddar sparat läge) – aldrig i ljudtråden.
namespace slagg
{
inline constexpr double maxSampleSeconds = 10.0;   // längre filer kortas (mono-mixas alltid)

struct SampleData
{
    dsp::SampleBuffer buffer;
    juce::String name;        // filnamnet som visas i remsan
    juce::String path;        // ursprunglig sökväg (reserv om inbäddad data saknas)
    juce::String flacBase64;  // inbäddad ljuddata (görs en gång vid inläsning)
    bool truncated = false;   // filen var längre än maxSampleSeconds
};

using SamplePtr = std::shared_ptr<SampleData>;

// Läser en ljudfil. Returnerar nullptr och fyller i error om den inte gick att läsa.
SamplePtr loadSampleFile (const juce::File& file, juce::String& error);

// Sparat läge: <Sample voice=".." name=".." path=".." rate=".." flac="base64">.
juce::ValueTree sampleToTree (const SampleData& s, int voice);
// Inbäddad data först, sedan sökvägen. nullptr om inget gick.
SamplePtr sampleFromTree (const juce::ValueTree& tree);

// Filändelser som går att dra in.
juce::String supportedSampleWildcard();
bool isSupportedSampleFile (const juce::String& path);
} // namespace slagg
