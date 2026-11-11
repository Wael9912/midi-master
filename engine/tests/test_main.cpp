// SPDX-License-Identifier: GPL-3.0-only
// Desktop tests for the engine core. No framework: each CHECK prints on failure; exit code = failures.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "midi_parser.h"
#include "spsc_queue.h"
#include "synth.h"

// ── allocation counter: the audio path must never allocate ──
static std::atomic<long> gAllocs{0};
void* operator new(std::size_t n) {
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++gChecks;                                                         \
        if (!(cond)) {                                                     \
            ++gFailures;                                                   \
            std::printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);  \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
        }                                                                  \
    } while (0)

using mm::MidiMessage;
using mm::MidiParser;
using mm::Synth;

static constexpr float kRates[] = {44100.f, 48000.f, 96000.f};

// Index of the first frame whose |sample| exceeds `thr`, or -1.
static int firstAudible(const std::vector<float>& buf, float thr) {
    for (size_t i = 0; i < buf.size(); ++i)
        if (std::fabs(buf[i]) > thr) return static_cast<int>(i / 2);
    return -1;
}

static void testFirstSoundWithin1ms() {
    for (float sr : kRates) {
        for (int vel : {1, 20, 64, 127}) {
            for (int note : {21, 36, 60, 84, 108}) {
                Synth s;
                s.prepare(sr);
                std::vector<float> buf(2 * 256);
                s.noteOn(0, note, vel);
                s.render(buf.data(), 256);
                const int f = firstAudible(buf, 1e-3f);  // −60 dBFS
                const int limit = static_cast<int>(sr * 0.001f);
                CHECK(f >= 0 && f <= limit, "sr=%g note=%d vel=%d first audible frame %d > %d (1 ms)", sr, note, vel, f, limit);
            }
        }
    }
}

static void testReleaseFreesVoice() {
    Synth s;
    s.prepare(48000.f);
    std::vector<float> buf(2 * 480);
    s.noteOn(0, 60, 100);
    s.render(buf.data(), 480);
    CHECK(s.activeVoices() == 1, "voices %d", s.activeVoices());
    s.noteOff(0, 60);
    for (int i = 0; i < 100; ++i) s.render(buf.data(), 480);  // 1 s
    CHECK(s.activeVoices() == 0, "voice still active 1 s after release: %d", s.activeVoices());
    float peak = 0;
    for (float x : buf) peak = std::fmax(peak, std::fabs(x));
    CHECK(peak < 1e-5f, "output not silent after release: %g", peak);
}

static void testSustainPedal() {
    Synth s;
    s.prepare(48000.f);
    std::vector<float> buf(2 * 480);
    s.handle({0xB0, 64, 127});
    s.noteOn(0, 60, 100);
    s.noteOff(0, 60);
    for (int i = 0; i < 10; ++i) s.render(buf.data(), 480);  // 100 ms
    CHECK(s.activeVoices() == 1, "pedal down: voice must keep sounding (%d)", s.activeVoices());
    s.handle({0xB0, 64, 0});
    for (int i = 0; i < 100; ++i) s.render(buf.data(), 480);
    CHECK(s.activeVoices() == 0, "pedal up: voice must end (%d)", s.activeVoices());
}

static void testStealingAndBounds() {
    Synth s;
    s.prepare(48000.f);
    std::vector<float> buf(2 * 192);
    for (int n = 20; n < 120; ++n) s.noteOn(0, n, 127);  // 100 notes into 32 voices
    for (int i = 0; i < 50; ++i) {
        s.render(buf.data(), 192);
        for (float x : buf) {
            CHECK(std::isfinite(x), "non-finite sample");
            CHECK(x >= -1.f && x <= 1.f, "sample out of [-1,1]: %g", x);
            if (!std::isfinite(x) || x < -1.f || x > 1.f) return;
        }
    }
    CHECK(s.activeVoices() <= Synth::kMaxVoices, "voices %d", s.activeVoices());
    // The newest note must be sounding (stealing takes the oldest).
    s.allSoundOff();
    CHECK(s.activeVoices() == 0, "allSoundOff left %d voices", s.activeVoices());
}

