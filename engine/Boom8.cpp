#include "Boom8.h"

#include <algorithm>
#include <cmath>

namespace boom8
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double dt = 1.0 / Boom8::sampleRate;

// lo..hi along a curve that feels even on a knob (equal ratios per turn).
float expMap(const float v01, const float lo, const float hi)
{
    return lo * std::pow(hi / lo, std::clamp(v01, 0.0f, 1.0f));
}

float unit(const float knob)
{
    return std::clamp(knob / 100.0f, 0.0f, 1.0f);
}

// Per-sample multiplier for an exponential decay with time constant `seconds`.
float decayFor(const float seconds)
{
    return static_cast<float>(std::exp(-dt / std::max(seconds, 0.0002f)));
}

// The six square oscillators the cymbal, hats and cowbell share (the cowbell
// uses the last two), in Hz, as measured on the hardware.
constexpr std::array<double, 6> kMetalHz { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

// Tom and conga pitch ranges over the TUNING knob, Hz.
struct Range { float lo, hi; };
constexpr Range kTomRange[3] { { 80.0f, 100.0f }, { 120.0f, 160.0f }, { 165.0f, 220.0f } };
constexpr Range kCongaRange[3] { { 165.0f, 220.0f }, { 250.0f, 310.0f }, { 370.0f, 455.0f } };

// Each voice's share of the mix, so the defaults balance (from the measured
// peaks of the test, test/EngineTest.cpp).
constexpr std::array<float, voiceCount> kTrim {
    0.92f,   // BD
    0.90f,   // SD
    0.85f, 0.85f, 0.85f,   // LT MT HT
    1.00f,   // RS
    0.95f,   // CP
    0.75f,   // CB
    1.30f,   // CY
    2.70f,   // OH
    3.60f,   // CH
    0.75f, 0.74f, 0.73f,   // LC MC HC
    0.50f,   // CL
    0.34f,   // MA
};

// The level knobs are audio taper.
float levelGain(const float knob)
{
    const float x = unit(knob);
    return x * x;
}

// A band-limited step for the square oscillators (polyBLEP).
float blep(double p, const double inc)
{
    if (p < inc)
    {
        p /= inc;
        return static_cast<float>(p + p - p * p - 1.0);
    }
    if (p > 1.0 - inc)
    {
        p = (p - 1.0) / inc;
        return static_cast<float>(p * p + p + p + 1.0);
    }
    return 0.0f;
}

float sine(const double phase)
{
    return static_cast<float>(std::sin(2.0 * kPi * phase));
}

// The output stage: clean until it nears full scale, then soft.
float outputStage(const float y)
{
    if (std::abs(y) <= 0.8f)
        return y;
    return std::copysign(0.8f + 0.2f * std::tanh((std::abs(y) - 0.8f) / 0.2f), y);
}

void advance(double& phase, const double hz)
{
    phase += hz * dt;
    phase -= std::floor(phase);
}
}

float dbToGain(const float db)
{
    return std::pow(10.0f, db / 20.0f);
}

void Boom8::Svf::set(const float hz, const float q)
{
    g = static_cast<float>(std::tan(kPi * std::min(hz, 20000.0f) / sampleRate));
    k = 1.0f / q;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void Boom8::Svf::run(const float x, float& low, float& band, float& high)
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    low = v2;
    band = v1;
    high = x - k * v1 - v2;
}

void Boom8::OnePole::lowpass(const float hz)
{
    a = static_cast<float>(1.0 - std::exp(-2.0 * kPi * std::min(hz, 20000.0f) * dt));
}

Boom8::Boom8()
{
    // The oscillators run all the time, from wherever they were at power on.
    for (std::size_t i = 0; i < metalPhase.size(); ++i)
        metalPhase[i] = 0.137 * static_cast<double>(i + 1);
}

float Boom8::noise()
{
    // xorshift32
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<float>(static_cast<std::int32_t>(rng)) * (1.0f / 2147483648.0f);
}

bool Boom8::sounding(const int voice) const
{
    return voice >= 0 && voice < voiceCount && voices[static_cast<std::size_t>(voice)].active;
}

float Boom8::tomHz(const int voice, const float tuning)
{
    const float t = unit(tuning);
    if (voice >= LT && voice <= HT)
    {
        const auto& r = kTomRange[voice - LT];
        return expMap(t, r.lo, r.hi);
    }
    if (voice >= LC && voice <= HC)
    {
        const auto& r = kCongaRange[voice - LC];
        return expMap(t, r.lo, r.hi);
    }
    return 0.0f;
}

