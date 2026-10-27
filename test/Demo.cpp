// Renders BOOM8 to WAV files to listen to, and measures each voice.
//
//   boom8-demo pattern out.wav [bpm]   a four-bar pattern on the default kit
//   boom8-demo voices <dir>            one WAV per voice (normal and accented),
//                                      and each one's peak, length and pitch

#include "Boom8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace boom8;

namespace
{
constexpr const char* kNames[voiceCount] = { "BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB",
                                             "CY", "OH", "CH", "LC", "MC", "HC", "CL", "MA" };

void put16(std::ofstream& o, unsigned v) { char b[2] = { char(v & 255), char((v >> 8) & 255) }; o.write(b, 2); }
void put32(std::ofstream& o, unsigned v) { put16(o, v & 0xFFFF); put16(o, v >> 16); }

void writeWav(const std::string& path, const std::vector<float>& mono)
{
    std::ofstream o(path, std::ios::binary);
    const unsigned bytes = static_cast<unsigned>(mono.size() * 4);
    o.write("RIFF", 4); put32(o, 36 + bytes); o.write("WAVEfmt ", 8); put32(o, 16); put16(o, 1); put16(o, 2);
    put32(o, 44100); put32(o, 44100 * 4); put16(o, 4); put16(o, 16); o.write("data", 4); put32(o, bytes);
    for (const float x : mono)
    {
        const auto s = static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
        put16(o, static_cast<std::uint16_t>(s));
        put16(o, static_cast<std::uint16_t>(s));
    }
}

std::vector<float> renderHit(const int voice, const int velocity, const double seconds)
{
    Boom8 d;
    d.trigger(voice, velocity);
    std::vector<float> l(static_cast<std::size_t>(seconds * Boom8::sampleRate)), r(l.size());
    d.render(l.data(), r.data(), static_cast<int>(l.size()));
    return l;
}

// The strongest frequency between lo and hi Hz in x[from, from + n), by Goertzel in 1 Hz steps.
double strongest(const std::vector<float>& x, const std::size_t from, const std::size_t n, const double lo, const double hi)
{
    double best = 0.0, bestHz = 0.0;
    for (double hz = lo; hz <= hi; hz += 1.0)
    {
        const double w = 2.0 * M_PI * hz / Boom8::sampleRate, c = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = from; i < std::min(x.size(), from + n); ++i)
        {
            const double s0 = x[i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (i - from) / n)) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
        if (p > best)
        {
            best = p;
            bestHz = hz;
        }
    }
    return bestHz;
}

// Spectral centroid of the first n samples, Hz (DFT in 50 Hz bins).
double centroid(const std::vector<float>& x, const std::size_t n)
{
    double num = 0.0, den = 0.0;
    for (double hz = 50.0; hz < 20000.0; hz += 50.0)
    {
        double re = 0.0, im = 0.0;
        const double w = 2.0 * M_PI * hz / Boom8::sampleRate;
        for (std::size_t i = 0; i < std::min(n, x.size()); ++i)
        {
            re += x[i] * std::cos(w * i);
            im += x[i] * std::sin(w * i);
        }
        const double m = std::sqrt(re * re + im * im);
        num += hz * m;
        den += m;
    }
    return den > 0 ? num / den : 0.0;
}

