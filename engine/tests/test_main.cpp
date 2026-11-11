// SPDX-License-Identifier: GPL-3.0-only
// Desktop tests for the engine core. No framework: each CHECK prints on failure; exit code = failures.
// MM_TEST_SF2=/path/to/bank.sf2 also plays every preset of a real bank (the bundled FluidR3Mono, decoded).
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <vector>

#include "arranger.h"
#include "builtin.h"
#include "midi_parser.h"
#include "org_sampler.h"
#include "orgset.h"
#include "sf2.h"
#include "sf2_synth.h"
#include "spsc_queue.h"
#include "styledsl.h"
#include "test_util.h"

// ── allocation counter: the audio path must never allocate ──
std::atomic<long> gAllocs{0};
void* operator new(std::size_t n) {
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int gFailures = 0, gChecks = 0;

std::string readFile(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    if (ok) *ok = static_cast<bool>(f);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static std::string envOr(const char* env, const char* fallback) {
    const char* v = std::getenv(env);
    return v && *v ? v : fallback;
}
std::string fixturesDir() { return envOr("MM_FIXTURES_DIR", MM_FIXTURES_DIR); }
std::string assetsDir() { return envOr("MM_ASSETS_DIR", MM_ASSETS_DIR); }
std::string fixture(const char* name) { return fixturesDir() + "/" + name; }

using namespace mm;

static constexpr float kRates[] = {44100.f, 48000.f, 96000.f};

static int firstAudible(const std::vector<float>& l, float thr) {
    for (size_t i = 0; i < l.size(); ++i)
        if (std::fabs(l[i]) > thr) return static_cast<int>(i);
    return -1;
}

// Renders `frames` of one instrument into a mono buffer (left channel).
template <typename Inst>
static std::vector<float> renderMono(Inst& inst, int frames) {
    std::vector<float> out;
    float l[kBlock], r[kBlock], rv[kBlock];
    for (int done = 0; done < frames; done += kBlock) {
        const int n = std::min(kBlock, frames - done);
        for (int i = 0; i < n; ++i) l[i] = r[i] = rv[i] = 0;
        inst.render(MixBus{l, r, rv, n});
        out.insert(out.end(), l, l + n);
    }
    return out;
}

// ───────────────────────── built-in sounds ─────────────────────────

static void testBuiltinFirstSound() {
    ChannelState ch[16];
    ch[0].keyPart = true;
    for (float sr : kRates) {
        for (int p = 0; p < builtinPresetCount(); ++p) {
            for (int vel : {1, 64, 127}) {
                for (int note : {36, 60, 84}) {
                    BuiltinSynth s;
                    s.prepare(sr, ch);
                    s.noteOn(0, note, vel, p);
                    const auto buf = renderMono(s, static_cast<int>(sr * 0.004f));
                    const int f = firstAudible(buf, 1e-3f);
                    const int limit = static_cast<int>(sr * 0.001f);
                    CHECK(f >= 0 && f <= limit, "%s sr=%g note=%d vel=%d: first audible frame %d > %d (1 ms)", builtinPreset(p).id, sr, note, vel, f, limit);
                }
            }
        }
        for (int note = 35; note <= 82; ++note) {
            BuiltinSynth s;
            s.prepare(sr, ch);
            s.noteOn(9, note, 100, -1);
            const auto buf = renderMono(s, static_cast<int>(sr * 0.004f));
            const int f = firstAudible(buf, 1e-3f);
            CHECK(f >= 0 && f <= static_cast<int>(sr * 0.001f), "kit note %d sr=%g: first audible frame %d", note, sr, f);
        }
    }
}

static void testBuiltinRelease() {
    ChannelState ch[16];
    for (int p = 0; p < builtinPresetCount(); ++p) {
        BuiltinSynth s;
        s.prepare(48000.f, ch);
        s.noteOn(2, 60, 100, p);
        renderMono(s, 4800);
        s.noteOff(2, 60);
        const auto tail = renderMono(s, 48000 * 2);
        CHECK(s.activeVoices() == 0, "%s: voice still active 2 s after release", builtinPreset(p).id);
        float peak = 0;
        for (size_t i = tail.size() - 480; i < tail.size(); ++i) peak = std::fmax(peak, std::fabs(tail[i]));
        CHECK(peak < 1e-4f, "%s: not silent after release (%g)", builtinPreset(p).id, peak);
    }
    // Sustain pedal on the channel keeps the note; pedal up releases it.
    BuiltinSynth s;
    s.prepare(48000.f, ch);
    ch[0].sustain = true;
    s.noteOn(0, 60, 100, 9);
    s.noteOff(0, 60);
    renderMono(s, 4800);
    CHECK(s.activeVoices() == 1, "pedal down keeps the voice");
    ch[0].sustain = false;
    s.sustainUp(0);
    renderMono(s, 48000);
    CHECK(s.activeVoices() == 0, "pedal up ends it");
    // Same key again does not stack.
    for (int i = 0; i < 10; ++i) { s.noteOn(0, 60, 100, 0); renderMono(s, 64); }
    CHECK(s.activeVoices() == 1, "repeated key stacked %d voices", s.activeVoices());
    // Drums free themselves.
    for (int n = 35; n <= 82; ++n) s.noteOn(9, n, 127, -1);
    renderMono(s, 48000 * 2);
    s.allOff(-1, true);
    renderMono(s, 4800);
    CHECK(s.activeVoices() == 0, "drum voices left: %d", s.activeVoices());
}

// ───────────────────────── SF2 player (synthetic bank) ─────────────────────────

struct Bytes {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back(static_cast<uint8_t>(v)); }
    void u16(int v) { u8(v & 255); u8((v >> 8) & 255); }
    void u32(uint32_t v) { u16(static_cast<int>(v & 0xFFFF)); u16(static_cast<int>(v >> 16)); }
    void str(const char* s, size_t n) { for (size_t i = 0; i < n; ++i) u8(i < std::strlen(s) ? s[i] : 0); }
    void chunk(const char* id, const Bytes& body) {
        str(id, 4);
        u32(static_cast<uint32_t>(body.b.size()));
        b.insert(b.end(), body.b.begin(), body.b.end());
        if (body.b.size() & 1) u8(0);
    }
    void list(const char* kind, const Bytes& body) {
        Bytes l;
        l.str(kind, 4);
        l.b.insert(l.b.end(), body.b.begin(), body.b.end());
        chunk("LIST", l);
    }
};

// Sample 0: 100 silent frames, then a 441 Hz sine (period 100 at 44.1 kHz), looped 2100..4000, root A4.
// Sample 1: a 2000-frame noise burst, root 60. Preset 0:0 "Sine", preset 128:0 "Kit" (36 and 42 share exclusive class 1).
static std::string writeTestBank() {
    Bytes smpl;
    for (int i = 0; i < 4100; ++i) smpl.u16(i < 100 ? 0 : static_cast<int>(std::lround(std::sin((i - 100) * 2 * M_PI / 100.0) * 16000)) & 0xFFFF);
    for (int i = 0; i < 46; ++i) smpl.u16(0);
    uint32_t seed = 7;
    for (int i = 0; i < 2000; ++i) { seed = seed * 1103515245u + 12345u; smpl.u16((static_cast<int>((seed >> 16) & 0x7FFF) - 16384) & 0xFFFF); }
    for (int i = 0; i < 46; ++i) smpl.u16(0);
    Bytes info, sdta, pdta;
    Bytes ifil; ifil.u16(2); ifil.u16(1);
    Bytes inam; inam.str("Test bank", 10);
    info.chunk("ifil", ifil);
    info.chunk("INAM", inam);
    sdta.chunk("smpl", smpl);
    Bytes phdr;
    phdr.str("Sine", 20); phdr.u16(0); phdr.u16(0); phdr.u16(0); phdr.u32(0); phdr.u32(0); phdr.u32(0);
    phdr.str("Kit", 20); phdr.u16(0); phdr.u16(128); phdr.u16(1); phdr.u32(0); phdr.u32(0); phdr.u32(0);
    phdr.str("EOP", 20); phdr.u16(0); phdr.u16(0); phdr.u16(2); phdr.u32(0); phdr.u32(0); phdr.u32(0);
    Bytes pbag; pbag.u16(0); pbag.u16(0); pbag.u16(1); pbag.u16(0); pbag.u16(2); pbag.u16(0);
    Bytes pmod; for (int i = 0; i < 10; ++i) pmod.u8(0);
    Bytes pgen; pgen.u16(41); pgen.u16(0); pgen.u16(41); pgen.u16(1); pgen.u16(0); pgen.u16(0);
    Bytes inst;
    inst.str("SineInst", 20); inst.u16(0);
    inst.str("KitInst", 20); inst.u16(1);
    inst.str("EOI", 20); inst.u16(3);
    Bytes ibag; ibag.u16(0); ibag.u16(0); ibag.u16(3); ibag.u16(0); ibag.u16(6); ibag.u16(0); ibag.u16(9); ibag.u16(0);
    Bytes imod; for (int i = 0; i < 10; ++i) imod.u8(0);
    Bytes igen;
    auto gen = [&](int op, int amt) { igen.u16(op); igen.u16(amt & 0xFFFF); };
    auto range = [&](int op, int lo, int hi) { igen.u16(op); igen.u8(lo); igen.u8(hi); };
    range(43, 0, 127); gen(54, 1); gen(53, 0);
    range(43, 36, 36); gen(57, 1); gen(53, 1);
    range(43, 42, 42); gen(57, 1); gen(53, 1);
    gen(0, 0);
    Bytes shdr;
    auto sh = [&](const char* nm, uint32_t s, uint32_t e, uint32_t ls, uint32_t le, int pitch) {
        shdr.str(nm, 20); shdr.u32(s); shdr.u32(e); shdr.u32(ls); shdr.u32(le); shdr.u32(44100); shdr.u8(pitch); shdr.u8(0); shdr.u16(0); shdr.u16(1);
    };
    sh("sine", 0, 4100, 2100, 4000, 69);
    sh("noise", 4146, 6146, 4146, 4146, 60);
    sh("EOS", 0, 0, 0, 0, 0);
    pdta.chunk("phdr", phdr); pdta.chunk("pbag", pbag); pdta.chunk("pmod", pmod); pdta.chunk("pgen", pgen);
    pdta.chunk("inst", inst); pdta.chunk("ibag", ibag); pdta.chunk("imod", imod); pdta.chunk("igen", igen); pdta.chunk("shdr", shdr);
    Bytes body;
    body.str("sfbk", 4);
    body.list("INFO", info);
    body.list("sdta", sdta);
    body.list("pdta", pdta);
    Bytes riff;
    riff.chunk("RIFF", body);
    char path[] = "/tmp/mm_test_XXXXXX";
    const int fd = mkstemp(path);
    if (fd >= 0) {
        if (write(fd, riff.b.data(), riff.b.size()) != static_cast<ssize_t>(riff.b.size())) std::printf("short write\n");
        close(fd);
    }
    return path;
}

static void testSf2() {
    const std::string path = writeTestBank();
    Sf2Bank bank;
    std::string err;
    CHECK(bank.open(path, &err), "test bank: %s", err.c_str());
    CHECK(bank.presets().size() == 2 && bank.name() == "Test bank", "presets %zu name %s", bank.presets().size(), bank.name().c_str());
    CHECK(bank.findPreset(0, 0) == 0 && bank.findPreset(128, 0) == 1 && bank.findPreset(0, 5) == -1, "findPreset");
    ChannelState ch[16];
    ch[0].keyPart = true;
    for (float sr : kRates) {
        Sf2Synth s;
        s.prepare(sr, ch);
        if (sr == kRates[0]) {
            s.noteOn(0, 69, 127, &bank, 0);
            CHECK(s.activeVoices() == 0, "a zone whose sample is not loaded yet is skipped");
        }
        bank.loadPreset(0);
        bank.loadPreset(1);
        s.noteOn(0, 69, 127, &bank, 0);
        const auto buf = renderMono(s, static_cast<int>(sr * 2));
        const int f = firstAudible(buf, 1e-3f);
        CHECK(f >= 0 && f <= static_cast<int>(sr * 0.001f), "sf2 sr=%g: first audible frame %d (leading silence must be skipped)", sr, f);
        int crossings = 0;
        for (size_t i = static_cast<size_t>(sr); i + 1 < buf.size(); ++i)
            if (buf[i] <= 0 && buf[i + 1] > 0) ++crossings;
        CHECK(std::abs(crossings - 441) <= 3, "sf2 sr=%g: pitch %d Hz (want 441), the loop must keep it going", sr, crossings);
        s.noteOff(0, 69);
        renderMono(s, static_cast<int>(sr * 1.5));
        CHECK(s.activeVoices() == 0, "sf2 sr=%g: release frees the voice", sr);
    }
    // Pitch follows the key: an octave up = 882 Hz.
    Sf2Synth s;
    s.prepare(48000, ch);
    s.noteOn(0, 81, 127, &bank, 0);
    const auto up = renderMono(s, 48000);
    int crossings = 0;
    for (size_t i = 0; i + 1 < up.size(); ++i)
        if (up[i] <= 0 && up[i + 1] > 0) ++crossings;
    CHECK(std::abs(crossings - 882) <= 4, "octave up: %d Hz", crossings);
    // Exclusive class: the second stroke chokes the first.
    s.kill();
    s.noteOn(9, 36, 100, &bank, 1);
    renderMono(s, 480);
    s.noteOn(9, 42, 100, &bank, 1);
    renderMono(s, 4800);
    CHECK(s.activeVoices() == 1, "exclusive class: %d voices", s.activeVoices());
    // Velocity curve: soft notes are quieter (SF2 default concave curve).
    s.kill();
    s.noteOn(0, 69, 127, &bank, 0);
    const auto loud = renderMono(s, 4800);
    s.kill();
    s.noteOn(0, 69, 32, &bank, 0);
    const auto soft = renderMono(s, 4800);
    float pl = 0, ps = 0;
    for (size_t i = 2400; i < 4800; ++i) { pl = std::fmax(pl, std::fabs(loud[i])); ps = std::fmax(ps, std::fabs(soft[i])); }
    CHECK(ps > 0 && std::fabs(pl / ps - (127.f / 32.f) * (127.f / 32.f)) < 0.5f, "velocity curve: ratio %g", pl / ps);
    // No allocation on note-on / render.
    const long before = gAllocs.load();
    float l[kBlock], r[kBlock], rv[kBlock];
    for (int i = 0; i < 200; ++i) {
        s.noteOn(0, 40 + i % 40, 100, &bank, 0);
        s.noteOn(9, i % 2 ? 36 : 42, 100, &bank, 1);
        for (int k = 0; k < kBlock; ++k) l[k] = r[k] = rv[k] = 0;
        s.render(MixBus{l, r, rv, kBlock});
        if (i % 3 == 0) s.noteOff(0, 40 + i % 40);
    }
    CHECK(gAllocs.load() == before, "sf2 audio path allocated %ld times", gAllocs.load() - before);
    CHECK(s.activeVoices() <= Sf2Synth::kMaxVoices, "voices %d", s.activeVoices());
    unlink(path.c_str());
    Sf2Bank none;
    CHECK(!none.open("/nonexistent.sf2", &err), "a missing file is refused");
}

// The real bundled bank, when MM_TEST_SF2 points at it (decoded to SF2 on the desktop).
static void testRealBank() {
    const char* path = std::getenv("MM_TEST_SF2");
    if (!path || !*path) return;
    Sf2Bank bank;
    std::string err;
    const bool opened = bank.open(path, &err);
    CHECK(opened, "%s: %s", path, err.c_str());
    if (!opened) return;
    ChannelState ch[16];
    const auto t0 = std::chrono::steady_clock::now();
    bank.loadAll();
    const double loadS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int silent = 0, loudest = 0, firstLate = 0;
    float maxPeak = 0;
    for (size_t p = 0; p < bank.presets().size(); ++p) {
        const Sf2Preset& pr = bank.presets()[p];
        Sf2Synth s;
        s.prepare(48000, ch);
        const bool kit = pr.bank == 128;
        if (kit) for (int n = 35; n <= 81; n += 3) s.noteOn(9, n, 100, &bank, static_cast<int>(p));
        else s.noteOn(0, 60, 100, &bank, static_cast<int>(p));
        const auto buf = renderMono(s, 24000);
        float peak = 0;
        bool finite = true;
        for (float x : buf) { finite = finite && std::isfinite(x); peak = std::fmax(peak, std::fabs(x)); }
        CHECK(finite, "preset %d:%d %s: non-finite output", pr.bank, pr.program, pr.name.c_str());
        if (peak < 1e-3f) { ++silent; std::printf("  silent: %d:%d %s\n", pr.bank, pr.program, pr.name.c_str()); }
        if (peak > maxPeak) { maxPeak = peak; loudest = static_cast<int>(p); }
        if (!kit) {
            const int f = firstAudible(buf, 1e-3f);
            if (f > 480) ++firstLate;  // slow-attack voicings (strings, pads) are the bank's design
        }
    }
    std::printf("real bank %s: %zu presets, %.1f MB resident after loading all (%.2f s), %d silent, %d melodic presets start later than 10 ms, loudest %s (%.2f)\n",
                bank.name().c_str(), bank.presets().size(), bank.residentBytes() / 1e6, loadS, silent, firstLate,
                bank.presets()[static_cast<size_t>(loudest)].name.c_str(), maxPeak);
    CHECK(silent == 0, "%d presets are silent", silent);
    const int piano = bank.findPreset(0, 0);
    if (piano >= 0) {
        Sf2Synth s;
        ChannelState c2[16];
        s.prepare(48000, c2);
        s.noteOn(0, 60, 100, &bank, piano);
        const auto buf = renderMono(s, 960);
        const int f = firstAudible(buf, 1e-3f);
        CHECK(f >= 0 && f <= 48, "piano: first audible frame %d (> 1 ms)", f);
    }
    Sf2Synth s;
    s.prepare(48000, ch);
    const int strings = std::max(0, bank.findPreset(0, 48));
    for (int i = 0; i < 32; ++i) { s.noteOn(0, 40 + i, 100, &bank, piano >= 0 ? piano : 0); s.noteOn(2, 40 + i, 100, &bank, strings); }
    const auto t1 = std::chrono::steady_clock::now();
    renderMono(s, 480000);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    std::printf("real bank: %d voices rendered at %.0fx real time on this machine\n", s.activeVoices(), 10.0 / secs);
}

// ───────────────────────── ORG sampler ─────────────────────────

static void testOrgSampler() {
    const std::string bytes = readFile(fixture("synth.set"));
    OrgSet set;
    std::string err;
    CHECK(parseOrgSet(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), set, true, &err), "set: %s", err.c_str());
    ChannelState ch[16];
    OrgSampler o;
    o.prepare(48000, ch);
    o.setSet(&set);
    o.noteOn(3, 60, 100, 0);  // "c4" root 60, 40 leading silent frames: skipped
    const auto buf = renderMono(o, 480);
    const int f = firstAudible(buf, 1e-3f);
    CHECK(f >= 0 && f <= 48, "org: first audible frame %d", f);
    renderMono(o, 4800);
    CHECK(o.activeVoices() == 0, "org: a one-shot sample ends by itself");
    o.noteOn(3, 72, 100, 0);  // "c5" loops
    renderMono(o, 48000);
    CHECK(o.activeVoices() == 1, "org: a looped sample keeps sounding");
    o.noteOff(3, 72);
    renderMono(o, 48000);
    CHECK(o.activeVoices() == 0, "org: release ends it");
    o.noteOn(9, 52, 100, 1);  // the stereo sample of the kit
    const auto st = renderMono(o, 100);
    CHECK(firstAudible(st, 1e-3f) >= 0, "org: stereo sample plays");
}