float Boom8::bdHz(const float bdTune)
{
    return 49.0f * std::pow(2.0f, std::clamp(bdTune, -12.0f, 12.0f) / 12.0f);
}

float Boom8::bdDecaySeconds(const float decay)
{
    return expMap(unit(decay), 0.045f, 0.42f);
}

float Boom8::cyDecaySeconds(const float decay)
{
    return expMap(unit(decay), 0.18f, 1.0f);
}

float Boom8::ohDecaySeconds(const float decay)
{
    return expMap(unit(decay), 0.04f, 0.26f);
}

float Boom8::lengthScale(const float decay)
{
    const float d = unit(decay);
    return d < 0.5f ? expMap(d * 2.0f, 0.25f, 1.0f) : expMap(d * 2.0f - 1.0f, 1.0f, 1.8f);
}

float Boom8::panOf(const int voice) const
{
    const auto& c = controls;
    switch (voice)
    {
    case BD: return c.bdPan;
    case SD: return c.sdPan;
    case LT:
    case LC: return c.ltPan;
    case MT:
    case MC: return c.mtPan;
    case HT:
    case HC: return c.htPan;
    case RS:
    case CL: return c.rsPan;
    case CP:
    case MA: return c.cpPan;
    case CB: return c.cbPan;
    case CY: return c.cyPan;
    case OH: return c.ohPan;
    case CH: return c.chPan;
    default: return 50.0f;
    }
}

float Boom8::accentGain(const float accent)
{
    return 1.0f + unit(accent);   // up to +6 dB
}

