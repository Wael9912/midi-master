// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// ORG 24 / Sofeh Music Studio ".Set" reader, ported from legacy-web/js/orgset.js (the reverse-engineered layout is
// documented there and in docs/CONTENT.md). Tested against engine/tests/fixtures/orgset.json and, locally, against the
// owner's own set. The parsed set also carries the instruments' PCM for the sampler (OrgSampler).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "style.h"

namespace mm {

struct OrgSample {
    int root = 60;
    std::string name;
    int rate = 44100, channels = 1, frames = 0, volume = 100, cents = 0;
    bool loop = false;
    int loopStart = 0, loopEnd = 0;
    size_t dataOffset = 0;
    // Filled when the set is loaded for playing (parseOrgSet with loadPcm): interleaved 16-bit PCM and the first frame
    // above −50 dB (leading silence is skipped at note-on).
    std::vector<int16_t> pcm;
    int lead = 0;
};

struct OrgInstrument {
    int index = 0;
    std::string name;
    bool isKit = false;
    std::vector<OrgSample> samples;
};

struct OrgRow {
    int note, vel, t, d;
};
struct OrgPattern {
    uint32_t partId = 0;
    std::string track;
    int type = 0, instrumentHint = 0;
    std::vector<OrgRow> rows;
};
struct OrgPart {
    uint32_t id = 0;
    std::string name;
    int type = 0, lengthTicks = 0;
    uint32_t link = 0;
};
struct OrgTrackHint {
    int hint = 0, hint2 = 0;
};

struct OrgSet {
    std::string version;
    std::vector<OrgInstrument> instruments;
    bool hasRhythm = false;
    std::vector<std::pair<std::string, OrgTrackHint>> tracks;  // rhythm track records, file order
    std::vector<OrgPattern> patterns;
    std::vector<OrgPart> parts;
    int tagCount = 0;
};

bool isOrgSet(const uint8_t* b, size_t n);
bool parseOrgSet(const uint8_t* b, size_t n, OrgSet& out, bool loadPcm, std::string* error = nullptr);
// Per rhythm track, the set instrument that best covers its notes (track name → instrument index), first-seen order.
std::vector<std::pair<std::string, int>> assignInstruments(const OrgSet& set);
// The set's rhythm as styles: Variation 1/2 (+ Intro, Fill, Ending) and, when present, Variation 3/4 as " (3/4)".
std::vector<Style> orgStyles(const OrgSet& set, const std::string& id, const std::string& name, int tempo = 100,
                             int meterNum = 6, int meterDen = 8);

}  // namespace mm
