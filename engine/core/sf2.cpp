// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "sf2.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace mm {
namespace {

bool fail(std::string* e, const char* m) {
    if (e) *e = m;
    return false;
}

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

std::string str20(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && p[i]; ++i) s += static_cast<char>(p[i]);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

bool readAt(int fd, void* buf, size_t n, uint64_t off) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
        const ssize_t got = pread(fd, p, n, static_cast<off_t>(off));
        if (got <= 0) return false;
        p += got;
        n -= static_cast<size_t>(got);
        off += static_cast<uint64_t>(got);
    }
    return true;
}

struct Chunk {
    uint64_t off = 0;  // payload offset in the file
    uint32_t size = 0;
};

}  // namespace

Sf2Bank::~Sf2Bank() {
    if (fd_ >= 0) close(fd_);
    for (int16_t* p : owned_) delete[] p;
}

bool Sf2Bank::open(const std::string& path, std::string* error) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) return fail(error, "cannot open the file");
    uint8_t h[12];
    if (!readAt(fd_, h, 12, 0) || std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "sfbk", 4) != 0)
        return fail(error, "not an SF2 file");
    const uint64_t riffEnd = 8 + static_cast<uint64_t>(le32(h + 4));
    Chunk smpl, phdr, pbag, pgen, inst, ibag, igen, shdr;
    std::string inam;
    int ifilMajor = 2;
    uint64_t p = 12;
    while (p + 8 <= riffEnd) {
        uint8_t c[12];
        if (!readAt(fd_, c, 12, p)) break;
        const uint32_t size = le32(c + 4);
        if (std::memcmp(c, "LIST", 4) == 0) {
            uint64_t q = p + 12;
            const uint64_t listEnd = p + 8 + size;
            while (q + 8 <= listEnd) {
                uint8_t s[8];
                if (!readAt(fd_, s, 8, q)) break;
                const uint32_t cs = le32(s + 4);
                const Chunk ch{q + 8, cs};
                if (std::memcmp(c + 8, "sdta", 4) == 0 && std::memcmp(s, "smpl", 4) == 0) smpl = ch;
                else if (std::memcmp(c + 8, "pdta", 4) == 0) {
                    if (!std::memcmp(s, "phdr", 4)) phdr = ch;
                    else if (!std::memcmp(s, "pbag", 4)) pbag = ch;
                    else if (!std::memcmp(s, "pgen", 4)) pgen = ch;
                    else if (!std::memcmp(s, "inst", 4)) inst = ch;
                    else if (!std::memcmp(s, "ibag", 4)) ibag = ch;
                    else if (!std::memcmp(s, "igen", 4)) igen = ch;
                    else if (!std::memcmp(s, "shdr", 4)) shdr = ch;
                } else if (std::memcmp(c + 8, "INFO", 4) == 0) {
                    if (!std::memcmp(s, "INAM", 4) && cs < 256) {
                        std::vector<uint8_t> b(cs);
                        if (readAt(fd_, b.data(), cs, q + 8)) inam = str20(b.data(), cs);
                    } else if (!std::memcmp(s, "ifil", 4) && cs >= 4) {
                        uint8_t b[4];
                        if (readAt(fd_, b, 4, q + 8)) ifilMajor = le16(b);
                    }
                }
                q += 8 + cs + (cs & 1);
            }
        }
        p += 8 + size + (size & 1);
    }
    if (!smpl.size) return fail(error, "the SF2 has no sample data");
    if (!phdr.size || !pbag.size || !pgen.size || !inst.size || !ibag.size || !igen.size || !shdr.size)
        return fail(error, "the SF2 is missing a header chunk");
    smplOffset_ = smpl.off;
    smplFrames_ = smpl.size / 2;
    name_ = inam;

    auto readChunk = [&](const Chunk& c, std::vector<uint8_t>& out) {
        out.resize(c.size);
        return readAt(fd_, out.data(), c.size, c.off);
    };
    std::vector<uint8_t> bPhdr, bPbag, bPgen, bInst, bIbag, bIgen, bShdr;
    if (!readChunk(phdr, bPhdr) || !readChunk(pbag, bPbag) || !readChunk(pgen, bPgen) || !readChunk(inst, bInst) ||
        !readChunk(ibag, bIbag) || !readChunk(igen, bIgen) || !readChunk(shdr, bShdr))
        return fail(error, "the SF2 is truncated");

    // Samples.
    const size_t ns = bShdr.size() / 46;
    sampleCount_ = ns > 0 ? ns - 1 : 0;  // the last record is the terminal "EOS"
    samples_.reset(new Sf2Sample[sampleCount_ ? sampleCount_ : 1]);
    for (size_t i = 0; i < sampleCount_; ++i) {
        const uint8_t* r = bShdr.data() + i * 46;
        Sf2Sample& s = samples_[i];
        s.name = str20(r, 20);
        s.start = le32(r + 20);
        s.end = le32(r + 24);
        s.loopStart = le32(r + 28);
        s.loopEnd = le32(r + 32);
        s.rate = le32(r + 36);
        s.originalPitch = r[40];
        s.correction = static_cast<int8_t>(r[41]);
        s.type = le16(r + 44);
        if (s.type & 0x10) return fail(error, "sf3");
    }
    if (ifilMajor >= 3) return fail(error, "sf3");

    struct Bag { uint16_t gen; };
    auto bags = [](const std::vector<uint8_t>& b) {
        std::vector<Bag> out(b.size() / 4);
        for (size_t i = 0; i < out.size(); ++i) out[i].gen = le16(b.data() + i * 4);
        return out;
    };
    struct GenRec { uint16_t op; uint8_t lo, hi; int16_t amt; };
    auto gens = [](const std::vector<uint8_t>& b) {
        std::vector<GenRec> out(b.size() / 4);
        for (size_t i = 0; i < out.size(); ++i) {
            const uint8_t* r = b.data() + i * 4;
            out[i] = {le16(r), r[2], r[3], static_cast<int16_t>(le16(r + 2))};
        }
        return out;
    };
    const auto pb = bags(bPbag), ib = bags(bIbag);
    const auto pg = gens(bPgen), ig = gens(bIgen);
    auto zonesOf = [](const std::vector<Bag>& bg, const std::vector<GenRec>& gn, size_t from, size_t to, int terminal) {
        std::vector<Sf2Zone> zones;
        for (size_t b = from; b < to && b + 1 < bg.size(); ++b) {
            Sf2Zone z;
            for (size_t g = bg[b].gen; g < bg[b + 1].gen && g < gn.size(); ++g) {
                const GenRec& r = gn[g];
                if (r.op == kGenKeyRange) { z.keyLo = r.lo; z.keyHi = r.hi; }
                else if (r.op == kGenVelRange) { z.velLo = r.lo; z.velHi = r.hi; }
                else if (r.op < kGenCount) {
                    z.gen[r.op] = r.amt;
                    z.set |= 1ULL << r.op;
                    if (static_cast<int>(r.op) == terminal) z.target = static_cast<uint16_t>(r.amt);
                }
            }
            zones.push_back(z);
        }
        return zones;
    };
    const size_t ni = bInst.size() / 22;
    for (size_t i = 0; i + 1 < ni; ++i) {
        const uint8_t* r = bInst.data() + i * 22;
        Sf2Instrument it;
        it.name = str20(r, 20);
        auto zones = zonesOf(ib, ig, le16(r + 20), le16(r + 22 + 20), kGenSampleId);
        if (!zones.empty() && !zones[0].has(kGenSampleId)) {
            it.hasGlobal = true;
            it.global = zones[0];
            zones.erase(zones.begin());
        }
        for (auto& z : zones)
            if (z.has(kGenSampleId) && z.target >= 0 && static_cast<size_t>(z.target) < sampleCount_) it.zones.push_back(z);
        instruments_.push_back(std::move(it));
    }
    const size_t np = bPhdr.size() / 38;
    for (size_t i = 0; i + 1 < np; ++i) {
        const uint8_t* r = bPhdr.data() + i * 38;
        Sf2Preset pr;
        pr.name = str20(r, 20);
        pr.program = le16(r + 20);
        pr.bank = le16(r + 22);
        auto zones = zonesOf(pb, pg, le16(r + 24), le16(r + 38 + 24), kGenInstrument);
        if (!zones.empty() && !zones[0].has(kGenInstrument)) {
            pr.hasGlobal = true;
            pr.global = zones[0];
            zones.erase(zones.begin());
        }
        for (auto& z : zones)
            if (z.has(kGenInstrument) && z.target >= 0 && static_cast<size_t>(z.target) < instruments_.size()) pr.zones.push_back(z);
        if (!pr.zones.empty()) presets_.push_back(std::move(pr));
    }
    std::stable_sort(presets_.begin(), presets_.end(),
                     [](const Sf2Preset& a, const Sf2Preset& b) { return a.bank != b.bank ? a.bank < b.bank : a.program < b.program; });
    if (presets_.empty()) return fail(error, "the SF2 has no presets");
    return true;
}

