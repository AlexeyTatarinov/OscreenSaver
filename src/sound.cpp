#include "sound.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "config.h"

namespace {

constexpr int kRate = 44100;
constexpr double kPi = 3.14159265358979323846;

// Small noise source that always gives the same sound.
struct Noise {
    uint32_t state = 0x12345678u;
    double Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 8388608.0 - 1.0;
    }
};

std::vector<uint8_t> MakeWave(const std::vector<double>& samples) {
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    std::vector<uint8_t> wav(44 + dataBytes);
    auto put32 = [&](size_t at, uint32_t v) { memcpy(&wav[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { memcpy(&wav[at], &v, 2); };
    memcpy(&wav[0], "RIFF", 4);
    put32(4, 36 + dataBytes);
    memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);  // PCM
    put16(22, 1);  // mono
    put32(24, kRate);
    put32(28, kRate * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(&wav[36], "data", 4);
    put32(40, dataBytes);
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto v = static_cast<int16_t>(std::clamp(samples[i], -1.0, 1.0) * 32000.0);
        memcpy(&wav[44 + i * 2], &v, 2);
    }
    return wav;
}

// Camera shutter: two short mechanical clicks (opening and closing).
std::vector<double> Shutter() {
    std::vector<double> s(kRate * 180 / 1000, 0.0);
    Noise noise;
    auto burst = [&](double startMs, double lengthMs, double loud, double toneHz) {
        const size_t from = static_cast<size_t>(startMs * kRate / 1000);
        const size_t count = static_cast<size_t>(lengthMs * kRate / 1000);
        double low = 0;
        for (size_t i = 0; i < count && from + i < s.size(); ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double env = std::exp(-t * 1000.0 / (lengthMs / 4.5));
            low += (noise.Next() - low) * 0.35;  // softer, less hissing noise
            s[from + i] += loud * env * (0.75 * low + 0.35 * std::sin(2 * kPi * toneHz * t));
        }
    };
    burst(0, 30, 0.9, 1800);
    burst(4, 60, 0.5, 140);  // body of the camera
    burst(78, 40, 0.75, 2300);
    burst(80, 55, 0.35, 160);
    return s;
}

// Soft click: one short tick.
std::vector<double> Click() {
    std::vector<double> s(kRate * 60 / 1000, 0.0);
    Noise noise;
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        s[i] = 0.6 * std::exp(-t * 180) * std::sin(2 * kPi * 1500 * t) + 0.25 * std::exp(-t * 600) * noise.Next();
    }
    return s;
}

// Chime: two quiet rising tones.
std::vector<double> Chime() {
    std::vector<double> s(kRate * 360 / 1000, 0.0);
    auto tone = [&](double startMs, double hz) {
        const size_t from = static_cast<size_t>(startMs * kRate / 1000);
        for (size_t i = 0; from + i < s.size(); ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double attack = std::min(1.0, t / 0.004);
            s[from + i] += 0.4 * attack * std::exp(-t * 14) *
                           (std::sin(2 * kPi * hz * t) + 0.2 * std::sin(2 * kPi * hz * 2 * t));
        }
    };
    tone(0, 880);
    tone(110, 1320);
    return s;
}

// Sounds are made once and kept: PlaySound reads the memory while it plays.
const std::vector<uint8_t>& Built(SaveSound sound) {
    static std::vector<uint8_t> waves[3];
    const int i = sound == SaveSound::Click ? 1 : sound == SaveSound::Chime ? 2 : 0;
    if (waves[i].empty()) waves[i] = MakeWave(i == 1 ? Click() : i == 2 ? Chime() : Shutter());
    return waves[i];
}

}  // namespace

// Recorded sounds kept inside the program (res/app.rc).
const void* FromResource(int id) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC found = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL data = found ? LoadResource(self, found) : nullptr;
    return data ? LockResource(data) : nullptr;
}

void PlaySaveSound(SaveSound sound, const std::wstring& file) {
    if (sound == SaveSound::File) {
        if (!file.empty() &&
            PlaySoundW(file.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT | SND_NOWAIT)) {
            return;
        }
        sound = SaveSound::Shutter;  // the file is missing or cannot be played
    }
    if (sound == SaveSound::Glass || sound == SaveSound::Birds) {
        if (const void* wave = FromResource(sound == SaveSound::Glass ? 201 : 202)) {
            PlaySoundW(static_cast<LPCWSTR>(wave), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
            return;
        }
        sound = SaveSound::Shutter;
    }
    PlaySoundW(reinterpret_cast<LPCWSTR>(Built(sound).data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void PlaySaveSound() {
    const int s = Cfg().sound;
    PlaySaveSound(s >= 0 && s < static_cast<int>(SaveSound::Count) ? static_cast<SaveSound>(s) : SaveSound::Shutter,
                  Cfg().soundFile);
}
