// BOOM8 engine test: the voices, and the MPC adapter driven through
// mpc_engine() as the wrapper drives it. Uses a scratch data folder.

extern "C" {
#include "engine.h"
}

#include "Boom8.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace boom8;
namespace fs = std::filesystem;

namespace
{
int failures = 0;
int checks = 0;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        ++checks;                                                       \
        if (!(cond))                                                    \
        {                                                               \
            ++failures;                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

std::vector<float> renderFor(Boom8& d, int frames)
{
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    for (int done = 0; done < frames; done += 128)
        d.render(l.data() + done, r.data() + done, std::min(128, frames - done));
    return l;
}

float peak(const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, x.size()); ++i)
        p = std::max(p, std::fabs(x[i]));
    return p;
}

// Upward zero crossings per second over a window: a pitch estimate for a sine-like voice.
double crossingsPerSecond(const std::vector<float>& x, std::size_t from, std::size_t n)
{
    int c = 0;
    for (std::size_t i = from + 1; i < from + n && i < x.size(); ++i)
        c += x[i - 1] < 0.0f && x[i] >= 0.0f ? 1 : 0;
    return c * Boom8::sampleRate / static_cast<double>(n);
}

void testVoices()
{
    for (int v = 0; v < voiceCount; ++v)
    {
        Boom8 d;
        d.trigger(v, 90);
        CHECK(d.sounding(v));
        const auto out = renderFor(d, 44100 * 8);
        bool finite = true;
        for (const float s : out)
            finite = finite && std::isfinite(s);
        CHECK(finite);
        CHECK(peak(out) > 0.1f && peak(out) < 0.7f);   // balanced: none far louder than the rest
        CHECK(!d.sounding(v));                         // every voice ends by itself within 8 s
    }

    // The bass drum sits at its pitch once the glide is over; BD TUNE moves it.
    for (const float st : { -12.0f, 0.0f, 12.0f })
    {
        Boom8 d;
        d.controls.bdTune = st;
        d.controls.bdDecay = 100.0f;
        d.trigger(BD, 90);
        const auto x = renderFor(d, 44100);
        const double hz = crossingsPerSecond(x, 4410, 30870);   // 0.7 s: 17 cycles at the lowest
        std::printf("BD at %+.0f st: %.1f Hz (expected %.1f)\n", st, hz, Boom8::bdHz(st));
        CHECK(std::abs(hz - Boom8::bdHz(st)) < Boom8::bdHz(st) * 0.06);
    }

    // TUNING moves the toms and congas through their ranges.
    for (const int v : { LT, MT, HT, LC, MC, HC })
        for (const float tuning : { 0.0f, 100.0f })
        {
            Boom8 d;
            d.controls.ltTuning = d.controls.mtTuning = d.controls.htTuning = tuning;
            d.trigger(v, 90);
            const auto x = renderFor(d, 8820);
            const double hz = Boom8::tomHz(v, tuning);
            CHECK(std::abs(crossingsPerSecond(x, 2205, 4410) - hz) < hz * 0.08);
        }
    CHECK(std::abs(Boom8::tomHz(LT, 0.0f) - 80.0f) < 0.01f && std::abs(Boom8::tomHz(HC, 100.0f) - 455.0f) < 0.01f);

    // DECAY makes the bass drum ring longer; SNAPPY adds the snares.
    Boom8 shortHit, longHit;
    shortHit.controls.bdDecay = 0.0f;
    longHit.controls.bdDecay = 100.0f;
    shortHit.trigger(BD, 90);
    longHit.trigger(BD, 90);
    const auto s1 = renderFor(shortHit, 44100), s2 = renderFor(longHit, 44100);
    CHECK(peak(s1, 22050) < 0.001f && peak(s2, 22050) > 0.05f);
    const auto snareHigh = [](float snappy) {
        Boom8 d;
        d.controls.sdSnappy = snappy;
        d.trigger(SD, 90);
        const auto x = renderFor(d, 8820);
        double hf = 0.0;   // the energy of the first difference: the noise's share
        for (std::size_t i = 1; i < x.size(); ++i)
            hf += (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        return hf;
    };
    CHECK(snareHigh(100.0f) > snareHigh(0.0f) * 10.0);

    // Velocity, then accent at 100 and up; the ACCENT knob sets how much.
    const auto hitPeak = [](int velocity, float accent, float sens) {
        Boom8 d;
        d.controls.accent = accent;
        d.controls.velocity = sens;
        d.controls.sdLevel = 40.0f;
        d.trigger(SD, velocity);
        return peak(renderFor(d, 4410));
    };
    CHECK(hitPeak(40, 50.0f, 100.0f) < hitPeak(90, 50.0f, 100.0f) * 0.7f);
    CHECK(std::abs(hitPeak(40, 50.0f, 0.0f) - hitPeak(90, 50.0f, 0.0f)) < 0.002f);   // VELOCITY 0: as the hardware
    CHECK(hitPeak(100, 100.0f, 0.0f) > hitPeak(99, 100.0f, 0.0f) * 1.5f);
    CHECK(std::abs(hitPeak(127, 0.0f, 0.0f) - hitPeak(99, 0.0f, 0.0f)) < 0.002f);

    // The closed hat chokes the open hat, unless CHOKE is off.
    for (const bool choke : { true, false })
    {
        Boom8 d;
        d.controls.choke = choke;
        d.controls.chLevel = 0.0f;   // hear only the open hat
        d.controls.ohDecay = 100.0f;
        d.trigger(OH, 90);
        renderFor(d, 4410);
        d.trigger(CH, 90);
        const auto after = renderFor(d, 4410);
        CHECK(choke ? peak(after, 400) < 0.0001f : peak(after, 400) > 0.01f);
    }

    // A conga and its tom play at once (each pair shares only its knobs).
    {
        Boom8 d;
        d.trigger(LT, 90);
        d.trigger(LC, 90);
        CHECK(d.sounding(LT) && d.sounding(LC));
    }

    // SD and CP DECAY: lower is tighter, higher longer; 50 is the hardware.
    const auto lengthMs = [](int voice, float decay) {
        Boom8 d;
        d.controls.sdDecay = d.controls.cpDecay = decay;
        d.trigger(voice, 90);
        const auto x = renderFor(d, 44100);
        const float p = peak(x);
        std::size_t last = 0;   // the last sample above -40 dB of the peak
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::fabs(x[i]) > p * 0.01f)
                last = i;
        return last * 1000.0 / Boom8::sampleRate;
    };
    for (const int v : { SD, CP })
    {
        const double tight = lengthMs(v, 0.0f), stock = lengthMs(v, 50.0f), loose = lengthMs(v, 100.0f);
        std::printf("%s length (-40 dB): %.0f / %.0f / %.0f ms at DECAY 0 / 50 / 100\n", v == SD ? "SD" : "CP", tight,
                    stock, loose);
        CHECK(tight < stock * 0.6 && loose > stock * 1.3);
    }

    // Pan: hard left is silent on the right; centre is the same on both; a
    // conga follows its tom's pan.
    for (const float pan : { 0.0f, 50.0f, 100.0f })
        for (const int v : { SD, LC })
        {
            Boom8 d;
            d.controls.sdPan = d.controls.ltPan = pan;
            d.trigger(v, 90);
            std::vector<float> l(4410), r(4410);
            d.render(l.data(), r.data(), 4410);
            const float pl = peak(l), pr = peak(r);
            CHECK(pan == 0.0f ? pr == 0.0f && pl > 0.1f : pan == 100.0f ? pl == 0.0f && pr > 0.1f : std::abs(pl - pr) < 1e-6f);
        }

    // Everything at once, accented, stays clean; panic fades it all out.
    Boom8 d;
    d.controls.volume = 6.0f;
    for (int v = 0; v < voiceCount; ++v)
        d.trigger(v, 127);
    const auto loud = renderFor(d, 4410);
    CHECK(peak(loud) <= 1.0f);
    d.fadeOutAll();
    const auto faded = renderFor(d, 1024);
    CHECK(peak(faded, 300) == 0.0f);
}

