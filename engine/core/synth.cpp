// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "synth.h"

#include <cmath>

namespace mm {
namespace {

constexpr int kTableBits = 12;
constexpr int kTableSize = 1 << kTableBits;
float gSine[kTableSize + 1];  // filled by prepare(), read by the audio thread
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kSilence = 1e-4f;  // −80 dB: the voice is free below this in release

inline float sine01(float phase) {  // phase in [0, 1)
    const float pos = phase * kTableSize;
    const int i = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(i);
    return gSine[i] + (gSine[i + 1] - gSine[i]) * frac;
}

inline float wrap01(float p) { return p - std::floor(p); }

}  // namespace

void Synth::prepare(float sampleRate) {
    sampleRate_ = sampleRate > 0.f ? sampleRate : 48000.f;
    for (int i = 0; i <= kTableSize; ++i) gSine[i] = std::sin(kTwoPi * static_cast<float>(i) / kTableSize);
    releaseMul_ = std::exp(-1.f / (0.08f * sampleRate_));
    for (auto& v : voices_) v = FmVoice{};
    parser_.reset();
    sustain_ = false;
}

FmVoice* Synth::allocate() {
    FmVoice* best = nullptr;
    for (auto& v : voices_)
        if (v.stage == FmVoice::Stage::Off) return &v;
    // Steal: the quietest released voice first, then the oldest one.
    for (auto& v : voices_)
        if (v.stage == FmVoice::Stage::Release && (!best || v.amp < best->amp)) best = &v;
    if (best) return best;
    for (auto& v : voices_)
        if (!best || v.age < best->age) best = &v;
    return best;
}

void Synth::noteOn(int channel, int note, int velocity) {
    if (note < 0 || note > 127) return;
    if (velocity <= 0) { noteOff(channel, note); return; }
    FmVoice* v = nullptr;
    for (auto& cand : voices_)  // same key again: restart that voice (no stacking, no click)
        if (cand.stage != FmVoice::Stage::Off && cand.note == note && cand.channel == channel) { v = &cand; break; }
    const bool reuse = v != nullptr;
    if (!v) v = allocate();

    const float vel = static_cast<float>(velocity > 127 ? 127 : velocity) / 127.f;
    const float freq = 440.f * std::exp2((static_cast<float>(note) - 69.f) / 12.f);
    const float sr = sampleRate_;
    v->note = note;
    v->channel = channel;
    v->held = true;
    v->sustained = false;
    v->age = ++ageCounter_;
    v->carrierInc = freq / sr;
    v->modInc = freq / sr;  // 1:1 ratio: a tine-like electric piano
    if (!reuse) { v->carrierPhase = 0.f; v->modPhase = 0.f; v->amp = 0.f; }
    v->peak = 0.06f + 0.30f * vel * std::sqrt(vel);
    v->attackStep = v->peak / (0.001f * sr);  // 1 ms attack
    const float tau = std::fmax(0.5f, std::fmin(3.f, 2.6f - (static_cast<float>(note) - 36.f) * 0.03f));
    v->decayMul = std::exp(-1.f / (tau * sr));
    v->releaseMul = releaseMul_;
    v->modIndex = 0.6f + 2.2f * vel;
    v->modIndexMul = std::exp(-1.f / (0.35f * sr));
    v->stage = FmVoice::Stage::Attack;
}

void Synth::release(FmVoice& v) {
    v.held = false;
    v.sustained = false;
    if (v.stage != FmVoice::Stage::Off) v.stage = FmVoice::Stage::Release;
}

void Synth::noteOff(int channel, int note) {
    for (auto& v : voices_) {
        if (v.stage == FmVoice::Stage::Off || !v.held || v.note != note || v.channel != channel) continue;
        if (sustain_) { v.held = false; v.sustained = true; }
        else release(v);
    }
}

void Synth::setSustain(bool down) {
    sustain_ = down;
    if (down) return;
    for (auto& v : voices_)
        if (v.sustained) release(v);
}

void Synth::allNotesOff() {
    sustain_ = false;
    for (auto& v : voices_)
        if (v.stage != FmVoice::Stage::Off) release(v);
}

void Synth::allSoundOff() {
    sustain_ = false;
    for (auto& v : voices_) v = FmVoice{};
}

void Synth::handle(const MidiMessage& m) {
    switch (m.type()) {
        case 0x90: noteOn(m.channel(), m.data1, m.data2); break;
        case 0x80: noteOff(m.channel(), m.data1); break;
        case 0xB0:
            if (m.data1 == 64) setSustain(m.data2 >= 64);
            else if (m.data1 == 120) allSoundOff();
            else if (m.data1 == 123) allNotesOff();
            break;
        default: break;  // program change, pitch bend: later milestones
    }
}

void Synth::render(float* stereo, int frames) {
    for (int i = 0; i < frames * 2; ++i) stereo[i] = 0.f;
    for (auto& v : voices_) {
        if (v.stage == FmVoice::Stage::Off) continue;
        for (int f = 0; f < frames; ++f) {
            switch (v.stage) {
                case FmVoice::Stage::Attack:
                    v.amp += v.attackStep;
                    if (v.amp >= v.peak) { v.amp = v.peak; v.stage = FmVoice::Stage::Decay; }
                    break;
                case FmVoice::Stage::Decay: v.amp *= v.decayMul; break;
                case FmVoice::Stage::Release: v.amp *= v.releaseMul; break;
                case FmVoice::Stage::Off: break;
            }
            const float mod = sine01(v.modPhase) * (v.modIndex + 0.15f) * (1.f / kTwoPi);
            const float s = sine01(wrap01(v.carrierPhase + mod)) * v.amp;
            v.modIndex *= v.modIndexMul;
            v.carrierPhase = wrap01(v.carrierPhase + v.carrierInc);
            v.modPhase = wrap01(v.modPhase + v.modInc);
            stereo[2 * f] += s;
            stereo[2 * f + 1] += s;
        }
        if ((v.stage == FmVoice::Stage::Release || v.stage == FmVoice::Stage::Decay) && v.amp < kSilence) v = FmVoice{};
    }
    for (int i = 0; i < frames * 2; ++i) stereo[i] = softClip(stereo[i] * masterGain_);
}

int Synth::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_)
        if (v.stage != FmVoice::Stage::Off) ++n;
    return n;
}

}  // namespace mm
