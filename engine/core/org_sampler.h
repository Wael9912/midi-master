// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// Plays the sampled instruments of an ORG 24 set (ported from legacy-web/js/orgsampler.js): the sample whose root is
// nearest the played note is pitched to it, looped when the set says so. Leading silence is skipped. The PCM is decoded
// when the set is loaded (never on the first note). Real-time safe.
#pragma once
#include "dsp.h"
#include "orgset.h"

namespace mm {

class OrgSampler {
public:
    static constexpr int kMaxVoices = 48;

    void prepare(float sampleRate, const ChannelState* channels);
    void setSet(const OrgSet* set);  // stops every voice of the previous set
    const OrgSet* set() const { return set_; }
    void noteOn(int ch, int note, int vel, int instrument);
    void noteOff(int ch, int note);
    void sustainUp(int ch);
    void allOff(int ch, bool fast);
    void kill();
    void render(const MixBus& mix);
    int activeVoices() const;

private:
    struct Voice {
        bool active = false, held = false, sustained = false, released = false;
        int ch = 0, note = 0;
        uint32_t age = 0;
        const OrgSample* s = nullptr;
        double pos = 0, inc = 0;
        bool loop = false;
        float peak = 0, amp = 1, releaseCoef = 1;
    };
    void release(Voice& v, bool fast);

    Voice voices_[kMaxVoices];
    const OrgSet* set_ = nullptr;
    const ChannelState* ch_ = nullptr;
    float sr_ = 48000.f;
    uint32_t age_ = 0;
};

}  // namespace mm
