#include "SampleLayer.h"

namespace slagg
{
namespace
{
    // WAV, AIFF, FLAC, Ogg (+ CoreAudio-format på Mac). Skapas per anrop (billigt, inga statiska objekt).
    struct Formats : juce::AudioFormatManager
    {
        Formats() { registerBasicFormats(); }
    };

    // Mono-mixar och kortar till maxSampleSeconds.
    SamplePtr readFrom (juce::AudioFormatReader& reader, juce::String& error)
    {
        if (reader.sampleRate < 1000.0 || reader.lengthInSamples <= 0 || reader.numChannels == 0)
        {
            error = juce::String::fromUTF8 ("Filen är tom eller trasig (empty or broken file)");
            return nullptr;
        }
        const auto maxLen = (juce::int64) (maxSampleSeconds * reader.sampleRate);
        const int len = (int) std::min (reader.lengthInSamples, maxLen);
        const int chans = (int) std::min (reader.numChannels, 8u);
        juce::AudioBuffer<float> buf (chans, len);
        if (! reader.read (&buf, 0, len, 0, true, chans > 1))
        {
            error = juce::String::fromUTF8 ("Kunde inte läsa ljudet (could not read audio)");
            return nullptr;
        }
        auto s = std::make_shared<SampleData>();
        s->buffer.rate = reader.sampleRate;
        s->buffer.data.assign ((size_t) len, 0.0f);
        const float g = 1.0f / (float) chans;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < len; ++i)
                s->buffer.data[(size_t) i] += d[i] * g;
        }
        for (auto& x : s->buffer.data)
            x = std::isfinite (x) ? juce::jlimit (-1.0f, 1.0f, x) : 0.0f;
        s->truncated = reader.lengthInSamples > maxLen;
        return s;
    }

    juce::String encodeFlac (const dsp::SampleBuffer& b)
    {
        juce::MemoryBlock mb;
        {
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream> (mb, false);
            juce::FlacAudioFormat flac;
            const auto opts = juce::AudioFormatWriterOptions()
                                  .withSampleRate (b.rate).withNumChannels (1).withBitsPerSample (24);
            auto w = flac.createWriterFor (out, opts);
            if (w == nullptr)
                return {};
            const float* chans[] { b.data.data() };
            if (! w->writeFromFloatArrays (chans, 1, (int) b.data.size()))
                return {};
        }   // skrivaren stängs -> FLAC-strömmen avslutas
        return mb.toBase64Encoding();
    }
}

SamplePtr loadSampleFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = juce::String::fromUTF8 ("Filen finns inte (file not found)");
        return nullptr;
    }
    Formats f;
    std::unique_ptr<juce::AudioFormatReader> reader (f.createReaderFor (file));
    if (reader == nullptr)
    {
        error = juce::String::fromUTF8 ("Okänt ljudformat – använd WAV, AIFF eller FLAC (unsupported format)");
        return nullptr;
    }
    auto s = readFrom (*reader, error);
    if (s == nullptr)
        return nullptr;
    s->name = file.getFileName();
    s->path = file.getFullPathName();
    s->flacBase64 = encodeFlac (s->buffer);
    return s;
}

juce::ValueTree sampleToTree (const SampleData& s, int voice)
{
    juce::ValueTree t ("Sample");
    t.setProperty ("voice", voice, nullptr);
    t.setProperty ("name", s.name, nullptr);
    t.setProperty ("path", s.path, nullptr);
    t.setProperty ("rate", s.buffer.rate, nullptr);
    t.setProperty ("flac", s.flacBase64, nullptr);
    return t;
}

SamplePtr sampleFromTree (const juce::ValueTree& t)
{
    juce::String error;
    const auto data = t.getProperty ("flac").toString();
    if (data.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (data))
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> reader (
                flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
            if (reader != nullptr)
            {
                if (auto s = readFrom (*reader, error))
                {
                    s->name = t.getProperty ("name").toString();
                    s->path = t.getProperty ("path").toString();
                    s->buffer.rate = (double) t.getProperty ("rate", reader->sampleRate);
                    s->flacBase64 = data;
                    return s;
                }
            }
        }
    }
    // Reserv: originalfilen (om den finns på den här datorn).
    const auto path = t.getProperty ("path").toString();
    if (juce::File::isAbsolutePath (path))
        if (auto s = loadSampleFile (juce::File (path), error))
        {
            if (t.hasProperty ("name"))
                s->name = t.getProperty ("name").toString();
            return s;
        }
    return nullptr;
}

juce::String supportedSampleWildcard()
{
    return Formats().getWildcardForAllFormats();
}

bool isSupportedSampleFile (const juce::String& path)
{
    return Formats().findFormatForFileExtension (juce::File (path).getFileExtension()) != nullptr;
}
} // namespace slagg
