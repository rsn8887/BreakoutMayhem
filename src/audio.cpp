// Breakout Mayhem - tiny procedural sound synth
// All sounds are generated at startup, no sample files needed.
#include "audio.h"
#include "vec.h"
#include <SDL.h>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace {

const int RATE = 44100;
const int VOICES = 16;

struct Voice {
    const std::vector<float>* buf = nullptr;
    double pos = 0;
    float step = 1, vol = 1;
};

std::vector<float> sounds[SFX_COUNT];
Voice voices[VOICES];
Uint32 lastPlayed[SFX_COUNT];
SDL_AudioDeviceID dev = 0;
bool on = true;

float frand() { return float(std::rand()) / RAND_MAX * 2.f - 1.f; }

// generic tone: frequency sweep f0->f1, waveform (0 sine, 1 square, 2 triangle), exponential decay
std::vector<float> tone(float dur, float f0, float f1, int wave, float decay, float noise = 0, float vol = 0.5f) {
    int n = int(dur * RATE);
    std::vector<float> s(n);
    double ph = 0;
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = float(i) / RATE;
        float f = f0 + (f1 - f0) * (t / dur);
        ph += f / RATE;
        float x = float(ph - std::floor(ph));
        float v;
        if (wave == 0) v = std::sin(2 * PI_F * x);
        else if (wave == 1) v = x < 0.5f ? 0.6f : -0.6f;
        else v = 4 * std::fabs(x - 0.5f) - 1;
        lp += (frand() - lp) * 0.3f;
        v = v * (1 - noise) + lp * noise * 2;
        float env = std::exp(-t * decay) * std::min(1.f, t * 800.f);
        s[i] = v * env * vol;
    }
    return s;
}

void mixInto(std::vector<float>& dst, const std::vector<float>& src, float at) {
    size_t off = size_t(at * RATE);
    if (dst.size() < off + src.size()) dst.resize(off + src.size(), 0.f);
    for (size_t i = 0; i < src.size(); i++) dst[off + i] += src[i];
}

