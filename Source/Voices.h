#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Analoga trummodeller. En röst per ljud, monofon (en trumma slår om sig själv).
// Allt är skrivet för att låta "analogt": exponentiella kurvor, mättning, lite brus i allt.
namespace slagg::dsp
{
inline constexpr int numVoices = 10;
enum VoiceType { kick = 0, snare, clap, rim, closedHat, openHat, cymbal, tomLow, tomMid, tomHigh };

inline constexpr float twoPi = 6.28318530718f;

struct Rng
{
    uint32_t s = 0x12345678u;
    float bipolar() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }
};

inline float softClip (float x) noexcept { return std::tanh (x); }

// Exponentiell avklingning: "seconds" = tid till -60 dB.
inline float decayCoef (float seconds, float sr) noexcept
{
    return std::exp (std::log (0.001f) / std::max (1.0e-4f, seconds * sr));
}

// Tillståndsvariabelfilter (TPT/Zavalishin) – stabilt även när cutoff ändras snabbt.
struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() noexcept { ic1 = ic2 = 0; }
    // Returnerar {lp, bp, hp}.
    std::array<float, 3> process (float x, float cutoff, float q, float sr) noexcept
    {
        const float g = std::tan (3.14159265f * std::clamp (cutoff, 20.0f, sr * 0.45f) / sr);
        const float k = 1.0f / std::max (0.3f, q);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + g * a1 * v3;
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }
};

// Bandbegränsad fyrkant (polyBLEP) till hihat-metallen.
struct Square
{
    float phase = 0;
    static float blep (float t, float dt) noexcept
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    float process (float dt) noexcept
    {
        float out = phase < 0.5f ? 1.0f : -1.0f;
        out += blep (phase, dt);
        float t2 = phase + 0.5f;
        if (t2 >= 1.0f) t2 -= 1.0f;
        out -= blep (t2, dt);
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
};

// Parametrar som rösten läser (riktiga enheter, sätts en gång per block).
struct VoiceParams
{
    float tune = 0;        // halvtoner -24..24
    float decay = 0.5f;    // 0..1 (tolkas per ljud)
    float tone = 0.5f;     // 0..1 (Sweep / Tone)
    float snap = 0.3f;     // 0..1 (Click / Snappy / Noise …)
    float drive = 0;       // 0..1
    float cutoff = 20000;  // Hz
    float reso = 0;        // 0..1
    float level = 0.8f;    // 0..1
    float pan = 0;         // -1..1
};

class DrumVoice
{
public:
    void prepare (VoiceType t, float sampleRate, uint32_t seed)
    {
        type = t;
        sr = sampleRate;
        rng.s = seed | 1u;
        reset();
    }

    void reset()
    {
        amp = 0; pitchEnv = 0; noiseEnv = 0; clickEnv = 0; chokeGain = 1; choking = false;
        phase1 = phase2 = 0; filter.reset(); colour.reset(); colour2.reset();
        for (auto& s : squares) s.phase = 0;
        sampleCount = 0;
    }

    bool isActive() const noexcept { return amp > 1.0e-5f || noiseEnv > 1.0e-5f; }

    void trigger (float velocity, const VoiceParams& p)
    {
        vel = std::clamp (velocity, 0.0f, 1.0f);
        velGain = 0.25f + 0.75f * vel;
        amp = 1.0f;
        noiseEnv = 1.0f;
        clickEnv = 1.0f;
        pitchEnv = 1.0f;
        chokeGain = 1.0f;
        choking = false;
        sampleCount = 0;
        // Analoga trummor startar inte alltid i exakt samma fas – lite variation.
        phase1 = type == kick || type >= tomLow ? 0.0f : 0.5f + 0.5f * rng.bipolar();
        phase2 = 0.0f;
        setup (p);
    }

    // Kort uttoning (öppen hihat stryps av stängd).
    void choke() noexcept { if (isActive()) choking = true; }

