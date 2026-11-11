// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "sf2_synth.h"

#include <cmath>

namespace mm {
namespace {

// SF2 2.04 defaults (§8.1.3) for the generators whose default is not 0.
int16_t defaultGen(int g) {
    switch (g) {
        case kGenInitialFilterFc: return 13500;
        case kGenDelayModLfo: case kGenDelayVibLfo: case kGenDelayModEnv: case kGenAttackModEnv: case kGenHoldModEnv:
        case kGenDecayModEnv: case kGenReleaseModEnv: case kGenDelayVolEnv: case kGenAttackVolEnv: case kGenHoldVolEnv:
        case kGenDecayVolEnv: case kGenReleaseVolEnv: return -12000;
        case kGenKeynum: case kGenVelocity: case kGenOverridingRootKey: return -1;
        case kGenScaleTuning: return 100;
        default: return 0;
    }
}

// Generators a preset zone may not offset (instrument-only), per SF2 §8.5.
bool instrumentOnly(int g) {
    switch (g) {
        case kGenStartAddrsOffset: case kGenEndAddrsOffset: case kGenStartloopAddrsOffset: case kGenEndloopAddrsOffset:
        case kGenStartAddrsCoarseOffset: case kGenEndAddrsCoarseOffset: case kGenStartloopAddrsCoarseOffset:
        case kGenEndloopAddrsCoarseOffset: case kGenKeynum: case kGenVelocity: case kGenSampleModes:
        case kGenExclusiveClass: case kGenOverridingRootKey: case kGenSampleId: case kGenInstrument:
        case kGenKeyRange: case kGenVelRange: return true;
        default: return false;
    }
}

struct Gens {
    int v[kGenCount];
};

inline float tc2s(int tc) { return std::exp2(static_cast<float>(tc) / 1200.f); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float lfoTri(float p) { return p < 0.25f ? 4.f * p : p < 0.75f ? 2.f - 4.f * p : 4.f * p - 4.f; }
inline float absHz(float cents) { return 8.176f * std::exp2(cents / 1200.f); }

}  // namespace

void Sf2Synth::prepare(float sampleRate, const ChannelState* channels) {
    sr_ = sampleRate > 0 ? sampleRate : 48000.f;
    ch_ = channels;
    kill();
}

void Sf2Synth::kill() {
    for (auto& v : voices_) v = Voice{};
}

void Sf2Synth::killBank(const Sf2Bank* bank) {
    for (auto& v : voices_)
        if (v.active && v.bank == bank) v = Voice{};
}

int Sf2Synth::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) n += v.active;
    return n;
}

Sf2Synth::Voice* Sf2Synth::allocate() {
    for (auto& v : voices_)
        if (!v.active) return &v;
    Voice* best = nullptr;
    for (auto& v : voices_)
        if (v.released && (!best || v.env < best->env)) best = &v;
    if (best) return best;
    for (auto& v : voices_)
        if (!best || v.age < best->age) best = &v;
    return best;
}

