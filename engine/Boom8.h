#pragma once

// BOOM8: a drum machine modelled on the classic 1980 analog rhythm machine's
// circuits: resonant "bridged-T" drums struck by a trigger pulse, a bank of
// six free-running square oscillators for the cymbal, hats and cowbell, and
// shaped white noise for the snare, clap and maracas. Each voice follows its
// circuit's structure (what feeds what, which envelopes, which filters) with
// frequencies and times from published measurements of the hardware.
//
// Sixteen sounds. As on the hardware, the toms share their LEVEL and TUNING
// knobs with the congas, the rim shot with the claves and the hand clap with
// the maracas; here both sounds of a pair can play at once.
//
// One instance is one machine. A new hit restarts its voice, as on the
// hardware. Not thread-safe: the owner calls everything from one thread at a
// time. 44.1 kHz, mono (the same on both outputs).

#include <array>
#include <cstdint>

namespace boom8
{

enum Voice : int
{
    BD = 0,   // bass drum
    SD,       // snare drum
    LT,       // low tom
    MT,       // mid tom
    HT,       // hi tom
    RS,       // rim shot
    CP,       // hand clap
    CB,       // cowbell
    CY,       // cymbal
    OH,       // open hi-hat
    CH,       // closed hi-hat
    LC,       // low conga
    MC,       // mid conga
    HC,       // hi conga
    CL,       // claves
    MA,       // maracas
    voiceCount
};

// The panel, 0..100 per knob (the hardware's 0..10 dial times ten).
struct Controls
{
    float accent = 60.0f;
    float volume = 0.0f;          // master, dB (-24..+6)

    float bdLevel = 80.0f, bdTone = 45.0f, bdDecay = 55.0f;
    float sdLevel = 70.0f, sdTone = 50.0f, sdSnappy = 60.0f;
    float ltLevel = 70.0f, ltTuning = 50.0f;   // and the low conga
    float mtLevel = 70.0f, mtTuning = 50.0f;   // and the mid conga
    float htLevel = 70.0f, htTuning = 50.0f;   // and the hi conga
    float rsLevel = 70.0f;                     // and the claves
    float cpLevel = 70.0f;                     // and the maracas
    float cbLevel = 60.0f;
    float cyLevel = 60.0f, cyTone = 50.0f, cyDecay = 60.0f;
    float ohLevel = 65.0f, ohDecay = 40.0f;
    float chLevel = 65.0f;

    // Not on the hardware.
    float velocity = 40.0f;   // how much pad velocity changes the level, 0..100
    float bdTune = 0.0f;      // bass drum pitch, semitones (-12..+12)
    bool choke = true;        // the closed hat cuts the open hat (always, on the hardware)
};

class Boom8
{
public:
    static constexpr double sampleRate = 44100.0;
    static constexpr int accentVelocity = 100;   // this hard and up is an accented step

    Boom8();

    Controls controls;

    void trigger(int voice, int velocity);
    void stopAll();     // immediate silence
    void fadeOutAll();  // every voice out over 5 ms

    // Adds `frames` of output to both channels (pre-cleared by the caller).
    void render(float* left, float* right, int frames);

    bool sounding(int voice) const;

    // What the knobs mean, for the screen.
    static float tomHz(int voice, float tuning);      // LT..HT and LC..HC: the pitch the tuning knob gives
    static float bdHz(float bdTune);                  // the bass drum's pitch at bdTune semitones
    static float bdDecaySeconds(float decay);         // time constants
    static float cyDecaySeconds(float decay);
    static float ohDecaySeconds(float decay);
    static float accentGain(float accent);            // an accented hit's extra gain

private:
    struct Svf   // TPT state-variable filter (Zavalishin)
    {
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
        void set(float hz, float q);
        void reset() { ic1 = ic2 = 0.0f; }
        void run(float x, float& low, float& band, float& high);
        float band(float x) { float l, b, h; run(x, l, b, h); return b; }
        float high(float x) { float l, b, h; run(x, l, b, h); return h; }
        float low(float x) { float l, b, h; run(x, l, b, h); return l; }
    };

    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void lowpass(float hz);
        float lp(float x) { z += a * (x - z); return z; }
        float hp(float x) { z += a * (x - z); return x - z; }
    };

    struct State
    {
        bool active = false;
        double t = 0.0;            // seconds since the hit
        float gain = 1.0f;         // level, velocity and accent
        float release = 1.0f;      // choke and panic fade, 1 = none
        float releaseStep = 0.0f;
        double phase1 = 0.0, phase2 = 0.0;
        float env1 = 0.0f, env2 = 0.0f, env3 = 0.0f;
        float coef1 = 0.0f, coef2 = 0.0f, coef3 = 0.0f;
        float freq1 = 0.0f, freq2 = 0.0f;
        float bend = 0.0f;         // pitch glide depth at the hit
        float mix = 0.5f;          // per voice: tone balance, noise share...
        Svf f1, f2, f3;
        OnePole p1, p2;
        std::array<float, 3> pink {};
    };

    float renderVoice(int voice, State& s, const std::array<float, 6>& metal);
    float noise();

    std::array<State, voiceCount> voices {};
    std::array<double, 6> metalPhase {};   // the free-running square oscillators
    std::uint32_t rng = 0x2545F491u;
};

float dbToGain(float db);

}
