#include "Parameters.h"

namespace slagg
{
namespace
{
    using APF = juce::AudioParameterFloat;
    using API = juce::AudioParameterInt;
    using APC = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;

    juce::ParameterID id (const juce::String& s) { return { s, 1 }; }

    juce::AudioParameterFloatAttributes percent()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    }
    juce::AudioParameterFloatAttributes unit (const juce::String& u, int decimals = 0)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [u, decimals] (float v, int) { return juce::String (v, decimals) + " " + u; });
    }
    // Av/på som val: AudioParameterBool återställs inte exakt från sparat läge (pluginval).
    const juce::StringArray offOn { "Off", "On" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (std::make_unique<APC> (id (pid::rate), "Rate", rateNames, 2));
    global->addChild (std::make_unique<APF> (id (pid::swing), "Swing", Range (0.0f, 1.0f), 0.0f, percent()));
    global->addChild (std::make_unique<APF> (id (pid::master), "Master", Range (-36.0f, 6.0f), -3.0f, unit ("dB", 1)));
    layout.add (std::move (global));

    for (int v = 0; v < dsp::numVoices; ++v)
    {
        const auto& info = voiceInfo[(size_t) v];
        const juce::String n (info.shortName);
        auto g = std::make_unique<juce::AudioProcessorParameterGroup> (info.prefix, n, "|");
        Range cutoff (40.0f, 20000.0f);
        cutoff.setSkewForCentre (1200.0f);

        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sTune)), n + " Tune", Range (-24.0f, 24.0f, 0.01f), 0.0f, unit ("st", 1)));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sDecay)), n + " Decay", Range (0.0f, 1.0f), info.defDecay, percent()));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sTone)), n + " " + info.toneLabel, Range (0.0f, 1.0f), info.defTone, percent()));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sSnap)), n + " " + info.snapLabel, Range (0.0f, 1.0f), info.defSnap, percent()));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sDrive)), n + " Drive", Range (0.0f, 1.0f), 0.0f, percent()));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sCutoff)), n + " Cutoff", cutoff, 20000.0f, unit ("Hz")));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sReso)), n + " Reso", Range (0.0f, 1.0f), 0.0f, percent()));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sLevel)), n + " Level", Range (0.0f, 1.0f), info.defLevel, percent()));
        g->addChild (std::make_unique<APF> (id (pid::id (v, pid::sPan)), n + " Pan", Range (-1.0f, 1.0f), 0.0f, percent()));
        g->addChild (std::make_unique<APC> (id (pid::id (v, pid::sOut)), n + " Output", juce::StringArray { "Main", "Own" }, 0));
        g->addChild (std::make_unique<APC> (id (pid::id (v, pid::sMute)), n + " Mute", offOn, 0));
        g->addChild (std::make_unique<APC> (id (pid::id (v, pid::sSeq)), n + " Sequencer", offOn, 1));
        g->addChild (std::make_unique<API> (id (pid::id (v, pid::sLen)), n + " Steps", 1, 32, 16));
        layout.add (std::move (g));
    }
    return layout;
}

void ParamPointers::bind (juce::AudioProcessorValueTreeState& s)
{
    auto p = [&s] (const juce::String& name)
    {
        auto* v = s.getRawParameterValue (name);
        jassert (v != nullptr);
        return v;
    };
    rate = p (pid::rate); swing = p (pid::swing); master = p (pid::master);
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        auto& x = voice[(size_t) v];
        x.tune = p (pid::id (v, pid::sTune));     x.decay = p (pid::id (v, pid::sDecay));
        x.tone = p (pid::id (v, pid::sTone));     x.snap = p (pid::id (v, pid::sSnap));
        x.drive = p (pid::id (v, pid::sDrive));   x.cutoff = p (pid::id (v, pid::sCutoff));
        x.reso = p (pid::id (v, pid::sReso));     x.level = p (pid::id (v, pid::sLevel));
        x.pan = p (pid::id (v, pid::sPan));       x.out = p (pid::id (v, pid::sOut));
        x.mute = p (pid::id (v, pid::sMute));     x.seq = p (pid::id (v, pid::sSeq));
        x.len = p (pid::id (v, pid::sLen));
    }
}

dsp::VoiceParams ParamPointers::voiceParams (int v) const
{
    const auto& x = voice[(size_t) v];
    dsp::VoiceParams r;
    r.tune = x.tune->load();   r.decay = x.decay->load(); r.tone = x.tone->load(); r.snap = x.snap->load();
    r.drive = x.drive->load(); r.cutoff = x.cutoff->load(); r.reso = x.reso->load();
    r.level = x.level->load(); r.pan = x.pan->load();
    return r;
}
} // namespace slagg
