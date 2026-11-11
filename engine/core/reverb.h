// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// A small feedback-delay-network reverb (8 lines, Householder feedback, one-pole damping), mono in, stereo out. Its
// buffers are sized in prepare(); process() is allocation-free. The dry path never passes through it, so it adds no
// latency to the notes.
#pragma once
#include <cmath>
#include <vector>

namespace mm {

class Reverb {
public:
    static constexpr int kLines = 8;

    void prepare(float sampleRate) {
        sr_ = sampleRate > 0 ? sampleRate : 48000.f;
        static const int kBase[kLines] = {1171, 1307, 1493, 1637, 1801, 1949, 2111, 2293};
        for (int i = 0; i < kLines; ++i) {
            len_[i] = static_cast<int>(kBase[i] * sr_ / 48000.f);
            buf_[i].assign(static_cast<size_t>(len_[i]), 0.f);
            pos_[i] = 0;
            lp_[i] = 0;
        }
        setTime(time_);
    }
    // Decay time to −60 dB, seconds.
    void setTime(float seconds) {
        time_ = seconds < 0.2f ? 0.2f : seconds > 6.f ? 6.f : seconds;
        for (int i = 0; i < kLines; ++i) g_[i] = std::pow(10.f, -3.f * static_cast<float>(len_[i]) / (time_ * sr_));
    }
    void clear() {
        for (int i = 0; i < kLines; ++i) {
            for (float& x : buf_[i]) x = 0.f;
            lp_[i] = 0;
        }
    }
    // Adds the wet signal of `in` (mono send) to left / right.
    void process(const float* in, float* left, float* right, int frames, float wet) {
        if (len_[0] == 0) return;
        const float damp = 0.32f;
        for (int f = 0; f < frames; ++f) {
            float y[kLines];
            float sum = 0.f;
            for (int i = 0; i < kLines; ++i) {
                y[i] = buf_[i][static_cast<size_t>(pos_[i])];
                sum += y[i];
            }
            const float h = sum * (2.f / kLines);  // Householder: y − 2/N·Σy
            const float x = in[f] * 0.35f;
            float l = 0.f, r = 0.f;
            for (int i = 0; i < kLines; ++i) {
                float fb = (y[i] - h) * g_[i];
                lp_[i] += (fb - lp_[i]) * (1.f - damp);
                fb = lp_[i];
                buf_[i][static_cast<size_t>(pos_[i])] = fb + x;
                if (++pos_[i] >= len_[i]) pos_[i] = 0;
                if (i & 1) r += y[i]; else l += y[i];
            }
            left[f] += l * wet;
            right[f] += r * wet;
        }
    }

private:
    float sr_ = 48000.f, time_ = 1.6f;
    std::vector<float> buf_[kLines];
    int len_[kLines] = {}, pos_[kLines] = {};
    float g_[kLines] = {}, lp_[kLines] = {};
};

}  // namespace mm
