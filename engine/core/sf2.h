// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// SoundFont 2 bank: the preset / instrument / zone hierarchy is parsed when the bank is opened; sample data is read
// lazily, per preset, on a worker thread (loadPreset), so a 120 MB bank costs only the sounds in use. The audio thread
// sees a sample once its data pointer is published (atomic, release/acquire) and skips zones whose sample is not loaded
// yet. SF3 (Ogg Vorbis) banks are converted to SF2 by the app first (Android MediaCodec): open() reports "sf3".
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mm {

enum Sf2Gen : int {
    kGenStartAddrsOffset = 0, kGenEndAddrsOffset = 1, kGenStartloopAddrsOffset = 2, kGenEndloopAddrsOffset = 3,
    kGenStartAddrsCoarseOffset = 4, kGenModLfoToPitch = 5, kGenVibLfoToPitch = 6, kGenModEnvToPitch = 7,
    kGenInitialFilterFc = 8, kGenInitialFilterQ = 9, kGenModLfoToFilterFc = 10, kGenModEnvToFilterFc = 11,
    kGenEndAddrsCoarseOffset = 12, kGenModLfoToVolume = 13, kGenChorusSend = 15, kGenReverbSend = 16, kGenPan = 17,
    kGenDelayModLfo = 21, kGenFreqModLfo = 22, kGenDelayVibLfo = 23, kGenFreqVibLfo = 24, kGenDelayModEnv = 25,
    kGenAttackModEnv = 26, kGenHoldModEnv = 27, kGenDecayModEnv = 28, kGenSustainModEnv = 29, kGenReleaseModEnv = 30,
    kGenKeynumToModEnvHold = 31, kGenKeynumToModEnvDecay = 32, kGenDelayVolEnv = 33, kGenAttackVolEnv = 34,
    kGenHoldVolEnv = 35, kGenDecayVolEnv = 36, kGenSustainVolEnv = 37, kGenReleaseVolEnv = 38,
    kGenKeynumToVolEnvHold = 39, kGenKeynumToVolEnvDecay = 40, kGenInstrument = 41, kGenKeyRange = 43,
    kGenVelRange = 44, kGenStartloopAddrsCoarseOffset = 45, kGenKeynum = 46, kGenVelocity = 47,
    kGenInitialAttenuation = 48, kGenEndloopAddrsCoarseOffset = 50, kGenCoarseTune = 51, kGenFineTune = 52,
    kGenSampleId = 53, kGenSampleModes = 54, kGenScaleTuning = 56, kGenExclusiveClass = 57, kGenOverridingRootKey = 58,
    kGenCount = 61
};

struct Sf2Zone {
    uint8_t keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
    uint64_t set = 0;  // bit g = generator g present
    int16_t gen[kGenCount] = {};
    int target = -1;  // instrument (preset zone) or sample (instrument zone) index
    bool has(int g) const { return (set >> g) & 1ULL; }
};

struct Sf2Instrument {
    std::string name;
    bool hasGlobal = false;
    Sf2Zone global;
    std::vector<Sf2Zone> zones;
};

struct Sf2Preset {
    std::string name;
    int bank = 0, program = 0;
    bool hasGlobal = false;
    Sf2Zone global;
    std::vector<Sf2Zone> zones;
};

struct Sf2Sample {
    std::string name;
    uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0, rate = 44100;
    int originalPitch = 60, correction = 0, type = 1;
    uint32_t lead = 0;                          // first frame above −60 dB (leading silence is skipped)
    std::atomic<const int16_t*> data{nullptr};  // frame 0 = `start`; 2 zero frames before, 4 after
};

class Sf2Bank {
public:
    static constexpr int kPadBefore = 2, kPadAfter = 4;

    Sf2Bank() = default;
    Sf2Bank(const Sf2Bank&) = delete;
    Sf2Bank& operator=(const Sf2Bank&) = delete;
    ~Sf2Bank();

    // Parses the bank's headers; keeps the file open for the sample reads. Error "sf3" = compressed bank.
    bool open(const std::string& path, std::string* error = nullptr);
    // Worker thread only.
    void loadPreset(int presetIndex);
    void loadAll();

    int findPreset(int bank, int program) const;  // index into presets(), -1 when missing
    const std::vector<Sf2Preset>& presets() const { return presets_; }
    const std::vector<Sf2Instrument>& instruments() const { return instruments_; }
    const Sf2Sample* samples() const { return samples_.get(); }
    size_t sampleCount() const { return sampleCount_; }
    const std::string& name() const { return name_; }
    size_t residentBytes() const { return resident_; }

private:
    void loadSample(int index);

    int fd_ = -1;
    uint64_t smplOffset_ = 0;
    uint32_t smplFrames_ = 0;
    std::string name_;
    std::vector<Sf2Preset> presets_;
    std::vector<Sf2Instrument> instruments_;
    std::unique_ptr<Sf2Sample[]> samples_;
    size_t sampleCount_ = 0;
    std::vector<int16_t*> owned_;
    size_t resident_ = 0;
};

}  // namespace mm
