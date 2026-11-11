// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "audio_engine.h"

#include <android/log.h>
#include <oboe/OboeExtensions.h>
#include <time.h>
#include <unistd.h>

#define LOG_TAG "MidiMaster"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace mm {
namespace {
inline int64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // same clock as AMidi timestamps (System.nanoTime)
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}
}  // namespace

bool AudioEngine::openStreamLocked() {
    oboe::AudioStreamBuilder b;
    b.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)  // AAudio falls back to Shared when Exclusive is refused
        ->setFormat(oboe::AudioFormat::Float)
        ->setFormatConversionAllowed(true)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setChannelConversionAllowed(true)
        ->setUsage(oboe::Usage::Game)
        ->setContentType(oboe::ContentType::Music)
        // no setSampleRate: the device's native rate avoids a resampler
        ->setDataCallback(this)
        ->setErrorCallback(this);
    const oboe::Result r = b.openStream(stream_);
    if (r != oboe::Result::OK || !stream_) {
        LOGW("openStream failed: %s", oboe::convertToText(r));
        stream_.reset();
        return false;
    }
    synth_.prepare(static_cast<float>(stream_->getSampleRate()));
    tuner_ = std::make_unique<oboe::LatencyTuner>(*stream_);  // starts at 2 bursts, grows on underruns
    stream_->setPerformanceHintEnabled(true);                    // ADPF: ask the CPU governor to keep up
    const oboe::Result s = stream_->requestStart();
    LOGI("stream: %d Hz, burst %d, buffer %d/%d, %s, %s, mmap %d, start %s", stream_->getSampleRate(),
         stream_->getFramesPerBurst(), stream_->getBufferSizeInFrames(), stream_->getBufferCapacityInFrames(),
         oboe::convertToText(stream_->getSharingMode()), oboe::convertToText(stream_->getPerformanceMode()),
         oboe::OboeExtensions::isMMapUsed(stream_.get()), oboe::convertToText(s));
    return s == oboe::Result::OK;
}

bool AudioEngine::start() {
    std::lock_guard<std::mutex> g(lock_);
    wanted_ = true;
    if (stream_) return true;
    return openStreamLocked();
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> g(lock_);
    wanted_ = false;
    if (stream_) {
        stream_->stop();
        stream_->close();
        stream_.reset();
    }
    tuner_.reset();
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    // Route change (headphones, USB DAC, Bluetooth) or audio server restart: open a new stream on the new route.
    LOGW("stream closed: %s, reopening", oboe::convertToText(error));
    std::lock_guard<std::mutex> g(lock_);
    stream_.reset();
    tuner_.reset();
    if (wanted_) openStreamLocked();
}

bool AudioEngine::openMidi(AMidiDevice* device, int portNumber) {
    closeMidi();
    AMidiOutputPort* port = nullptr;
    const media_status_t st = AMidiOutputPort_open(device, portNumber, &port);
    if (st != AMEDIA_OK || !port) {
        LOGW("AMidiOutputPort_open failed: %d", st);
        AMidiDevice_release(device);
        return false;
    }
    std::lock_guard<std::mutex> g(lock_);
    midiDevice_ = device;
    midiPort_.store(port);
    return true;
}

void AudioEngine::closeMidi() {
    AMidiOutputPort* port = midiPort_.exchange(nullptr);
    while (midiBusy_.load()) usleep(500);  // the callback may still be reading the old port
    if (port) AMidiOutputPort_close(port);
    std::lock_guard<std::mutex> g(lock_);
    if (midiDevice_) {
        AMidiDevice_release(midiDevice_);
        midiDevice_ = nullptr;
    }
    commands_.push({Command::Kind::AllNotesOff});  // keys held on the unplugged keyboard must not hang
}

