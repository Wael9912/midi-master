// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "sty.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace mm {
namespace {

struct Reader {
    const uint8_t* b;
    size_t n;
    int at(size_t i) const { return i < n ? b[i] : -1; }
    uint32_t u32(size_t i) const {
        if (i + 4 > n) return 0;
        return (static_cast<uint32_t>(b[i]) << 24) | (static_cast<uint32_t>(b[i + 1]) << 16) | (static_cast<uint32_t>(b[i + 2]) << 8) | b[i + 3];
    }
    int u16(size_t i) const { return i + 2 > n ? 0 : (b[i] << 8) | b[i + 1]; }
    std::string ascii(size_t i, size_t len) const {
        std::string s;
        for (size_t k = 0; k < len && i + k < n; ++k) s += static_cast<char>(b[i + k]);
        return s;
    }
};

std::string trim(const std::string& s) {
    size_t a = 0, z = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    while (a < z && ws(s[a])) ++a;
    while (z > a && ws(s[z - 1])) --z;
    return s.substr(a, z - a);
}

bool readVar(const Reader& r, size_t& i, uint32_t& v) {
    v = 0;
    for (int guard = 0; guard < 5; ++guard) {
        const int c = r.at(i++);
        if (c < 0) return false;
        v = (v << 7) | static_cast<uint32_t>(c & 0x7f);
        if (!(c & 0x80)) return true;
    }
    return false;
}

struct Note {
    double t;  // ticks
    int ch, n, v;
    double d;  // ticks
};
struct Marker {
    double t;
    std::string name;
};
struct Smf {
    int division = 480;
    int tempo = 120;
    int meterNum = 4, meterDen = 4;
    std::vector<Marker> markers;
    int program[16];
    std::vector<Note> notes;
    size_t midiEnd = 0;
    bool sff2 = false;
};

bool fail(std::string* e, const char* m) {
    if (e) *e = m;
    return false;
}

bool parseSmf(const Reader& r, Smf& smf, std::string* err) {
    for (int& p : smf.program) p = -1;
    smf.division = r.u16(12);
    if (smf.division <= 0) return fail(err, "not a Yamaha style (bad division)");
    if (r.ascii(14, 4) != "MTrk") return fail(err, "not a Yamaha style (no MTrk)");
    const size_t end = 22 + static_cast<size_t>(r.u32(18));
    size_t i = 22;
    double t = 0;
    int status = -1;
    int bank[16] = {};
    std::map<int, size_t> open;  // ch * 128 + note → index in notes
    auto closeNote = [&](int ch, int n, double at) {
        auto it = open.find(ch * 128 + n);
        if (it == open.end()) return;
        Note& rec = smf.notes[it->second];
        rec.d = std::max(1.0, at - rec.t);
        open.erase(it);
    };
    while (i < end) {
        uint32_t d = 0;
        if (!readVar(r, i, d)) return fail(err, "truncated style file");
        t += d;
        const int s = r.at(i);
        if (s < 0) return fail(err, "truncated style file");
        if (s == 0xff) {
            const int type = r.at(i + 1);
            size_t j = i + 2;
            uint32_t len = 0;
            if (!readVar(r, j, len)) return fail(err, "truncated style file");
            i = j;
            if (type == 0x06) smf.markers.push_back({t, trim(r.ascii(i, len))});
            else if (type == 0x51 && len >= 3) {
                const int us = (r.at(i) << 16) | (r.at(i + 1) << 8) | r.at(i + 2);
                if (us > 0) smf.tempo = static_cast<int>(std::floor(60e6 / us + 0.5));
            } else if (type == 0x58 && len >= 2) {
                smf.meterNum = r.at(i);
                smf.meterDen = 1 << std::min(r.at(i + 1), 6);
            }
            i += len;
            if (type == 0x2f) break;
            continue;
        }
        if (s == 0xf0 || s == 0xf7) {
            size_t j = i + 1;
            uint32_t len = 0;
            if (!readVar(r, j, len)) return fail(err, "truncated style file");
            i = j + len;
            continue;
        }
        if (s & 0x80) { status = s; ++i; }
        if (status < 0) return fail(err, "bad MIDI status");
        const int kind = status & 0xf0, ch = status & 0x0f;
        const int d1 = r.at(i), d2 = r.at(i + 1);
        if (kind == 0x90 && d2 > 0) {
            closeNote(ch, d1, t);
            smf.notes.push_back({t, ch, d1, d2, smf.division / 4.0});
            open[ch * 128 + d1] = smf.notes.size() - 1;
            i += 2;
        } else if (kind == 0x80 || kind == 0x90) {
            closeNote(ch, d1, t);
            i += 2;
        } else if (kind == 0xb0) {
            if (d1 == 0) bank[ch] |= d2 << 7;
            else if (d1 == 32) bank[ch] |= d2;
            i += 2;
        } else if (kind == 0xc0) {
            smf.program[ch] = d1;
            i += 1;
        } else if (kind == 0xa0 || kind == 0xe0) i += 2;
        else if (kind == 0xd0) i += 1;
        else return fail(err, "bad MIDI status");
        if (d1 < 0) return fail(err, "truncated style file");
    }
    for (auto& kv : open) {
        Note& rec = smf.notes[kv.second];
        rec.d = std::max(1.0, t - rec.t);
    }
    smf.midiEnd = end;
    smf.sff2 = std::any_of(smf.markers.begin(), smf.markers.end(), [](const Marker& m) { return m.name == "SFF2"; });
    return true;
}

struct Ctab {
    int src = 0, dest = -1, root = 0, type = 2, ntr = 0, ntt = 0;
};

// The CSEG covering "Main A" (else the first): its Ctab records keyed by source channel, in ascending order.
bool parseCasm(const Reader& r, size_t midiEnd, bool sff2, std::map<int, Ctab>& ctabs) {
    size_t p = midiEnd;
    while (p + 8 <= r.n && r.ascii(p, 4) != "CASM") p += 8 + static_cast<size_t>(r.u32(p + 4));
    if (p + 8 > r.n) return false;
    const size_t casmEnd = p + 8 + static_cast<size_t>(r.u32(p + 4));
    p += 8;
    bool haveFirst = false;
    std::map<int, Ctab> first;
    while (p + 8 <= casmEnd && p + 8 <= r.n && r.ascii(p, 4) == "CSEG") {
        const size_t segEnd = p + 8 + static_cast<size_t>(r.u32(p + 4));
        size_t q = p + 8;
        std::vector<std::string> parts;
        std::map<int, Ctab> seg;
        while (q + 8 <= segEnd && q + 8 <= r.n) {
            const std::string id = r.ascii(q, 4);
            const size_t len = r.u32(q + 4);
            const size_t d = q + 8;
            auto byte = [&](size_t k) { return k < len ? r.at(d + k) : -1; };
            if (id == "Sdec") {
                const std::string s = r.ascii(d, len);
                size_t a = 0;
                for (;;) {
                    const size_t c = s.find(',', a);
                    parts.push_back(trim(s.substr(a, c == std::string::npos ? std::string::npos : c - a)));
                    if (c == std::string::npos) break;
                    a = c + 1;
                }
            } else if (id == "Ctab" || id == "Ctb2") {
                Ctab rec;
                rec.src = byte(0) < 0 ? 0 : byte(0);
                rec.dest = byte(9);
                rec.root = byte(18) < 0 ? 0 : byte(18);
                rec.type = byte(19);
                if (id == "Ctb2" || sff2) {
                    rec.ntr = byte(26);
                    rec.ntt = byte(27) < 0 ? 0 : (byte(27) & 0x7f);
                } else {
                    rec.ntr = byte(20);
                    rec.ntt = byte(21);  // -1 when missing: like JS undefined, it is not 0 (bypass)
                }
                seg[rec.src] = rec;
            }
            q += 8 + len;
        }
        if (!haveFirst) { first = seg; haveFirst = true; }
        if (std::find(parts.begin(), parts.end(), "Main A") != parts.end()) {
            ctabs = seg;
            return true;
        }
        p = segEnd;
    }
    if (!haveFirst) return false;
    ctabs = first;
    return true;
}

struct DestTrack {
    const char* name;
    Role role;
    int channel;
};

const DestTrack* destTrack(int dest) {
    static const DestTrack k[8] = {
        {"perc", Role::Perc, 10},  {"drums", Role::Drums, 9},  {"bass", Role::Bass, 2},     {"chord", Role::Chord, 3},
        {"chord2", Role::Chord, 6}, {"pad", Role::Pad, 4},      {"phrase", Role::Phrase, 5}, {"phrase2", Role::Phrase, 7},
    };
    return dest >= 8 && dest <= 15 ? &k[dest - 8] : nullptr;
}

struct Section {
    double start, end;
};

struct PartMap {
    std::vector<const char*> names[kPartCount];
};

}  // namespace

