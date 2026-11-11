// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "sequencer.h"

#include <cmath>

namespace mm {

void Sequencer::setStyle(const Style* style, int64_t now) {
    const bool was = playing_;
    if (was) stop();
    style_ = style;
    if (style_) tempo_ = style_->tempo < 40 ? 40 : style_->tempo > 240 ? 240 : style_->tempo;
    variation_ = 0;
    if (was && style_) start(false, now);
}

void Sequencer::setTempo(int bpm, int64_t now) {
    const int t = bpm < 40 ? 40 : bpm > 240 ? 240 : bpm;
    if (t == tempo_) return;
    if (playing_) {
        // Keep the beat position: re-anchor the part start under the new tempo (the web app jumped instead).
        const double beats = (static_cast<double>(now) - partStart_) / framesPerBeat();
        tempo_ = t;
        partStart_ = static_cast<double>(now) - beats * framesPerBeat();
    } else {
        tempo_ = t;
    }
}

void Sequencer::start(bool intro, int64_t now) {
    if (!style_ || playing_) return;
    playing_ = true;
    syncArmed_ = false;
    enterPart(intro ? kIntro : mainPart(), static_cast<double>(now));
}

void Sequencer::stop() {
    if (!playing_) return;
    playing_ = false;
    pending_ = kPendNone;
}

void Sequencer::fill(int toVariation) {
    if (!playing_) {
        if (toVariation >= 0) setVariation(toVariation);
        return;
    }
    pendingVariation_ = toVariation >= 0 ? toVariation : variation_;
    pending_ = kPendFill;
}

void Sequencer::setVariation(int v) {
    if (!playing_) { variation_ = v; return; }
    if (v != variation_) fill(v);
}

void Sequencer::ending() {
    if (playing_) pending_ = kPendEnding;
}

void Sequencer::enterPart(int part, double at) {
    part_ = part;
    partStart_ = at;
    barIndex_ = 0;
    eventIndex_ = 0;
}

int Sequencer::nextPart() {
    const int p = part_;
    if (p == kEnding) return -1;
    if (pending_ == kPendEnding) { pending_ = kPendNone; return kEnding; }
    if (p == kIntro) return mainPart();
    if (p == kFillA || p == kFillB) {
        if (pendingVariation_ >= 0) { variation_ = pendingVariation_; pendingVariation_ = -1; }
        return mainPart();
    }
    if (pending_ == kPendFill) { pending_ = kPendNone; return variation_ == 1 ? kFillB : kFillA; }
    return mainPart();
}

void Sequencer::process(int64_t from, int frames, SequencerSink& sink) {
    if (!playing_ || !style_) return;
    const double horizon = static_cast<double>(from + frames);  // exclusive
    for (int guard = 0; guard < 256; ++guard) {
        const PartData& part = style_->parts[part_];
        const double fpb = framesPerBeat();
        const double barEndBeat = (barIndex_ + 1) * style_->beatsPerBar;
        while (eventIndex_ < part.events.size() && part.events[eventIndex_].t < barEndBeat - 1e-9) {
            const Event& ev = part.events[eventIndex_];
            const double t = partStart_ + ev.t * fpb;
            if (t >= horizon) return;
            ++eventIndex_;
            play(ev, t, ev.d * fpb, sink);
        }
        const double barEnd = partStart_ + barEndBeat * fpb;
        if (barEnd >= horizon) return;
        // Bar line reached: a queued fill or ending cuts a main part here.
        const bool inMain = part_ == kMainA || part_ == kMainB;
        const bool interrupt = inMain && (pending_ == kPendFill || pending_ == kPendEnding);
        if (!interrupt && barIndex_ + 1 < part.bars) {
            ++barIndex_;
            continue;
        }
        const int next = nextPart();
        if (next < 0) {
            playing_ = false;
            pending_ = kPendNone;
            return;
        }
        enterPart(next, barEnd);
    }
}

void Sequencer::play(const Event& ev, double at, double dur, SequencerSink& sink) {
    if (ev.track < 64 && (muted_ >> ev.track) & 1ULL) return;
    if (ev.track >= style_->tracks.size()) return;
    const Track& tr = style_->tracks[ev.track];
    const int n = transposeNote(ev.n, tr.transpose, chord_);
    sink.seqNote(tr.channel, n, ev.v, at, dur);
}

double Sequencer::beatsAt(int64_t now) const {
    if (!playing_ || !style_) return 0;
    const double b = (static_cast<double>(now) - partStart_) / framesPerBeat();
    return b > 0 ? b : 0;
}

}  // namespace mm