void Sf2Synth::noteOn(int ch, int note, int vel, const Sf2Bank* bank, int presetIndex) {
    if (!bank || note < 0 || note > 127 || ch < 0 || ch > 15) return;
    if (vel <= 0) { noteOff(ch, note); return; }
    if (presetIndex < 0 || static_cast<size_t>(presetIndex) >= bank->presets().size()) return;
    const Sf2Preset& preset = bank->presets()[static_cast<size_t>(presetIndex)];
    for (const Sf2Zone& pz : preset.zones) {
        if (note < pz.keyLo || note > pz.keyHi || vel < pz.velLo || vel > pz.velHi) continue;
        const Sf2Instrument& inst = bank->instruments()[static_cast<size_t>(pz.target)];
        for (const Sf2Zone& iz : inst.zones) {
            if (note < iz.keyLo || note > iz.keyHi || vel < iz.velLo || vel > iz.velHi) continue;
            const Sf2Sample& s = bank->samples()[static_cast<size_t>(iz.target)];
            const int16_t* data = s.data.load(std::memory_order_acquire);
            if (!data) continue;  // not loaded yet (the app loads a preset's samples before selecting it)
            Gens g;
            for (int k = 0; k < kGenCount; ++k) {
                int val = iz.has(k) ? iz.gen[k] : (inst.hasGlobal && inst.global.has(k)) ? inst.global.gen[k] : defaultGen(k);
                if (!instrumentOnly(k)) val += pz.has(k) ? pz.gen[k] : (preset.hasGlobal && preset.global.has(k)) ? preset.global.gen[k] : 0;
                g.v[k] = val;
            }
            const int exclusive = g.v[kGenExclusiveClass];
            if (exclusive)
                for (auto& o : voices_)
                    if (o.active && o.ch == ch && o.exclusive == exclusive && o.bank == bank) release(o, true);

            Voice* v = allocate();
            *v = Voice{};
            v->active = true;
            v->held = true;
            v->ch = ch;
            v->note = note;
            v->exclusive = exclusive;
            v->age = ++age_;
            v->bank = bank;
            v->data = data;
            const int64_t len = static_cast<int64_t>(s.end) - s.start;
            int64_t start = g.v[kGenStartAddrsOffset] + 32768LL * g.v[kGenStartAddrsCoarseOffset];
            int64_t end = len + g.v[kGenEndAddrsOffset] + 32768LL * g.v[kGenEndAddrsCoarseOffset];
            int64_t ls = static_cast<int64_t>(s.loopStart) - s.start + g.v[kGenStartloopAddrsOffset] + 32768LL * g.v[kGenStartloopAddrsCoarseOffset];
            int64_t le = static_cast<int64_t>(s.loopEnd) - s.start + g.v[kGenEndloopAddrsOffset] + 32768LL * g.v[kGenEndloopAddrsCoarseOffset];
            if (end > len) end = len;
            if (start < 0) start = 0;
            if (start >= end - 1) start = 0;
            v->end = end;
            const int modes = g.v[kGenSampleModes] & 3;
            const bool loopOk = ls >= 0 && le > ls + 1 && le <= end;
            v->mode = loopOk && (modes == 1 || modes == 3) ? modes : 0;
            v->loopStart = ls;
            v->loopEnd = le;
            // Skip the sample's leading silence (never into the loop).
            int64_t pos = start;
            if (start == 0 && s.lead > 0) pos = std::min<int64_t>(s.lead, v->mode ? ls : end - 2);
            v->pos = static_cast<double>(pos > 0 ? pos : 0);

            // Pitch.
            int root = g.v[kGenOverridingRootKey];
            if (root < 0 || root > 127) root = s.originalPitch > 127 ? 60 : s.originalPitch;
            const int playNote = g.v[kGenKeynum] >= 0 ? g.v[kGenKeynum] : note;
            const float cents = static_cast<float>(playNote - root) * static_cast<float>(g.v[kGenScaleTuning]) +
                                g.v[kGenCoarseTune] * 100.f + g.v[kGenFineTune] + s.correction;
            v->baseInc = std::exp2(cents / 1200.0) * static_cast<double>(s.rate ? s.rate : 44100) / sr_;

            // Level: attenuation (×0.4, as FluidSynth) and the default velocity curve (concave, 960 cB).
            const int velocity = g.v[kGenVelocity] >= 0 ? g.v[kGenVelocity] : vel;
            const float vn = static_cast<float>(velocity > 127 ? 127 : velocity < 1 ? 1 : velocity) / 127.f;
            const float atten = 0.4f * static_cast<float>(clampi(g.v[kGenInitialAttenuation], 0, 1440));
            v->gain = std::pow(10.f, -atten / 200.f) * vn * vn;
            const float pan = static_cast<float>(clampi(g.v[kGenPan], -500, 500));
            const float th = (pan + 500.f) / 1000.f * (kPi / 2);
            v->panL = std::fmin(1.f, std::cos(th) * 1.41421356f);
            v->panR = std::fmin(1.f, std::sin(th) * 1.41421356f);
            v->send = clampi(g.v[kGenReverbSend], 0, 1000) / 1000.f;

            // Volume envelope (hold and decay scale with the key, per keynumToVolEnvHold / Decay).
            const int keyOff = 60 - note;
            const auto frames = [&](float sec) { const float f = sec * sr_; return f < 1 ? 1 : static_cast<int>(f); };
            const int delayTc = g.v[kGenDelayVolEnv];
            v->volLeft = delayTc <= -12000 ? 0 : frames(tc2s(clampi(delayTc, -12000, 5000)));
            v->attackFrames = frames(tc2s(clampi(g.v[kGenAttackVolEnv], -12000, 8000)));
            v->attackStep = 1.f / static_cast<float>(v->attackFrames);
            v->volStage = v->volLeft > 0 ? 0 : 1;
            if (v->volStage == 1) v->volLeft = v->attackFrames;
            v->volHold = frames(tc2s(clampi(g.v[kGenHoldVolEnv] + keyOff * g.v[kGenKeynumToVolEnvHold], -12000, 5000)));
            const float decayS = tc2s(clampi(g.v[kGenDecayVolEnv] + keyOff * g.v[kGenKeynumToVolEnvDecay], -12000, 8000));
            const float releaseS = tc2s(clampi(g.v[kGenReleaseVolEnv], -12000, 8000));
            v->sustain = std::pow(10.f, -static_cast<float>(clampi(g.v[kGenSustainVolEnv], 0, 1440)) / 200.f);
            v->decayMul = std::pow(10.f, -5.f / static_cast<float>(frames(decayS)));      // 100 dB over the decay time
            v->releaseMul = std::pow(10.f, -5.f / static_cast<float>(frames(releaseS)));  // 100 dB over the release time
            v->env = 0;
            // Modulation envelope (linear, 0..1).
            const int mDelayTc = g.v[kGenDelayModEnv];
            v->modStage = 0;
            v->modLeft = mDelayTc <= -12000 ? 0 : frames(tc2s(clampi(mDelayTc, -12000, 5000)));
            v->modAttackStep = 1.f / static_cast<float>(frames(tc2s(clampi(g.v[kGenAttackModEnv], -12000, 8000))));
            v->modHold = frames(tc2s(clampi(g.v[kGenHoldModEnv] + keyOff * g.v[kGenKeynumToModEnvHold], -12000, 5000)));
            v->modDecayStep = 1.f / static_cast<float>(frames(tc2s(clampi(g.v[kGenDecayModEnv] + keyOff * g.v[kGenKeynumToModEnvDecay], -12000, 8000))));
            v->modReleaseStep = 1.f / static_cast<float>(frames(tc2s(clampi(g.v[kGenReleaseModEnv], -12000, 8000))));
            v->modSustain = 1.f - clampi(g.v[kGenSustainModEnv], 0, 1000) / 1000.f;
            // LFOs.
            v->modLfoDelay = g.v[kGenDelayModLfo] <= -12000 ? 0 : frames(tc2s(clampi(g.v[kGenDelayModLfo], -12000, 5000)));
            v->vibLfoDelay = g.v[kGenDelayVibLfo] <= -12000 ? 0 : frames(tc2s(clampi(g.v[kGenDelayVibLfo], -12000, 5000)));
            v->modLfoInc = absHz(static_cast<float>(clampi(g.v[kGenFreqModLfo], -16000, 4500))) / sr_;
            v->vibLfoInc = absHz(static_cast<float>(clampi(g.v[kGenFreqVibLfo], -16000, 4500))) / sr_;
            v->modLfoToPitch = static_cast<float>(clampi(g.v[kGenModLfoToPitch], -12000, 12000));
            v->vibLfoToPitch = static_cast<float>(clampi(g.v[kGenVibLfoToPitch], -12000, 12000));
            v->modEnvToPitch = static_cast<float>(clampi(g.v[kGenModEnvToPitch], -12000, 12000));
            v->modLfoToFc = static_cast<float>(clampi(g.v[kGenModLfoToFilterFc], -12000, 12000));
            v->modEnvToFc = static_cast<float>(clampi(g.v[kGenModEnvToFilterFc], -12000, 12000));
            v->modLfoToVol = static_cast<float>(clampi(g.v[kGenModLfoToVolume], -960, 960));
            // Filter.
            v->fcCents = static_cast<float>(clampi(g.v[kGenInitialFilterFc], 1500, 13500));
            const float qDb = clampi(g.v[kGenInitialFilterQ], 0, 960) / 10.f;
            v->q = std::pow(10.f, qDb / 20.f);
            v->filterGain = 1.f / std::sqrt(v->q);
            v->filterOn = v->fcCents < 13490.f || v->modLfoToFc != 0 || v->modEnvToFc != 0;
            if (!v->filterOn) v->filterGain = 1.f;
        }
    }
}

