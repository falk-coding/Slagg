#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Voices.h"

// Parameter-ID:n. ÄNDRA ALDRIG ett ID efter första release – då tappar sparade projekt sina värden.
// Per ljud: <prefix><Suffix>, t.ex. "kickTune", "chhDecay". Nya parametrar: standardvärde som inte ändrar ljudet.
namespace slagg
{
struct VoiceInfo
{
    const char* prefix;     // ID-prefix
    const char* titleUtf8;  // "SVENSKA|English" (sektionsrubrik)
    const char* shortName;  // kort namn i rutnätet och i MIDI-filer
    int midiNote;           // GM-trumnot (in och ut)
    const char* toneLabel;  // vad "Tone" heter på just det här ljudet
    const char* snapLabel;  // vad "Snap" heter
    float defLevel;
    float defDecay;
    float defTone;
    float defSnap;
};

inline const std::array<VoiceInfo, dsp::numVoices> voiceInfo {{
    { "kick",  "BASTRUMMA|Kick",        "Kick",       36, "Sweep", "Click",  0.85f, 0.45f, 0.45f, 0.25f },
    { "snare", "VIRVEL|Snare",          "Snare",      38, "Tone",  "Snappy", 0.7f,  0.4f,  0.5f,  0.5f  },
    { "clap",  "HANDKLAPP|Clap",        "Clap",       39, "Tone",  "Spread", 0.7f,  0.35f, 0.5f,  0.4f  },
    { "rim",   "KANT|Rim",              "Rim",        37, "Tone",  "Click",  0.6f,  0.3f,  0.5f,  0.4f  },
    { "chh",   "HIHAT STÄNGD|Closed hat","Closed hat", 42, "Tone",  "Noise",  0.55f, 0.3f,  0.5f,  0.25f },
    { "ohh",   "HIHAT ÖPPEN|Open hat",  "Open hat",   46, "Tone",  "Noise",  0.5f,  0.4f,  0.5f,  0.25f },
    { "cym",   "CYMBAL|Cymbal",         "Cymbal",     49, "Tone",  "Noise",  0.45f, 0.5f,  0.4f,  0.3f  },
    { "tomL",  "TOM LÅG|Tom low",       "Tom low",    41, "Sweep", "Noise",  0.7f,  0.45f, 0.4f,  0.2f  },
    { "tomM",  "TOM MELLAN|Tom mid",    "Tom mid",    45, "Sweep", "Noise",  0.7f,  0.45f, 0.4f,  0.2f  },
    { "tomH",  "TOM HÖG|Tom high",      "Tom high",   48, "Sweep", "Noise",  0.7f,  0.45f, 0.4f,  0.2f  },
}};

namespace pid
{
    // Globala
    inline constexpr const char* rate   = "rate";
    inline constexpr const char* swing  = "swing";
    inline constexpr const char* master = "master";
    // v0.2
    inline constexpr const char* humanize    = "humanize";     // 0 = av
    inline constexpr const char* rumbleAmt   = "rumbleAmt";    // 0 = av
    inline constexpr const char* rumbleDecay = "rumbleDecay";
    inline constexpr const char* rumbleTone  = "rumbleTone";
    inline constexpr const char* rumbleDrive = "rumbleDrive";
    inline constexpr const char* rumbleDuck  = "rumbleDuck";

    // Per ljud (suffix)
    inline constexpr const char* sTune   = "Tune";
    inline constexpr const char* sDecay  = "Decay";
    inline constexpr const char* sTone   = "Tone";
    inline constexpr const char* sSnap   = "Snap";
    inline constexpr const char* sDrive  = "Drive";
    inline constexpr const char* sCutoff = "Cutoff";
    inline constexpr const char* sReso   = "Reso";
    inline constexpr const char* sLevel  = "Level";
    inline constexpr const char* sPan    = "Pan";
    inline constexpr const char* sOut    = "Out";    // Main / Own (egen utgång)
    inline constexpr const char* sMute   = "Mute";   // Off / On – tystar ljudet helt (även MIDI in)
    inline constexpr const char* sSeq    = "Seq";    // Off / On – sekvensern spelar ljudet (av efter MIDI-dragning)
    inline constexpr const char* sLen    = "Len";    // 1..32 steg (polymeter)

    inline juce::String id (int voice, const char* suffix) { return juce::String (voiceInfo[(size_t) voice].prefix) + suffix; }
}

inline const juce::StringArray rateNames { "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline double rateToPpq (int index)
{
    static constexpr double v[] { 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return v[juce::jlimit (0, 4, index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Råa pekare för ljudtråden.
struct ParamPointers
{
    struct Voice
    {
        std::atomic<float>* tune {}; std::atomic<float>* decay {}; std::atomic<float>* tone {}; std::atomic<float>* snap {};
        std::atomic<float>* drive {}; std::atomic<float>* cutoff {}; std::atomic<float>* reso {}; std::atomic<float>* level {};
        std::atomic<float>* pan {}; std::atomic<float>* out {}; std::atomic<float>* mute {}; std::atomic<float>* seq {};
        std::atomic<float>* len {};
    };
    std::array<Voice, dsp::numVoices> voice {};
    std::atomic<float>* rate {}; std::atomic<float>* swing {}; std::atomic<float>* master {};
    std::atomic<float>* humanize {};
    std::atomic<float>* rumbleAmt {}; std::atomic<float>* rumbleDecay {}; std::atomic<float>* rumbleTone {};
    std::atomic<float>* rumbleDrive {}; std::atomic<float>* rumbleDuck {};

    void bind (juce::AudioProcessorValueTreeState& apvts);
    dsp::VoiceParams voiceParams (int v) const;
};
} // namespace slagg
