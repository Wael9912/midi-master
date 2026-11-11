// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// The arranger: everything that happens between a key press and the speaker, on the audio thread.
//  • the player's keys (USB keyboard via AMidi, or the screen) → split point, chord zone, layer, transpose
//    (the logic of legacy-web/js/app.js noteOn / noteOff);
//  • the chord detector feeding the style sequencer, which runs in the same callback with no look-ahead;
//  • 16 channels, each playing a program from one of three instruments (built-in, SF2 bank, ORG set);
//  • buses (keys 0–1, style 2–8, drums 9–10), reverb, master gain and the soft clip.
// The UI talks to it only through Command (an SPSC queue in AudioEngine) and reads its state from atomics. Resources
// (banks, sets, styles) arrive as pointers built on a worker thread; replaced ones are handed back through retired()
// so they are freed off the audio thread.
#pragma once
#include <atomic>
#include <cstdint>

#include "builtin.h"
#include "chord.h"
#include "midi_parser.h"
#include "org_sampler.h"
#include "reverb.h"
#include "sequencer.h"
#include "sf2_synth.h"
#include "spsc_queue.h"
#include "style.h"

namespace mm {

struct Program {
    enum Kind : uint8_t { Builtin = 0, Kit = 1, Sf2 = 2, Org = 3 };
    uint8_t kind = Builtin;
    int16_t index = 0;   // built-in preset / ORG instrument / SF2 bank slot
    int16_t bank = 0;    // SF2 bank number (128 = kits)
    int16_t program = 0;
    int16_t preset = -1;  // resolved SF2 preset index (audio thread)
};

struct Command {
    enum Kind : uint8_t {
        KeyOn, KeyOff,    // a = note, b = velocity: the player's keys
        Sustain,          // a = down
        Bend,             // f = semitones
        Panic,            // every note off, held keys forgotten
        SetProgram,       // a = channel, b = kind, c = index, d = (bank << 8) | program
        Preview,          // a = channel, b = note: a short note when nothing else sounds
        AddBank,          // a = slot, p = Sf2Bank*
        RemoveBank,       // a = slot
        SetOrg,           // p = OrgSet* (nullptr removes)
        SetStyle,         // p = Style*
        Start,            // a = intro
        Stop, Fill,       // Fill: a = target variation (-1 = stay)
        Variation,        // a = 0 (A) / 1 (B)
        Ending,
        Sync,             // a = armed
        Tempo,            // a = bpm
        Split, Transpose, ChordMode, LowerOn, Layer,  // a
        MuteMask,         // a = low 32 bits, b = high 32 bits
        Bus,              // a = 0 keys / 1 style / 2 drums / 3 master, f = gain
        ReverbLevel,      // f = 0..1
    };
    uint8_t kind = KeyOn;
    int32_t a = 0, b = 0, c = 0, d = 0;
    float f = 0.f;
    void* p = nullptr;
};

struct Retired {
    enum Kind : uint8_t { Bank, Org, Style } kind;
    void* p;
};

class Arranger final : public SequencerSink {
public:
    static constexpr int kBanks = 16;

    Arranger();
    void prepare(float sampleRate);  // not on the audio thread; keeps programs, styles and settings
    void command(const Command& c);
    void handleMidi(const MidiMessage& m);
    void handleMidiBytes(const uint8_t* b, size_t n) {
        parser_.feed(b, n, [this](const MidiMessage& m) { handleMidi(m); });
    }
    void render(float* stereo, int frames);  // interleaved stereo, overwrites
    bool retired(Retired& out) { return retired_.pop(out); }

    // State for the UI (written by the audio thread).
    std::atomic<uint32_t> held[4];
    std::atomic<int> chordState{0};      // 0 = none, else (root + 1) | type << 8
    std::atomic<int> transportState{0};  // playing | part << 1 | pending << 4 | (pendingVariation + 1) << 6 | variation << 8 | sync << 9
    std::atomic<float> beats{0.f};       // beats since the current part started
    std::atomic<int> tempoState{120};
    std::atomic<int> voicesState{0};

    // SequencerSink
    void seqNote(int channel, int note, int velocity, double at, double durFrames) override;

    // For tests.
    const Sequencer& sequencer() const { return seq_; }
    int64_t now() const { return now_; }
    const Program& program(int ch) const { return programs_[ch]; }

private:
    void keyOn(int note, int vel);
    void keyOff(int note);
    void engineOn(int ch, int note, int vel);
    void engineOff(int ch, int note);
    void sustain(int ch, bool down);
    void allOff(int ch, bool fast);
    void scheduleOff(int ch, int note, int64_t at);
    bool flushOff(int ch, int note);
    void resolveProgram(int ch);
    void retire(Retired::Kind k, void* p);
    void publish();
    static int busOf(int ch) { return ch <= 1 ? 0 : (ch == 9 || ch == 10) ? 2 : 1; }
    void applyGains();

    float sr_ = 48000.f;
    int64_t now_ = 0;
    ChannelState chan_[16];
    Program programs_[16];
    Sf2Bank* banks_[kBanks] = {};
    const OrgSet* org_ = nullptr;
    const Style* style_ = nullptr;
    BuiltinSynth builtin_;
    Sf2Synth sf2_;
    OrgSampler orgSampler_;
    Sequencer seq_;
    Reverb reverb_;
    MidiParser parser_;
    SpscQueue<Retired, 64> retired_;

    // keyboard
    int split_ = 54, transpose_ = 0;
    bool oneFinger_ = false, lowerOn_ = false, layer_ = false;
    uint8_t heldMask_[128] = {};  // bit 0 upper, bit 1 lower
    int8_t heldNote_[128] = {};   // the transposed note that sounds
    bool chordZone_[128] = {};
    bool kbSustain_ = false;
    float bendSemis_ = 0.f;

    // pending note-offs of sequencer / preview notes
    struct Off {
        int64_t at;
        uint8_t ch, note;
    };
    static constexpr int kMaxOffs = 512;
    Off offs_[kMaxOffs];
    int nOffs_ = 0;

    float bus_[4] = {0.8f, 0.8f, 0.8f, 0.9f};
    float reverbLevel_ = 0.5f;
    float mixL_[kBlock], mixR_[kBlock], mixRev_[kBlock];
};

}  // namespace mm
