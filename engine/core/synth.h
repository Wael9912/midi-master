// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// M1 test instrument: a two-operator FM electric piano. Its job is to prove the latency path, so it is built to be
// heard at once: 1 ms attack, no look-ahead, no leading silence. Real-time safe: everything is preallocated in the
// object, render() and handle() never allocate, lock or call the OS.
#pragma once
#include <cstdint>

#include "midi_parser.h"

namespace mm {

struct FmVoice {
    enum class Stage : uint8_t { Off, Attack, Decay, Release };
    Stage stage = Stage::Off;
    int note = -1;
    int channel = 0;
    bool held = false;       // key is down
    bool sustained = false;  // key released while the pedal is down
    uint32_t age = 0;        // start order, for stealing the oldest
    float carrierPhase = 0.f, modPhase = 0.f;  // 0..1
    float carrierInc = 0.f, modInc = 0.f;
    float amp = 0.f;          // current envelope value
    float peak = 0.f;         // velocity gain
    float attackStep = 0.f;   // per sample
    float decayMul = 1.f;     // per sample
    float releaseMul = 1.f;   // per sample
    float modIndex = 0.f;     // current FM index
    float modIndexMul = 1.f;  // per-sample decay of the index
};

class Synth {
public:
    static constexpr int kMaxVoices = 32;

    void prepare(float sampleRate);
    void handle(const MidiMessage& m);
    // Feeds raw MIDI bytes (any chunking, running status).
    void handleBytes(const uint8_t* bytes, size_t n) {
        parser_.feed(bytes, n, [this](const MidiMessage& m) { handle(m); });
    }
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void allNotesOff();     // release everything
    void allSoundOff();     // silence at once
    void setSustain(bool down);
    void setMasterGain(float g) { masterGain_ = g; }

    // Writes `frames` interleaved stereo float frames (overwrites the buffer).
    void render(float* stereo, int frames);

    int activeVoices() const;
    float sampleRate() const { return sampleRate_; }

    // Soft clip with no look-ahead (replaces the web app's DynamicsCompressor, which added 6 ms).
    static float softClip(float x) {
        if (x > 3.f) return 1.f;
        if (x < -3.f) return -1.f;
        const float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

private:
    FmVoice* allocate();
    void release(FmVoice& v);

    FmVoice voices_[kMaxVoices];
    MidiParser parser_;
    float sampleRate_ = 48000.f;
    float masterGain_ = 0.5f;
    float releaseMul_ = 0.999f;
    bool sustain_ = false;
    uint32_t ageCounter_ = 0;
};

}  // namespace mm
