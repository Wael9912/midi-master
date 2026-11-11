// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// Small DSP building blocks shared by the instruments. Everything is inline, allocation-free and real-time safe.
#pragma once
#include <cmath>
#include <cstdint>

namespace mm {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr int kBlock = 32;  // the mixer renders in blocks of at most this many frames

// Shared per-channel state the instruments read while rendering (owned by the Arranger, audio thread only).
struct ChannelState {
    float gain = 1.f;       // bus × channel volume, linear
    float reverb = 0.f;     // reverb send 0..1
    float bend = 1.f;       // pitch factor from the bend wheel
    bool sustain = false;   // pedal down
    bool keyPart = false;   // channels 0–1 (the player's hands): attacks are capped at 10 ms
};

// The block the instruments add into.
struct MixBus {
    float* left;
    float* right;
    float* reverb;  // mono send
    int frames;
};

// RBJ biquad, transposed direct form II. Coefficients follow the Web Audio spec so the ported sounds match:
// lowpass / highpass take Q in dB, bandpass takes a linear Q.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    void lowpass(float freq, float qDb, float sr) {
        const float w0 = kTwoPi * clampFreq(freq, sr) / sr;
        const float cs = std::cos(w0), alpha = std::sin(w0) / (2.f * std::pow(10.f, qDb / 20.f));
        set((1 - cs) * 0.5f, 1 - cs, (1 - cs) * 0.5f, 1 + alpha, -2 * cs, 1 - alpha);
    }
    void highpass(float freq, float qDb, float sr) {
        const float w0 = kTwoPi * clampFreq(freq, sr) / sr;
        const float cs = std::cos(w0), alpha = std::sin(w0) / (2.f * std::pow(10.f, qDb / 20.f));
        set((1 + cs) * 0.5f, -(1 + cs), (1 + cs) * 0.5f, 1 + alpha, -2 * cs, 1 - alpha);
    }
    void bandpass(float freq, float q, float sr) {
        const float w0 = kTwoPi * clampFreq(freq, sr) / sr;
        const float cs = std::cos(w0), alpha = std::sin(w0) / (2.f * (q > 1e-3f ? q : 1e-3f));
        set(alpha, 0, -alpha, 1 + alpha, -2 * cs, 1 - alpha);
    }
    // SF2 style: Q as a linear resonance factor.
    void lowpassLinearQ(float freq, float q, float sr) {
        const float w0 = kTwoPi * clampFreq(freq, sr) / sr;
        const float cs = std::cos(w0), alpha = std::sin(w0) / (2.f * (q > 0.5f ? q : 0.5f));
        set((1 - cs) * 0.5f, 1 - cs, (1 - cs) * 0.5f, 1 + alpha, -2 * cs, 1 - alpha);
    }
    float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    static float clampFreq(float f, float sr) {
        const float hi = sr * 0.49f;
        return f < 10.f ? 10.f : f > hi ? hi : f;
    }
    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        const float inv = 1.f / na0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }
};

// PolyBLEP correction for band-limited saw / square.
inline float polyBlep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

enum class Wave : uint8_t { Sine, Triangle, Saw, Square };

inline float sineFast(float phase01) {
    // Parabolic sine approximation with one refinement step: error < 0.1 %, fine for oscillators and LFOs.
    float x = phase01 - std::floor(phase01);
    x = x * kTwoPi - kPi;  // −π..π
    float y = 1.27323954f * x - 0.405284735f * x * (x < 0 ? -x : x);
    y = 0.225f * (y * (y < 0 ? -y : y) - y) + y;
    return -y;  // sin(phase·2π)
}

inline float oscillator(Wave w, float phase, float dt) {
    switch (w) {
        case Wave::Sine: return sineFast(phase);
        case Wave::Triangle: return 1.f - 4.f * std::fabs(phase - 0.5f);
        case Wave::Saw: return 2.f * phase - 1.f - polyBlep(phase, dt);
        case Wave::Square: {
            float v = phase < 0.5f ? 1.f : -1.f;
            v += polyBlep(phase, dt);
            float p2 = phase + 0.5f;
            if (p2 >= 1.f) p2 -= 1.f;
            v -= polyBlep(p2, dt);
            return v;
        }
    }
    return 0.f;
}

// Cheap white noise (xorshift32), −1..1.
inline float noise(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(static_cast<int32_t>(s)) * (1.f / 2147483648.f);
}

inline float centsToRatio(float cents) { return std::exp2(cents * (1.f / 1200.f)); }

// Soft clip with no look-ahead (the web app's DynamicsCompressor added 6 ms to every note).
inline float softClip(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

}  // namespace mm