    // Läs nya parametrar (kallas en gång per block, även medan rösten klingar).
    void setup (const VoiceParams& p)
    {
        params = p;
        const float d = std::clamp (p.decay, 0.0f, 1.0f);
        auto range = [d] (float lo, float hi) { return lo * std::pow (hi / lo, d); };
        const float tuneRatio = std::exp2 (p.tune / 12.0f);

        switch (type)
        {
            case kick:
                baseHz = 48.0f * tuneRatio;
                ampCoef = decayCoef (range (0.12f, 2.2f), sr);
                pitchCoef = decayCoef (0.035f + 0.08f * (1.0f - p.tone), sr);
                sweep = 0.5f + 7.0f * p.tone;               // hur högt pitchen startar
                clickCoef = decayCoef (0.006f, sr);
                break;
            case snare:
                baseHz = 185.0f * tuneRatio;
                ampCoef = decayCoef (range (0.06f, 0.5f), sr);
                noiseCoef = decayCoef (range (0.08f, 0.9f), sr);
                pitchCoef = decayCoef (0.02f, sr);
                sweep = 0.4f;
                break;
            case clap:
                baseHz = 1100.0f * tuneRatio;
                noiseCoef = decayCoef (range (0.08f, 1.2f), sr);
                ampCoef = decayCoef (0.009f, sr);           // varje "smäll"
                break;
            case rim:
                baseHz = 480.0f * tuneRatio;
                ampCoef = decayCoef (range (0.015f, 0.18f), sr);
                clickCoef = decayCoef (0.003f, sr);
                break;
            case closedHat:
            case openHat:
            case cymbal:
            {
                baseHz = (type == cymbal ? 0.8f : 1.0f) * tuneRatio;
                const float lo = type == closedHat ? 0.02f : (type == openHat ? 0.12f : 0.4f);
                const float hi = type == closedHat ? 0.35f : (type == openHat ? 1.6f : 5.0f);
                ampCoef = decayCoef (range (lo, hi), sr);
                break;
            }
            case tomLow:
            case tomMid:
            case tomHigh:
                baseHz = (type == tomLow ? 85.0f : (type == tomMid ? 125.0f : 175.0f)) * tuneRatio;
                ampCoef = decayCoef (range (0.12f, 1.6f), sr);
                pitchCoef = decayCoef (0.05f + 0.1f * (1.0f - p.tone), sr);
                sweep = 0.1f + 1.4f * p.tone;
                noiseCoef = decayCoef (0.03f, sr);
                break;
        }
        chokeCoef = decayCoef (0.012f, sr);
    }