// ───────────────────────── the arranger, end to end ─────────────────────────

static std::vector<Style> gStyles;

static Command cmd(uint8_t kind, int a = 0, int b = 0, int c = 0, int d = 0, float f = 0, void* p = nullptr) {
    Command x;
    x.kind = kind; x.a = a; x.b = b; x.c = c; x.d = d; x.f = f; x.p = p;
    return x;
}

static void renderFor(Arranger& a, double seconds, float* peak = nullptr) {
    static float buf[2 * 192];
    const int blocks = static_cast<int>(seconds * 48000 / 192);
    for (int i = 0; i < blocks; ++i) {
        a.render(buf, 192);
        for (float x : buf) {
            if (peak) *peak = std::fmax(*peak, std::fabs(x));
            if (!std::isfinite(x)) { CHECK(false, "non-finite output"); return; }
        }
    }
}

static const Style* styleById(const char* id) {
    for (const auto& s : gStyles)
        if (s.id == id) return &s;
    return &gStyles[0];
}

static void testArranger() {
    std::string err;
    CHECK(compilePack(readFile(assetsDir() + "/styles.json"), gStyles, &err), "%s", err.c_str());
    Arranger a;
    a.prepare(48000);
    a.command(cmd(Command::KeyOn, 72, 100));
    renderFor(a, 0.01);
    CHECK(a.voicesState.load() == 1, "upper key: %d voices", a.voicesState.load());
    a.command(cmd(Command::KeyOn, 36, 100));
    a.command(cmd(Command::KeyOn, 40, 100));
    a.command(cmd(Command::KeyOn, 43, 100));
    renderFor(a, 0.01);
    CHECK(a.voicesState.load() == 1, "chord zone is silent without 'left hand also sounds': %d", a.voicesState.load());
    CHECK(a.chordState.load() == ((0 + 1) | (static_cast<int>(ChordType::Maj) << 8)), "C major detected: %x", a.chordState.load());
    CHECK((a.held[1].load() >> (36 - 32)) & 1, "held key bits");
    a.command(cmd(Command::KeyOff, 40));
    a.command(cmd(Command::KeyOn, 39, 100));  // C minor
    renderFor(a, 0.01);
    CHECK((a.chordState.load() >> 8) == static_cast<int>(ChordType::Min), "C minor detected");
    a.command(cmd(Command::Panic));
    renderFor(a, 0.3);
    CHECK(a.voicesState.load() == 0 && a.held[1].load() == 0, "panic: %d voices", a.voicesState.load());
    a.command(cmd(Command::Layer, 1));
    a.command(cmd(Command::KeyOn, 72, 100));
    renderFor(a, 0.01);
    CHECK(a.voicesState.load() == 2, "layer: %d voices", a.voicesState.load());
    a.command(cmd(Command::Layer, 0));
    a.command(cmd(Command::LowerOn, 1));
    a.command(cmd(Command::KeyOn, 40, 100));
    renderFor(a, 0.01);
    CHECK(a.voicesState.load() == 3, "lower on: %d voices", a.voicesState.load());
    a.command(cmd(Command::LowerOn, 0));
    a.command(cmd(Command::Panic));
    renderFor(a, 0.3);
    const uint8_t on[] = {0x90, 76, 90, 77, 90}, off[] = {0x90, 76, 0, 77, 0};
    a.handleMidiBytes(on, sizeof on);
    renderFor(a, 0.01);
    CHECK(a.voicesState.load() == 2, "MIDI bytes: %d voices", a.voicesState.load());
    a.handleMidiBytes(off, sizeof off);
    renderFor(a, 1);
    CHECK(a.voicesState.load() == 0, "MIDI note-offs: %d voices", a.voicesState.load());

    // A style: sync start on the first chord, fill, tempo, ending, and no allocation while it all plays.
    Style* daloka = new Style(*styleById("daloka"));
    a.command(cmd(Command::SetStyle, 0, 0, 0, 0, 0, daloka));
    a.command(cmd(Command::Sync, 1));
    renderFor(a, 0.2);
    CHECK(!(a.transportState.load() & 1), "sync start waits");
    const long before = gAllocs.load();
    a.command(cmd(Command::KeyOn, 43, 100));  // G
    renderFor(a, 0.05);
    CHECK(a.transportState.load() & 1, "first chord starts the rhythm");
    float peak = 0;
    renderFor(a, 2, &peak);
    CHECK(a.voicesState.load() > 0, "the rhythm sounds");
    a.command(cmd(Command::KeyOn, 72, 110));
    a.command(cmd(Command::Fill, 1));
    a.command(cmd(Command::SetProgram, 0, Program::Builtin, builtinPresetIndex("strings")));
    renderFor(a, 6, &peak);
    CHECK(((a.transportState.load() >> 8) & 1) == 1, "fill to B lands on B");
    a.command(cmd(Command::KeyOff, 43));
    a.command(cmd(Command::KeyOn, 45, 100));
    a.command(cmd(Command::KeyOn, 48, 100));
    a.command(cmd(Command::KeyOn, 52, 100));  // A minor
    a.command(cmd(Command::Tempo, 150));
    renderFor(a, 3, &peak);
    CHECK(a.tempoState.load() == 150, "tempo");
    a.command(cmd(Command::Ending));
    renderFor(a, 6, &peak);
    CHECK(!(a.transportState.load() & 1), "ending stops the rhythm");
    CHECK(gAllocs.load() == before, "the arranger allocated %ld times while playing", gAllocs.load() - before);
    CHECK(peak <= 1.f && peak > 0.01f, "output peak %g", peak);
    a.command(cmd(Command::Panic));
    Style* pop = new Style(*styleById("pop"));
    a.command(cmd(Command::SetStyle, 0, 0, 0, 0, 0, pop));
    Retired r;
    CHECK(a.retired(r) && r.kind == Retired::Style && r.p == daloka, "old style retired");
    delete daloka;
    a.command(cmd(Command::Start, 1));
    renderFor(a, 1);
    a.command(cmd(Command::Stop));
    renderFor(a, 2);
    CHECK(!(a.transportState.load() & 1) && a.voicesState.load() == 0, "stop: %d voices", a.voicesState.load());
    for (const Style& st : gStyles) {
        Style* s = new Style(st);
        a.command(cmd(Command::SetStyle, 0, 0, 0, 0, 0, s));
        while (a.retired(r)) delete static_cast<Style*>(r.p);
        a.command(cmd(Command::Start, 1));
        float pk = 0;
        renderFor(a, 2, &pk);
        a.command(cmd(Command::Fill));
        renderFor(a, 3, &pk);
        a.command(cmd(Command::Ending));
        renderFor(a, 10, &pk);
        CHECK(!(a.transportState.load() & 1), "%s: ending stops", st.id.c_str());
        CHECK(pk > 0.01f && pk <= 1.f, "%s: peak %g", st.id.c_str(), pk);
    }
    a.command(cmd(Command::SetStyle, 0, 0, 0, 0, 0, nullptr));
    while (a.retired(r)) delete static_cast<Style*>(r.p);
}

