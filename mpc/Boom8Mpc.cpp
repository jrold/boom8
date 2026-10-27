// MPC OS (Open MPC VST2 wrapper) engine for BOOM8.
//
// The wrapper (mpc-vst-plugins wrapper/vst2_wrap.c) calls this engine one call
// at a time per instance, under its own per-instance lock: set_param and
// get_param from MPC's screen thread, midi and render from the audio thread.
// So the controls are plain fields; there are no other threads.

extern "C" {
#include "engine.h"
}

#include "Boom8.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace boom8;

constexpr int kMaxFrames = 128;                     // wrapper/engine.h: render() gets 1..128 frames
constexpr const char* kPadMapFile = "padmap.txt";   // the learned pads new tracks start with
constexpr auto kDoubleStopWindow = std::chrono::milliseconds(1200);
constexpr int kDebounceMs = 350;
constexpr int kHitDebounceMs = 120;   // the screen's instrument keys: one hit per tap, but quick taps work
constexpr int kKeyVelocity = 90;      // an instrument key hits this hard (not accented)

// Voice keys and names, in voice order (the order PAD LEARN asks for them).
constexpr std::array<const char*, voiceCount> kVoiceKey { "bd", "sd", "lt", "mt", "ht", "rs", "cp", "cb",
                                                          "cy", "oh", "ch", "lc", "mc", "hc", "cl", "ma" };
constexpr std::array<const char*, voiceCount> kVoiceName {
    "BASS DRUM", "SNARE DRUM", "LOW TOM", "MID TOM", "HI TOM", "RIM SHOT", "HAND CLAP", "COWBELL",
    "CYMBAL", "OPEN HAT", "CLOSED HAT", "LOW CONGA", "MID CONGA", "HI CONGA", "CLAVES", "MARACAS" };

// Pads play these notes unless PAD LEARN has mapped others: General MIDI's
// drum notes, a few per voice where GM has them.
const std::map<int, int> kDefaultNotes {
    { 35, BD }, { 36, BD },
    { 38, SD }, { 40, SD },
    { 41, LT }, { 43, LT },
    { 45, MT }, { 47, MT },
    { 48, HT }, { 50, HT },
    { 37, RS },
    { 39, CP },
    { 56, CB },
    { 49, CY }, { 51, CY }, { 52, CY }, { 55, CY }, { 57, CY }, { 59, CY },
    { 46, OH },
    { 42, CH }, { 44, CH },
    { 64, LC }, { 63, MC }, { 62, HC },
    { 75, CL },
    { 70, MA },
};

// The knobs: key, the control, its range, and how the screen shows it.
enum class Show { dial, dB, tuning, semitones, percent };
struct Knob
{
    const char* key;
    float Controls::*field;
    float lo, hi;
    Show show;
    int voice;   // for tuning: whose pitch
};
const Knob kKnobs[] = {
    { "accent", &Controls::accent, 0, 100, Show::dial, -1 },
    { "volume", &Controls::volume, -24, 6, Show::dB, -1 },
    { "bd_level", &Controls::bdLevel, 0, 100, Show::dial, -1 },
    { "bd_tone", &Controls::bdTone, 0, 100, Show::dial, -1 },
    { "bd_decay", &Controls::bdDecay, 0, 100, Show::dial, -1 },
    { "sd_level", &Controls::sdLevel, 0, 100, Show::dial, -1 },
    { "sd_tone", &Controls::sdTone, 0, 100, Show::dial, -1 },
    { "sd_snappy", &Controls::sdSnappy, 0, 100, Show::dial, -1 },
    { "lt_level", &Controls::ltLevel, 0, 100, Show::dial, -1 },
    { "lt_tuning", &Controls::ltTuning, 0, 100, Show::tuning, LT },
    { "mt_level", &Controls::mtLevel, 0, 100, Show::dial, -1 },
    { "mt_tuning", &Controls::mtTuning, 0, 100, Show::tuning, MT },
    { "ht_level", &Controls::htLevel, 0, 100, Show::dial, -1 },
    { "ht_tuning", &Controls::htTuning, 0, 100, Show::tuning, HT },
    { "rs_level", &Controls::rsLevel, 0, 100, Show::dial, -1 },
    { "cp_level", &Controls::cpLevel, 0, 100, Show::dial, -1 },
    { "cb_level", &Controls::cbLevel, 0, 100, Show::dial, -1 },
    { "cy_level", &Controls::cyLevel, 0, 100, Show::dial, -1 },
    { "cy_tone", &Controls::cyTone, 0, 100, Show::dial, -1 },
    { "cy_decay", &Controls::cyDecay, 0, 100, Show::dial, -1 },
    { "oh_level", &Controls::ohLevel, 0, 100, Show::dial, -1 },
    { "oh_decay", &Controls::ohDecay, 0, 100, Show::dial, -1 },
    { "ch_level", &Controls::chLevel, 0, 100, Show::dial, -1 },
    { "velocity", &Controls::velocity, 0, 100, Show::percent, -1 },
    { "bd_tune", &Controls::bdTune, -12, 12, Show::semitones, -1 },
};