    // Ett sampel, mono.
    float process() noexcept
    {
        if (! isActive())
            return 0.0f;

        float x = 0.0f;
        const float noise = rng.bipolar();
        ++sampleCount;

        switch (type)
        {
            case kick:
            {
                const float f = baseHz * (1.0f + sweep * pitchEnv);
                phase1 += f / sr;
                if (phase1 >= 1.0f) phase1 -= 1.0f;
                x = std::sin (twoPi * phase1) * amp;
                // Klick: kort brus + spik, högpassat.
                const auto c = colour.process (noise, 3500.0f, 0.7f, sr);
                x += (c[2] * 0.6f + (sampleCount < 3 ? 1.0f : 0.0f)) * clickEnv * params.snap * 0.9f;
                pitchEnv *= pitchCoef;
                clickEnv *= clickCoef;
                amp *= ampCoef;
                break;
            }
            case snare:
            {
                const float f = baseHz * (1.0f + sweep * pitchEnv);
                phase1 += f / sr;            if (phase1 >= 1.0f) phase1 -= 1.0f;
                phase2 += f * 1.47f / sr;    if (phase2 >= 1.0f) phase2 -= 1.0f;
                const float body = (std::sin (twoPi * phase1) + 0.6f * std::sin (twoPi * phase2)) * amp;
                const auto n = colour.process (noise, 1800.0f + 5000.0f * params.tone, 0.6f, sr);
                const float snappy = (n[2] * 0.7f + n[1] * 0.5f) * noiseEnv;
                x = body * (1.0f - 0.6f * params.tone) * 0.8f + snappy * (0.3f + 1.2f * params.snap);
                pitchEnv *= pitchCoef;
                amp *= ampCoef;
                noiseEnv *= noiseCoef;
                break;
            }
            case clap:
            {
                // Tre snabba smällar (som flera händer) och sedan en svans.
                const float t = (float) sampleCount / sr;
                const float spread = 0.006f + 0.012f * params.snap;
                const int burst = (int) (t / spread);
                if (burst < 3 && t - (float) burst * spread < 1.0f / sr)
                    amp = 1.0f;
                const auto n = colour.process (noise, baseHz * (0.7f + 1.6f * params.tone), 1.6f, sr);
                const float bursts = burst < 3 ? amp : 0.0f;
                x = n[1] * (bursts * 1.6f + noiseEnv * (burst >= 3 ? 0.9f : 0.3f)) * 1.4f;
                amp *= ampCoef;
                if (burst >= 2) noiseEnv *= noiseCoef;
                break;
            }
            case rim:
            {
                phase1 += baseHz / sr;           if (phase1 >= 1.0f) phase1 -= 1.0f;
                phase2 += baseHz * 3.55f / sr;   if (phase2 >= 1.0f) phase2 -= 1.0f;
                const float tone = std::sin (twoPi * phase1) * (1.0f - 0.5f * params.tone)
                                 + std::sin (twoPi * phase2) * (0.3f + 0.7f * params.tone);
                const auto c = colour.process (noise, 4000.0f, 1.0f, sr);
                x = tone * amp * 0.8f + c[1] * clickEnv * (0.5f + params.snap);
                amp *= ampCoef;
                clickEnv *= clickCoef;
                break;
            }
            case closedHat:
            case openHat:
            case cymbal:
            {
                // Sex fyrkanter i oharmoniska förhållanden ger metallen.
                static constexpr float ratios[] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                float metal = 0.0f;
                for (size_t i = 0; i < 6; ++i)
                    metal += squares[i].process (std::min (0.45f, ratios[i] * baseHz * 1.7f / sr));
                metal = metal * (1.0f - 0.7f * params.snap) + noise * 2.5f * params.snap;
                const float centre = (type == cymbal ? 5000.0f : 7500.0f) * (0.6f + 0.9f * params.tone);
                const auto b = colour.process (metal, centre, 1.2f, sr);
                const auto h = colour2.process (b[1], type == cymbal ? 3000.0f : 6000.0f, 0.7f, sr);
                x = h[2] * amp * 0.55f;
                amp *= ampCoef;
                break;
            }
            case tomLow:
            case tomMid:
            case tomHigh:
            {
                const float f = baseHz * (1.0f + sweep * pitchEnv);
                phase1 += f / sr;
                if (phase1 >= 1.0f) phase1 -= 1.0f;
                const float body = std::sin (twoPi * phase1) + 0.15f * std::sin (2.0f * twoPi * phase1);
                const auto n = colour.process (noise, baseHz * 6.0f, 0.8f, sr);
                x = body * amp + n[1] * noiseEnv * params.snap * 1.5f;
                pitchEnv *= pitchCoef;
                amp *= ampCoef;
                noiseEnv *= noiseCoef;
                break;
            }
        }

        if (type != clap && type != snare && type < tomLow)
            noiseEnv = 0.0f;   // bara snare, clap och toms använder brushöljet

        if (choking)
        {
            chokeGain *= chokeCoef;
            if (chokeGain < 1.0e-4f) { amp = noiseEnv = 0.0f; choking = false; }
        }

        // Drive, filter och nivå.
        x *= velGain * chokeGain;
        if (params.drive > 0.001f)
        {
            const float g = 1.0f + params.drive * 9.0f;
            x = softClip (x * g) / std::sqrt (g) * (1.0f + params.drive * 0.6f);
        }
        if (params.cutoff < 19000.0f || params.reso > 0.01f)
            x = filter.process (x, params.cutoff, 0.6f + params.reso * 9.0f, sr)[0];

        return x * params.level;
    }

private:
    VoiceType type = kick;
    float sr = 44100.0f;
    VoiceParams params;
    Rng rng;
    Svf filter, colour, colour2;
    std::array<Square, 6> squares {};

    float vel = 1, velGain = 1;
    float amp = 0, ampCoef = 0.999f;
    float pitchEnv = 0, pitchCoef = 0.99f, sweep = 0;
    float noiseEnv = 0, noiseCoef = 0.99f;
    float clickEnv = 0, clickCoef = 0.9f;
    float chokeGain = 1, chokeCoef = 0.99f;
    bool choking = false;
    float baseHz = 50;
    float phase1 = 0, phase2 = 0;
    int64_t sampleCount = 0;
};
} // namespace slagg::dsp