static void testParserAndQueue() {
    std::vector<MidiMessage> got;
    auto sink = [&](const MidiMessage& m) { got.push_back(m); };
    MidiParser p;
    const uint8_t a[] = {0x90, 60, 0xFE, 100, 62, 0xF8};
    const uint8_t b[] = {90, 0x80, 60, 0};
    p.feed(a, sizeof a, sink);
    p.feed(b, sizeof b, sink);
    CHECK(got.size() == 3, "got %zu messages", got.size());
    if (got.size() == 3) {
        CHECK(got[0].status == 0x90 && got[0].data1 == 60 && got[0].data2 == 100, "msg0");
        CHECK(got[1].status == 0x90 && got[1].data1 == 62 && got[1].data2 == 90, "msg1 running status");
        CHECK(got[2].status == 0x80 && got[2].data1 == 60, "msg2");
    }
    got.clear();
    const uint8_t c[] = {5, 0xF0, 0x43, 0x10, 0x4C, 0xF7, 0xC1, 24, 0xE0, 0, 64};
    p.feed(c, sizeof c, sink);
    CHECK(got.size() == 2, "got %zu messages after sysex", got.size());
    SpscQueue<int, 8> q;
    int v = 0;
    CHECK(!q.pop(v), "empty pop");
    for (int i = 0; i < 8; ++i) CHECK(q.push(i), "push %d", i);
    CHECK(!q.push(99), "push into full queue");
    for (int i = 0; i < 8; ++i) CHECK(q.pop(v) && v == i, "pop %d got %d", i, v);
    for (float x = -10.f; x <= 10.f; x += 0.01f) {
        const float y = softClip(x);
        if (y < -1.f || y > 1.f) { CHECK(false, "softClip(%g) = %g", x, y); break; }
    }
    CHECK(std::fabs(softClip(0.1f) - 0.1f) < 0.002f, "softClip is transparent at low level");
}