void Boom8::trigger(const int voice, const int velocity)
{
    if (voice < 0 || voice >= voiceCount || velocity <= 0)
        return;
    const auto& c = controls;
    State& s = voices[static_cast<std::size_t>(voice)];
    const bool wasRinging = s.active && s.env1 > 0.05f;

    // A new hit restarts the envelopes; a drum still ringing keeps its phase,
    // as a resonator struck again does.
    const double keepPhase1 = wasRinging ? s.phase1 : 0.0;
    const double keepPhase2 = wasRinging ? s.phase2 : 0.0;
    const Svf keepF1 = s.f1, keepF2 = s.f2, keepF3 = s.f3;
    s = State {};
    s.active = true;
    s.phase1 = keepPhase1;
    s.phase2 = keepPhase2;
    if (wasRinging)
    {
        s.f1 = keepF1;
        s.f2 = keepF2;
        s.f3 = keepF3;
    }

    const bool accented = velocity >= accentVelocity;
    const float v01 = std::clamp(velocity, 1, 127) / 127.0f;
    const float sens = unit(c.velocity);
    s.gain = kTrim[static_cast<std::size_t>(voice)] * ((1.0f - sens) + sens * std::pow(v01, 1.6f));
    if (accented)
        s.gain *= accentGain(c.accent);
    // A harder trigger pulse bends a drum's pitch further.
    const float strike = accented ? 1.0f + 0.5f * unit(c.accent) : 1.0f;
    // Pan: the balance law, so a centred voice is as loud as before and a
    // hard-panned one keeps its level on its side.
    const float pan = unit(panOf(voice));
    s.panL = std::min(1.0f, 2.0f * (1.0f - pan));
    s.panR = std::min(1.0f, 2.0f * pan);

    switch (voice)
    {
    case BD:
        s.gain *= levelGain(c.bdLevel);
        s.freq1 = bdHz(c.bdTune);
        s.bend = 0.20f * strike;
        s.env1 = 1.0f;
        s.coef1 = decayFor(bdDecaySeconds(c.bdDecay));
        s.env2 = 1.0f;
        s.coef2 = decayFor(0.0007f);   // the trigger pulse's edge
        s.env3 = 1.0f;
        s.coef3 = decayFor(0.018f);    // the pitch glide
        s.mix = unit(c.bdTone);
        s.p1.lowpass(expMap(s.mix, 220.0f, 5200.0f));
        break;
    case SD:
    {
        s.gain *= levelGain(c.sdLevel);
        s.freq1 = 180.0f;
        s.freq2 = 330.0f;
        s.bend = 0.05f * strike;
        s.env1 = s.env2 = s.env3 = 1.0f;
        const float length = lengthScale(c.sdDecay);
        s.coef1 = decayFor(0.050f * length);
        s.coef2 = decayFor(0.034f * length);
        s.coef3 = decayFor(0.072f * length);   // the snares
        s.mix = unit(c.sdTone);
        s.f1.set(1800.0f, 0.7f);
        s.p1.lowpass(expMap(s.mix, 3800.0f, 11000.0f));
        s.p2.a = unit(c.sdSnappy);              // (the snappy amount, kept here)
        break;
    }
    case LT:
    case MT:
    case HT:
    {
        const float level = voice == LT ? c.ltLevel : voice == MT ? c.mtLevel : c.htLevel;
        const float tuning = voice == LT ? c.ltTuning : voice == MT ? c.mtTuning : c.htTuning;
        s.gain *= levelGain(level);
        s.freq1 = tomHz(voice, tuning);
        s.bend = 0.14f * strike;
        s.env1 = s.env2 = s.env3 = 1.0f;
        s.coef1 = decayFor(voice == LT ? 0.20f : voice == MT ? 0.16f : 0.13f);
        s.coef2 = decayFor(0.030f);    // the pink noise
        s.coef3 = decayFor(0.045f);    // the pitch glide
        s.p1.lowpass(2600.0f);
        break;
    }
    case LC:
    case MC:
    case HC:
    {
        const float level = voice == LC ? c.ltLevel : voice == MC ? c.mtLevel : c.htLevel;
        const float tuning = voice == LC ? c.ltTuning : voice == MC ? c.mtTuning : c.htTuning;
        s.gain *= levelGain(level);
        s.freq1 = tomHz(voice, tuning);
        s.bend = 0.05f * strike;
        s.env1 = s.env2 = s.env3 = 1.0f;
        s.coef1 = decayFor(voice == LC ? 0.085f : voice == MC ? 0.072f : 0.060f);
        s.coef2 = decayFor(0.0008f);   // the strike
        s.coef3 = decayFor(0.016f);
        break;
    }
    case RS:
        s.gain *= levelGain(c.rsLevel);
        s.freq1 = 455.0f;
        s.freq2 = 1667.0f;
        s.env1 = s.env2 = 1.0f;
        s.coef1 = decayFor(0.0085f);
        s.coef2 = decayFor(0.0050f);
        s.p1.lowpass(200.0f);          // used as a high-pass
        break;
    case CL:
        s.gain *= levelGain(c.rsLevel);
        s.freq1 = 2500.0f;
        s.env1 = 1.0f;
        s.coef1 = decayFor(0.013f);
        break;
    case CP:
        s.gain *= levelGain(c.cpLevel);
        s.length = lengthScale(c.cpDecay);
        s.f1.set(1000.0f, 1.3f);
        s.f2.set(1150.0f, 0.9f);       // the reverb tail's filter
        break;
    case MA:
        s.gain *= levelGain(c.cpLevel);
        s.f1.set(5000.0f, 0.7f);
        s.coef1 = decayFor(0.014f);
        break;
    case CB:
        s.gain *= levelGain(c.cbLevel);
        s.env1 = s.env2 = 1.0f;
        s.coef1 = decayFor(0.0035f);   // the strike
        s.coef2 = decayFor(0.090f);    // the ring
        s.f1.set(1200.0f, 0.9f);
        s.p1.lowpass(300.0f);          // used as a high-pass
        break;
    case CY:
        s.gain *= levelGain(c.cyLevel);
        s.mix = unit(c.cyTone);
        s.env1 = s.env2 = s.env3 = 1.0f;
        s.coef1 = decayFor(cyDecaySeconds(c.cyDecay));   // the low band
        s.coef2 = decayFor(0.15f);                        // the high band
        s.coef3 = decayFor(0.050f);                       // the top
        s.f1.set(3440.0f, 1.3f);
        s.f2.set(7100.0f, 1.0f);
        s.p1.lowpass(9000.0f);          // used as a high-pass
        s.p2.lowpass(800.0f);           // used as a high-pass
        break;
    case OH:
    case CH:
        s.gain *= levelGain(voice == OH ? c.ohLevel : c.chLevel);
        s.env1 = 1.0f;
        s.coef1 = decayFor(voice == OH ? ohDecaySeconds(c.ohDecay) : 0.011f);
        s.f1.set(7100.0f, 1.0f);
        s.p1.lowpass(6000.0f);          // used as a high-pass
        break;
    default:
        break;
    }

    // The closed hat cuts the open hat.
    if (voice == CH && controls.choke && voices[OH].active)
        voices[OH].releaseStep = std::max(voices[OH].releaseStep, static_cast<float>(dt / 0.004));
}

