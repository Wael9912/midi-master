// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "styledsl.h"

#include <algorithm>
#include <cstdlib>

namespace mm {
namespace {

// GM kit numbers, as DRUM_NOTES in styledsl.js.
int drumNote(char c) {
    switch (c) {
        case 'D': return 63;  // daloka / bangaz dum
        case 'T': return 60;  // tak
        case 'M': return 64;  // mambo
        case 'K': return 36;
        case 'A': return 38;
        case 'L': return 41;
        case 'H': return 48;
        case 'C': return 39;
        case 'J': return 70;
        case 'W': return 76;
        case 'R': return 54;
        case 'S': return 42;
        case 'O': return 46;
        case 'B': return 56;
        case 'Z': return 49;
        case 'N': return 45;
        default: return -1;
    }
}

int velocityFor(char c) {
    if (c == 'X') return 118;
    if (c == 'x') return 96;
    if (c == 'o') return 64;
    return 0;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::vector<std::string_view> splitWs(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        const size_t start = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

// parseInt(): leading digits, else 0 → the JS loop then stops (s += NaN); we treat a bad length as 1 step.
int parseLen(std::string_view s) {
    int v = 0;
    size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); ++i; }
    return i == 0 ? -1 : v;
}

bool drumEvents(uint8_t track, const json::Value& grid, int totalSteps, double stepBeats, std::vector<Event>& out,
                std::string* error) {
    for (const auto& kv : grid.object) {
        if (kv.first.size() != 1 || drumNote(kv.first[0]) < 0) return fail(error, "unknown drum \"" + kv.first + "\"");
        const int note = drumNote(kv.first[0]);
        std::string pat;
        for (char c : kv.second.str())
            if (!isSpace(c)) pat += c;
        if (pat.empty()) continue;
        for (int s = 0; s < totalSteps; ++s) {
            const int v = velocityFor(pat[static_cast<size_t>(s) % pat.size()]);
            if (v) out.push_back(Event{s * stepBeats, stepBeats * 0.5, track, static_cast<int16_t>(note), static_cast<uint8_t>(v)});
        }
    }
    return true;
}

bool pitchedEvents(uint8_t track, std::string_view grid, int totalSteps, double stepBeats, double gate,
                   std::vector<Event>& out, std::string* error) {
    const auto tokens = splitWs(grid);
    if (tokens.empty()) return true;
    int s = 0;
    size_t lastFrom = 0, lastCount = 0;  // the previous token's events in `out` (for '-' ties)
    while (s < totalSteps) {
        const std::string_view tok = tokens[static_cast<size_t>(s) % tokens.size()];
        if (tok == "-") {
            for (size_t k = 0; k < lastCount; ++k) out[lastFrom + k].d += stepBeats;
            s += 1;
            continue;
        }
        if (tok == ".") { lastCount = 0; s += 1; continue; }
        const size_t colon = tok.find(':');
        std::string_view notesPart = tok.substr(0, colon);
        int len = 1;
        if (colon != std::string_view::npos) {
            const size_t colon2 = tok.find(':', colon + 1);
            const std::string_view lenPart = tok.substr(colon + 1, colon2 == std::string_view::npos ? std::string_view::npos : colon2 - colon - 1);
            if (!lenPart.empty()) {
                len = parseLen(lenPart);
                if (len < 0) return true;  // JS: s += NaN ends the loop
            }
        }
        int vel = 88;
        if (!notesPart.empty() && notesPart.back() == '!') { vel = 112; notesPart.remove_suffix(1); }
        else if (!notesPart.empty() && notesPart.back() == '?') { vel = 60; notesPart.remove_suffix(1); }
        lastFrom = out.size();
        lastCount = 0;
        size_t p = 0;
        for (;;) {
            const size_t plus = notesPart.find('+', p);
            const std::string_view nt = notesPart.substr(p, plus == std::string_view::npos ? std::string_view::npos : plus - p);
            const int n = noteNumber(nt);
            if (n < 0) return fail(error, "bad note token \"" + std::string(nt) + "\"");
            out.push_back(Event{s * stepBeats, stepBeats * len * gate, track, static_cast<int16_t>(n), static_cast<uint8_t>(vel)});
            ++lastCount;
            if (plus == std::string_view::npos) break;
            p = plus + 1;
        }
        s += len;
    }
    return true;
}

}  // namespace

int noteNumber(std::string_view t) {
    // /^([A-Ga-g])([#b]?)(-?\d)$/
    if (t.size() < 2 || t.size() > 4) return -1;
    static const int kPc[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    char letter = t[0];
    if (letter >= 'a' && letter <= 'g') letter = static_cast<char>(letter - 'a' + 'A');
    if (letter < 'A' || letter > 'G') return -1;
    int n = kPc[letter - 'A'];
    size_t i = 1;
    if (i < t.size() && (t[i] == '#' || t[i] == 'b')) { n += t[i] == '#' ? 1 : -1; ++i; }
    bool neg = false;
    if (i < t.size() && t[i] == '-') { neg = true; ++i; }
    if (i + 1 != t.size() || t[i] < '0' || t[i] > '9') return -1;
    const int oct = (neg ? -1 : 1) * (t[i] - '0');
    const int v = n + (oct + 1) * 12;
    return v < 0 || v > 127 ? -1 : v;
}

bool compileStyle(const json::Value& src, Style& st, std::string* error) {
    st = Style{};
    st.id = src["id"].str();
    st.name = src["name"].str();
    st.nameAr = src["name_ar"].truthy() ? src["name_ar"].str() : st.name;
    st.group = src["group"].truthy() ? src["group"].str() : "west";
    st.tempo = src["tempo"].truthy() ? src["tempo"].integer() : 120;
    const json::Value& meter = src["meter"];
    st.meterNum = meter.truthy() ? meter[0].integer(4) : 4;
    st.meterDen = meter.truthy() ? meter[1].integer(4) : 4;
    if (st.meterNum <= 0 || st.meterDen <= 0) return fail(error, st.id + ": bad meter");
    st.steps = src["steps"].truthy() ? src["steps"].integer() : (st.meterDen == 8 ? st.meterNum * 2 : st.meterNum * 4);
    if (st.steps <= 0) return fail(error, st.id + ": bad steps");
    st.beatsPerBar = static_cast<double>(st.meterNum * 4) / st.meterDen;
    const double stepBeats = st.beatsPerBar / st.steps;
    static const char* kVoiceRoles[6] = {"upper", "lower", "bass", "chord", "pad", "phrase"};
    for (int i = 0; i < 6; ++i) st.voices[i] = src["voices"][kVoiceRoles[i]].str();

    const json::Value& tracks = src["tracks"];
    if (!tracks.isObject()) return fail(error, st.id + ": no tracks");
    for (const auto& kv : tracks.object) {
        Track t;
        t.name = kv.first;
        if (!roleFromName(kv.second["role"].str(), t.role)) return fail(error, st.id + "/" + kv.first + ": bad role");
        t.channel = kv.second["channel"].integer(-1);
        if (t.channel < 0 || t.channel > 15) return fail(error, st.id + "/" + kv.first + ": channel must be a number");
        const std::string& tr = kv.second["transpose"].str();
        if (tr == "none") t.transpose = Transpose::None;
        else if (tr == "root") t.transpose = Transpose::Root;
        else if (tr == "chord") t.transpose = Transpose::Chord;
        else return fail(error, st.id + "/" + kv.first + ": bad transpose");
        const json::Value& gate = kv.second["gate"];
        t.gate = gate.isNull() ? 0.9 : gate.num(0.9);  // `?? 0.9`
        st.tracks.push_back(std::move(t));
        if (st.tracks.size() > 255) return fail(error, st.id + ": too many tracks");
    }

    const json::Value& parts = src["parts"];
    for (int p = 0; p < kPartCount; ++p) {
        const json::Value* part = parts.find(partName(p));
        if (!part || !part->isObject()) return fail(error, st.id + ": missing part " + partName(p));
        PartData& pd = st.parts[p];
        pd.bars = (*part)["bars"].truthy() ? (*part)["bars"].integer(1) : 1;
        if (pd.bars <= 0) pd.bars = 1;
        pd.beats = pd.bars * st.beatsPerBar;
        const int totalSteps = pd.bars * st.steps;
        for (const auto& kv : part->object) {
            if (kv.first == "bars") continue;
            const int ti = st.trackIndex(kv.first);
            if (ti < 0) return fail(error, st.id + "/" + partName(p) + ": unknown track " + kv.first);
            const Track& t = st.tracks[static_cast<size_t>(ti)];
            bool ok;
            if (t.role == Role::Drums || t.role == Role::Perc)
                ok = drumEvents(static_cast<uint8_t>(ti), kv.second, totalSteps, stepBeats, pd.events, error);
            else
                ok = pitchedEvents(static_cast<uint8_t>(ti), kv.second.str(), totalSteps, stepBeats, t.gate, pd.events, error);
            if (!ok) {
                if (error) *error = st.id + ": " + *error;
                return false;
            }
        }
        std::stable_sort(pd.events.begin(), pd.events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
    }
    return true;
}

bool compilePack(std::string_view text, std::vector<Style>& out, std::string* error) {
    json::Value root;
    if (!json::parse(text, root, error)) return false;
    std::vector<const json::Value*> list;
    if (root.isArray())
        for (const auto& v : root.array) list.push_back(&v);
    else if (root.isObject())
        list.push_back(&root);
    else
        return fail(error, "a pack is a JSON array of styles");
    std::string errs;
    for (const json::Value* src : list) {
        Style st;
        std::string e;
        if (compileStyle(*src, st, &e)) out.push_back(std::move(st));
        else errs += (errs.empty() ? "" : "; ") + e;
    }
    if (error) *error = errs;
    return !out.empty();
}

const char* partName(int part) {
    static const char* kNames[kPartCount] = {"intro", "mainA", "mainB", "fillA", "fillB", "ending"};
    return part >= 0 && part < kPartCount ? kNames[part] : "";
}

const char* roleName(Role r) {
    static const char* kNames[] = {"drums", "perc", "bass", "chord", "pad", "phrase"};
    return kNames[static_cast<int>(r)];
}

bool roleFromName(const std::string& s, Role& out) {
    for (int i = 0; i < 6; ++i)
        if (s == roleName(static_cast<Role>(i))) { out = static_cast<Role>(i); return true; }
    return false;
}

}  // namespace mm
