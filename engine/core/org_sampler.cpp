// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "org_sampler.h"

#include <cmath>
#include <cstdlib>

namespace mm {

void OrgSampler::prepare(float sampleRate, const ChannelState* channels) {
    sr_ = sampleRate > 0 ? sampleRate : 48000.f;
    ch_ = channels;
    kill();
}

void OrgSampler::setSet(const OrgSet* set) {
    kill();
    set_ = set;
}

void OrgSampler::kill() {
    for (auto& v : voices_) v = Voice{};
}

int OrgSampler::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) n += v.active;
    return n;
}

void OrgSampler::noteOn(int ch, int note, int vel, int instrument) {
    if (!set_ || instrument < 0 || instrument >= static_cast<int>(set_->instruments.size()) || note < 0 || note > 127) return;
    if (vel <= 0) { noteOff(ch, note); return; }
    const OrgInstrument& inst = set_->instruments[static_cast<size_t>(instrument)];
    const OrgSample* best = nullptr;
    int bd = 1 << 30;
    for (const auto& s : inst.samples) {
        const int d = std::abs(s.root - note);
        if (d < bd) { bd = d; best = &s; }
    }
    if (!best || best->pcm.empty() || best->frames < 2) return;
    Voice* v = nullptr;
    for (auto& c : voices_)
        if (!c.active) { v = &c; break; }
    if (!v) {  // steal the oldest
        for (auto& c : voices_)
            if (!v || c.age < v->age) v = &c;
    }
    *v = Voice{};
    v->active = true;
    v->held = true;
    v->ch = ch;
    v->note = note;
    v->age = ++age_;
    v->s = best;
    const float cents = best->cents > -100 && best->cents < 100 ? static_cast<float>(best->cents) : 0.f;
    v->inc = std::exp2((note - best->root + cents / 100.0) / 12.0) * best->rate / sr_;
    v->loop = best->loop && best->loopEnd > best->loopStart && best->loopEnd <= best->frames && best->loopStart >= 0;
    v->pos = v->loop ? 0 : best->lead;
    const float vv = std::fmax(0.05f, static_cast<float>(vel > 127 ? 127 : vel) / 127.f);
    v->peak = (0.3f + 0.7f * vv) * std::fmin(1.5f, static_cast<float>(best->volume ? best->volume : 100) / 100.f);
    v->amp = 1.f;
}

void OrgSampler::release(Voice& v, bool fast) {
    if (!v.active || v.released) return;
    v.released = true;
    v.held = false;
    v.sustained = false;
    v.releaseCoef = std::exp(-1.f / (((fast ? 0.03f : 0.14f) / 4.f) * sr_));
}

void OrgSampler::noteOff(int ch, int note) {
    for (auto& v : voices_) {
        if (!v.active || !v.held || v.ch != ch || v.note != note) continue;
        if (ch_ && ch_[ch].sustain) { v.held = false; v.sustained = true; }
        else release(v, false);
    }
}

void OrgSampler::sustainUp(int ch) {
    for (auto& v : voices_)
        if (v.active && v.ch == ch && v.sustained) release(v, false);
}

void OrgSampler::allOff(int ch, bool fast) {
    for (auto& v : voices_)
        if (v.active && (ch < 0 || v.ch == ch)) release(v, fast);
}

void OrgSampler::render(const MixBus& mix) {
    for (auto& v : voices_) {
        if (!v.active) continue;
        const OrgSample& s = *v.s;
        const ChannelState& cs = ch_[v.ch];
        const double inc = v.inc * cs.bend;
        const float g = v.peak * cs.gain, send = cs.reverb;
        const int16_t* pcm = s.pcm.data();
        const int chans = s.channels;
        const double end = static_cast<double>(s.frames - 1);
        const double loopStart = s.loopStart, loopEnd = s.loopEnd, loopLen = loopEnd - loopStart;
        for (int f = 0; f < mix.frames; ++f) {
            if (v.loop)
                while (v.pos >= loopEnd) v.pos -= loopLen;
            if (!v.loop && v.pos >= end) { v.active = false; break; }
            const int i = static_cast<int>(v.pos);
            const float fr = static_cast<float>(v.pos - i);
            int j = i + 1;
            if (v.loop && j >= s.loopEnd) j = s.loopStart;
            if (j >= s.frames) j = s.frames - 1;
            float l, r;
            if (chans == 2) {
                const float l0 = pcm[2 * i], l1 = pcm[2 * j], r0 = pcm[2 * i + 1], r1 = pcm[2 * j + 1];
                l = (l0 + (l1 - l0) * fr) * (1.f / 32768.f);
                r = (r0 + (r1 - r0) * fr) * (1.f / 32768.f);
            } else {
                const float a = pcm[i], b = pcm[j];
                l = r = (a + (b - a) * fr) * (1.f / 32768.f);
            }
            if (v.released) v.amp *= v.releaseCoef;
            const float k = g * v.amp;
            mix.left[f] += l * k;
            mix.right[f] += r * k;
            mix.reverb[f] += (l + r) * 0.5f * k * send;
            v.pos += inc;
        }
        if (v.released && v.amp < 1e-4f) v.active = false;
    }
}

}  // namespace mm