static void testSameKeyDoesNotStack() {
    Synth s;
    s.prepare(48000.f);
    std::vector<float> buf(2 * 64);
    for (int i = 0; i < 10; ++i) { s.noteOn(0, 60, 100); s.render(buf.data(), 64); }
    CHECK(s.activeVoices() == 1, "repeated key stacked %d voices", s.activeVoices());
}

static void testNoAllocationOnAudioPath() {
    Synth s;
    s.prepare(48000.f);
    std::vector<float> buf(2 * 192);
    const uint8_t bytes[] = {0x90, 60, 100, 64, 90, 0xFE, 67, 80, 0x80, 60, 0, 0xB0, 64, 127};
    const long before = gAllocs.load();
    for (int i = 0; i < 200; ++i) {
        s.handleBytes(bytes, sizeof bytes);
        s.render(buf.data(), 192);
    }
    s.allNotesOff();
    s.render(buf.data(), 192);
    CHECK(gAllocs.load() == before, "audio path allocated %ld times", gAllocs.load() - before);
}

static void testSoftClip() {
    for (float x = -10.f; x <= 10.f; x += 0.01f) {
        const float y = Synth::softClip(x);
        CHECK(y >= -1.f && y <= 1.f, "softClip(%g) = %g", x, y);
        if (y < -1.f || y > 1.f) return;
    }
    CHECK(std::fabs(Synth::softClip(0.1f) - 0.1f) < 0.002f, "softClip not transparent at low level: %g", Synth::softClip(0.1f));
    CHECK(Synth::softClip(3.f) == 1.f && Synth::softClip(-3.f) == -1.f, "softClip ends");
}

static void testParser() {
    std::vector<MidiMessage> got;
    auto sink = [&](const MidiMessage& m) { got.push_back(m); };
    MidiParser p;
    // running status, active sensing in the middle of a message, clock bytes, split chunk
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
    // SysEx is skipped; program change takes one data byte; a stray data byte with no status is dropped
    const uint8_t c[] = {5, 0xF0, 0x43, 0x10, 0x4C, 0xF7, 0xC1, 24, 0xE0, 0, 64};
    p.feed(c, sizeof c, sink);
    CHECK(got.size() == 2, "got %zu messages after sysex", got.size());
    if (got.size() == 2) {
        CHECK(got[0].type() == 0xC0 && got[0].channel() == 1 && got[0].data1 == 24, "program change");
        CHECK(got[1].type() == 0xE0 && got[1].data2 == 64, "pitch bend");
    }
    // note-on velocity 0 = note-off in the synth
    Synth s;
    s.prepare(48000.f);
    const uint8_t on[] = {0x90, 60, 100}, off[] = {0x90, 60, 0};
    std::vector<float> buf(2 * 4800);
    s.handleBytes(on, 3);
    s.handleBytes(off, 3);
    for (int i = 0; i < 10; ++i) s.render(buf.data(), 4800);
    CHECK(s.activeVoices() == 0, "note-on vel 0 did not release");
}

static void testQueue() {
    mm::SpscQueue<int, 8> q;
    int v = 0;
    CHECK(!q.pop(v), "empty pop");
    for (int i = 0; i < 8; ++i) CHECK(q.push(i), "push %d", i);
    CHECK(!q.push(99), "push into full queue");
    for (int i = 0; i < 8; ++i) { CHECK(q.pop(v) && v == i, "pop %d got %d", i, v); }
    CHECK(!q.pop(v), "empty again");
}

int main() {
    testFirstSoundWithin1ms();
    testReleaseFreesVoice();
    testSustainPedal();
    testStealingAndBounds();
    testSameKeyDoesNotStack();
    testNoAllocationOnAudioPath();
    testSoftClip();
    testParser();
    testQueue();
    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
