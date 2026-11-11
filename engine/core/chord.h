// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// Chord detection and chord-relative transposition, ported from legacy-web/js/chord.js and tested against its output
// (engine/tests/fixtures/chords.json, transpose.json). Real-time safe: no allocation.
#pragma once
#include <cstdint>

namespace mm {

enum class ChordType : uint8_t { Maj, Min, Dom7, Maj7, Min7, Sus4, Dim, Aug, Count };

struct Chord {
    int8_t root = -1;  // pitch class 0..11, -1 = no chord
    ChordType type = ChordType::Maj;
    int8_t bass = -1;  // MIDI note
    bool valid() const { return root >= 0; }
};

// How a style track follows the chord: 'none' (drums), 'root' (shift by the root), 'chord' (re-map, then shift).
enum class Transpose : uint8_t { None, Root, Chord };

const char* chordTypeId(ChordType t);     // "maj", "min", "dom7", … (the JS keys)
const char* chordTypeLabel(ChordType t);  // "", "m", "7", "maj7", …
bool chordTypeFromId(const char* id, ChordType& out);

// `notes` are MIDI notes (any order, no duplicates needed); n ≤ 128.
Chord detectFingered(const int* notes, int n);
Chord detectOneFinger(const int* notes, int n);
inline Chord detectChord(const int* notes, int n, bool oneFinger) {
    return oneFinger ? detectOneFinger(notes, n) : detectFingered(notes, n);
}

// A pattern note written over C major, played over `chord`.
int transposeNote(int note, Transpose rule, const Chord& chord);

}  // namespace mm