const Knob* findKnob(const std::string& key)
{
    for (const auto& k : kKnobs)
        if (key == k.key)
            return &k;
    return nullptr;
}

// "hit_sd" -> SD; -1 when it isn't an instrument key.
int hitVoice(const std::string& key)
{
    if (key.rfind("hit_", 0) != 0)
        return -1;
    for (int v = 0; v < voiceCount; ++v)
        if (key.compare(4, std::string::npos, kVoiceKey[static_cast<std::size_t>(v)]) == 0)
            return v;
    return -1;
}

enum ChangedKey : unsigned
{
    changedLearn = 1u << 0,
};

int parseInt(const char* text, const int fallback)
{
    if (text == nullptr || *text == '\0')
        return fallback;
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    return end == text || !std::isfinite(v) ? fallback : static_cast<int>(std::lround(v));
}

float parseFloat(const char* text, const float fallback)
{
    if (text == nullptr || *text == '\0')
        return fallback;
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    return end == text || !std::isfinite(v) ? fallback : static_cast<float>(v);
}

int writeText(char* buf, const int len, const std::string& text)
{
    if (buf == nullptr || len <= 0)
        return 0;
    const auto n = static_cast<int>(std::min<std::size_t>(text.size(), static_cast<std::size_t>(len - 1)));
    std::memcpy(buf, text.data(), static_cast<std::size_t>(n));
    buf[n] = '\0';
    return n;
}

std::string formatFloat(const float v, const int decimals)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", decimals, static_cast<double>(v));
    return b;
}

std::string noteName(const int note)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string(names[((note % 12) + 12) % 12]) + std::to_string(note / 12 - 2);   // MPC: 60 = C3
}

class Instance
{
public:
    explicit Instance(const char* dataDir)
    {
        learnedNotes.reserve(voiceCount);   // learned on the audio thread: no allocation there
        if (dataDir != nullptr && *dataDir != '\0')
        {
            pluginData = dataDir;
            std::ifstream in(std::filesystem::path(dataDir) / kPadMapFile);
            std::vector<int> notes;
            for (int n; in >> n && notes.size() < static_cast<std::size_t>(voiceCount);)
                notes.push_back(n);
            learnedNotes = notes;
        }
        left.fill(0.0f);
        right.fill(0.0f);
        saver = std::thread([this] { saveLoop(); });
    }

    ~Instance()
    {
        {
            std::scoped_lock lock(saverMutex);
            quit = true;
        }
        saverCv.notify_all();
        saver.join();
    }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    // ---- wrapper calls ----

