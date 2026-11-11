// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "builtin.h"

#include <cmath>
#include <cstring>

namespace mm {
namespace {

constexpr float kSilence = 1e-4f;  // −80 dB: a decaying voice is freed below this

const BuiltinPreset kPresets[] = {
    // id          n  waves                                               detune (cents)        cutoff  q    a      d     s     r     fenv  vib  vibD level organ fm
    {"lead", 2, {Wave::Saw, Wave::Saw}, {0, 8}, 2600, 2, 0.01f, 0.25f, 0.7f, 0.15f, 1800, 5.5f, 6, 0.35f, false, false},
    {"oud", 2, {Wave::Triangle, Wave::Saw}, {0, -3}, 2800, 1, 0.003f, 0.9f, 0, 0.12f, 3500, 0, 0, 0.5f, false, false},
    {"tambour", 2, {Wave::Saw, Wave::Square}, {0, 5}, 1600, 3, 0.004f, 1.1f, 0, 0.15f, 2500, 0, 0, 0.42f, false, false},
    {"accordion", 3, {Wave::Square, Wave::Saw, Wave::Square}, {0, 4, -4}, 3200, 0.7f, 0.025f, 0.1f, 1, 0.1f, 400, 5.2f, 5, 0.22f, false, false},
    {"sax", 2, {Wave::Saw, Wave::Saw}, {0, 3}, 1500, 3, 0.035f, 0.3f, 0.85f, 0.15f, 2600, 5, 8, 0.33f, false, false},
    {"strings", 3, {Wave::Saw, Wave::Saw, Wave::Saw}, {-7, 0, 7}, 2200, 0.5f, 0.12f, 0.3f, 0.9f, 0.5f, 600, 4.5f, 4, 0.2f, false, false},
    {"brass", 2, {Wave::Saw, Wave::Saw}, {0, 6}, 1800, 2.5f, 0.03f, 0.3f, 0.8f, 0.14f, 3000, 0, 0, 0.3f, false, false},
    {"piano", 2, {Wave::Triangle, Wave::Sine}, {0, 0.5f}, 4500, 0.5f, 0.003f, 1.6f, 0.15f, 0.25f, 3000, 0, 0, 0.5f, false, false},
    {"organ", 4, {Wave::Sine, Wave::Sine, Wave::Sine, Wave::Sine}, {0, 1200, 1902, 2400}, 6000, 0.3f, 0.01f, 0.05f, 1, 0.06f, 0, 0, 0, 0.22f, true, false},
    {"bass", 2, {Wave::Sine, Wave::Saw}, {0, 0}, 700, 1.5f, 0.005f, 0.35f, 0.6f, 0.08f, 900, 0, 0, 0.55f, false, false},
    {"flute", 2, {Wave::Sine, Wave::Triangle}, {0, 0}, 3000, 1, 0.04f, 0.2f, 0.9f, 0.18f, 500, 5, 10, 0.4f, false, false},
    {"epiano", 1, {Wave::Sine}, {0}, 18000, 0, 0.001f, 0, 0, 0.08f, 0, 0, 0, 0.36f, false, true},
};
constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

inline float mtof(float n) { return 440.f * std::exp2((n - 69.f) / 12.f); }

}  // namespace

int builtinPresetCount() { return kPresetCount; }
const BuiltinPreset& builtinPreset(int i) { return kPresets[i < 0 || i >= kPresetCount ? 0 : i]; }
int builtinPresetIndex(const char* id) {
    for (int i = 0; i < kPresetCount; ++i)
        if (std::strcmp(kPresets[i].id, id) == 0) return i;
    return -1;
}

void BuiltinSynth::prepare(float sampleRate, const ChannelState* channels) {
    sr_ = sampleRate > 0 ? sampleRate : 48000.f;
    ch_ = channels;
    for (auto& v : voices_) v = Voice{};
    for (auto& d : drums_) d = DrumVoice{};
}

BuiltinSynth::Voice* BuiltinSynth::allocate() {
    for (auto& v : voices_)
        if (!v.active) return &v;
    Voice* best = nullptr;
    for (auto& v : voices_)  // the quietest released voice, else the oldest
        if (v.released && (!best || v.amp < best->amp)) best = &v;
    if (best) return best;
    for (auto& v : voices_)
        if (!best || v.age < best->age) best = &v;
    return best;
}

void BuiltinSynth::noteOn(int ch, int note, int vel, int preset) {
    if (note < 0 || note > 127 || ch < 0 || ch > 15) return;
    if (vel <= 0) { noteOff(ch, note); return; }
    if (preset < 0) { startDrum(ch, note, vel); return; }
    const BuiltinPreset& p = builtinPreset(preset);
    Voice* v = nullptr;
    for (auto& c : voices_)  // the same key again restarts its voice (no stacking)
        if (c.active && c.note == note && c.ch == ch && !c.released) { v = &c; break; }
    const bool reuse = v != nullptr;
    if (!v) v = allocate();
    const float amp0 = reuse ? v->amp : 0.f;
    const Biquad filt = reuse ? v->filter : Biquad{};
    *v = Voice{};
    v->active = true;
    v->ch = ch;
    v->note = note;
    v->held = true;
    v->age = ++age_;
    v->p = &p;
    v->amp = amp0;
    v->filter = filt;
    const float vv = std::fmax(0.05f, static_cast<float>(vel > 127 ? 127 : vel) / 127.f);
    const float freq = mtof(static_cast<float>(note));
    const bool keyPart = ch_ && ch_[ch].keyPart;
    if (p.fm) {
        // M1 two-operator electric piano (1:1 tine).
        const float velN = static_cast<float>(vel > 127 ? 127 : vel) / 127.f;
        v->inc[0] = v->inc[1] = freq / sr_;
        v->peakTarget = 0.06f + 0.30f * velN * std::sqrt(velN);
        v->attackLeft = static_cast<int>(0.001f * sr_) + 1;
        v->attackStep = (v->peakTarget - v->amp) / static_cast<float>(v->attackLeft);
        const float tau = std::fmax(0.5f, std::fmin(3.f, 2.6f - (static_cast<float>(note) - 36.f) * 0.03f));
        v->decayMul = std::exp(-1.f / (tau * sr_));
        v->releaseCoef = std::exp(-1.f / (p.r * sr_));
        v->modIndex = 0.6f + 2.2f * velN;
        v->modIndexMul = std::exp(-1.f / (0.35f * sr_));
        return;
    }
    for (int i = 0; i < p.nOsc; ++i) {
        v->inc[i] = freq * centsToRatio(p.detune[i]) / sr_;
        v->oscGain[i] = p.organ ? 1.f / static_cast<float>(i + 1) : 1.f / static_cast<float>(p.nOsc);
        if (!reuse) v->phase[i] = 0.f;
    }
    if (p.vib > 0) v->vibInc = p.vib / sr_;
    const float a = keyPart ? std::fmin(p.a, 0.01f) : p.a;  // ≤ 10 ms on the player's hands
    const float cut = std::fmin(18000.f, p.cutoff * (0.5f + vv));
    v->fcBase = cut;
    v->fcEnv = p.fenv * vv;
    v->fenvDelay = static_cast<int>(a * sr_);
    v->fcCoef = std::exp(-1.f / (std::fmax(0.03f, p.d / 3.f) * sr_));
    v->peakTarget = p.level * (0.35f + 0.65f * vv);
    v->attackLeft = static_cast<int>(a * sr_) + 1;
    v->attackStep = (v->peakTarget - v->amp) / static_cast<float>(v->attackLeft);
    v->decayCoef = std::exp(-1.f / (std::fmax(0.01f, p.d / 3.f) * sr_));
    v->releaseCoef = std::exp(-1.f / (std::fmax(0.005f, p.r / 4.f) * sr_));
}

void BuiltinSynth::release(Voice& v, bool fast) {
    if (!v.active) return;
    v.held = false;
    v.sustained = false;
    v.released = true;
    v.attackLeft = 0;
    if (fast) v.releaseCoef = std::exp(-1.f / ((0.03f / 4.f) * sr_));
}

void BuiltinSynth::noteOff(int ch, int note) {
    for (auto& v : voices_) {
        if (!v.active || !v.held || v.note != note || v.ch != ch) continue;
        if (ch_ && ch_[ch].sustain) { v.held = false; v.sustained = true; }
        else release(v, false);
    }
}

void BuiltinSynth::sustainUp(int ch) {
    for (auto& v : voices_)
        if (v.active && v.ch == ch && v.sustained) release(v, false);
}

void BuiltinSynth::allOff(int ch, bool fast) {
    for (auto& v : voices_)
        if (v.active && (ch < 0 || v.ch == ch)) release(v, fast);
    if (fast)
        for (auto& d : drums_)
            if (d.active && (ch < 0 || d.ch == ch)) d.active = false;
}

void BuiltinSynth::kill() {
    for (auto& v : voices_) v = Voice{};
    for (auto& d : drums_) d = DrumVoice{};
}

int BuiltinSynth::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) n += v.active;
    for (const auto& d : drums_) n += d.active;
    return n;
}