void build() {
    sounds[SFX_BAT] = tone(0.09f, 420, 300, 2, 40, 0.1f, 0.6f);
    sounds[SFX_WALL] = tone(0.05f, 200, 160, 0, 70, 0.2f, 0.35f);
    sounds[SFX_BRICK] = tone(0.06f, 700, 660, 1, 55, 0.0f, 0.25f);
    {
        auto a = tone(0.18f, 900, 380, 1, 22, 0.1f, 0.22f);
        mixInto(a, tone(0.14f, 0, 0, 0, 30, 1.0f, 0.25f), 0);
        sounds[SFX_BREAK] = a;
    }
    {
        auto a = tone(0.25f, 1480, 1480, 0, 18, 0, 0.18f);
        mixInto(a, tone(0.25f, 2110, 2110, 0, 25, 0, 0.12f), 0);
        sounds[SFX_METAL] = a;
    }
    {
        auto a = tone(0.12f, 1046, 1046, 2, 20, 0, 0.3f);
        mixInto(a, tone(0.22f, 1318, 1318, 2, 14, 0, 0.3f), 0.07f);
        sounds[SFX_BUMPER] = a;
    }
    {
        auto a = tone(0.6f, 0, 0, 0, 6, 1.0f, 0.55f);
        mixInto(a, tone(0.4f, 120, 40, 0, 8, 0.2f, 0.5f), 0);
        sounds[SFX_EXPLODE] = a;
    }
    sounds[SFX_HOLE] = tone(0.5f, 620, 70, 0, 4, 0.05f, 0.45f);
    {
        std::vector<float> a;
        const float notes[3] = {660, 880, 1320};
        for (int i = 0; i < 3; i++) mixInto(a, tone(0.1f, notes[i], notes[i], 2, 20, 0, 0.3f), i * 0.06f);
        sounds[SFX_PILL] = a;
    }
    sounds[SFX_PORTAL] = tone(0.3f, 200, 1400, 0, 6, 0.05f, 0.3f);
    {
        auto a = tone(0.35f, 500, 120, 1, 6, 0, 0.18f);
        mixInto(a, tone(0.25f, 130, 500, 1, 8, 0, 0.15f), 0.18f);
        sounds[SFX_GRAVITY] = a;
    }
    {
        std::vector<float> a;
        const float notes[5] = {523, 659, 784, 1046, 1318};
        for (int i = 0; i < 5; i++) mixInto(a, tone(0.25f, notes[i], notes[i], 2, 8, 0, 0.3f), i * 0.09f);
        sounds[SFX_CLEAR] = a;
    }
    {
        std::vector<float> a;
        const float notes[4] = {660, 523, 440, 330};
        for (int i = 0; i < 4; i++) mixInto(a, tone(0.22f, notes[i], notes[i] * 0.97f, 1, 9, 0, 0.2f), i * 0.14f);
        sounds[SFX_LOSE] = a;
    }
    {
        std::vector<float> a;
        const float notes[5] = {392, 370, 349, 330, 262};
        for (int i = 0; i < 5; i++) mixInto(a, tone(0.4f, notes[i], notes[i] * 0.98f, 2, 4, 0, 0.3f), i * 0.25f);
        sounds[SFX_GAMEOVER] = a;
    }
    sounds[SFX_PUCK] = tone(0.04f, 1800, 1500, 0, 90, 0.4f, 0.35f);
    {
        auto a = tone(0.3f, 300, 80, 0, 8, 0.3f, 0.4f);
        mixInto(a, tone(0.12f, 900, 1200, 2, 25, 0, 0.2f), 0.15f);
        sounds[SFX_SINK] = a;
    }
    sounds[SFX_MENU] = tone(0.05f, 880, 880, 2, 50, 0, 0.25f);
    sounds[SFX_LASER] = tone(0.08f, 1700, 500, 1, 30, 0, 0.14f);
    {
        auto a = tone(0.06f, 520, 520, 1, 40, 0, 0.2f);
        mixInto(a, tone(0.08f, 780, 780, 1, 40, 0, 0.2f), 0.05f);
        sounds[SFX_SWITCH] = a;
    }
    sounds[SFX_ZAP] = tone(0.25f, 900, 120, 1, 12, 0.35f, 0.3f);
}

void callback(void*, Uint8* stream, int len) {
    float* out = reinterpret_cast<float*>(stream);
    int n = len / int(sizeof(float)) / 2;
    for (int i = 0; i < n; i++) {
        float m = 0;
        for (auto& v : voices) {
            if (!v.buf) continue;
            size_t p = size_t(v.pos);
            if (p + 1 >= v.buf->size()) { v.buf = nullptr; continue; }
            float fr = float(v.pos - double(p));
            m += ((*v.buf)[p] * (1 - fr) + (*v.buf)[p + 1] * fr) * v.vol;
            v.pos += v.step;
        }
        m = std::tanh(m * 0.9f);
        out[2 * i] = m;
        out[2 * i + 1] = m;
    }
}

} // namespace

namespace Audio {

bool init() {
    build();
    SDL_AudioSpec want{}, have{};
    want.freq = RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = callback;
    dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev) {
        SDL_Log("Audio disabled: %s", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void shutdown() {
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
}

void play(Sfx s, float volume, float pitch) {
    if (!dev || !on || s < 0 || s >= SFX_COUNT) return;
    Uint32 now = SDL_GetTicks();
    if (now - lastPlayed[s] < 30) return; // avoid machine-gun stacking
    lastPlayed[s] = now;
    SDL_LockAudioDevice(dev);
    Voice* slot = nullptr;
    for (auto& v : voices)
        if (!v.buf) { slot = &v; break; }
    if (!slot) {
        // steal the voice that is furthest along
        slot = &voices[0];
        for (auto& v : voices)
            if (v.pos > slot->pos) slot = &v;
    }
    slot->buf = &sounds[s];
    slot->pos = 0;
    slot->step = pitch;
    slot->vol = std::min(volume, 1.5f);
    SDL_UnlockAudioDevice(dev);
}

void setEnabled(bool e) { on = e; }
bool enabled() { return on; }

} // namespace Audio