    void midi(const std::uint8_t* msg, const int len)
    {
        if (msg == nullptr || len < 1)
            return;
        const auto status = msg[0] & 0xF0;
        const int d1 = len > 1 ? msg[1] & 0x7F : 0;
        const int d2 = len > 2 ? msg[2] & 0x7F : 0;
        if (status == 0x90 && d2 > 0)
        {
            // PAD LEARN: each new note becomes the next voice, BD to MA.
            if (learning && std::find(learnedNotes.begin(), learnedNotes.end(), d1) == learnedNotes.end())
            {
                learnedNotes.push_back(d1);
                changedKeys |= changedLearn;
                if (static_cast<int>(learnedNotes.size()) == voiceCount)
                {
                    // All sixteen: done. The file is written off the audio thread.
                    learning = false;
                    for (int i = 0; i < voiceCount; ++i)
                        toSave[static_cast<std::size_t>(i)] = learnedNotes[static_cast<std::size_t>(i)];
                    savePending.store(true, std::memory_order_release);
                }
            }
            const int voice = voiceFor(d1);
            if (voice >= 0)
                machine.trigger(voice, d2);
        }
        else if (status == 0xB0 && (d1 == 120 || d1 == 123))
        {
            machine.fadeOutAll();
        }
    }

    void setParam(const char* key, const char* val)
    {
        if (key == nullptr)
            return;
        const std::string k(key);
        if (k == "state")
        {
            restoreState(val != nullptr ? val : "");
            return;
        }
        const int v = parseInt(val, 0);
        auto& c = machine.controls;
        if (k == "mains")
        {
            // MPC suspends and resumes its plug-ins on every STOP press (our
            // patched wrapper passes it on): two close together stop all sound.
            if (v == 0)
            {
                const auto now = std::chrono::steady_clock::now();
                if (lastSuspend && now - *lastSuspend < kDoubleStopWindow)
                    machine.fadeOutAll();
                lastSuspend = now;
            }
            return;
        }
        if (const int voice = hitVoice(k); voice >= 0)
        {
            if (v != 0 && !bouncing(k, kHitDebounceMs))
                machine.trigger(voice, kKeyVelocity);
            return;
        }
        if (k == "learn")
        {
            setLearning(v != 0);
            return;
        }
        if (k == "pads_clear")
        {
            if (v != 0 && !bouncing(k, debounceMs))
            {
                learning = false;
                learnedNotes.clear();
                std::error_code ec;
                if (!pluginData.empty())
                    std::filesystem::remove(std::filesystem::path(pluginData) / kPadMapFile, ec);
                changedKeys |= changedLearn;
            }
            return;
        }
        if (k == "choke")
        {
            c.choke = v != 0;
            return;
        }
        if (const Knob* knob = findKnob(k))
        {
            float x = std::clamp(parseFloat(val, c.*(knob->field)), knob->lo, knob->hi);
            if (knob->show == Show::semitones)
                x = std::round(x);
            c.*(knob->field) = x;
        }
    }

    int getParam(const char* key, char* buf, const int len)
    {
        if (key == nullptr)
            return 0;
        const std::string k(key);
        const auto& c = machine.controls;
        const auto num = [&](const double x) { return writeText(buf, len, formatFloat(static_cast<float>(x), 4)); };

        if (k == "choke") return num(c.choke);
        if (k == "learn") return num(learning);
        if (k == "pads_clear" || hitVoice(k) >= 0) return num(0);
        if (k == "pads_info") return writeText(buf, len, padsText());
        if (k == "state") return writeText(buf, len, saveState());

        const bool display = k.size() > 8 && k.compare(k.size() - 8, 8, "_display") == 0;
        const Knob* knob = findKnob(display ? k.substr(0, k.size() - 8) : k);
        if (knob == nullptr)
            return 0;
        const float x = c.*(knob->field);
        if (!display)
            return num(x);
        switch (knob->show)
        {
        case Show::dial: return writeText(buf, len, formatFloat(x / 10.0f, 1));
        case Show::dB: return writeText(buf, len, (x > 0 ? "+" : "") + formatFloat(x, 1) + " dB");
        case Show::tuning: return writeText(buf, len, formatFloat(Boom8::tomHz(knob->voice, x), 0) + " Hz");
        case Show::semitones:
        {
            const int st = static_cast<int>(std::lround(x));
            return writeText(buf, len, st == 0 ? "0 st" : (st > 0 ? "+" : "") + std::to_string(st) + " st");
        }
        case Show::percent: return writeText(buf, len, formatFloat(x, 0) + "%");
        }
        return 0;
    }

