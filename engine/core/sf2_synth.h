// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// SoundFont 2 voices: key/velocity zones, preset generators added to instrument ones, root key / tuning / scale tuning,
// loop modes 1 and 3, the DAHDSR volume envelope (linear attack, decay and release in dB), the modulation envelope and
// both LFOs (pitch, filter, volume), a resonant low-pass per voice, pan, reverb send, exclusive classes (hi-hat choke)
// and the SF2 default velocity curve. 4-point cubic interpolation. Same attenuation scaling as FluidSynth (×0.4), the
// player the bundled FluidR3Mono bank was voiced for. Real-time safe: fixed pool, no allocation.
#pragma once
#include "dsp.h"
#include "sf2.h"

namespace mm {

class Sf2Synth {
public:
    static constexpr int kMaxVoices = 96;

    void prepare(float sampleRate, const ChannelState* channels);
    void noteOn(int ch, int note, int vel, const Sf2Bank* bank, int preset);
    void noteOff(int ch, int note);
    void sustainUp(int ch);
    void allOff(int ch, bool fast);
    void killBank(const Sf2Bank* bank);  // before a bank is removed
    void kill();
    void render(const MixBus& mix);
    int activeVoices() const;

private:
    struct Voice {
        bool active = false, held = false, sustained = false, released = false;
        int ch = 0, note = 0, exclusive = 0;
        uint32_t age = 0;
        const Sf2Bank* bank = nullptr;
        const int16_t* data = nullptr;
        int64_t end = 0, loopStart = 0, loopEnd = 0;
        int mode = 0;  // 0 none, 1 loop, 3 loop until release
        double pos = 0, baseInc = 0;
        // volume envelope: 0 delay, 1 attack, 2 hold, 3 decay, 4 sustain, 5 release
        int volStage = 0, volLeft = 0, volHold = 0, attackFrames = 1;
        float env = 0, attackStep = 0, decayMul = 1, sustain = 1, releaseMul = 1;
        // modulation envelope (0..1)
        int modStage = 0, modLeft = 0, modHold = 0;
        float modEnv = 0, modAttackStep = 0, modDecayStep = 0, modSustain = 1, modReleaseStep = 0;
        // LFOs
        int modLfoDelay = 0, vibLfoDelay = 0;
        float modLfoPhase = 0, modLfoInc = 0, vibLfoPhase = 0, vibLfoInc = 0;
        float modLfoToPitch = 0, vibLfoToPitch = 0, modEnvToPitch = 0, modLfoToFc = 0, modEnvToFc = 0, modLfoToVol = 0;
        // filter
        bool filterOn = false;
        float fcCents = 13500, q = 1, filterGain = 1;
        Biquad filter;
        float gain = 0, panL = 1, panR = 1, send = 0;
    };
    Voice* allocate();
    void release(Voice& v, bool fast);
    void renderVoice(Voice& v, const MixBus& mix);

    Voice voices_[kMaxVoices];
    const ChannelState* ch_ = nullptr;
    float sr_ = 48000.f;
    uint32_t age_ = 0;
};

}  // namespace mm
