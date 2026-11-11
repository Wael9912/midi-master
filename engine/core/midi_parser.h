// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#pragma once
#include <cstddef>
#include <cstdint>

namespace mm {

// One channel message (note on/off, CC, program change, pitch bend…). System messages are dropped.
struct MidiMessage {
    uint8_t status = 0;  // 0x80..0xEF
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    int channel() const { return status & 0x0F; }
    int type() const { return status & 0xF0; }
};

// Byte-stream parser with running status. Real-time bytes (0xF8..0xFF, e.g. clock and active sensing) may appear
// anywhere and are skipped; SysEx is skipped up to 0xF7. Chunks may split a message anywhere.
// No allocation, no locks: safe inside the audio callback.
class MidiParser {
public:
    // Calls sink(const MidiMessage&) for each complete channel message in [bytes, bytes + n).
    template <typename Sink>
    void feed(const uint8_t* bytes, size_t n, Sink&& sink) {
        for (size_t i = 0; i < n; ++i) {
            const uint8_t b = bytes[i];
            if (b >= 0xF8) continue;                 // real-time: ignore, keep state
            if (b >= 0x80) {                         // status byte
                if (b == 0xF0) { inSysex_ = true; running_ = 0; have_ = 0; continue; }
                if (b == 0xF7) { inSysex_ = false; continue; }
                inSysex_ = false;
                if (b >= 0xF0) { running_ = 0; have_ = 0; continue; }  // other system common: drop, cancels running status
                running_ = b;
                have_ = 0;
                continue;
            }
            if (inSysex_ || running_ == 0) continue;  // data byte with no status
            data_[have_++] = b;
            if (have_ >= needed(running_)) {
                MidiMessage m;
                m.status = running_;
                m.data1 = data_[0];
                m.data2 = have_ > 1 ? data_[1] : 0;
                have_ = 0;
                sink(m);
            }
        }
    }
    void reset() { running_ = 0; have_ = 0; inSysex_ = false; }

private:
    static int needed(uint8_t status) {
        const int t = status & 0xF0;
        return (t == 0xC0 || t == 0xD0) ? 1 : 2;
    }
    uint8_t running_ = 0;
    uint8_t data_[2] = {0, 0};
    int have_ = 0;
    bool inSysex_ = false;
};

}  // namespace mm