// ---- the adapter ----

std::string get(const mpc_engine_t* e, void* inst, const char* key)
{
    char buf[4096] = {};
    return e->get_param(inst, key, buf, sizeof buf) > 0 ? buf : "";
}

int renderPeak(const mpc_engine_t* e, void* inst, int frames)
{
    std::vector<std::int16_t> out(256);
    int p = 0;
    for (int done = 0; done < frames; done += 128)
    {
        e->render(inst, out.data(), 128);
        for (int i = 0; i < 128; ++i)
            p = std::max(p, std::abs(static_cast<int>(out[static_cast<std::size_t>(i) * 2])));
    }
    return p;
}

float playPeak(const mpc_engine_t* e, void* inst, int note, int frames = 4096)
{
    const std::uint8_t on[3] = { 0x90, static_cast<std::uint8_t>(note), 90 };
    e->midi(inst, on, 3);
    return renderPeak(e, inst, frames) / 32767.0f;
}

void silence(const mpc_engine_t* e, void* inst)
{
    const std::uint8_t off[3] = { 0xB0, 123, 0 };
    e->midi(inst, off, 3);
    renderPeak(e, inst, 2560);
}

std::string readAll(const fs::path& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void testAdapter(const fs::path& root)
{
    const mpc_engine_t* e = mpc_engine();
    const auto data = root / "data";
    fs::create_directories(data);
    void* inst = e->create(data.string().c_str());
    CHECK(inst != nullptr);

    // General MIDI drum notes by default, one for every sound; other notes play nothing.
    for (const int n : { 36, 38, 41, 45, 48, 37, 39, 56, 49, 46, 42, 64, 63, 62, 75, 70 })
    {
        silence(e, inst);
        CHECK(playPeak(e, inst, n) > 0.05f);
    }
    silence(e, inst);
    CHECK(playPeak(e, inst, 60) == 0.0f);
    CHECK(get(e, inst, "pads_info").rfind("PADS: GM DRUM NOTES", 0) == 0);

    // What the screen shows.
    CHECK(get(e, inst, "bd_level_display") == "-3.9 dB");   // audio taper: (80 / 100)^2
    CHECK(get(e, inst, "accent_display") == "6.0");
    e->set_param(inst, "cb_level", "0");
    CHECK(get(e, inst, "cb_level_display") == "-INF");
    e->set_param(inst, "cb_level", "60");
    CHECK(get(e, inst, "oh_pan_display") == "C");
    e->set_param(inst, "oh_pan", "20");
    CHECK(get(e, inst, "oh_pan_display") == "L30");
    CHECK(get(e, inst, "cp_decay_display") == "5.0");
    CHECK(get(e, inst, "lt_tuning_display") == "89 Hz");
    CHECK(get(e, inst, "volume_display") == "0.0 dB");
    CHECK(get(e, inst, "velocity_display") == "40%");
    e->set_param(inst, "bd_tune", "-3.4");
    CHECK(get(e, inst, "bd_tune") == "-3.0000" && get(e, inst, "bd_tune_display") == "-3 st");
    e->set_param(inst, "bd_tune", "99");
    CHECK(get(e, inst, "bd_tune_display") == "+12 st");
    e->set_param(inst, "bd_level", "nonsense");
    CHECK(get(e, inst, "bd_level") == "80.0000");

    // PAD LEARN: sixteen pads in order, then it finishes by itself.
    e->set_param(inst, "learn", "1");
    CHECK(get(e, inst, "pads_info").rfind("LEARN: HIT THE PAD FOR BASS DRUM  (1 OF 16)", 0) == 0);
    for (int i = 0; i < 16; ++i)
    {
        const std::uint8_t on[3] = { 0x90, static_cast<std::uint8_t>(60 + i), 90 };
        e->midi(inst, on, 3);
        if (i == 7)
            CHECK(get(e, inst, "pads_info").rfind("LEARN: HIT THE PAD FOR CYMBAL  (9 OF 16)", 0) == 0);
    }
    CHECK(get(e, inst, "learn") == "0.0000");
    CHECK(get(e, inst, "pads_info").rfind("PADS: LEARNED  BD C3 SD C#3", 0) == 0);
    std::string expected;
    for (int i = 0; i < 16; ++i)
        expected += std::to_string(60 + i) + "\n";
    for (int i = 0; i < 50 && readAll(data / "padmap.txt") != expected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(readAll(data / "padmap.txt") == expected);
    silence(e, inst);
    CHECK(playPeak(e, inst, 60) > 0.05f);   // C3: the bass drum now
    silence(e, inst);
    CHECK(playPeak(e, inst, 36) == 0.0f);   // the GM note isn't a pad any more
    char key[64];
    CHECK(mpc_engine_changed(inst, key, sizeof key) == 1 && std::string(key) == "learn");

    // A new track starts with the learned pads; a project brings its own.
    void* other = e->create(data.string().c_str());
    CHECK(playPeak(e, other, 75) > 0.02f);   // D#4: the maracas
    e->set_param(inst, "bd_decay", "25");
    e->set_param(inst, "sd_snappy", "90");
    e->set_param(inst, "choke", "0");
    e->set_param(inst, "cp_decay", "12");
    const auto state = get(e, inst, "state");
    CHECK(state.find(";bd_decay=25.00;") != std::string::npos && state.find(";sd_snappy=90.00;") != std::string::npos
          && state.find(";choke=0;") != std::string::npos
          && state.find(";cp_decay=12.00;") != std::string::npos && state.find(";oh_pan=20.00;") != std::string::npos && state.find(";padmap=60.61.62.63") != std::string::npos);
    e->set_param(other, "state", state.c_str());
    CHECK(get(e, other, "state") == state);
    e->destroy(other);

    // CLEAR PADS: back to General MIDI, for new tracks too.
    e->set_param(inst, "pads_clear", "1");
    CHECK(get(e, inst, "pads_info").rfind("PADS: GM DRUM NOTES", 0) == 0 && !fs::exists(data / "padmap.txt"));
    silence(e, inst);
    CHECK(playPeak(e, inst, 38) > 0.05f);

    // A pad hit lights its column's hit dot for 150 ms; the adapter reports
    // it going on and off.
    {
        void* h = e->create(nullptr);
        char changed[64];
        while (mpc_engine_changed(h, changed, sizeof changed)) {}
        CHECK(get(e, h, "act_lt") == "0.0000");
        const std::uint8_t hit[3] = { 0x90, 64, 90 };
        e->midi(h, hit, 3);
        CHECK(get(e, h, "act_lt") == "1.0000");
        CHECK(mpc_engine_changed(h, changed, sizeof changed) == 1 && std::string(changed) == "act_lt");
        CHECK(mpc_engine_changed(h, changed, sizeof changed) == 0);
        renderPeak(e, h, 4096);   // 93 ms: still lit
        CHECK(get(e, h, "act_lt") == "1.0000");
        renderPeak(e, h, 4096);
        CHECK(get(e, h, "act_lt") == "0.0000");
        CHECK(mpc_engine_changed(h, changed, sizeof changed) == 1 && std::string(changed) == "act_lt");
        e->set_param(h, "act_lt", "1");   // the screen can't light it
        CHECK(get(e, h, "act_lt") == "0.0000");
        CHECK(get(e, h, "state").find("act_") == std::string::npos);
        e->destroy(h);
    }

    // MIDI LEARN: a nudge picks a control (its value stays), the next CC maps
    // to it on that channel; mapped CCs move it; the mapping is saved with
    // the project; CLEAR and CLEAR ALL forget.
    {
        void* m = e->create(nullptr);
        char changed[64];
        const auto drain = [&](void* i, const std::string& want) {
            bool seen = false;
            while (mpc_engine_changed(i, changed, sizeof changed))
                seen = seen || want == changed;
            return seen;
        };
        const auto cc = [&](void* i, int channel, int number, int value) {
            const std::uint8_t msg[3] = { static_cast<std::uint8_t>(0xB0 | channel), static_cast<std::uint8_t>(number),
                                          static_cast<std::uint8_t>(value) };
            e->midi(i, msg, 3);
        };
        CHECK(get(e, m, "midi_info").rfind("MIDI: NOTHING MAPPED", 0) == 0);
        e->set_param(m, "sd_snappy", "40");
        e->set_param(m, "midi_learn", "1");
        CHECK(get(e, m, "midi_info").rfind("MIDI LEARN: NUDGE", 0) == 0);
        e->set_param(m, "sd_snappy", "45");   // the nudge
        CHECK(get(e, m, "sd_snappy") == "40.0000");
        CHECK(get(e, m, "sd_snappy_display") == "LEARN");
        CHECK(drain(m, "sd_snappy"));   // the screen puts the knob back
        CHECK(get(e, m, "midi_info").rfind("SD SNAPPY: MOVE A CONTROL", 0) == 0);
        cc(m, 1, 21, 127);   // channel 2
        CHECK(get(e, m, "sd_snappy") == "100.0000");
        CHECK(get(e, m, "sd_snappy_display") == "CC21 CH2");
        CHECK(get(e, m, "midi_info").rfind("SD SNAPPY = CC21 CH2", 0) == 0);
        CHECK(drain(m, "sd_snappy"));
        cc(m, 0, 21, 0);     // the same CC on channel 1: not ours
        CHECK(get(e, m, "sd_snappy") == "100.0000");
        cc(m, 1, 21, 0);
        CHECK(get(e, m, "sd_snappy") == "0.0000");
        cc(m, 1, 121, 0);    // a channel mode message is never learned
        e->set_param(m, "midi_learn", "0");
        CHECK(get(e, m, "sd_snappy_display") == "0.0");
        CHECK(get(e, m, "midi_info") == "MIDI: 1 CONTROL MAPPED  SD SNAPPY CC21 CH2");
        e->set_param(m, "sd_snappy", "70");   // out of learn mode a touch is a touch
        CHECK(get(e, m, "sd_snappy") == "70.0000");

        // Saved with the project; a new track starts with none.
        const auto saved = get(e, m, "state");
        CHECK(saved.find(";midimap=1.21.sd_snappy") != std::string::npos);
        void* n = e->create(nullptr);
        CHECK(get(e, n, "midi_info").rfind("MIDI: NOTHING MAPPED", 0) == 0);
        e->set_param(n, "state", saved.c_str());
        cc(n, 1, 21, 127);
        CHECK(get(e, n, "sd_snappy") == "100.0000");

        // Learning the same CC for another control moves it there.
        e->set_param(n, "midi_learn", "1");
        e->set_param(n, "bd_decay", "51");
        cc(n, 1, 21, 64);
        CHECK(get(e, n, "bd_decay_display") == "CC21 CH2" && get(e, n, "sd_snappy_display") != "CC21 CH2");
        // A second CC for a control replaces its first.
        e->set_param(n, "bd_decay", "52");
        cc(n, 1, 22, 64);
        e->set_param(n, "midi_learn", "0");
        CHECK(get(e, n, "midi_info") == "MIDI: 1 CONTROL MAPPED  BD DECAY CC22 CH2");
        // CLEAR forgets the control learned last; CLEAR ALL every mapping.
        e->set_param(n, "midi_clear", "1");
        CHECK(get(e, n, "midi_info").rfind("MIDI: NOTHING MAPPED", 0) == 0);
        e->set_param(n, "state", saved.c_str());
        e->set_param(n, "midi_clear_all", "1");
        CHECK(get(e, n, "midi_info").rfind("MIDI: NOTHING MAPPED", 0) == 0);
        e->destroy(n);
        e->destroy(m);
    }

    // STOP twice stops all sound; once lets it ring.
    e->set_param(inst, "cy_decay", "100");
    const std::uint8_t cy[3] = { 0x90, 49, 127 };
    e->midi(inst, cy, 3);
    renderPeak(e, inst, 512);
    e->set_param(inst, "mains", "0");
    e->set_param(inst, "mains", "1");
    CHECK(renderPeak(e, inst, 512) > 0);
    e->set_param(inst, "mains", "0");
    e->set_param(inst, "mains", "1");
    renderPeak(e, inst, 512);
    CHECK(renderPeak(e, inst, 512) == 0);

    e->destroy(inst);
}

void timeRendering()
{
    Boom8 d;
    std::vector<float> l(128), r(128);
    const int blocks = 44100 * 4 / 128;
    double worst = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        if (b % 20 == 0)   // every voice going at once, retriggered
            for (int v = 0; v < voiceCount; ++v)
                d.trigger(v, 127);
        const auto t0 = std::chrono::steady_clock::now();
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        d.render(l.data(), r.data(), 128);
        worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const double deadline = 128.0 / 44.1;
    std::printf("all 16 voices: %.3f ms/block avg (%.1f%% of a core), worst %.3f ms (deadline %.2f ms)\n", total / blocks,
                100.0 * (total / blocks) / deadline, worst, deadline);
}
}

int main()
{
    const auto root = fs::temp_directory_path() / ("boom8-test-" + std::to_string(std::rand()) + std::to_string(::time(nullptr)));
    fs::create_directories(root);
    setenv("BOOM8_DEBOUNCE_MS", "0", 1);

    testVoices();
    testAdapter(root);
    timeRendering();

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