const char* sniffFormat(const uint8_t* b, size_t n) {
    auto starts = [&](const char* s) {
        const size_t l = std::strlen(s);
        return n >= l && std::memcmp(b, s, l) == 0;
    };
    if (starts("MThd")) return "yamaha";
    if (starts("Music Studio")) return "org24";
    if (starts("OC31") || starts("KORG") || starts("STY1")) return "korg";
    return "unknown";
}

bool convertSty(const uint8_t* bytes, size_t n, const std::string& id, const std::string& name, std::vector<Style>& out,
                std::string* err) {
    const std::string kind = sniffFormat(bytes, n);
    if (kind != "yamaha") return fail(err, kind == "org24" ? "org24" : kind == "korg" ? "korg" : "notYamaha");
    const Reader r{bytes, n};
    Smf smf;
    if (!parseSmf(r, smf, err)) return false;
    std::map<int, Ctab> casm;
    parseCasm(r, smf.midiEnd, smf.sff2, casm);

    // Sections: markers other than SFF1 / SFF2 / SInt, in first-seen order (a repeated name keeps its first place).
    std::vector<std::pair<std::string, Section>> sections;
    std::vector<const Marker*> ms;
    for (const auto& m : smf.markers)
        if (m.name != "SFF1" && m.name != "SFF2" && m.name != "SInt") ms.push_back(&m);
    double lastTick = 0;
    for (const auto& nt : smf.notes) lastTick = std::max(lastTick, nt.t + nt.d);
    for (size_t k = 0; k < ms.size(); ++k) {
        const Section sec{ms[k]->t, k + 1 < ms.size() ? ms[k + 1]->t : std::max(lastTick, ms[k]->t + smf.division)};
        auto it = std::find_if(sections.begin(), sections.end(), [&](const auto& s) { return s.first == ms[k]->name; });
        if (it != sections.end()) it->second = sec;
        else sections.emplace_back(ms[k]->name, sec);
    }
    auto findSection = [&](const char* nm) -> const Section* {
        for (const auto& s : sections)
            if (s.first == nm) return &s.second;
        return nullptr;
    };

    const double beatsPerBar = static_cast<double>(smf.meterNum * 4) / smf.meterDen;
    const double ticksPerBeat = smf.division;

    // Channel plan: source channel → Ctab (default: channel = destination), ascending source channels.
    std::map<int, Ctab> plan;
    for (int ch = 8; ch <= 15; ++ch) {
        Ctab c;
        c.src = ch;
        c.dest = ch;
        c.root = 0;
        c.type = 2;
        c.ntr = ch <= 9 ? 1 : 0;
        c.ntt = ch <= 9 ? 0 : 1;
        plan[ch] = c;
    }
    for (const auto& kv : casm) plan[kv.second.src] = kv.second;
    std::vector<Track> tracks;  // first-insertion order, latest value (JS object semantics)
    int gm[16];
    for (int& g : gm) g = -1;
    for (const auto& kv : plan) {
        const Ctab& rec = kv.second;
        const DestTrack* dt = destTrack(rec.dest);
        if (!dt) continue;
        const bool isDrum = dt->role == Role::Drums || dt->role == Role::Perc;
        Track t;
        t.name = dt->name;
        t.role = dt->role;
        t.channel = dt->channel;
        t.transpose = isDrum || rec.ntt == 0 ? Transpose::None : Transpose::Chord;
        t.gate = 1;
        t.src = kv.first;
        t.shift = isDrum ? 0 : (rec.root > 6 ? 12 - rec.root : -rec.root);
        auto it = std::find_if(tracks.begin(), tracks.end(), [&](const Track& x) { return x.name == t.name; });
        if (it != tracks.end()) *it = t;
        else tracks.push_back(t);
        if (kv.first >= 0 && kv.first < 16 && smf.program[kv.first] >= 0 && !isDrum) gm[dt->channel] = smf.program[kv.first];
    }
    int bySrc[16];
    for (int& b : bySrc) b = -1;
    for (size_t k = 0; k < tracks.size(); ++k)
        if (tracks[k].src >= 0 && tracks[k].src < 16) bySrc[tracks[k].src] = static_cast<int>(k);

    PartMap ab, cd;
    ab.names[kIntro] = {"Intro A", "Intro B", "Intro C"};
    ab.names[kMainA] = {"Main A"};
    ab.names[kMainB] = {"Main B", "Main A"};
    ab.names[kFillA] = {"Fill In AA", "Fill In AB"};
    ab.names[kFillB] = {"Fill In BB", "Fill In BA", "Fill In AA"};
    ab.names[kEnding] = {"Ending A", "Ending B", "Ending C"};
    cd.names[kIntro] = {"Intro B", "Intro C", "Intro A"};
    cd.names[kMainA] = {"Main C"};
    cd.names[kMainB] = {"Main D", "Main C"};
    cd.names[kFillA] = {"Fill In CC"};
    cd.names[kFillB] = {"Fill In DD", "Fill In CC"};
    cd.names[kEnding] = {"Ending B", "Ending C", "Ending A"};

    auto build = [&](const PartMap& map, const std::string& suffix, Style& st) -> bool {
        if (!findSection(map.names[kMainA][0])) return false;
        st = Style{};
        st.id = id + suffix;
        st.name = name + (suffix.empty() ? "" : " (C/D)");
        st.nameAr = st.name;
        st.group = "yamaha";
        st.tempo = smf.tempo;
        st.meterNum = smf.meterNum;
        st.meterDen = smf.meterDen;
        st.steps = smf.meterDen == 8 ? smf.meterNum * 2 : smf.meterNum * 4;
        st.beatsPerBar = beatsPerBar;
        for (int c = 0; c < 16; ++c) st.gmPrograms[c] = gm[c];
        static const int kOrder[kPartCount] = {kMainA, kMainB, kIntro, kFillA, kFillB, kEnding};
        for (int part : kOrder) {
            const Section* sec = nullptr;
            for (const char* c : map.names[part])
                if ((sec = findSection(c))) break;
            PartData& pd = st.parts[part];
            pd.events.clear();
            if (sec) {
                pd.bars = std::max(1, static_cast<int>(std::floor((sec->end - sec->start) / (ticksPerBeat * beatsPerBar) + 0.5)));
                for (const auto& nt : smf.notes) {
                    if (nt.t < sec->start || nt.t >= sec->end) continue;
                    if (nt.ch < 0 || nt.ch > 15 || bySrc[nt.ch] < 0) continue;
                    const int ti = bySrc[nt.ch];
                    pd.events.push_back(Event{(nt.t - sec->start) / ticksPerBeat, nt.d / ticksPerBeat, static_cast<uint8_t>(ti),
                                              static_cast<int16_t>(nt.n + tracks[static_cast<size_t>(ti)].shift), static_cast<uint8_t>(nt.v)});
                }
            } else {
                // Missing part: one bar of main A.
                pd.bars = 1;
                for (const Event& e : st.parts[kMainA].events)
                    if (e.t < beatsPerBar) pd.events.push_back(e);
            }
            std::stable_sort(pd.events.begin(), pd.events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
            pd.beats = pd.bars * beatsPerBar;
        }
        // Keep the tracks that have notes, re-index the events.
        std::vector<int> remap(tracks.size(), -1);
        for (size_t k = 0; k < tracks.size(); ++k) {
            bool used = false;
            for (const auto& pd : st.parts)
                for (const Event& e : pd.events)
                    if (e.track == k) { used = true; break; }
            if (used) {
                remap[k] = static_cast<int>(st.tracks.size());
                st.tracks.push_back(tracks[k]);
            }
        }
        for (auto& pd : st.parts)
            for (Event& e : pd.events) e.track = static_cast<uint8_t>(remap[e.track]);
        return true;
    };
    Style a, c;
    if (build(ab, "", a)) out.push_back(std::move(a));
    if (build(cd, "-cd", c)) out.push_back(std::move(c));
    if (out.empty()) return fail(err, "no Main A section");
    return true;
}

}  // namespace mm