    void render(std::int16_t* out, const int frames)
    {
        if (out == nullptr || frames <= 0)
            return;
        int done = 0;
        while (done < frames)
        {
            const int n = std::min(kMaxFrames, frames - done);
            std::fill(left.begin(), left.begin() + n, 0.0f);
            std::fill(right.begin(), right.begin() + n, 0.0f);
            machine.render(left.data(), right.data(), n);
            for (int i = 0; i < n; ++i)
            {
                out[(done + i) * 2] = toPcm(left[static_cast<std::size_t>(i)]);
                out[(done + i) * 2 + 1] = toPcm(right[static_cast<std::size_t>(i)]);
            }
            done += n;
        }
    }

    bool takeChangedKey(char* key, const int len)
    {
        if ((changedKeys & changedLearn) != 0)
        {
            changedKeys &= ~changedLearn;
            writeText(key, len, "learn");
            return true;
        }
        return false;
    }

private:
    static std::int16_t toPcm(const float v)
    {
        return static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
    }

    int voiceFor(const int note) const
    {
        if (!learnedNotes.empty())
        {
            const auto it = std::find(learnedNotes.begin(), learnedNotes.end(), note);
            return it == learnedNotes.end() ? -1 : static_cast<int>(it - learnedNotes.begin());
        }
        const auto it = kDefaultNotes.find(note);
        return it == kDefaultNotes.end() ? -1 : it->second;
    }

    // A skin button sends its press twice; a one-shot key ignores the repeat.
    bool bouncing(const std::string& key, const int ms)
    {
        const auto now = std::chrono::steady_clock::now();
        auto& last = lastPress[key];
        if (last.time_since_epoch().count() != 0 && now - last < std::chrono::milliseconds(ms))
            return true;
        last = now;
        return false;
    }

    // ---- PAD LEARN ----

    void setLearning(const bool on)
    {
        if (on == learning)
            return;
        learning = on;
        changedKeys |= changedLearn;
        if (on)
        {
            learnedNotes.clear();   // start over: the next pad is the bass drum
            return;
        }
        writePadMap(learnedNotes);
    }

    // New tracks start with these pads (not on the audio thread).
    void writePadMap(const std::vector<int>& notes) const
    {
        if (notes.empty() || pluginData.empty())
            return;
        std::ofstream out(std::filesystem::path(pluginData) / kPadMapFile, std::ios::trunc);
        for (const int n : notes)
            out << n << "\n";
    }

    // Saves the pad map when PAD LEARN finished on the audio thread.
    void saveLoop()
    {
        std::unique_lock lock(saverMutex);
        while (!quit)
        {
            saverCv.wait_for(lock, std::chrono::milliseconds(250));
            if (savePending.exchange(false, std::memory_order_acq_rel))
                writePadMap(std::vector<int>(toSave.begin(), toSave.end()));
        }
    }

    std::string padsText() const
    {
        if (learning)
        {
            const auto n = learnedNotes.size();
            return n < static_cast<std::size_t>(voiceCount)
                ? "LEARN: HIT THE PAD FOR " + std::string(kVoiceName[n]) + "  (" + std::to_string(n + 1) + " OF 16)"
                : "LEARNED ALL SIXTEEN";
        }
        if (!learnedNotes.empty())
        {
            std::string s = "PADS: LEARNED  ";
            for (std::size_t i = 0; i < learnedNotes.size(); ++i)
                s += std::string(i ? " " : "") + kVoiceKey[i] + " " + noteName(learnedNotes[i]);
            for (auto& ch : s)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return s;
        }
        return "PADS: GM DRUM NOTES (BD 36, SD 38, LT 41, MT 45, HT 48, RS 37, CP 39, CB 56, CY 49, OH 46, CH 42...)";
    }