void Boom8::stopAll()
{
    for (auto& s : voices)
        s.active = false;
}

void Boom8::fadeOutAll()
{
    for (auto& s : voices)
        if (s.active)
            s.releaseStep = std::max(s.releaseStep, static_cast<float>(dt / 0.005));
}

float Boom8::renderVoice(const int voice, State& s, const std::array<float, 6>& metal)
{
    s.t += dt;
    float out = 0.0f;

    switch (voice)
    {
    case BD:
    {
        // The resonator rings at its pitch, bent up at the hit; the trigger
        // pulse's edge comes through as the click; TONE is a low-pass after both.
        s.env1 *= s.coef1;
        s.env2 *= s.coef2;
        s.env3 *= s.coef3;
        advance(s.phase1, s.freq1 * (1.0 + s.bend * s.env3));
        const float body = sine(s.phase1) * s.env1;
        const float click = s.env2 * (0.25f + 0.75f * s.mix);
        const float x = s.p1.lp(body + click);
        out = std::tanh(1.3f * x) * (1.0f / std::tanh(1.3f));
        if (s.env1 < 0.0005f && s.t > 0.02)
            s.active = false;
        break;
    }
    case SD:
    {
        // Two resonators (TONE leans the balance to the upper one) and
        // high-passed noise (SNAPPY is its level, TONE its brightness).
        s.env1 *= s.coef1;
        s.env2 *= s.coef2;
        s.env3 *= s.coef3;
        const double bend = 1.0 + s.bend * s.env3;
        advance(s.phase1, s.freq1 * bend);
        advance(s.phase2, s.freq2 * bend);
        const float tone = sine(s.phase1) * s.env1 * (0.85f - 0.4f * s.mix)
                         + sine(s.phase2) * s.env2 * (0.45f + 0.45f * s.mix);
        const float snares = s.p1.lp(s.f1.high(noise())) * s.env3 * s.p2.a * 1.1f;
        out = std::tanh(1.1f * (tone + snares));
        if (s.env1 < 0.0005f && s.env3 < 0.0005f && s.t > 0.02)
            s.active = false;
        break;
    }
    case LT:
    case MT:
    case HT:
    {
        // A resonator falling to its pitch, with a breath of pink noise.
        s.env1 *= s.coef1;
        s.env2 *= s.coef2;
        s.env3 *= s.coef3;
        advance(s.phase1, s.freq1 * (1.0 + s.bend * s.env3));
        const float w = noise();
        s.pink[0] = 0.99765f * s.pink[0] + w * 0.0990460f;   // Kellet's economy pinking filter
        s.pink[1] = 0.96300f * s.pink[1] + w * 0.2965164f;
        s.pink[2] = 0.57000f * s.pink[2] + w * 1.0526913f;
        const float pink = (s.pink[0] + s.pink[1] + s.pink[2] + w * 0.1848f) * 0.25f;
        out = std::tanh(1.1f * (sine(s.phase1) * s.env1 + s.p1.lp(pink) * s.env2 * 0.35f));
        if (s.env1 < 0.0005f && s.t > 0.02)
            s.active = false;
        break;
    }
    case LC:
    case MC:
    case HC:
    {
        // The same resonators tuned up and damped harder.
        s.env1 *= s.coef1;
        s.env2 *= s.coef2;
        s.env3 *= s.coef3;
        advance(s.phase1, s.freq1 * (1.0 + s.bend * s.env3));
        out = std::tanh(1.1f * (sine(s.phase1) * s.env1 + s.env2 * 0.25f));
        if (s.env1 < 0.0005f && s.t > 0.02)
            s.active = false;
        break;
    }
    case RS:
    {
        // Two short resonances through a high-pass, squared off by the VCA.
        s.env1 *= s.coef1;
        s.env2 *= s.coef2;
        advance(s.phase1, s.freq1);
        advance(s.phase2, s.freq2);
        const float x = s.p1.hp(sine(s.phase1) * s.env1 * 0.7f + sine(s.phase2) * s.env2);
        out = std::tanh(4.0f * x) * 0.6f;
        if (s.env1 < 0.0005f && s.t > 0.01)
            s.active = false;
        break;
    }
    case CL:
    {
        s.env1 *= s.coef1;
        advance(s.phase1, s.freq1);
        out = sine(s.phase1) * s.env1;
        if (s.env1 < 0.0005f && s.t > 0.01)
            s.active = false;
        break;
    }
    case CP:
    {
        // Band-passed noise through a sawtooth envelope: three quick claps
        // ~10 ms apart, then the reverb tail.
        const double t = s.t;
        float claps = 0.0f;
        if (t < 0.0315)
        {
            const double slot = std::fmod(t, 0.0105);
            claps = static_cast<float>(std::exp(-slot / 0.0028));
        }
        const double tau = 0.068 * s.length;
        const float tail = t >= 0.021 ? static_cast<float>(std::exp(-(t - 0.021) / tau)) * 0.85f : 0.0f;
        const float n = noise();
        out = std::tanh(2.2f * (s.f1.band(n) * claps * 1.8f + s.f2.band(n) * tail * 0.8f));
        if (t > 0.021 + tau * 7.6)   // -66 dB
            s.active = false;
        break;
    }
    case MA:
    {
        // High-passed noise with a soft attack.
        const float attack = static_cast<float>(std::min(1.0, s.t / 0.0025));
        if (s.t > 0.0025)
            s.env1 = s.env1 == 0.0f ? 1.0f : s.env1 * s.coef1;
        out = s.f1.high(noise()) * (s.t <= 0.0025 ? attack : s.env1) * 0.9f;
        if (s.t > 0.0025 && s.env1 < 0.0005f)
            s.active = false;
        break;
    }
    case CB:
    {
        // The 540 and 800 Hz oscillators, a two-stage envelope, a band-pass.
        s.env1 *= s.coef1;
        s.env2 *= s.coef2;
        const float x = (metal[4] + metal[5]) * 0.5f * (0.6f * s.env1 + 0.4f * s.env2);
        out = s.p1.hp(s.f1.band(x)) * 1.6f;
        if (s.env2 < 0.0005f && s.t > 0.01)
            s.active = false;
        break;
    }
    case CY:
    {
        // All six oscillators into a low and a high band, each with its own
        // envelope; TONE leans from the low band to the high ones.
        s.env1 *= s.coef1;
        s.env2 *= s.coef2;
        s.env3 *= s.coef3;
        float sum = 0.0f;
        for (const float m : metal)
            sum += m;
        sum *= 1.0f / 6.0f;
        const float lowBand = s.f1.band(sum) * s.env1 * (1.0f - 0.7f * s.mix);
        const float highBand = (s.f2.band(sum) * s.env2 + s.p1.hp(sum) * s.env3 * 0.6f) * (0.35f + 0.65f * s.mix);
        out = s.p2.hp(lowBand + highBand) * 1.4f;
        if (s.env1 < 0.0005f && s.env2 < 0.0005f && s.t > 0.02)
            s.active = false;
        break;
    }
    case OH:
    case CH:
    {
        // All six oscillators, band-passed high and high-passed again.
        s.env1 *= s.coef1;
        float sum = 0.0f;
        for (const float m : metal)
            sum += m;
        sum *= 1.0f / 6.0f;
        out = s.p1.hp(s.f1.band(sum)) * s.env1 * 2.2f;
        if (s.env1 < 0.0005f && s.t > 0.005)
            s.active = false;
        break;
    }
    default:
        break;
    }

    if (s.releaseStep > 0.0f)
    {
        s.release -= s.releaseStep;
        if (s.release <= 0.0f)
        {
            s.active = false;
            return 0.0f;
        }
    }
    return out * s.gain * s.release;
}

void Boom8::render(float* left, float* right, const int frames)
{
    const float volume = dbToGain(std::clamp(controls.volume, -24.0f, 6.0f));
    std::array<float, 6> metal {};
    for (int i = 0; i < frames; ++i)
    {
        for (std::size_t m = 0; m < metal.size(); ++m)
        {
            const double inc = kMetalHz[m] * dt;
            double& p = metalPhase[m];
            p += inc;
            p -= std::floor(p);
            const double q = p + 0.5 - std::floor(p + 0.5);
            metal[m] = (p < 0.5 ? 1.0f : -1.0f) + blep(p, inc) - blep(q, inc);
        }
        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < voiceCount; ++v)
        {
            State& s = voices[static_cast<std::size_t>(v)];
            if (!s.active)
                continue;
            const float x = renderVoice(v, s, metal);
            l += x * s.panL;
            r += x * s.panR;
        }
        left[i] += outputStage(l * volume);
        right[i] += outputStage(r * volume);
    }
}

}