void BuiltinSynth::render(const MixBus& mix) {
    for (auto& v : voices_)
        if (v.active) renderVoice(v, mix);
    for (auto& d : drums_)
        if (d.active) renderDrum(d, mix);
}

void BuiltinSynth::renderVoice(Voice& v, const MixBus& mix) {
    const ChannelState& cs = ch_[v.ch];
    const float gain = cs.gain;
    const float send = cs.reverb;
    const BuiltinPreset& p = *v.p;
    const int n = mix.frames;
    if (p.fm) {
        const float inc = v.inc[0] * cs.bend;
        for (int f = 0; f < n; ++f) {
            if (v.attackLeft > 0) { v.amp += v.attackStep; if (--v.attackLeft == 0) v.amp = v.peakTarget; }
            else v.amp *= v.released ? v.releaseCoef : v.decayMul;
            const float mod = sineFast(v.phase[1]) * (v.modIndex + 0.15f) * (1.f / kTwoPi);
            const float s = sineFast(v.phase[0] + mod) * v.amp * gain;
            v.modIndex *= v.modIndexMul;
            v.phase[0] += inc; if (v.phase[0] >= 1.f) v.phase[0] -= 1.f;
            v.phase[1] += inc; if (v.phase[1] >= 1.f) v.phase[1] -= 1.f;
            mix.left[f] += s;
            mix.right[f] += s;
            mix.reverb[f] += s * send;
        }
        if (v.attackLeft == 0 && v.amp < kSilence) v.active = false;
        return;
    }
    // Control rate (once per block): vibrato, bend, filter envelope.
    float ratio = cs.bend;
    if (v.vibInc > 0) {
        ratio *= centsToRatio(p.vibDepth * sineFast(v.vibPhase));
        v.vibPhase += v.vibInc * static_cast<float>(n);
        if (v.vibPhase >= 1.f) v.vibPhase -= std::floor(v.vibPhase);
    }
    if (v.fenvDelay > 0) v.fenvDelay -= n;
    else v.fcEnv *= std::pow(v.fcCoef, static_cast<float>(n));
    v.filter.lowpass(v.fcBase + v.fcEnv, p.q, sr_);
    float inc[4];
    for (int i = 0; i < p.nOsc; ++i) inc[i] = v.inc[i] * ratio;
    const float target = p.s * v.peakTarget;
    for (int f = 0; f < n; ++f) {
        if (v.attackLeft > 0) {
            v.amp += v.attackStep;
            if (--v.attackLeft == 0) v.amp = v.peakTarget;
        } else if (v.released) {
            v.amp *= v.releaseCoef;
        } else {
            v.amp = target + (v.amp - target) * v.decayCoef;
        }
        float s = 0.f;
        for (int i = 0; i < p.nOsc; ++i) {
            s += oscillator(p.wave[i], v.phase[i], inc[i]) * v.oscGain[i];
            v.phase[i] += inc[i];
            if (v.phase[i] >= 1.f) v.phase[i] -= 1.f;
        }
        s = v.filter.process(s) * v.amp * gain;
        mix.left[f] += s;
        mix.right[f] += s;
        mix.reverb[f] += s * send;
    }
    if (v.attackLeft == 0 && v.amp < kSilence && (v.released || p.s <= 0.f)) v.active = false;
}