int Sf2Bank::findPreset(int bank, int program) const {
    for (size_t i = 0; i < presets_.size(); ++i)
        if (presets_[i].bank == bank && presets_[i].program == program) return static_cast<int>(i);
    return -1;
}

void Sf2Bank::loadSample(int index) {
    if (index < 0 || static_cast<size_t>(index) >= sampleCount_) return;
    Sf2Sample& s = samples_[static_cast<size_t>(index)];
    if (s.data.load(std::memory_order_acquire)) return;
    if ((s.type & 0x8000) || s.end <= s.start || s.end > smplFrames_) return;  // ROM or broken sample
    const size_t len = s.end - s.start;
    int16_t* buf = new int16_t[len + kPadBefore + kPadAfter]();
    if (!readAt(fd_, buf + kPadBefore, len * 2, smplOffset_ + static_cast<uint64_t>(s.start) * 2)) {
        delete[] buf;
        return;
    }
    uint32_t lead = 0;
    for (size_t i = 0; i < len; ++i)
        if (std::abs(static_cast<int>(buf[kPadBefore + i])) > 33) { lead = static_cast<uint32_t>(i); break; }
    s.lead = lead;
    owned_.push_back(buf);
    resident_ += (len + kPadBefore + kPadAfter) * 2;
    s.data.store(buf + kPadBefore, std::memory_order_release);
}

void Sf2Bank::loadPreset(int presetIndex) {
    if (presetIndex < 0 || static_cast<size_t>(presetIndex) >= presets_.size()) return;
    for (const auto& pz : presets_[static_cast<size_t>(presetIndex)].zones) {
        const Sf2Instrument& it = instruments_[static_cast<size_t>(pz.target)];
        for (const auto& iz : it.zones) loadSample(iz.target);
    }
}

void Sf2Bank::loadAll() {
    for (size_t i = 0; i < presets_.size(); ++i) loadPreset(static_cast<int>(i));
}

}  // namespace mm