void Sf2Synth::release(Voice& v, bool fast) {
    if (!v.active || v.released) return;
    v.released = true;
    v.held = false;
    v.sustained = false;
    if (fast) v.releaseMul = std::pow(10.f, -5.f / (0.005f * sr_));
    if (v.volStage == 0) { v.active = false; return; }  // still in its delay: nothing heard yet
    v.volStage = 5;
    v.modStage = 5;
}

void Sf2Synth::noteOff(int ch, int note) {
    for (auto& v : voices_) {
        if (!v.active || !v.held || v.ch != ch || v.note != note) continue;
        if (ch_ && ch_[ch].sustain) { v.held = false; v.sustained = true; }
        else release(v, false);
    }
}

void Sf2Synth::sustainUp(int ch) {
    for (auto& v : voices_)
        if (v.active && v.ch == ch && v.sustained) release(v, false);
}

void Sf2Synth::allOff(int ch, bool fast) {
    for (auto& v : voices_)
        if (v.active && (ch < 0 || v.ch == ch)) release(v, fast);
}

void Sf2Synth::render(const MixBus& mix) {
    for (auto& v : voices_)
        if (v.active) renderVoice(v, mix);
}

void Sf2Synth::renderVoice(Voice& v, const MixBus& mix) {
    const int n = mix.frames;
    const ChannelState& cs = ch_[v.ch];
    // ── control rate: LFOs and the modulation envelope, once per block ──
    float modLfo = 0, vibLfo = 0;
    if (v.modLfoDelay > 0) v.modLfoDelay -= n;
    else {
        modLfo = lfoTri(v.modLfoPhase);
        v.modLfoPhase += v.modLfoInc * n;
        v.modLfoPhase -= std::floor(v.modLfoPhase);
    }
    if (v.vibLfoDelay > 0) v.vibLfoDelay -= n;
    else {
        vibLfo = lfoTri(v.vibLfoPhase);
        v.vibLfoPhase += v.vibLfoInc * n;
        v.vibLfoPhase -= std::floor(v.vibLfoPhase);
    }
    switch (v.modStage) {
        case 0:  // delay
            if ((v.modLeft -= n) <= 0) v.modStage = 1;
            break;
        case 1:  // attack
            v.modEnv += v.modAttackStep * n;
            if (v.modEnv >= 1.f) { v.modEnv = 1.f; v.modStage = 2; v.modLeft = v.modHold; }
            break;
        case 2:  // hold
            if ((v.modLeft -= n) <= 0) v.modStage = 3;
            break;
        case 3:  // decay
            v.modEnv -= v.modDecayStep * n;
            if (v.modEnv <= v.modSustain) { v.modEnv = v.modSustain; v.modStage = 4; }
            break;
        case 5:  // release
            v.modEnv -= v.modReleaseStep * n;
            if (v.modEnv < 0) v.modEnv = 0;
            break;
        default: break;
    }
    const float pitchCents = modLfo * v.modLfoToPitch + vibLfo * v.vibLfoToPitch + v.modEnv * v.modEnvToPitch;
    double inc = v.baseInc * cs.bend;
    if (pitchCents != 0) inc *= std::exp2(pitchCents / 1200.0);
    if (v.filterOn) {
        const float fc = v.fcCents + modLfo * v.modLfoToFc + v.modEnv * v.modEnvToFc;
        v.filter.lowpassLinearQ(absHz(fc < 1500 ? 1500 : fc > 13500 ? 13500 : fc), v.q, sr_);
    }
    float lfoVol = 1.f;
    if (v.modLfoToVol != 0) lfoVol = std::pow(10.f, -(modLfo * v.modLfoToVol) / 200.f);
    const float g = v.gain * lfoVol * v.filterGain * cs.gain * (1.f / 32768.f);
    const float gl = g * v.panL, gr = g * v.panR;
    const float send = v.send + cs.reverb;
    const int16_t* d = v.data;
    const int64_t loopStart = v.loopStart, loopEnd = v.loopEnd, loopLen = loopEnd - loopStart;
    const int64_t last = v.end;  // data is valid (zero padded) up to end + 3
    for (int f = 0; f < n; ++f) {
        // Volume envelope, per sample.
        switch (v.volStage) {
            case 0:
                if (--v.volLeft <= 0) v.volStage = 1;
                break;
            case 1:
                v.env += v.attackStep;
                if (v.env >= 1.f) { v.env = 1.f; v.volStage = 2; v.volLeft = v.volHold; }
                break;
            case 2:
                if (--v.volLeft <= 0) v.volStage = 3;
                break;
            case 3:
                v.env *= v.decayMul;
                if (v.env <= v.sustain) { v.env = v.sustain; v.volStage = 4; }
                break;
            case 5:
                v.env *= v.releaseMul;
                break;
            default: break;
        }
        const bool looping = v.mode == 1 || (v.mode == 3 && !v.released);
        if (looping)
            while (v.pos >= static_cast<double>(loopEnd)) v.pos -= static_cast<double>(loopLen);
        const int64_t i = static_cast<int64_t>(v.pos);
        if (!looping && i >= last - 1) { v.active = false; return; }
        const float fr = static_cast<float>(v.pos - static_cast<double>(i));
        int64_t i0 = i - 1, i2 = i + 1, i3 = i + 2;
        if (looping) {
            if (i2 >= loopEnd) i2 -= loopLen;
            if (i3 >= loopEnd) i3 -= loopLen;
        }
        if (i0 < -Sf2Bank::kPadBefore) i0 = -Sf2Bank::kPadBefore;
        const float x0 = d[i0], x1 = d[i], x2 = d[i2], x3 = d[i3];
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        float y = ((c3 * fr + c2) * fr + c1) * fr + x1;
        if (v.filterOn) y = v.filter.process(y);
        y *= v.env;
        mix.left[f] += y * gl;
        mix.right[f] += y * gr;
        mix.reverb[f] += y * (gl + gr) * 0.5f * send;
        v.pos += inc;
    }
    if ((v.volStage == 5 || v.volStage == 4 || v.volStage == 3) && v.env < 1e-5f) v.active = false;
}

}  // namespace mm
