// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// A compiled style: the shape legacy-web/js/styledsl.js produces ({ bars, beats, events: [{track, t, n, v, d}] } per
// part), shared by the built-in pack, Yamaha .sty files and ORG 24 sets. Built off the audio thread, immutable after.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "chord.h"

namespace mm {

enum class Role : uint8_t { Drums, Perc, Bass, Chord, Pad, Phrase };

enum Part : int { kIntro, kMainA, kMainB, kFillA, kFillB, kEnding, kPartCount };

const char* partName(int part);  // "intro", "mainA", …
const char* roleName(Role r);    // "drums", "perc", …
bool roleFromName(const std::string& s, Role& out);

struct Track {
    std::string name;
    Role role = Role::Drums;
    int channel = 9;
    Transpose transpose = Transpose::None;
    double gate = 0.9;
    int orgInstrument = -1;  // ORG sets: the set instrument that plays this track
    int src = -1;            // Yamaha: source channel
    int shift = 0;           // Yamaha: semitones that bring the recorded chord root back to C
};

struct Event {
    double t = 0;  // beats from the part start
    double d = 0;  // beats
    uint8_t track = 0;
    int16_t n = 0;  // may leave 0..127 after a Yamaha shift; the engine drops notes out of range
    uint8_t v = 0;
};

struct PartData {
    int bars = 1;
    double beats = 4;
    std::vector<Event> events;  // sorted by t (stable)
};

struct Style {
    std::string id, name, nameAr, group;  // group: "sudan" | "west" | "yamaha" | "org"
    int tempo = 120;
    int meterNum = 4, meterDen = 4;
    int steps = 16;
    double beatsPerBar = 4;
    std::vector<Track> tracks;
    PartData parts[kPartCount];
    // Default sounds of the built-in styles (synth preset names by role: upper, lower, bass, chord, pad, phrase).
    std::string voices[6];
    int gmPrograms[16];  // Yamaha: GM program per arranger channel, -1 = none
    Style() { for (int& p : gmPrograms) p = -1; }
    int trackIndex(const std::string& name) const {
        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].name == name) return static_cast<int>(i);
        return -1;
    }
};

}  // namespace mm
