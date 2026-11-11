// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// The style sequencer, ported from legacy-web/js/sequencer.js. It runs inside the audio callback with no look-ahead:
// process() is called for each block and emits the notes that start inside it, so a chord change applies to the very
// next note. Times are frames (double, so bars chained by arithmetic never drift). Real-time safe: no allocation.
#pragma once
#include <cstdint>

#include "chord.h"
#include "style.h"

namespace mm {

class SequencerSink {
public:
    virtual ~SequencerSink() = default;
    // A note that starts at frame `at` (inside the current block) and lasts `durFrames`.
    virtual void seqNote(int channel, int note, int velocity, double at, double durFrames) = 0;
};

class Sequencer {
public:
    enum Pending : int { kPendNone = 0, kPendFill = 1, kPendEnding = 2 };

    void setSampleRate(double sr) { sampleRate_ = sr > 0 ? sr : 48000; }
    // Changing the style while playing restarts it from main A (as the web app did). Tempo becomes the style's tempo.
    void setStyle(const Style* style, int64_t now);
    const Style* style() const { return style_; }
    void setTempo(int bpm, int64_t now);
    int tempo() const { return tempo_; }
    void setChord(const Chord& c) { chord_ = c; }
    const Chord& chord() const { return chord_; }

    void start(bool intro, int64_t now);
    void stop();
    bool playing() const { return playing_; }
    void fill(int toVariation /* -1 = stay, 0 = A, 1 = B */);
    void setVariation(int v);
    void ending();
    void armSync(bool on) { syncArmed_ = on; }
    bool syncArmed() const { return syncArmed_; }
    void setMuted(int track, bool on) {
        if (track >= 0 && track < 64) muted_ = on ? (muted_ | (1ULL << track)) : (muted_ & ~(1ULL << track));
    }
    void setMuteMask(uint64_t m) { muted_ = m; }

    // Emits every note that starts in [from, from + frames).
    void process(int64_t from, int frames, SequencerSink& sink);

    int part() const { return part_; }
    int pending() const { return pending_; }
    int pendingVariation() const { return pendingVariation_; }
    int variation() const { return variation_; }
    double partStart() const { return partStart_; }
    // Beats since the current part started (0 when stopped).
    double beatsAt(int64_t now) const;

private:
    double framesPerBeat() const { return sampleRate_ * 60.0 / tempo_; }
    int mainPart() const { return variation_ == 1 ? kMainB : kMainA; }
    void enterPart(int part, double at);
    int nextPart();  // -1 = stop
    void play(const Event& ev, double at, double dur, SequencerSink& sink);

    const Style* style_ = nullptr;
    double sampleRate_ = 48000;
    int tempo_ = 120;
    Chord chord_;
    bool playing_ = false;
    bool syncArmed_ = false;
    int part_ = kMainA;
    int variation_ = 0;  // 0 = A, 1 = B
    int pending_ = kPendNone;
    int pendingVariation_ = -1;
    uint64_t muted_ = 0;
    double partStart_ = 0;  // frame of the current part's first beat
    int barIndex_ = 0;
    size_t eventIndex_ = 0;
};

}  // namespace mm
