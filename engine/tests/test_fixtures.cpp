// SPDX-License-Identifier: GPL-3.0-only
// The ported logic must reproduce the reference web engine exactly. The fixtures come from
// legacy-web/tools/dump_fixtures.mjs (run it again after changing the reference).
#include <dirent.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "chord.h"
#include "json.h"
#include "orgset.h"
#include "sequencer.h"
#include "sty.h"
#include "styledsl.h"
#include "test_util.h"

std::string assetsDir();

using namespace mm;

namespace {

json::Value load(const std::string& path) {
    bool ok = false;
    const std::string text = readFile(path, &ok);
    json::Value v;
    std::string err;
    CHECK(ok, "cannot read %s", path.c_str());
    if (ok) CHECK(json::parse(text, v, &err), "%s: %s", path.c_str(), err.c_str());
    return v;
}

bool near(double a, double b, double tol = 2e-6) { return std::fabs(a - b) <= tol; }

const char* transposeId(Transpose t) { return t == Transpose::None ? "none" : t == Transpose::Root ? "root" : "chord"; }

int partIndex(const std::string& n) {
    for (int p = 0; p < kPartCount; ++p)
        if (n == partName(p)) return p;
    return -1;
}

// Compares a compiled style with its JS dump ({id, tempo, meter, beatsPerBar, tracks, parts, [gmPrograms]}).
void compareStyle(const Style& st, const json::Value& js, const std::string& tag, bool withOrg) {
    CHECK(st.id == js["id"].str(), "%s: id %s vs %s", tag.c_str(), st.id.c_str(), js["id"].str().c_str());
    CHECK(st.tempo == js["tempo"].integer(), "%s: tempo %d vs %d", tag.c_str(), st.tempo, js["tempo"].integer());
    CHECK(st.meterNum == js["meter"][0].integer() && st.meterDen == js["meter"][1].integer(), "%s: meter", tag.c_str());
    CHECK(near(st.beatsPerBar, js["beatsPerBar"].num()), "%s: beatsPerBar", tag.c_str());
    if (js.find("steps")) CHECK(st.steps == js["steps"].integer(), "%s: steps %d vs %d", tag.c_str(), st.steps, js["steps"].integer());
    if (js.find("name")) CHECK(st.name == js["name"].str(), "%s: name '%s' vs '%s'", tag.c_str(), st.name.c_str(), js["name"].str().c_str());
    const json::Value& tracks = js["tracks"];
    CHECK(st.tracks.size() == tracks.size(), "%s: %zu tracks vs %zu", tag.c_str(), st.tracks.size(), tracks.size());
    for (const auto& kv : tracks.object) {
        const int ti = st.trackIndex(kv.first);
        CHECK(ti >= 0, "%s: missing track %s", tag.c_str(), kv.first.c_str());
        if (ti < 0) continue;
        const Track& t = st.tracks[static_cast<size_t>(ti)];
        CHECK(kv.second[0].str() == roleName(t.role), "%s/%s: role", tag.c_str(), kv.first.c_str());
        CHECK(kv.second[1].integer() == t.channel, "%s/%s: channel %d vs %d", tag.c_str(), kv.first.c_str(), t.channel, kv.second[1].integer());
        CHECK(kv.second[2].str() == transposeId(t.transpose), "%s/%s: transpose %s vs %s", tag.c_str(), kv.first.c_str(),
              transposeId(t.transpose), kv.second[2].str().c_str());
        if (withOrg) CHECK(kv.second[3].integer(-1) == t.orgInstrument, "%s/%s: orgInstrument %d vs %d", tag.c_str(), kv.first.c_str(),
                           t.orgInstrument, kv.second[3].integer(-1));
    }
    if (const json::Value* gm = js.find("gmPrograms")) {
        int n = 0;
        for (int c = 0; c < 16; ++c) n += st.gmPrograms[c] >= 0;
        CHECK(static_cast<size_t>(n) == gm->size(), "%s: %d GM programs vs %zu", tag.c_str(), n, gm->size());
        for (const auto& kv : gm->object) {
            const int ch = std::atoi(kv.first.c_str());
            CHECK(ch >= 0 && ch < 16 && st.gmPrograms[ch] == kv.second.integer(), "%s: GM program on %d", tag.c_str(), ch);
        }
    }
    for (const auto& kv : js["parts"].object) {
        const int p = partIndex(kv.first);
        CHECK(p >= 0, "%s: unknown part %s", tag.c_str(), kv.first.c_str());
        if (p < 0) continue;
        const PartData& pd = st.parts[p];
        const json::Value& jp = kv.second;
        CHECK(pd.bars == jp["bars"].integer(), "%s/%s: bars %d vs %d", tag.c_str(), kv.first.c_str(), pd.bars, jp["bars"].integer());
        CHECK(near(pd.beats, jp["beats"].num()), "%s/%s: beats", tag.c_str(), kv.first.c_str());
        const json::Value& ev = jp["events"];
        CHECK(pd.events.size() == ev.size(), "%s/%s: %zu events vs %zu", tag.c_str(), kv.first.c_str(), pd.events.size(), ev.size());
        const size_t n = std::min(pd.events.size(), ev.size());
        int bad = 0;
        for (size_t i = 0; i < n && bad < 3; ++i) {
            const Event& e = pd.events[i];
            const json::Value& j = ev[i];
            const std::string& tn = st.tracks[e.track].name;
            const bool same = tn == j[0].str() && near(e.t, j[1].num()) && e.n == j[2].integer() && e.v == j[3].integer() && near(e.d, j[4].num());
            if (!same) {
                ++bad;
                CHECK(same, "%s/%s event %zu: [%s %.6f %d %d %.6f] vs [%s %.6f %d %d %.6f]", tag.c_str(), kv.first.c_str(), i, tn.c_str(), e.t,
                      e.n, e.v, e.d, j[0].str().c_str(), j[1].num(), j[2].integer(), j[3].integer(), j[4].num());
            }
        }
    }
}

void testChords() {
    const json::Value cases = load(fixture("chords.json"));
    int bad = 0;
    for (const auto& c : cases.array) {
        int notes[128], n = 0;
        for (const auto& x : c[0].array) notes[n++] = x.integer();
        const bool single = c[1].str() == "single";
        const Chord got = detectChord(notes, n, single);
        bool same;
        if (c[2].isNull()) same = !got.valid();
        else same = got.valid() && got.root == c[2][0].integer() && c[2][1].str() == chordTypeId(got.type) && got.bass == c[2][2].integer();
        if (!same && bad++ < 5)
            CHECK(same, "chord case %s n=%d: got %d %s %d", c[1].str().c_str(), n, got.root, got.valid() ? chordTypeId(got.type) : "-", got.bass);
    }
    CHECK(bad == 0, "%d of %zu chord cases differ", bad, cases.size());
}

void testTranspose() {
    const json::Value fx = load(fixture("transpose.json"));
    int bad = 0;
    for (const auto& row : fx["table"].array) {
        const std::string& rule = row[0].str();
        const Transpose t = rule == "none" ? Transpose::None : rule == "root" ? Transpose::Root : Transpose::Chord;
        Chord c;
        c.root = static_cast<int8_t>(row[1].integer());
        chordTypeFromId(row[2].str().c_str(), c.type);
        for (int n = 0; n < 128; ++n)
            if (transposeNote(n, t, c) != row[3][static_cast<size_t>(n)].integer()) ++bad;
    }
    CHECK(bad == 0, "%d transpositions differ", bad);
}

std::vector<Style> builtinStyles() {
    std::vector<Style> styles;
    std::string err;
    const std::string pack = readFile(assetsDir() + "/styles.json");
    CHECK(compilePack(pack, styles, &err), "built-in pack: %s", err.c_str());
    CHECK(err.empty(), "built-in pack errors: %s", err.c_str());
    return styles;
}

void testCompiledStyles(const std::vector<Style>& styles) {
    const json::Value fx = load(fixture("styles_compiled.json"));
    CHECK(styles.size() == fx.size(), "%zu built-in styles vs %zu", styles.size(), fx.size());
    for (size_t i = 0; i < std::min(styles.size(), fx.size()); ++i) compareStyle(styles[i], fx[i], "builtin:" + styles[i].id, false);
    CHECK(noteNumber("C4") == 60 && noteNumber("A0") == 21 && noteNumber("Bb2") == 46 && noteNumber("x") == -1, "noteNumber");
}

struct Capture : SequencerSink {
    double sr = 48000;
    std::vector<std::vector<double>> notes;
    void seqNote(int ch, int n, int v, double at, double dur) override { notes.push_back({double(ch), double(n), double(v), at / sr, dur / sr}); }
};

void testSequencerRuns(const std::vector<Style>& styles) {
    const json::Value runs = load(fixture("sequencer.json"));
    const double sr = 48000;
    for (size_t r = 0; r < runs.size(); ++r) {
        const json::Value& run = runs[r];
        const Style* st = nullptr;
        for (const auto& s : styles)
            if (s.id == run["style"].str()) st = &s;
        CHECK(st, "run %zu: style %s", r, run["style"].str().c_str());
        if (!st) continue;
        Sequencer seq;
        seq.setSampleRate(sr);
        seq.setStyle(st, 0);
        seq.setTempo(run["tempo"].integer(), 0);
        if (!run["chord"].isNull()) {
            Chord c;
            c.root = static_cast<int8_t>(run["chord"][0].integer());
            chordTypeFromId(run["chord"][1].str().c_str(), c.type);
            seq.setChord(c);
        }
        Capture cap;
        cap.sr = sr;
        std::vector<std::pair<int, double>> parts;
        int64_t now = 0;
        seq.start(false, now);
        parts.push_back({seq.part(), seq.partStart() / sr});
        const json::Value& script = run["script"];
        size_t next = 0;
        // The JS run scheduled up to (clock + 0.2) at its last tick; compare everything well inside that window.
        double lastPlayed = 0;
        for (const auto& p : run["played"].array) lastPlayed = std::max(lastPlayed, p[3].num());
        const double limit = run["played"].size() ? lastPlayed - 0.3 : 0;
        const int64_t endFrame = static_cast<int64_t>((limit + 0.5) * sr);
        int k = 0;
        while (now < endFrame) {
            const int n = (k++ % 3 == 0) ? 17 : 32;  // uneven blocks: the result must not depend on them
            while (next < script.size() && script[next]["at"].num() * sr <= static_cast<double>(now)) {
                const json::Value& c = script[next++];
                const std::string& cmd = c["cmd"].str();
                if (cmd == "chord") {
                    Chord ch;
                    ch.root = static_cast<int8_t>(c["chord"]["root"].integer());
                    chordTypeFromId(c["chord"]["type"].str().c_str(), ch.type);
                    seq.setChord(ch);
                } else if (cmd == "fill") {
                    const std::string& to = c["to"].str();
                    seq.fill(to == "A" ? 0 : to == "B" ? 1 : -1);
                } else if (cmd == "variation") {
                    seq.setVariation(c["v"].str() == "B" ? 1 : 0);
                } else if (cmd == "ending") {
                    seq.ending();
                } else if (cmd == "mute") {
                    seq.setMuted(st->trackIndex(c["track"].str()), c["on"].boolean);
                }
            }
            const int before = seq.part();
            const double beforeStart = seq.partStart();
            seq.process(now, n, cap);
            if (seq.playing() && (seq.part() != before || seq.partStart() != beforeStart)) parts.push_back({seq.part(), seq.partStart() / sr});
            now += n;
            if (!seq.playing()) break;
        }
        const std::string tag = "seq run " + std::to_string(r) + " (" + st->id + ")";
        std::vector<const json::Value*> want;
        for (const auto& p : run["played"].array)
            if (p[3].num() < limit) want.push_back(&p);
        std::vector<const std::vector<double>*> got;
        for (const auto& p : cap.notes)
            if (p[3] < limit) got.push_back(&p);
        CHECK(got.size() == want.size(), "%s: %zu notes vs %zu", tag.c_str(), got.size(), want.size());
        int bad = 0;
        for (size_t i = 0; i < std::min(got.size(), want.size()) && bad < 3; ++i) {
            const auto& g = *got[i];
            const json::Value& w = *want[i];
            const bool same = g[0] == w[0].num() && g[1] == w[1].num() && g[2] == w[2].num() && near(g[3], w[3].num(), 3e-6) && near(g[4], w[4].num(), 3e-6);
            if (!same) {
                ++bad;
                CHECK(same, "%s note %zu: [%g %g %g %.6f %.6f] vs [%g %g %g %.6f %.6f]", tag.c_str(), i, g[0], g[1], g[2], g[3], g[4], w[0].num(), w[1].num(),
                      w[2].num(), w[3].num(), w[4].num());
            }
        }
        size_t wantParts = 0;
        for (const auto& p : run["parts"].array)
            if (p[1].num() < limit) ++wantParts;
        size_t gotParts = 0;
        for (const auto& p : parts)
            if (p.second < limit) ++gotParts;
        CHECK(gotParts == wantParts, "%s: %zu parts vs %zu", tag.c_str(), gotParts, wantParts);
        for (size_t i = 0; i < std::min(gotParts, wantParts); ++i)
            CHECK(run["parts"][i][0].str() == partName(parts[i].first) && near(parts[i].second, run["parts"][i][1].num(), 3e-6), "%s part %zu: %s@%.4f vs %s@%.4f",
                  tag.c_str(), i, partName(parts[i].first), parts[i].second, run["parts"][i][0].str().c_str(), run["parts"][i][1].num());
    }
}

void testSty() {
    const json::Value cases = load(fixture("sty.json"));
    for (const auto& c : cases.array) {
        const std::string bytes = readFile(fixture(c["file"].str().c_str()));
        const auto* b = reinterpret_cast<const uint8_t*>(bytes.data());
        CHECK(c["format"].str() == sniffFormat(b, bytes.size()), "%s: format", c["file"].str().c_str());
        std::vector<Style> out;
        std::string err;
        const std::string base = c["file"].str().substr(6, c["file"].str().size() - 10);  // synth_<name>.sty
        CHECK(convertSty(b, bytes.size(), "sty:" + base, base, out, &err), "%s: %s", c["file"].str().c_str(), err.c_str());
        CHECK(out.size() == c["styles"].size(), "%s: %zu styles vs %zu", c["file"].str().c_str(), out.size(), c["styles"].size());
        for (size_t i = 0; i < std::min(out.size(), c["styles"].size()); ++i) compareStyle(out[i], c["styles"][i], c["file"].str(), true);
    }
    std::vector<Style> out;
    std::string err;
    const uint8_t junk[] = {1, 2, 3, 4};
    CHECK(!convertSty(junk, 4, "x", "x", out, &err) && err == "notYamaha", "garbage refused (%s)", err.c_str());
    const uint8_t org[] = "Music Studio Music";
    CHECK(!convertSty(org, sizeof org, "x", "x", out, &err) && err == "org24", "an ORG set is named (%s)", err.c_str());
}

void compareOrg(const OrgSet& p, const json::Value& js, const std::string& tag, const std::string& id, const std::string& name) {
    CHECK(p.version == js["version"].str(), "%s: version %s", tag.c_str(), p.version.c_str());
    CHECK(p.instruments.size() == js["instruments"].size(), "%s: %zu instruments vs %zu", tag.c_str(), p.instruments.size(), js["instruments"].size());
    for (size_t i = 0; i < std::min(p.instruments.size(), js["instruments"].size()); ++i) {
        const OrgInstrument& in = p.instruments[i];
        const json::Value& ji = js["instruments"][i];
        CHECK(in.name == ji["name"].str() && in.isKit == ji["isKit"].boolean, "%s: instrument %zu %s", tag.c_str(), i, in.name.c_str());
        CHECK(in.samples.size() == ji["samples"].size(), "%s: %s samples %zu vs %zu", tag.c_str(), in.name.c_str(), in.samples.size(), ji["samples"].size());
        for (size_t k = 0; k < std::min(in.samples.size(), ji["samples"].size()); ++k) {
            const OrgSample& s = in.samples[k];
            const json::Value& j = ji["samples"][k];
            const bool same = s.root == j[0].integer() && s.name == j[1].str() && s.rate == j[2].integer() && s.channels == j[3].integer() &&
                              s.frames == j[4].integer() && s.volume == j[5].integer() && s.cents == j[6].integer() && s.loop == j[7].boolean &&
                              s.loopStart == j[8].integer() && s.loopEnd == j[9].integer() && static_cast<double>(s.dataOffset) == j[10].num();
            CHECK(same, "%s: %s sample %zu (%s)", tag.c_str(), in.name.c_str(), k, s.name.c_str());
        }
    }
    const json::Value& rh = js["rhythm"];
    CHECK(p.hasRhythm == !rh.isNull(), "%s: rhythm", tag.c_str());
    if (!rh.isNull()) {
        CHECK(p.tracks.size() == rh["tracks"].size(), "%s: %zu track records vs %zu", tag.c_str(), p.tracks.size(), rh["tracks"].size());
        for (const auto& t : p.tracks) {
            const json::Value* j = rh["tracks"].find(t.first);
            CHECK(j && (*j)[0].integer() == t.second.hint && (*j)[1].integer() == t.second.hint2, "%s: track record %s", tag.c_str(), t.first.c_str());
        }
        CHECK(p.patterns.size() == rh["patterns"].size(), "%s: %zu patterns vs %zu", tag.c_str(), p.patterns.size(), rh["patterns"].size());
        for (size_t i = 0; i < std::min(p.patterns.size(), rh["patterns"].size()); ++i) {
            const OrgPattern& pt = p.patterns[i];
            const json::Value& j = rh["patterns"][i];
            bool same = static_cast<double>(pt.partId) == j[0].num() && pt.track == j[1].str() && pt.type == j[2].integer() &&
                        pt.instrumentHint == j[3].integer() && pt.rows.size() == j[4].size();
            for (size_t k = 0; same && k < pt.rows.size(); ++k)
                same = pt.rows[k].note == j[4][k][0].integer() && pt.rows[k].vel == j[4][k][1].integer() && pt.rows[k].t == j[4][k][2].integer() &&
                       pt.rows[k].d == j[4][k][3].integer();
            CHECK(same, "%s: pattern %zu", tag.c_str(), i);
        }
        CHECK(p.parts.size() == rh["parts"].size(), "%s: %zu parts vs %zu", tag.c_str(), p.parts.size(), rh["parts"].size());
        for (size_t i = 0; i < std::min(p.parts.size(), rh["parts"].size()); ++i) {
            const OrgPart& pt = p.parts[i];
            const json::Value& j = rh["parts"][i];
            CHECK(static_cast<double>(pt.id) == j[0].num() && pt.name == j[1].str() && pt.type == j[2].integer() && pt.lengthTicks == j[3].integer() &&
                      static_cast<double>(pt.link) == j[4].num(),
                  "%s: part %zu %s", tag.c_str(), i, pt.name.c_str());
        }
    }
    const auto assign = assignInstruments(p);
    CHECK(assign.size() == js["assign"].size(), "%s: %zu assignments vs %zu", tag.c_str(), assign.size(), js["assign"].size());
    for (const auto& a : assign) {
        const json::Value* j = js["assign"].find(a.first);
        CHECK(j && j->integer() == a.second, "%s: %s → %d (want %d)", tag.c_str(), a.first.c_str(), a.second, j ? j->integer() : -1);
    }
    const auto styles = orgStyles(p, id, name, 165);
    CHECK(styles.size() == js["styles"].size(), "%s: %zu styles vs %zu", tag.c_str(), styles.size(), js["styles"].size());
    for (size_t i = 0; i < std::min(styles.size(), js["styles"].size()); ++i) compareStyle(styles[i], js["styles"][i], tag + ":" + styles[i].id, true);
}

void testOrg() {
    const std::string bytes = readFile(fixture("synth.set"));
    const auto* b = reinterpret_cast<const uint8_t*>(bytes.data());
    CHECK(isOrgSet(b, bytes.size()), "synthetic set recognised");
    OrgSet set;
    std::string err;
    CHECK(parseOrgSet(b, bytes.size(), set, true, &err), "synthetic set: %s", err.c_str());
    compareOrg(set, load(fixture("orgset.json")), "org synth", "org:synth", "Synth");
    // PCM and leading silence: "c4" starts with 40 silent frames.
    if (!set.instruments.empty() && !set.instruments[0].samples.empty()) {
        const OrgSample& s = set.instruments[0].samples[0];
        CHECK(s.pcm.size() == 300 && s.lead == 40, "c4 PCM %zu frames, lead %d", s.pcm.size(), s.lead);
    }
    OrgSet bad;
    const uint8_t mthd[] = {'M', 'T', 'h', 'd'};
    CHECK(!parseOrgSet(mthd, 4, bad, false, &err), "a non-set is refused");
}

// The owner's own files, when the local fixtures exist (never committed).
void testLocal() {
    DIR* d = opendir(MM_LOCAL_FIXTURES);
    if (!d) return;
    while (dirent* e = readdir(d)) {
        const std::string fn = e->d_name;
        if (fn.size() < 6 || fn.substr(fn.size() - 5) != ".json") continue;
        const json::Value js = load(std::string(MM_LOCAL_FIXTURES) + "/" + fn);
        bool ok = false;
        const std::string bytes = readFile(js["file"].str(), &ok);
        if (!ok) { std::printf("local fixture %s: source file missing, skipped\n", fn.c_str()); continue; }
        const auto* b = reinterpret_cast<const uint8_t*>(bytes.data());
        const std::string name = fn.substr(0, fn.size() - 5);
        if (js["kind"].str() == "org24") {
            OrgSet set;
            std::string err;
            CHECK(parseOrgSet(b, bytes.size(), set, true, &err), "%s: %s", fn.c_str(), err.c_str());
            compareOrg(set, js, "local " + name, "org:" + name, name);
            size_t pcm = 0;
            for (const auto& in : set.instruments)
                for (const auto& s : in.samples) pcm += s.pcm.size() * 2;
            std::printf("local %s: %zu instruments, %.1f MB PCM — compared with the web reader\n", name.c_str(), set.instruments.size(), pcm / 1e6);
        } else {
            std::vector<Style> out;
            std::string err;
            CHECK(convertSty(b, bytes.size(), "sty:" + name, name, out, &err), "%s: %s", fn.c_str(), err.c_str());
            for (size_t i = 0; i < std::min(out.size(), js["styles"].size()); ++i) compareStyle(out[i], js["styles"][i], "local " + name, true);
        }
    }
    closedir(d);
}

void testDrift(const std::vector<Style>& styles) {
    // 10,000 bars of main A chained by arithmetic land exactly on the bar lines.
    const Style* pop = nullptr;
    for (const auto& s : styles)
        if (s.id == "pop") pop = &s;
    if (!pop) return;
    const double sr = 48000;
    Sequencer seq;
    seq.setSampleRate(sr);
    seq.setStyle(pop, 0);
    seq.setTempo(240, 0);  // one bar per second
    seq.start(false, 0);
    struct Kicks : SequencerSink {
        long onBar = 0, other = 0;
        double maxErr = 0;
        void seqNote(int ch, int n, int, double at, double) override {
            if (ch == 9 && n == 36) {
                const double bars = at / 48000.0;
                const double err = std::fabs(bars - std::round(bars)) * 48000.0;
                if (err < 1e-3) ++onBar; else ++other;
            }
            const double sixteenth = at / 3000.0;  // 16th = 0.0625 s = 3000 frames
            maxErr = std::max(maxErr, std::fabs(sixteenth - std::round(sixteenth)) * 3000.0);
        }
    } k;
    const int64_t total = static_cast<int64_t>(10000 * sr);
    for (int64_t now = 0; now < total + 64; now += 192) seq.process(now, 192, k);
    CHECK(k.onBar == 10001, "kick on every bar line over 10,000 bars: %ld", k.onBar);
    CHECK(k.maxErr < 1e-3, "max timing error %.6f frames", k.maxErr);
}

}  // namespace

void runFixtureTests() {
    testChords();
    testTranspose();
    const auto styles = builtinStyles();
    testCompiledStyles(styles);
    testSequencerRuns(styles);
    testDrift(styles);
    testSty();
    testOrg();
    testLocal();
}