// ───────────────────────── percussion kit ─────────────────────────

BuiltinSynth::DrumVoice* BuiltinSynth::drum() {
    for (auto& d : drums_)
        if (!d.active) return &d;
    DrumVoice* best = nullptr;  // steal the one closest to its end
    for (auto& d : drums_)
        if (!best || (d.life - d.t) < (best->life - best->t)) best = &d;
    return best;
}

void BuiltinSynth::tone(int ch, float delayS, Wave w, float f0, float f1, float sweepS, float gain, float durS) {
    DrumVoice* d = drum();
    *d = DrumVoice{};
    d->active = true;
    d->ch = ch;
    d->wave = w;
    d->delay = static_cast<int>(delayS * sr_);
    d->dur = static_cast<int>(durS * sr_) + 1;
    d->life = d->dur + static_cast<int>(0.02f * sr_);
    d->freq = f0;
    d->f1 = f1;
    d->sweep = static_cast<int>(sweepS * sr_) + 1;
    d->fMul = std::exp(std::log(f1 / f0) / static_cast<float>(d->sweep));
    d->g = gain;
    d->gMul = std::exp(std::log(0.001f / std::fmax(gain, 0.0011f)) / static_cast<float>(d->dur));
}

void BuiltinSynth::noiseBurst(int ch, float delayS, float level, float durS, float freq, int type, float q) {
    DrumVoice* d = drum();
    *d = DrumVoice{};
    d->active = true;
    d->isNoise = true;
    d->ch = ch;
    d->delay = static_cast<int>(delayS * sr_);
    d->dur = static_cast<int>(durS * sr_) + 1;
    d->life = d->dur + static_cast<int>(0.02f * sr_);
    d->g = level;
    d->gMul = std::exp(std::log(0.001f / std::fmax(level, 0.0011f)) / static_cast<float>(d->dur));
    seed_ = seed_ * 1664525u + 1013904223u;
    d->rng = seed_ | 1u;
    if (type == 1) d->filter.highpass(freq, q, sr_);
    else if (type == 2) d->filter.bandpass(freq, q, sr_);
    else d->filter.lowpass(freq, q, sr_);
}

