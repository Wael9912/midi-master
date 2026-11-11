// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "orgset.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace mm {
namespace {

constexpr int kTicksPerBar = 1000;

struct B {
    const uint8_t* b;
    size_t n;
    uint32_t le32(size_t i) const {
        if (i + 4 > n) return 0;
        return static_cast<uint32_t>(b[i]) | (static_cast<uint32_t>(b[i + 1]) << 8) | (static_cast<uint32_t>(b[i + 2]) << 16) |
               (static_cast<uint32_t>(b[i + 3]) << 24);
    }
    int32_t lei32(size_t i) const { return static_cast<int32_t>(le32(i)); }
    int le16(size_t i) const { return i + 2 > n ? 0 : b[i] | (b[i + 1] << 8); }
    int at(size_t i) const { return i < n ? b[i] : 0; }
    std::string txt(size_t i, size_t len) const {
        std::string s;
        for (size_t k = 0; k < len && i + k < n; ++k) {
            const uint8_t c = b[i + k];
            if (!c) break;
            s += static_cast<char>(c);
        }
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

struct Tag {
    size_t off;
    std::string name;
    size_t len, body;
};

std::vector<Tag> scanTags(const B& b, const std::string& version) {
    std::vector<Tag> tags;
    const std::string v = "\r" + version + "\r";
    if (b.n <= v.size()) return tags;
    const size_t n = b.n - v.size();
    const uint8_t* p = b.b;
    size_t i = 0;
    while (i < n) {
        const void* hit = std::memchr(p + i, 13, n - i);
        if (!hit) break;
        i = static_cast<size_t>(static_cast<const uint8_t*>(hit) - p);
        if (std::memcmp(p + i, v.data(), v.size()) == 0) {
            for (size_t k = 0; k <= 40; ++k) {
                if (i < k + 4) break;
                const size_t s = i - k;
                const size_t want = k + v.size();
                if (b.le32(s - 4) == want) {
                    bool ok = true;
                    for (size_t q = s; q < i; ++q)
                        if (p[q] < 32 || p[q] > 126) { ok = false; break; }
                    if (ok) tags.push_back({s - 4, b.txt(s, k), want, s + want});
                    break;
                }
            }
        }
        ++i;
    }
    return tags;
}

bool sampleAt(const B& b, size_t o, OrgSample& s) {
    if (o + 97 > b.n || b.b[o + 1] != 0x20) return false;
    const int rate = b.le16(o + 33), bits = b.at(o + 35), channels = b.at(o + 36);
    const uint32_t rate2 = b.le32(o + 37);
    if (bits != 16 || (channels != 1 && channels != 2) || static_cast<uint32_t>(rate) != rate2 || rate < 6000 || rate > 96000) return false;
    const int32_t frames = b.lei32(o + 45);
    if (frames <= 0 || o + 97 + static_cast<size_t>(frames) * channels * 2 > b.n) return false;
    s = OrgSample{};
    s.root = b.b[o];
    s.name = trim(b.txt(o + 2, 31));
    s.rate = rate;
    s.channels = channels;
    s.frames = frames;
    s.volume = b.lei32(o + 53);
    s.cents = b.lei32(o + 65);
    s.loop = b.lei32(o + 69) == 2;
    s.loopStart = b.lei32(o + 73);
    s.loopEnd = b.lei32(o + 77);
    s.dataOffset = o + 97;
    return true;
}

std::vector<OrgSample> samplesBetween(const B& b, size_t from, size_t to) {
    std::vector<OrgSample> out;
    if (to < 97) return out;
    const size_t lim = std::min(from + 4000, to - 97);
    OrgSample s;
    size_t o = 0;
    bool found = false;
    for (size_t q = from; q < lim; ++q)
        if (sampleAt(b, q, s)) { o = q; found = true; break; }
    while (found && o < to - 97) {
        if (!sampleAt(b, o, s)) break;
        const size_t next = s.dataOffset + static_cast<size_t>(s.frames) * s.channels * 2;
        out.push_back(std::move(s));
        o = next;
    }
    return out;
}

std::vector<OrgRow> noteRows(const B& b, size_t o, size_t limit) {
    std::vector<OrgRow> rows;
    while (o + 24 <= limit) {
        const int32_t kind = b.lei32(o), note = b.lei32(o + 8), vel = b.lei32(o + 12), t = b.lei32(o + 16), d = b.lei32(o + 20);
        if (!(kind == 1 || kind == 2) || note < 0 || note > 127 || t < 0 || t > 10000000) break;
        if (kind == 2 && (vel <= 0 || vel > 255 || d <= 0)) break;
        if (kind == 2) rows.push_back({note, vel, t, d});
        o += 24;
    }
    return rows;
}

const char* trackForType(int type) {
    switch (type) {
        case 8: return "bass";
        case 9: return "drums";
        case 10: return "perc";
        case 11: return "acc1";
        case 12: return "acc2";
        case 13: return "acc3";
        case 14: return "acc4";
        case 15: return "acc5";
        default: return nullptr;
    }
}

const char* trackForName(const std::string& n) {
    if (n == "Bass") return "bass";
    if (n == "Acc. 1") return "acc1";
    if (n == "Acc. 2") return "acc2";
    if (n == "Acc. 3") return "acc3";
    if (n == "Acc. 4") return "acc4";
    if (n == "Acc. 5") return "acc5";
    if (n == "Drum") return "drums";
    if (n == "Percussion") return "perc";
    return nullptr;
}

bool icontains(const std::string& hay, const char* needle) {
    const size_t l = std::strlen(needle);
    if (l > hay.size()) return false;
    for (size_t i = 0; i + l <= hay.size(); ++i) {
        size_t k = 0;
        while (k < l && std::tolower(static_cast<unsigned char>(hay[i + k])) == needle[k]) ++k;
        if (k == l) return true;
    }
    return false;
}

void loadPcm(const B& b, OrgSample& s) {
    const size_t count = static_cast<size_t>(s.frames) * s.channels;
    s.pcm.resize(count);
    const uint8_t* p = b.b + s.dataOffset;
    for (size_t i = 0; i < count; ++i) s.pcm[i] = static_cast<int16_t>(static_cast<uint16_t>(p[2 * i] | (p[2 * i + 1] << 8)));
    // Leading silence: first frame above −50 dBFS (|x| > 103 of 32768). Looped samples keep their start.
    s.lead = 0;
    if (!s.loop) {
        const int thr = 103;
        for (int f = 0; f < s.frames; ++f) {
            bool loud = false;
            for (int c = 0; c < s.channels; ++c)
                if (std::abs(s.pcm[static_cast<size_t>(f) * s.channels + c]) > thr) loud = true;
            if (loud) { s.lead = f; break; }
        }
    }
}

struct PartSet {
    const char* suffix;
    const char* label;
    std::vector<const char*> names[kPartCount];
};

}  // namespace

bool isOrgSet(const uint8_t* b, size_t n) { return n >= 12 && std::memcmp(b, "Music Studio", 12) == 0; }

bool parseOrgSet(const uint8_t* bytes, size_t n, OrgSet& out, bool withPcm, std::string* error) {
    out = OrgSet{};
    if (!isOrgSet(bytes, n)) {
        if (error) *error = "org24";
        return false;
    }
    const B b{bytes, n};
    // The version: the first "\r<d>.<d>.<d>\r" in the first 120 bytes (zero bytes read as spaces).
    std::string head;
    for (size_t i = 0; i < std::min<size_t>(120, n); ++i) head += bytes[i] ? static_cast<char>(bytes[i]) : ' ';
    for (size_t i = 0; i < head.size() && out.version.empty(); ++i) {
        if (head[i] != '\r') continue;
        size_t j = i + 1;
        int groups = 0;
        while (groups < 3) {
            const size_t s = j;
            while (j < head.size() && head[j] >= '0' && head[j] <= '9') ++j;
            if (j == s) break;
            ++groups;
            if (groups < 3) {
                if (j < head.size() && head[j] == '.') ++j;
                else break;
            }
        }
        if (groups == 3 && j < head.size() && head[j] == '\r') out.version = head.substr(i + 1, j - i - 1);
    }
    if (out.version.empty()) {
        if (error) *error = "org24";
        return false;
    }
    const std::vector<Tag> tags = scanTags(b, out.version);
    out.tagCount = static_cast<int>(tags.size());
    auto isSetup = [](const std::string& s) {
        return s == "Normal" || s == "Master" || s == "Up 1" || s == "Up 2" || s == "Up 3" || s == "Low";
    };
    int rhythmTag = -1, limitIdx = -1;
    for (size_t k = 0; k < tags.size(); ++k) {
        if (rhythmTag < 0 && tags[k].name == "Rhythm") rhythmTag = static_cast<int>(k);
        if (limitIdx < 0 && (isSetup(tags[k].name) || tags[k].name == "Rhythm")) limitIdx = static_cast<int>(k);
    }
    const size_t endOfInstruments = limitIdx >= 0 ? tags[static_cast<size_t>(limitIdx)].off : n;
    const size_t instTags = limitIdx >= 0 ? static_cast<size_t>(limitIdx) : tags.size();
    for (size_t k = 0; k < instTags; ++k) {
        const Tag& t = tags[k];
        const size_t next = k + 1 < tags.size() ? tags[k + 1].off : endOfInstruments;
        std::vector<OrgSample> samples = samplesBetween(b, t.body, next);
        if (samples.empty()) continue;
        OrgInstrument inst;
        inst.index = static_cast<int>(out.instruments.size());
        inst.name = t.name;
        bool allOneShot = true, anyLow = false;
        std::vector<int> roots;
        for (const auto& s : samples) {
            if (s.loop) allOneShot = false;
            if (s.root < 48) anyLow = true;
            if (std::find(roots.begin(), roots.end(), s.root) == roots.end()) roots.push_back(s.root);
        }
        inst.isKit = samples.size() >= 6 && allOneShot && roots.size() == samples.size() && anyLow;
        inst.samples = std::move(samples);
        if (withPcm)
            for (auto& s : inst.samples) loadPcm(b, s);
        out.instruments.push_back(std::move(inst));
    }
    if (rhythmTag >= 0) {
        out.hasRhythm = true;
        bool seenPatternArea = false;
        for (size_t k = static_cast<size_t>(rhythmTag) + 1; k < tags.size(); ++k) {
            const Tag& t = tags[k];
            const size_t next = k + 1 < tags.size() ? tags[k + 1].off : n;
            const char* tr = trackForName(t.name);
            if (!tr) continue;
            const bool known = std::any_of(out.tracks.begin(), out.tracks.end(), [&](const auto& x) { return x.first == tr; });
            if (!seenPatternArea && !known && next - t.body > 2000) {
                out.tracks.push_back({tr, OrgTrackHint{b.at(t.body + 153), b.at(t.body + 12)}});
                continue;
            }
            seenPatternArea = true;
            OrgPattern p;
            p.partId = b.le32(t.body);
            p.type = b.le16(t.body + 6);
            p.rows = noteRows(b, t.body + 535, next);
            if (p.rows.empty()) continue;
            const char* byType = trackForType(p.type);
            p.track = byType ? byType : tr;
            p.instrumentHint = b.at(t.body + 12);
            out.patterns.push_back(std::move(p));
        }
        static const char* kNames[] = {"Variation 1", "Variation 2", "Variation 3", "Variation 4", "Intro 1",
                                       "Intro 2",     "Intro 3",     "Fill 1",      "Fill 2",      "Fill 3",
                                       "Fill 4",      "Break",       "Ending 1",    "Ending 2",    "Ending 3"};
        const size_t start = tags[static_cast<size_t>(rhythmTag)].off;
        for (size_t o = std::max<size_t>(start, 8); o + 60 < n; ++o) {
            if (bytes[o] != 0x20 || bytes[o + 1] < 65 || bytes[o + 1] > 90) continue;
            const std::string nm = b.txt(o + 1, 31);
            bool known = false;
            for (const char* k : kNames)
                if (nm == k) { known = true; break; }
            if (!known) continue;
            const uint32_t partId = b.le32(o - 8);
            const bool wanted = std::any_of(out.patterns.begin(), out.patterns.end(), [&](const OrgPattern& p) { return p.partId == partId; });
            if (!wanted) continue;
            out.parts.push_back({partId, nm, b.at(o + 32), b.lei32(o + 40), b.le32(o + 44)});
        }
    }
    return true;
}

std::vector<std::pair<std::string, int>> assignInstruments(const OrgSet& set) {
    std::vector<std::pair<std::string, int>> choice;
    if (!set.hasRhythm) return choice;
    std::vector<std::pair<std::string, std::vector<int>>> notesOf;
    for (const auto& p : set.patterns) {
        auto it = std::find_if(notesOf.begin(), notesOf.end(), [&](const auto& x) { return x.first == p.track; });
        if (it == notesOf.end()) { notesOf.push_back({p.track, {}}); it = notesOf.end() - 1; }
        for (const auto& r : p.rows) it->second.push_back(r.note);
    }
    auto fits = [](const OrgInstrument& inst, const std::vector<int>& notes) {
        int lo = 1000, hi = -1000;
        for (const auto& s : inst.samples) { lo = std::min(lo, s.root); hi = std::max(hi, s.root); }
        int inside = 0, near = 0;
        for (int nn : notes) {
            if (nn >= lo - 2 && nn <= hi + 2) ++inside;
            if (nn >= lo - 9 && nn <= hi + 9) ++near;
        }
        const double len = static_cast<double>(notes.size());
        return inside / len + 0.5 * (near / len) - 0.3 * ((hi - lo) / 127.0);
    };
    for (const auto& tn : notesOf) {
        const std::string& track = tn.first;
        const std::vector<int>& notes = tn.second;
        if (notes.empty()) continue;
        int hint = -1;
        for (const auto& t : set.tracks)
            if (t.first == track) hint = t.second.hint;
        const bool drumTrack = track == "drums" || track == "perc";
        auto keyword = [&](const std::string& name) {
            if (track == "bass") return icontains(name, "bass");
            if (track == "drums") return icontains(name, "drum") || icontains(name, "kit");
            if (track == "perc")
                return icontains(name, "kit") || icontains(name, "perc") || icontains(name, "var") || icontains(name, "dal") ||
                       icontains(name, "tabl") || icontains(name, "bongo") || icontains(name, "daf");
            return false;
        };
        int best = -1;
        double bestScore = 0;
        for (const auto& inst : set.instruments) {
            if (inst.name == "-") continue;
            const double score = fits(inst, notes) + (inst.index == hint ? 0.5 : 0) + (drumTrack && inst.isKit ? 0.3 : 0) +
                                 (!drumTrack && inst.isKit ? -0.6 : 0) + (keyword(inst.name) ? 0.4 : 0) +
                                 (inst.samples.size() >= 4 ? 0.1 : 0);
            if (best < 0 || score > bestScore) { best = inst.index; bestScore = score; }  // first of equals wins (stable sort)
        }
        if (best >= 0) choice.push_back({track, best});
    }
    return choice;
}

std::vector<Style> orgStyles(const OrgSet& set, const std::string& id, const std::string& name, int tempo, int meterNum,
                             int meterDen) {
    std::vector<Style> out;
    if (!set.hasRhythm || set.patterns.empty()) return out;
    const auto choice = assignInstruments(set);
    const double beatsPerBar = static_cast<double>(meterNum * 4) / meterDen;
    auto byName = [&](const char* nm) -> const OrgPart* {
        const OrgPart* last = nullptr;  // a later record of the same name wins (JS object assignment)
        for (const auto& p : set.parts)
            if (p.name == nm) last = &p;
        return last;
    };
    // Tracks: first-seen order over the patterns.
    std::vector<Track> tracks;
    for (const auto& p : set.patterns) {
        Track t;
        t.name = p.track;
        if (p.track == "bass") { t.role = Role::Bass; t.channel = 2; }
        else if (p.track == "acc1") { t.role = Role::Chord; t.channel = 3; }
        else if (p.track == "acc2") { t.role = Role::Chord; t.channel = 6; }
        else if (p.track == "acc3") { t.role = Role::Pad; t.channel = 4; }
        else if (p.track == "acc4") { t.role = Role::Phrase; t.channel = 5; }
        else if (p.track == "acc5") { t.role = Role::Phrase; t.channel = 7; }
        else if (p.track == "drums") { t.role = Role::Drums; t.channel = 9; }
        else if (p.track == "perc") { t.role = Role::Perc; t.channel = 10; }
        else continue;
        t.transpose = t.role == Role::Drums || t.role == Role::Perc ? Transpose::None : Transpose::Chord;
        t.gate = 1;
        for (const auto& c : choice)
            if (c.first == p.track) t.orgInstrument = c.second;
        auto it = std::find_if(tracks.begin(), tracks.end(), [&](const Track& x) { return x.name == t.name; });
        if (it != tracks.end()) *it = t;
        else tracks.push_back(t);
    }
    std::vector<PartSet> sets(2);
    sets[0] = {"", "", {}};
    sets[0].names[kIntro] = {"Intro 1", "Intro 2", "Intro 3"};
    sets[0].names[kMainA] = {"Variation 1"};
    sets[0].names[kMainB] = {"Variation 2", "Variation 1"};
    sets[0].names[kFillA] = {"Fill 1", "Fill 2"};
    sets[0].names[kFillB] = {"Fill 2", "Fill 1"};
    sets[0].names[kEnding] = {"Ending 1", "Ending 2", "Ending 3", "Intro 2", "Fill 1"};
    sets[1] = {"-34", " (3/4)", {}};
    sets[1].names[kIntro] = {"Intro 2", "Intro 3", "Intro 1"};
    sets[1].names[kMainA] = {"Variation 3"};
    sets[1].names[kMainB] = {"Variation 4", "Variation 3"};
    sets[1].names[kFillA] = {"Fill 3", "Fill 4", "Fill 1"};
    sets[1].names[kFillB] = {"Fill 4", "Fill 3", "Fill 2"};
    sets[1].names[kEnding] = {"Ending 2", "Ending 3", "Ending 1", "Fill 3"};
    for (const PartSet& ps : sets) {
        const OrgPart* mainPart = nullptr;
        for (const char* nm : ps.names[kMainA])
            if ((mainPart = byName(nm))) break;
        if (!mainPart) continue;
        Style st;
        st.id = id + ps.suffix;
        st.name = name + ps.label;
        st.nameAr = st.name;
        st.group = "org";
        st.tempo = tempo;
        st.meterNum = meterNum;
        st.meterDen = meterDen;
        st.steps = meterDen == 8 ? meterNum * 2 : meterNum * 4;
        st.beatsPerBar = beatsPerBar;
        st.tracks = tracks;
        for (int key = 0; key < kPartCount; ++key) {
            const OrgPart* part = nullptr;
            for (const char* nm : ps.names[key])
                if ((part = byName(nm))) break;
            if (!part) part = mainPart;
            PartData& pd = st.parts[key];
            pd.bars = std::max(1, static_cast<int>(std::floor(part->lengthTicks / static_cast<double>(kTicksPerBar) + 0.5)));
            pd.beats = pd.bars * beatsPerBar;
            for (const auto& pat : set.patterns) {
                if (pat.partId != part->id) continue;
                const int ti = st.trackIndex(pat.track);
                if (ti < 0) continue;
                for (const auto& row : pat.rows) {
                    if (row.t >= part->lengthTicks) continue;
                    const int v = std::min(127, static_cast<int>(std::floor((row.vel > 127 ? row.vel / 2.0 : row.vel) + 0.5)));
                    pd.events.push_back(Event{(row.t / static_cast<double>(kTicksPerBar)) * beatsPerBar,
                                              std::max(0.05, (row.d / static_cast<double>(kTicksPerBar)) * beatsPerBar),
                                              static_cast<uint8_t>(ti), static_cast<int16_t>(row.note), static_cast<uint8_t>(v)});
                }
            }
            std::stable_sort(pd.events.begin(), pd.events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
        }
        out.push_back(std::move(st));
    }
    return out;
}

}  // namespace mm
