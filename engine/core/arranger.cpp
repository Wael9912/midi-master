// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "arranger.h"

#include <cmath>
#include <cstring>

namespace mm {

Arranger::Arranger() {
    for (auto& h : held) h.store(0);
    for (int ch = 0; ch < 16; ++ch) {
        programs_[ch].kind = (ch == 9 || ch == 10) ? Program::Kit : Program::Builtin;
        programs_[ch].index = 0;
        chan_[ch].keyPart = ch <= 1;
        chan_[ch].reverb = ch <= 1 ? 0.12f : (ch == 9 || ch == 10) ? 0.05f : 0.1f;
    }
    applyGains();
}

void Arranger::prepare(float sampleRate) {
    sr_ = sampleRate > 0 ? sampleRate : 48000.f;
    builtin_.prepare(sr_, chan_);
    sf2_.prepare(sr_, chan_);
    orgSampler_.prepare(sr_, chan_);
    orgSampler_.setSet(org_);
    seq_.setSampleRate(sr_);
    seq_.stop();
    reverb_.prepare(sr_);
    nOffs_ = 0;
    parser_.reset();
    std::memset(heldMask_, 0, sizeof heldMask_);
    std::memset(chordZone_, 0, sizeof chordZone_);
    publish();
}

void Arranger::applyGains() {
    for (int ch = 0; ch < 16; ++ch) chan_[ch].gain = bus_[busOf(ch)];
}

void Arranger::retire(Retired::Kind k, void* p) {
    if (!p) return;
    // The queue holds 64; the UI drains it every 250 ms. A full queue leaks rather than frees on this thread.
    retired_.push(Retired{k, p});
}

void Arranger::resolveProgram(int ch) {
    Program& p = programs_[ch];
    p.preset = -1;
    if (p.kind == Program::Sf2 && p.index >= 0 && p.index < kBanks && banks_[p.index])
        p.preset = static_cast<int16_t>(banks_[p.index]->findPreset(p.bank, p.program));
}

// ───────────────────────── commands ─────────────────────────

void Arranger::command(const Command& c) {
    switch (c.kind) {
        case Command::KeyOn: keyOn(c.a, c.b); break;
        case Command::KeyOff: keyOff(c.a); break;
        case Command::Sustain:
            kbSustain_ = c.a != 0;
            sustain(0, kbSustain_);
            sustain(1, kbSustain_);
            break;
        case Command::Bend:
            bendSemis_ = c.f;
            chan_[0].bend = chan_[1].bend = std::exp2(c.f / 12.f);
            break;
        case Command::Panic:
            allOff(-1, true);
            nOffs_ = 0;
            std::memset(heldMask_, 0, sizeof heldMask_);
            std::memset(chordZone_, 0, sizeof chordZone_);
            kbSustain_ = false;
            for (auto& cs : chan_) cs.sustain = false;
            break;
        case Command::SetProgram: {
            if (c.a < 0 || c.a > 15) break;
            Program& p = programs_[c.a];
            p.kind = static_cast<uint8_t>(c.b);
            p.index = static_cast<int16_t>(c.c);
            p.bank = static_cast<int16_t>((c.d >> 8) & 0xFFFF);
            p.program = static_cast<int16_t>(c.d & 0xFF);
            resolveProgram(c.a);
            break;
        }
        case Command::Preview:
            if (!seq_.playing() && c.a >= 0 && c.a < 16) {
                for (int n = 0; n < 128; ++n)
                    if (heldMask_[n]) return;  // the player is playing: no preview
                seqNote(c.a, c.b, 96, static_cast<double>(now_), 0.6 * sr_);
            }
            break;
        case Command::AddBank:
            if (c.a < 0 || c.a >= kBanks) { retire(Retired::Bank, c.p); break; }
            if (banks_[c.a]) {
                sf2_.killBank(banks_[c.a]);
                retire(Retired::Bank, banks_[c.a]);
            }
            banks_[c.a] = static_cast<Sf2Bank*>(c.p);
            for (int ch = 0; ch < 16; ++ch) resolveProgram(ch);
            break;
        case Command::RemoveBank:
            if (c.a < 0 || c.a >= kBanks || !banks_[c.a]) break;
            sf2_.killBank(banks_[c.a]);
            retire(Retired::Bank, banks_[c.a]);
            banks_[c.a] = nullptr;
            for (int ch = 0; ch < 16; ++ch) resolveProgram(ch);
            break;
        case Command::SetOrg:
            if (org_ != c.p) {
                orgSampler_.setSet(static_cast<const OrgSet*>(c.p));
                retire(Retired::Org, const_cast<OrgSet*>(org_));
                org_ = static_cast<const OrgSet*>(c.p);
            }
            break;
        case Command::SetStyle: {
            const Style* old = style_;
            const bool was = seq_.playing();
            if (was) { allOff(-2, false); nOffs_ = 0; }
            style_ = static_cast<const Style*>(c.p);
            seq_.setStyle(style_, now_);
            if (old != style_) retire(Retired::Style, const_cast<Style*>(old));
            break;
        }
        case Command::Start:
            if (!seq_.playing()) seq_.start(c.a != 0, now_);
            break;
        case Command::Stop:
            if (seq_.playing()) {
                seq_.stop();
                for (int i = 0; i < nOffs_; ++i) engineOff(offs_[i].ch, offs_[i].note);
                nOffs_ = 0;
                allOff(-2, false);
            }
            break;
        case Command::Fill: seq_.fill(c.a); break;
        case Command::Variation: seq_.setVariation(c.a); break;
        case Command::Ending: seq_.ending(); break;
        case Command::Sync: seq_.armSync(c.a != 0); break;
        case Command::Tempo: seq_.setTempo(c.a, now_); break;
        case Command::Split: split_ = c.a; break;
        case Command::Transpose: transpose_ = c.a; break;
        case Command::ChordMode: oneFinger_ = c.a != 0; break;
        case Command::LowerOn: lowerOn_ = c.a != 0; break;
        case Command::Layer: layer_ = c.a != 0; break;
        case Command::MuteMask:
            seq_.setMuteMask(static_cast<uint32_t>(c.a) | (static_cast<uint64_t>(static_cast<uint32_t>(c.b)) << 32));
            break;
        case Command::Bus:
            if (c.a >= 0 && c.a < 4) bus_[c.a] = c.f < 0 ? 0 : c.f > 1.5f ? 1.5f : c.f;
            applyGains();
            break;
        case Command::ReverbLevel: reverbLevel_ = c.f < 0 ? 0 : c.f > 1 ? 1 : c.f; break;
    }
}

void Arranger::handleMidi(const MidiMessage& m) {
    switch (m.type()) {
        case 0x90: if (m.data2 > 0) keyOn(m.data1, m.data2); else keyOff(m.data1); break;
        case 0x80: keyOff(m.data1); break;
        case 0xB0:
            if (m.data1 == 64) { kbSustain_ = m.data2 >= 64; sustain(0, kbSustain_); sustain(1, kbSustain_); }
            else if (m.data1 == 120 || m.data1 == 123) { Command c; c.kind = Command::Panic; command(c); }
            break;
        case 0xE0: {
            const int v = (m.data2 << 7) | m.data1;
            const float semis = (static_cast<float>(v) - 8192.f) / 8192.f * 2.f;
            chan_[0].bend = chan_[1].bend = std::exp2(semis / 12.f);
            break;
        }
        default: break;  // program changes from the keyboard's own panel are ignored
    }
}

// ───────────────────────── the player's keys ─────────────────────────

void Arranger::keyOn(int note, int vel) {
    if (note < 0 || note > 127) return;
    if (vel <= 0) { keyOff(note); return; }
    if (heldMask_[note]) keyOff(note);
    uint8_t mask = 0;
    if (note < split_) {
        chordZone_[note] = true;
        int notes[128], n = 0;
        for (int k = 0; k < 128; ++k)
            if (chordZone_[k]) notes[n++] = k;
        const Chord chord = detectChord(notes, n, oneFinger_);
        if (chord.valid()) seq_.setChord(chord);
        if (seq_.syncArmed() && !seq_.playing()) seq_.start(false, now_);
        if (lowerOn_) mask |= 2;
    } else {
        mask |= 1;
        if (layer_) mask |= 2;
    }
    int sound = note + transpose_;
    if (sound < 0) sound = 0;
    if (sound > 127) sound = 127;
    heldNote_[note] = static_cast<int8_t>(sound);
    heldMask_[note] = static_cast<uint8_t>(mask | 4);  // bit 2: the key is down (even when it sounds nothing)
    if (mask & 1) engineOn(0, sound, vel);
    if (mask & 2) engineOn(1, sound, vel);
}

void Arranger::keyOff(int note) {
    if (note < 0 || note > 127) return;
    chordZone_[note] = false;
    const uint8_t mask = heldMask_[note];
    if (mask & 1) engineOff(0, heldNote_[note]);
    if (mask & 2) engineOff(1, heldNote_[note]);
    heldMask_[note] = 0;
}

// ───────────────────────── routing to the instruments ─────────────────────────

void Arranger::engineOn(int ch, int note, int vel) {
    if (note < 0 || note > 127) return;
    const Program& p = programs_[ch];
    switch (p.kind) {
        case Program::Sf2:
            if (p.preset >= 0 && banks_[p.index]) { sf2_.noteOn(ch, note, vel, banks_[p.index], p.preset); return; }
            break;
        case Program::Org:
            if (org_ && p.index >= 0 && p.index < static_cast<int>(org_->instruments.size())) {
                orgSampler_.noteOn(ch, note, vel, p.index);
                return;
            }
            break;
        case Program::Kit: builtin_.noteOn(ch, note, vel, -1); return;
        default: builtin_.noteOn(ch, note, vel, p.index); return;
    }
    // A bank or set that is gone: the built-in lead / kit.
    builtin_.noteOn(ch, note, vel, (ch == 9 || ch == 10) ? -1 : 0);
}

void Arranger::engineOff(int ch, int note) {
    builtin_.noteOff(ch, note);
    sf2_.noteOff(ch, note);
    orgSampler_.noteOff(ch, note);
}

void Arranger::sustain(int ch, bool down) {
    chan_[ch].sustain = down;
    if (down) return;
    builtin_.sustainUp(ch);
    sf2_.sustainUp(ch);
    orgSampler_.sustainUp(ch);
}

// ch = -1: every channel; -2: the style channels (everything but the player's 0–1).
void Arranger::allOff(int ch, bool fast) {
    if (ch == -2) {
        for (int c = 2; c < 16; ++c) allOff(c, fast);
        return;
    }
    builtin_.allOff(ch, fast);
    sf2_.allOff(ch, fast);
    orgSampler_.allOff(ch, fast);
}

void Arranger::scheduleOff(int ch, int note, int64_t at) {
    if (nOffs_ >= kMaxOffs) {  // full: end the oldest now
        engineOff(offs_[0].ch, offs_[0].note);
        offs_[0] = offs_[--nOffs_];
    }
    offs_[nOffs_++] = Off{at, static_cast<uint8_t>(ch), static_cast<uint8_t>(note)};
}

bool Arranger::flushOff(int ch, int note) {
    for (int i = 0; i < nOffs_; ++i)
        if (offs_[i].ch == ch && offs_[i].note == note) {
            engineOff(ch, note);
            offs_[i] = offs_[--nOffs_];
            return true;
        }
    return false;
}

void Arranger::seqNote(int channel, int note, int velocity, double at, double durFrames) {
    if (note < 0 || note > 127 || channel < 0 || channel > 15) return;
    flushOff(channel, note);  // the same note again: end the previous one first, its off must not cut this one
    engineOn(channel, note, velocity);
    int64_t off = static_cast<int64_t>(std::llround(at + durFrames));
    if (off <= now_) off = now_ + 1;
    scheduleOff(channel, note, off);
}

// ───────────────────────── render ─────────────────────────

void Arranger::render(float* stereo, int frames) {
    int done = 0;
    while (done < frames) {
        const int n = frames - done < kBlock ? frames - done : kBlock;
        const int64_t end = now_ + n;
        for (int i = 0; i < nOffs_;) {
            if (offs_[i].at < end) {
                engineOff(offs_[i].ch, offs_[i].note);
                offs_[i] = offs_[--nOffs_];
            } else {
                ++i;
            }
        }
        seq_.process(now_, n, *this);
        for (int f = 0; f < n; ++f) mixL_[f] = mixR_[f] = mixRev_[f] = 0.f;
        const MixBus mix{mixL_, mixR_, mixRev_, n};
        builtin_.render(mix);
        sf2_.render(mix);
        orgSampler_.render(mix);
        if (reverbLevel_ > 0.f) reverb_.process(mixRev_, mixL_, mixR_, n, reverbLevel_);
        const float master = bus_[3];
        float* out = stereo + 2 * done;
        for (int f = 0; f < n; ++f) {
            out[2 * f] = softClip(mixL_[f] * master);
            out[2 * f + 1] = softClip(mixR_[f] * master);
        }
        now_ = end;
        done += n;
    }
    publish();
}

void Arranger::publish() {
    uint32_t bits[4] = {0, 0, 0, 0};
    for (int n = 0; n < 128; ++n)
        if (heldMask_[n]) bits[n >> 5] |= 1u << (n & 31);
    for (int i = 0; i < 4; ++i) held[i].store(bits[i], std::memory_order_relaxed);
    const Chord& c = seq_.chord();
    chordState.store(c.valid() ? (c.root + 1) | (static_cast<int>(c.type) << 8) : 0, std::memory_order_relaxed);
    const int ts = (seq_.playing() ? 1 : 0) | (seq_.part() << 1) | (seq_.pending() << 4) | ((seq_.pendingVariation() + 1) << 6) |
                   (seq_.variation() << 8) | (seq_.syncArmed() ? 1 << 9 : 0);
    transportState.store(ts, std::memory_order_relaxed);
    beats.store(static_cast<float>(seq_.beatsAt(now_)), std::memory_order_relaxed);
    tempoState.store(seq_.tempo(), std::memory_order_relaxed);
    voicesState.store(builtin_.activeVoices() + sf2_.activeVoices() + orgSampler_.activeVoices(), std::memory_order_relaxed);
}

}  // namespace mm