void BuiltinSynth::startDrum(int ch, int note, int vel) {
    const float v = std::fmax(0.1f, static_cast<float>(vel > 127 ? 127 : vel) / 127.f);
    auto kick = [&](float vv, float f0, float f1, float dur) {
        tone(ch, 0, Wave::Sine, f0, f1, 0.09f, 1.1f * vv, dur);
        noiseBurst(ch, 0, 0.4f * vv, 0.012f, 3000, 1, 0.8f);
    };
    auto membrane = [&](float vv, float f, float dur, float noiseMix) {
        tone(ch, 0, Wave::Sine, f * 1.5f, f, 0.03f, 0.9f * vv, dur);
        noiseBurst(ch, 0, noiseMix * vv, dur * 0.4f, f * 6, 2, 0.8f);
    };
    auto tak = [&](float vv, float f, float dur) {
        noiseBurst(ch, 0, 0.8f * vv, dur, f, 2, 2);
        tone(ch, 0, Wave::Triangle, f * 0.6f, f * 0.35f, dur, 0.35f * vv, dur);
    };
    auto shaker = [&](float vv, float dur, float f) { noiseBurst(ch, 0, 0.35f * vv, dur, f, 1, 0.8f); };
    auto bell = [&](float vv) {
        tone(ch, 0, Wave::Square, 560, 560, 0.01f, 0.6f * vv * 0.5f, 0.25f);
        tone(ch, 0, Wave::Square, 840, 840, 0.01f, 0.3f * vv * 0.5f, 0.25f);
        tone(ch, 0, Wave::Square, 1344, 1344, 0.01f, 0.2f * vv * 0.5f, 0.25f);
    };
    switch (note) {
        case 35: kick(v, 120, 42, 0.5f); break;
        case 36: kick(v, 150, 48, 0.45f); break;
        case 38: membrane(v * 0.8f, 190, 0.12f, 0.2f); noiseBurst(ch, 0, 0.7f * v, 0.16f, 1800, 1, 0.7f); break;
        case 40: membrane(v * 0.72f, 190, 0.12f, 0.2f); noiseBurst(ch, 0, 0.63f * v, 0.16f, 1800, 1, 0.7f); break;
        case 63: membrane(v, 215, 0.26f, 0.35f); break;       // dum
        case 62: membrane(v * 0.8f, 260, 0.12f, 0.5f); break;
        case 60: tak(v, 1500, 0.08f); break;                  // tak
        case 61: tak(v, 1100, 0.1f); break;
        case 64: membrane(v, 105, 0.42f, 0.3f); break;        // mambo
        case 41: membrane(v, 120, 0.35f, 0.5f); break;        // nugara / tabl low
        case 43: membrane(v, 140, 0.33f, 0.5f); break;
        case 45: membrane(v, 170, 0.3f, 0.45f); break;
        case 47: membrane(v, 210, 0.26f, 0.45f); break;
        case 48: membrane(v, 320, 0.18f, 0.5f); break;        // nugara / tabl high
        case 50: membrane(v, 380, 0.16f, 0.5f); break;
        case 65: membrane(v, 420, 0.14f, 0.7f); break;
        case 66: membrane(v, 300, 0.18f, 0.7f); break;
        case 39:                                              // clap
            for (int i = 0; i < 3; ++i) noiseBurst(ch, i * 0.011f, 0.5f * v, 0.02f, 1800, 2, 1);
            noiseBurst(ch, 0.03f, 0.6f * v, 0.14f, 1600, 2, 1.2f);
            break;
        case 70: shaker(v, 0.05f, 8000); break;               // kashkoush
        case 69: shaker(v, 0.08f, 6500); break;
        case 82: shaker(v, 0.1f, 7500); break;
        case 42: shaker(v, 0.06f, 7000); break;
        case 44: shaker(v * 0.7f, 0.05f, 7500); break;
        case 46: shaker(v, 0.35f, 6000); break;
        case 51: shaker(v * 0.8f, 0.6f, 5000); break;
        case 59: shaker(v * 0.8f, 0.5f, 5500); break;
        case 53: bell(v * 0.7f); break;
        case 76: tak(v, 2300, 0.04f); break;                  // shatam sticks
        case 77: tak(v, 1700, 0.05f); break;
        case 75: tak(v, 2500, 0.03f); break;
        case 54:                                              // riq
            noiseBurst(ch, 0, 0.45f * v, 0.1f, 6500, 1, 0.7f);
            noiseBurst(ch, 0.02f, 0.25f * v, 0.12f, 9000, 2, 3);
            break;
        case 37: tak(v, 2600, 0.05f); break;
        case 56: bell(v); break;
        case 67: bell(v * 0.8f); break;
        case 68: bell(v * 0.7f); break;
        case 80: tak(v * 0.6f, 6000, 0.06f); break;
        case 81: shaker(v * 0.6f, 0.5f, 9000); break;
        case 49: shaker(v, 1.2f, 4000); break;
        case 57: shaker(v, 1.0f, 4500); break;
        case 52: shaker(v, 0.9f, 3500); break;
        case 55: shaker(v, 0.5f, 5000); break;
        case 73: case 74: shaker(v * 0.6f, 0.12f, 3000); break;
        default:
            if (note < 60) membrane(v, 180, 0.2f, 0.4f);
            else tak(v, 1800, 0.06f);
    }
}

void BuiltinSynth::renderDrum(DrumVoice& d, const MixBus& mix) {
    const ChannelState& cs = ch_[d.ch];
    const float gain = cs.gain, send = cs.reverb;
    for (int f = 0; f < mix.frames; ++f) {
        if (d.delay > 0) { --d.delay; continue; }
        if (d.t >= d.life) { d.active = false; return; }
        float s;
        if (d.isNoise) {
            s = d.filter.process(noise(d.rng));
        } else {
            const float dt = d.freq / sr_;
            s = oscillator(d.wave, d.phase, dt);
            d.phase += dt;
            if (d.phase >= 1.f) d.phase -= 1.f;
            if (d.t < d.sweep) d.freq *= d.fMul;
            else d.freq = d.f1;
        }
        s *= d.g * gain;
        if (d.t < d.dur) d.g *= d.gMul;
        mix.left[f] += s;
        mix.right[f] += s;
        mix.reverb[f] += s * send;
        ++d.t;
    }
}

}  // namespace mm
