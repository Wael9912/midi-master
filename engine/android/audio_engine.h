// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// Oboe output stream + AMidi input, both serviced in the audio callback: a key press is read and rendered in the same
// callback, with no Java/Kotlin thread in between.
#pragma once
#include <amidi/AMidi.h>
#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "spsc_queue.h"
#include "synth.h"

namespace mm {

struct Command {
    enum class Kind : uint8_t { NoteOn, NoteOff, AllNotesOff, Gain } kind;
    uint8_t note = 0, velocity = 0;
    float value = 0.f;
};

// Indices of the array returned by stats() (mirrored in Kotlin NativeEngine.Stat).
enum Stat : int {
    kRunning, kSampleRate, kFramesPerBurst, kBufferSize, kBufferCapacity, kExclusive, kLowLatency, kMMap,
    kLatencyMs, kXRuns, kVoices, kMidiMessages, kHandoffAvgMs, kHandoffMaxMs, kRenderMaxPct, kMidiOpen,
    kStatCount
};

class AudioEngine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    bool start();  // open + start the stream (idempotent)
    void stop();   // stop + close
    bool openMidi(AMidiDevice* device, int portNumber);  // takes ownership of the device
    void closeMidi();
    bool send(const Command& c) { return commands_.push(c); }
    void stats(double out[kStatCount]);
    void resetPeaks() { resetPeaks_.store(true); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openStreamLocked();
    void pollMidi();

    std::mutex lock_;  // stream open/close and MIDI open/close only; never taken on the audio thread
    std::shared_ptr<oboe::AudioStream> stream_;
    std::unique_ptr<oboe::LatencyTuner> tuner_;
    bool wanted_ = false;

    Synth synth_;
    SpscQueue<Command, 256> commands_;

    AMidiDevice* midiDevice_ = nullptr;
    std::atomic<AMidiOutputPort*> midiPort_{nullptr};
    std::atomic<bool> midiBusy_{false};

    std::atomic<long> midiMessages_{0};
    std::atomic<long> handoffSumUs_{0}, handoffCount_{0}, handoffMaxUs_{0};
    std::atomic<int> renderMaxPct_{0};
    std::atomic<int> voices_{0};
    std::atomic<bool> resetPeaks_{false};
};

}  // namespace mm