void AudioEngine::pollMidi() {
    midiBusy_.store(true);
    AMidiOutputPort* port = midiPort_.load();
    if (port) {
        uint8_t buf[1024];
        int32_t opcode = 0;
        size_t n = 0;
        int64_t ts = 0;
        const int64_t now = nowNs();
        for (int guard = 0; guard < 64; ++guard) {  // bounded: never spin in the callback
            const ssize_t got = AMidiOutputPort_receive(port, &opcode, buf, sizeof buf, &n, &ts);
            if (got <= 0) break;
            if (opcode != AMIDI_OPCODE_DATA || n == 0) continue;
            synth_.handleBytes(buf, n);
            midiMessages_.fetch_add(1, std::memory_order_relaxed);
            if (ts > 0 && now >= ts) {
                const long us = static_cast<long>((now - ts) / 1000);
                handoffSumUs_.fetch_add(us, std::memory_order_relaxed);
                handoffCount_.fetch_add(1, std::memory_order_relaxed);
                if (us > handoffMaxUs_.load(std::memory_order_relaxed)) handoffMaxUs_.store(us, std::memory_order_relaxed);
            }
        }
    }
    midiBusy_.store(false);
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) {
    const int64_t t0 = nowNs();
    if (resetPeaks_.exchange(false)) {
        handoffMaxUs_.store(0);
        handoffSumUs_.store(0);
        handoffCount_.store(0);
        renderMaxPct_.store(0);
    }
    if (tuner_) tuner_->tune();
    Command c;
    while (commands_.pop(c)) {
        switch (c.kind) {
            case Command::Kind::NoteOn: synth_.noteOn(0, c.note, c.velocity); break;
            case Command::Kind::NoteOff: synth_.noteOff(0, c.note); break;
            case Command::Kind::AllNotesOff: synth_.allNotesOff(); break;
            case Command::Kind::Gain: synth_.setMasterGain(c.value); break;
        }
    }
    pollMidi();
    synth_.render(static_cast<float*>(audioData), numFrames);
    voices_.store(synth_.activeVoices(), std::memory_order_relaxed);

    const double budgetNs = 1e9 * numFrames / stream->getSampleRate();
    const int pct = static_cast<int>(100.0 * static_cast<double>(nowNs() - t0) / budgetNs);
    if (pct > renderMaxPct_.load(std::memory_order_relaxed)) renderMaxPct_.store(pct, std::memory_order_relaxed);
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::stats(double out[kStatCount]) {
    for (int i = 0; i < kStatCount; ++i) out[i] = 0;
    std::lock_guard<std::mutex> g(lock_);
    if (stream_) {
        out[kRunning] = stream_->getState() == oboe::StreamState::Started ? 1 : 0;
        out[kSampleRate] = stream_->getSampleRate();
        out[kFramesPerBurst] = stream_->getFramesPerBurst();
        out[kBufferSize] = stream_->getBufferSizeInFrames();
        out[kBufferCapacity] = stream_->getBufferCapacityInFrames();
        out[kExclusive] = stream_->getSharingMode() == oboe::SharingMode::Exclusive ? 1 : 0;
        out[kLowLatency] = stream_->getPerformanceMode() == oboe::PerformanceMode::LowLatency ? 1 : 0;
        out[kMMap] = oboe::OboeExtensions::isMMapUsed(stream_.get()) ? 1 : 0;
        auto lat = stream_->calculateLatencyMillis();
        out[kLatencyMs] = lat ? lat.value() : -1;
        auto x = stream_->getXRunCount();
        out[kXRuns] = x ? x.value() : -1;
    }
    out[kVoices] = voices_.load();
    out[kMidiMessages] = static_cast<double>(midiMessages_.load());
    const long cnt = handoffCount_.load();
    out[kHandoffAvgMs] = cnt ? handoffSumUs_.load() / 1000.0 / cnt : 0;
    out[kHandoffMaxMs] = handoffMaxUs_.load() / 1000.0;
    out[kRenderMaxPct] = renderMaxPct_.load();
    out[kMidiOpen] = midiPort_.load() ? 1 : 0;
}

}  // namespace mm