void mixLevelReport();

int main() {
    if (std::getenv("MM_ONLY_PERF")) {  // on the phone: just the real-bank timings
        gStyles.clear();
        std::string err;
        compilePack(readFile(assetsDir() + "/styles.json"), gStyles, &err);
        testRealBank();
        mixLevelReport();
        std::printf("%d checks, %d failed\n", gChecks, gFailures);
        return gFailures == 0 ? 0 : 1;
    }
    testParserAndQueue();
    runFixtureTests();
    testBuiltinFirstSound();
    testBuiltinRelease();
    testSf2();
    testRealBank();
    testOrgSampler();
    testArranger();
    mixLevelReport();
    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

// Mix level of a full style on the real bank (MM_TEST_SF2): how hard does the soft clip work?
void mixLevelReport() {
    const char* path = std::getenv("MM_TEST_SF2");
    if (!path || !*path) return;
    auto* bank = new Sf2Bank();
    std::string err;
    if (!bank->open(path, &err)) return;
    bank->loadAll();
    Arranger a;
    a.prepare(48000);
    a.command(cmd(Command::AddBank, 0, 0, 0, 0, 0, bank));
    const int progs[][3] = {{0, 0, 0}, {1, 0, 48}, {2, 0, 33}, {3, 0, 21}, {4, 0, 48}, {5, 0, 81}, {6, 0, 24}, {7, 0, 73}, {9, 128, 0}, {10, 128, 0}};
    for (const auto& p : progs) a.command(cmd(Command::SetProgram, p[0], Program::Sf2, 0, (p[1] << 8) | p[2]));
    for (const char* id : {"tumtum", "daloka", "pop", "ballad"}) {
        Style* st = new Style(*styleById(id));
        a.command(cmd(Command::SetStyle, 0, 0, 0, 0, 0, st));
        Retired r;
        while (a.retired(r)) if (r.kind == Retired::Style) delete static_cast<Style*>(r.p);
        a.command(cmd(Command::KeyOn, 43, 100));
        a.command(cmd(Command::KeyOn, 47, 100));
        a.command(cmd(Command::KeyOn, 50, 100));
        a.command(cmd(Command::Start, 0));
        double sum = 0, tsum = 0, tmax = 0;
        long n = 0, hot = 0, blocks = 0;
        float peak = 0;
        static float buf[2 * 96];
        for (int i = 0; i < 48000 * 8 / 96; ++i) {  // 96-frame blocks, the phone's burst
            if (i % 120 == 0) for (int k = 0; k < 4; ++k) a.command(cmd(Command::KeyOn, 67 + k * 4 + (i / 120) % 3, 110));
            if (i % 120 == 60) for (int k = 0; k < 4; ++k) a.command(cmd(Command::KeyOff, 67 + k * 4 + (i / 120) % 3));
            const auto t0 = std::chrono::steady_clock::now();
            a.render(buf, 96);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            if (i > 10) { tsum += us; tmax = std::max(tmax, us); ++blocks; }
            for (float x : buf) { sum += x * x; ++n; peak = std::fmax(peak, std::fabs(x)); hot += std::fabs(x) > 0.78f; }
        }
        std::printf("mix %s on FluidR3: RMS %.1f dBFS, peak %.2f, %.2f%% past the clip knee; 96-frame block avg %.0f us, max %.0f us (budget 2000 us), voices %d\n",
                    id, 10 * std::log10(sum / n), peak, 100.0 * hot / n, tsum / blocks, tmax, a.voicesState.load());
        a.command(cmd(Command::Stop));
        a.command(cmd(Command::Panic));
        float tmp = 0;
        renderFor(a, 1, &tmp);
    }
    a.command(cmd(Command::SetStyle, 0, 0, 0, 0, 0, nullptr));
    a.command(cmd(Command::RemoveBank, 0));
    Retired r;
    while (a.retired(r)) {
        if (r.kind == Retired::Bank) delete static_cast<Sf2Bank*>(r.p);
        else if (r.kind == Retired::Style) delete static_cast<Style*>(r.p);
    }
}
