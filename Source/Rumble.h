#pragma once

#include "Voices.h"
#include <vector>

// Muller (rumble): det klassiska "drivet" i mörk techno. Kicken skickas in i en lång, mörk efterklang,
// som lågpassfiltreras, mättas och "duckas" av varje kick – så att mullret sväller upp mellan slagen
// och ger en pumpande, rullande bas under kicken.
namespace slagg::dsp
{
struct RumbleParams
{
    float amount = 0;     // 0..1 (0 = av)
    float decay = 0.5f;   // 0..1 -> 0,8–8 s efterklang
    float tone = 160;     // Hz, lågpassfiltrets brytfrekvens
    float drive = 0.4f;   // 0..1 mättning
    float duck = 0.7f;    // 0..1 hur mycket varje kick trycker ner mullret
};

class Rumble
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        // Fyra fördröjningslinjer (ömsesidigt "ojämna" längder) – ett litet FDN-rum.
        static constexpr float ms[] { 37.1f, 43.7f, 53.3f, 61.9f };
        for (size_t i = 0; i < 4; ++i)
        {
            lines[i].assign ((size_t) std::max (8.0f, ms[i] * 0.001f * sr) + 1, 0.0f);
            pos[i] = 0;
            damp[i] = 0;
        }
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        damp.fill (0);
        lastDecay = -1.0f;
        lp.reset(); lp2.reset(); hp.reset();
        duckEnv = 0;
    }

    // Kallas när kicken slår. releaseSeconds: hur snabbt mullret sväller tillbaka (följer tempot).
    void duckTrigger (float releaseSeconds) noexcept
    {
        duckEnv = 1.0f;
        duckCoef = std::exp (-1.0f / std::max (0.01f, releaseSeconds * 0.35f * sr));
    }

    bool isQuiet() const noexcept { return quietSamples > (int) sr; }

    // in: kickens torra signal (mono). Returnerar mullret (mono).
    float process (float in, const RumbleParams& p) noexcept
    {
        if (p.amount <= 0.0001f && isQuiet())
            return 0.0f;

        if (std::abs (p.decay - lastDecay) > 1.0e-6f)
        {
            lastDecay = p.decay;
            const float t60 = 0.8f * std::pow (10.0f, std::clamp (p.decay, 0.0f, 1.0f));   // 0,8–8 s
            for (size_t i = 0; i < 4; ++i)
                gains[i] = std::pow (10.0f, -3.0f * (float) lines[i].size() / (t60 * sr));
        }
        const float send = in * p.amount * 0.6f;

        // FDN: läs, blanda med Hadamard-matris, dämpa diskanten, skriv tillbaka.
        std::array<float, 4> o {};
        for (size_t i = 0; i < 4; ++i)
            o[i] = lines[i][pos[i]];
        const float a = o[0] + o[1], b = o[0] - o[1], c = o[2] + o[3], d = o[2] - o[3];
        const std::array<float, 4> mixed { (a + c) * 0.5f, (b + d) * 0.5f, (a - c) * 0.5f, (b - d) * 0.5f };
        for (size_t i = 0; i < 4; ++i)
        {
            damp[i] += 0.35f * (mixed[i] - damp[i]);              // mörk efterklang
            lines[i][pos[i]] = send + damp[i] * gains[i];
            if (++pos[i] >= lines[i].size()) pos[i] = 0;
        }
        float wet = (o[0] + o[1] + o[2] + o[3]) * 0.5f;

        // Bara det låga: 24 dB lågpass, mättning, bort med likström/infraljud.
        wet = lp.process (wet, p.tone, 0.7f, sr)[0];
        wet = lp2.process (wet, p.tone, 0.7f, sr)[0];
        const float g = 1.0f + p.drive * 8.0f;
        wet = std::tanh (wet * g * 2.0f) / std::sqrt (g);
        wet = hp.process (wet, 32.0f, 0.7f, sr)[2];

        // Ducking: varje kick trycker ner mullret, som sedan sväller upp till nästa slag.
        const float gain = 1.0f - std::clamp (p.duck, 0.0f, 1.0f) * duckEnv;
        duckEnv *= duckCoef;

        const float out = wet * gain * 1.1f;
        quietSamples = std::abs (out) < 1.0e-5f && std::abs (in) < 1.0e-5f ? quietSamples + 1 : 0;
        return out;
    }

private:
    float sr = 44100.0f;
    std::array<std::vector<float>, 4> lines;
    std::array<size_t, 4> pos {};
    std::array<float, 4> damp {}, gains {};
    float lastDecay = -1.0f;
    Svf lp, lp2, hp;
    float duckEnv = 0, duckCoef = 0.999f;
    int quietSamples = 1 << 30;
};
} // namespace slagg::dsp
