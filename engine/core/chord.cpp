// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "chord.h"

#include <cstring>

namespace mm {
namespace {

struct TypeDef {
    const char* id;
    const char* label;
    int8_t map[12];  // written pitch class → interval above the root; -1 = keep the pitch class
};

// Same tables as CHORD_TYPES in chord.js (pitch classes 1, 3, 6, 8 are not in the maps: they keep their value).
const TypeDef kTypes[] = {
    {"maj", "", {0, -1, 2, -1, 4, 5, -1, 7, -1, 9, 10, 11}},
    {"min", "m", {0, -1, 2, -1, 3, 5, -1, 7, -1, 8, 10, 10}},
    {"dom7", "7", {0, -1, 2, -1, 4, 5, -1, 7, -1, 9, 10, 10}},
    {"maj7", "maj7", {0, -1, 2, -1, 4, 5, -1, 7, -1, 9, 11, 11}},
    {"min7", "m7", {0, -1, 2, -1, 3, 5, -1, 7, -1, 8, 10, 10}},
    {"sus4", "sus4", {0, -1, 2, -1, 5, 5, -1, 7, -1, 9, 10, 10}},
    {"dim", "dim", {0, -1, 2, -1, 3, 5, -1, 6, -1, 9, 9, 9}},
    {"aug", "aug", {0, -1, 2, -1, 4, 5, -1, 8, -1, 9, 10, 11}},
};

struct Template {
    ChordType type;
    int n;
    int8_t pcs[4];
};

// Fingered templates in the JS order (most specific first).
const Template kTemplates[] = {
    {ChordType::Dom7, 4, {0, 4, 7, 10}}, {ChordType::Maj7, 4, {0, 4, 7, 11}}, {ChordType::Min7, 4, {0, 3, 7, 10}},
    {ChordType::Dom7, 3, {0, 4, 10}},    {ChordType::Maj7, 3, {0, 4, 11}},    {ChordType::Min7, 3, {0, 3, 10}},
    {ChordType::Maj, 3, {0, 4, 7}},      {ChordType::Min, 3, {0, 3, 7}},      {ChordType::Sus4, 3, {0, 5, 7}},
    {ChordType::Dim, 3, {0, 3, 6}},      {ChordType::Aug, 3, {0, 4, 8}},      {ChordType::Maj, 2, {0, 4}},
    {ChordType::Min, 2, {0, 3}},         {ChordType::Maj, 2, {0, 7}},
};

inline int pc(int n) { return ((n % 12) + 12) % 12; }

void sortAsc(int* a, int n) {
    for (int i = 1; i < n; ++i) {
        const int v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; --j; }
        a[j + 1] = v;
    }
}

}  // namespace

const char* chordTypeId(ChordType t) { return kTypes[static_cast<int>(t)].id; }
const char* chordTypeLabel(ChordType t) { return kTypes[static_cast<int>(t)].label; }

bool chordTypeFromId(const char* id, ChordType& out) {
    for (int i = 0; i < static_cast<int>(ChordType::Count); ++i)
        if (std::strcmp(kTypes[i].id, id) == 0) { out = static_cast<ChordType>(i); return true; }
    return false;
}

Chord detectFingered(const int* notes, int n) {
    Chord none;
    if (!notes || n <= 0) return none;
    if (n > 128) n = 128;
    int sorted[128];
    for (int i = 0; i < n; ++i) sorted[i] = notes[i];
    sortAsc(sorted, n);
    // Unique pitch classes, ascending.
    bool seen[12] = {};
    for (int i = 0; i < n; ++i) seen[pc(sorted[i])] = true;
    int pcs[12], np = 0;
    for (int p = 0; p < 12; ++p)
        if (seen[p]) pcs[np++] = p;
    if (np == 1) return Chord{static_cast<int8_t>(pcs[0]), ChordType::Maj, static_cast<int8_t>(sorted[0])};
    const int bassPc = pc(sorted[0]);
    // The lowest note's pitch class first, then the others in ascending order.
    int roots[12], nr = 0;
    roots[nr++] = bassPc;
    for (int i = 0; i < np; ++i)
        if (pcs[i] != bassPc) roots[nr++] = pcs[i];
    for (const Template& t : kTemplates) {
        if (t.n != np) continue;
        for (int r = 0; r < nr; ++r) {
            int rel[12];
            for (int i = 0; i < np; ++i) rel[i] = (pcs[i] - roots[r] + 12) % 12;
            sortAsc(rel, np);
            bool same = true;
            for (int i = 0; i < np && same; ++i) same = rel[i] == t.pcs[i];
            if (same) return Chord{static_cast<int8_t>(roots[r]), t.type, static_cast<int8_t>(sorted[0])};
        }
    }
    return none;
}

Chord detectOneFinger(const int* notes, int n) {
    Chord none;
    if (!notes || n <= 0) return none;
    if (n > 128) n = 128;
    int top = notes[0];
    for (int i = 1; i < n; ++i)
        if (notes[i] > top) top = notes[i];
    const int8_t root = static_cast<int8_t>(pc(top));
    if (n == 1) return Chord{root, ChordType::Maj, static_cast<int8_t>(top)};
    // The JS sorts descending and looks at every note after the first; with distinct notes that is every other note.
    bool has7 = false, hasMin = false, skippedTop = false;
    for (int i = 0; i < n; ++i) {
        if (notes[i] == top && !skippedTop) { skippedTop = true; continue; }
        const int d = top - notes[i];
        if (d == 1 || d == 2) has7 = true;
        if (d == 3 || d == 4) hasMin = true;
    }
    if (has7 && hasMin) return Chord{root, ChordType::Min7, static_cast<int8_t>(top)};
    if (hasMin) return Chord{root, ChordType::Min, static_cast<int8_t>(top)};
    if (has7) return Chord{root, ChordType::Dom7, static_cast<int8_t>(top)};
    return detectFingered(notes, n);
}

int transposeNote(int note, Transpose rule, const Chord& chord) {
    if (rule == Transpose::None || !chord.valid()) return note;
    int shift = chord.root;
    if (shift > 6) shift -= 12;
    if (rule == Transpose::Root) return note + shift;
    const int p = pc(note);
    const int8_t m = kTypes[static_cast<int>(chord.type)].map[p];
    const int interval = m >= 0 ? m : p;
    return note - p + interval + shift;
}

}  // namespace mm