    // ---- state ----

    std::string saveState() const
    {
        const auto& c = machine.controls;
        std::string s = "v=1";
        const auto add = [&s](const std::string& k, const std::string& v) { s += ";" + k + "=" + v; };
        for (const auto& k : kKnobs)
            add(k.key, formatFloat(c.*(k.field), 2));
        add("choke", std::to_string(c.choke));
        std::string map;
        for (const int n : learnedNotes)
            map += (map.empty() ? "" : ".") + std::to_string(n);
        add("padmap", map);
        return s;
    }

    void restoreState(const std::string& state)
    {
        std::size_t pos = 0;
        while (pos <= state.size())
        {
            const auto end = std::min(state.find(';', pos), state.size());
            const auto item = state.substr(pos, end - pos);
            pos = end + 1;
            const auto eq = item.find('=');
            if (eq == std::string::npos)
                continue;
            const auto k = item.substr(0, eq);
            const auto v = item.substr(eq + 1);
            if (k == "padmap")
            {
                std::vector<int> notes;
                for (std::size_t at = 0; at < v.size() && notes.size() < static_cast<std::size_t>(voiceCount);)
                {
                    const auto dot = std::min(v.find('.', at), v.size());
                    notes.push_back(parseInt(v.substr(at, dot - at).c_str(), -1));
                    at = dot + 1;
                }
                learning = false;
                learnedNotes = notes;
            }
            else if (k == "choke" || findKnob(k) != nullptr)
            {
                setParam(k.c_str(), v.c_str());
            }
        }
    }

    Boom8 machine;
    std::array<float, kMaxFrames> left {};
    std::array<float, kMaxFrames> right {};
    std::string pluginData;   // the plug-in's data/ folder (pad map)
    bool learning = false;
    std::array<int, voiceCount> toSave {};   // the learned map, for the saver
    std::atomic<bool> savePending { false };
    std::mutex saverMutex;
    std::condition_variable saverCv;
    bool quit = false;
    std::vector<int> learnedNotes;   // learnedNotes[v] plays voice v
    unsigned changedKeys = 0;
    std::optional<std::chrono::steady_clock::time_point> lastSuspend;
    std::map<std::string, std::chrono::steady_clock::time_point> lastPress;
    // BOOM8_DEBOUNCE_MS: the offline test turns it off to press keys quickly.
    const int debounceMs = std::getenv("BOOM8_DEBOUNCE_MS") ? std::atoi(std::getenv("BOOM8_DEBOUNCE_MS")) : kDebounceMs;
    std::thread saver;   // last: started after everything it uses
};

void* engineCreate(const char* dataDir)
{
    try
    {
        return new Instance(dataDir);
    }
    catch (...)
    {
        return nullptr;
    }
}

void engineDestroy(void* inst) { delete static_cast<Instance*>(inst); }
void engineMidi(void* inst, const std::uint8_t* msg, const int len) { static_cast<Instance*>(inst)->midi(msg, len); }
void engineSetParam(void* inst, const char* key, const char* val) { static_cast<Instance*>(inst)->setParam(key, val); }
int engineGetParam(void* inst, const char* key, char* buf, const int len)
{
    return static_cast<Instance*>(inst)->getParam(key, buf, len);
}
void engineRender(void* inst, std::int16_t* out, const int frames) { static_cast<Instance*>(inst)->render(out, frames); }

const mpc_engine_t kEngine { engineCreate, engineDestroy, engineMidi, engineSetParam, engineGetParam, engineRender, nullptr, nullptr };
}

extern "C" const mpc_engine_t* mpc_engine(void)
{
    return &kEngine;
}

extern "C" int mpc_engine_changed(void* inst, char* key, const int len)
{
    return static_cast<Instance*>(inst)->takeChangedKey(key, len) ? 1 : 0;
}