int voices(const std::string& dir)
{
    std::printf("%-3s %7s %7s %9s %9s %10s %10s\n", "", "peak", "acc pk", "-20 dB", "-40 dB", "pitch@0", "centroid");
    for (int v = 0; v < voiceCount; ++v)
    {
        const auto x = renderHit(v, 90, 3.0);
        const auto a = renderHit(v, 127, 3.0);
        float peak = 0.0f, apeak = 0.0f;
        for (const float s : x)
            peak = std::max(peak, std::abs(s));
        for (const float s : a)
            apeak = std::max(apeak, std::abs(s));
        // When the envelope (10 ms peak windows) last passes -20 / -40 dB of the peak.
        double t20 = 0, t40 = 0;
        for (std::size_t i = 0; i < x.size(); i += 441)
        {
            float w = 0.0f;
            for (std::size_t j = i; j < std::min(x.size(), i + 441); ++j)
                w = std::max(w, std::abs(x[j]));
            if (w > peak * 0.1f)
                t20 = (i + 441) / Boom8::sampleRate;
            if (w > peak * 0.01f)
                t40 = (i + 441) / Boom8::sampleRate;
        }
        const double hi = (v == BD || v == LT || v == MT || v == HT || v == SD) ? 500.0 : 3000.0;
        const double pitch0 = strongest(x, 0, 2048, 30.0, hi);
        std::printf("%-3s %7.3f %7.3f %8.0fms %8.0fms %8.0fHz %8.0fHz\n", kNames[v], peak, apeak, t20 * 1000, t40 * 1000,
                    pitch0, centroid(x, 4096));
        writeWav(dir + "/" + kNames[v] + ".wav", x);
        writeWav(dir + "/" + kNames[v] + "-accent.wav", a);
    }
    // The bass drum's pitch as it falls.
    const auto bd = renderHit(BD, 127, 1.0);
    std::printf("BD pitch (accented):");
    for (const double ms : { 0.0, 20.0, 40.0, 80.0, 160.0 })
        std::printf(" %.0fms %.0fHz", ms, strongest(bd, static_cast<std::size_t>(ms * 44.1), 4096, 30.0, 150.0));
    std::printf("\n");
    return 0;
}

int pattern(const std::string& path, const double bpm)
{
    const int stepFrames = static_cast<int>(Boom8::sampleRate * 60.0 / bpm / 4.0);   // 16ths
    // x = hit, A = accented hit; 32 steps (two bars), played twice.
    struct Track { int voice; const char* steps; };
    const Track bars[2][9] = {
        {
            { BD, "A.........x.....A.........x...x." },
            { SD, "....A.......A.......A.......A..." },
            { CH, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x." },
            { OH, "..............x...............x." },
            { CP, "....x...............x..........." },
            { CB, "........................x..x...." },
            { RS, "...........x...............x...." },
            { MA, ".x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x" },
            { CY, "A..............................." },
        },
        {
            { BD, "A.........x.....A.........x.A..." },
            { SD, "....A.......A.......A...xxxxAxAx" },
            { CH, "x.x.x.x.x.x.x.x.x.x.x.x........." },
            { OH, "..............x................." },
            { HT, "........................A......." },
            { MT, "..........................A....." },
            { LT, "............................A..." },
            { CL, "..x.....x..x....x.....x.....x..." },
            { HC, "......x...............x........." },
        },
    };

    Boom8 d;
    d.controls.bdDecay = 70.0f;
    std::vector<float> out;
    std::vector<float> l(stepFrames), r(stepFrames);
    const auto run = [&](const int frames) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        d.render(l.data(), r.data(), frames);
        out.insert(out.end(), l.begin(), l.begin() + frames);
    };
    for (int rep = 0; rep < 2; ++rep)
        for (const auto& bar : bars)
            for (int step = 0; step < 32; ++step)
            {
                for (const auto& t : bar)
                    if (t.steps[step] != '.')
                        d.trigger(t.voice, t.steps[step] == 'A' ? 127 : 90);
                run(stepFrames);
            }
    for (int b = 0; b < 24; ++b)   // the tails
        run(stepFrames);
    writeWav(path, out);
    std::printf("wrote %s (%.1f s at %.0f BPM)\n", path.c_str(), out.size() / 44100.0, bpm);
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "voices") == 0)
        return voices(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "pattern") == 0)
        return pattern(argv[2], argc > 3 ? std::atof(argv[3]) : 112.0);
    std::fprintf(stderr, "usage: boom8-demo pattern out.wav [bpm] | voices <dir>\n");
    return 2;
}
