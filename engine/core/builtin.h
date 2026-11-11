// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// The built-in sounds, ported from legacy-web/js/synth.js: eleven subtractive presets (lead, oud, tambour, accordion,
// sax, strings, brass, piano, organ, bass, flute), the M1 FM electric piano, and the percussion kit covering the GM
// drum map with the Sudanese strokes (dum 63, tak 60, mambo 64, kashkoush 70, shatam 76, riq 54).
// Real-time safe: fixed voice pools, no allocation, no locks.
#pragma once
#include <cstdint>

#include "dsp.h"

namespace mm {

struct BuiltinPreset {
    const char* id;
    int nOsc;
    Wave wave[4];
    float detune[4];  // cents
    float cutoff, q, a, d, s, r, fenv, vib, vibDepth, level;
    bool organ;
    bool fm;
};

int builtinPresetCount();
const BuiltinPreset& builtinPreset(int i);
int builtinPresetIndex(const char* id);  // -1 when unknown

class BuiltinSynth {
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kMaxDrumVoices = 64;

    void prepare(float sampleRate, const ChannelState* channels);
    // `preset` = builtin preset index, or -1 for the kit.
    void noteOn(int ch, int note, int vel, int preset);
    void noteOff(int ch, int note);
    void sustainUp(int ch);            // releases the notes the pedal held
    void allOff(int ch, bool fast);    // ch = -1: all channels
    void kill();                       // silence at once
    void render(const MixBus& mix);
    int activeVoices() const;

private:
    struct Voice {
        bool active = false;
        int ch = 0, note = 0;
        bool held = false, sustained = false, released = false;
        uint32_t age = 0;
        const BuiltinPreset* p = nullptr;
        float phase[4] = {}, inc[4] = {}, oscGain[4] = {};
        float vibPhase = 0, vibInc = 0;
        // amplitude envelope
        int attackLeft = 0;
        float amp = 0, attackStep = 0, peakTarget = 0, decayCoef = 0, releaseCoef = 0;
        // filter envelope
        int fenvDelay = 0;
        float fcBase = 0, fcEnv = 0, fcCoef = 0;
        Biquad filter;
        // FM electric piano
        float modIndex = 0, modIndexMul = 1, decayMul = 1;
    };
    struct DrumVoice {
        bool active = false;
        bool isNoise = false;
        Wave wave = Wave::Sine;
        int ch = 9;
        int delay = 0, t = 0, life = 0, sweep = 0, dur = 0;
        float phase = 0, freq = 0, f1 = 0, fMul = 1;
        float g = 0, gMul = 1;
        uint32_t rng = 1;
        Biquad filter;
    };

    Voice* allocate();
    void release(Voice& v, bool fast);
    void startDrum(int ch, int note, int vel);
    DrumVoice* drum();
    void tone(int ch, float delayS, Wave w, float f0, float f1, float sweepS, float gain, float durS);
    void noiseBurst(int ch, float delayS, float level, float durS, float freq, int type /*0 lp,1 hp,2 bp*/, float q);
    void renderVoice(Voice& v, const MixBus& mix);
    void renderDrum(DrumVoice& d, const MixBus& mix);

    Voice voices_[kMaxVoices];
    DrumVoice drums_[kMaxDrumVoices];
    const ChannelState* ch_ = nullptr;
    float sr_ = 48000.f;
    uint32_t age_ = 0;
    uint32_t seed_ = 0x9E3779B9u;
};

}  // namespace mm
