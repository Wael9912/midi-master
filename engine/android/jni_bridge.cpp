// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
// JNI for io.github.wael9912.midimaster.NativeEngine.
//  • Commands (send, *Attach, styleSelect) go into a single-producer queue: call them from the MAIN thread only.
//  • Loading (bankOpen, bankLoadPreset, orgOpen, styles*) is heavy: call it from ONE worker thread.
#include <amidi/AMidi.h>
#include <android/log.h>
#include <fcntl.h>
#include <jni.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "audio_engine.h"
#include "orgset.h"
#include "sf2.h"
#include "sty.h"
#include "styledsl.h"

namespace {

mm::AudioEngine& engine() {
    static mm::AudioEngine e;
    return e;
}

std::mutex gLibLock;  // the style library and the last error (worker + main thread; never the audio thread)
std::map<std::string, std::vector<mm::Style>> gStyles;  // source → styles
std::string gLastError;

std::string str(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

// Valid 1–3 byte UTF-8 for NewStringUTF; stray bytes (Latin-1 names in style files) are re-encoded.
std::string sanitize(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 0;
        bool ok = len > 0 && i + static_cast<size_t>(len) <= s.size();
        for (int k = 1; ok && k < len; ++k) ok = (static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) >> 6) == 2;
        if (ok && c != 0) { out.append(s, i, static_cast<size_t>(len)); i += static_cast<size_t>(len); continue; }
        if (c == 0) { ++i; continue; }
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
        ++i;
    }
    return out;
}

jstring jstr(JNIEnv* env, const std::string& s) { return env->NewStringUTF(sanitize(s).c_str()); }

std::string q(const std::string& s) {  // a JSON string
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o + "\"";
}

std::string styleMeta(const mm::Style& st) {
    std::string o = "{\"id\":" + q(st.id) + ",\"name\":" + q(st.name) + ",\"nameAr\":" + q(st.nameAr) + ",\"group\":" + q(st.group) +
                    ",\"tempo\":" + std::to_string(st.tempo) + ",\"meter\":[" + std::to_string(st.meterNum) + "," + std::to_string(st.meterDen) +
                    "],\"beatsPerBar\":" + std::to_string(st.beatsPerBar) + ",\"tracks\":[";
    for (size_t i = 0; i < st.tracks.size(); ++i) {
        const mm::Track& t = st.tracks[i];
        o += (i ? "," : "") + std::string("[") + q(t.name) + "," + q(mm::roleName(t.role)) + "," + std::to_string(t.channel) + "," +
             std::to_string(t.orgInstrument) + "]";
    }
    static const char* kRoles[6] = {"upper", "lower", "bass", "chord", "pad", "phrase"};
    o += "],\"voices\":{";
    bool first = true;
    for (int i = 0; i < 6; ++i) {
        if (st.voices[i].empty()) continue;
        o += (first ? "" : ",") + q(kRoles[i]) + ":" + q(st.voices[i]);
        first = false;
    }
    o += "},\"gm\":{";
    first = true;
    for (int c = 0; c < 16; ++c) {
        if (st.gmPrograms[c] < 0) continue;
        o += (first ? "" : ",") + q(std::to_string(c)) + ":" + std::to_string(st.gmPrograms[c]);
        first = false;
    }
    return o + "}}";
}

std::string addStyles(const std::string& source, std::vector<mm::Style>&& styles) {
    std::string o = "[";
    for (size_t i = 0; i < styles.size(); ++i) o += (i ? "," : "") + styleMeta(styles[i]);
    o += "]";
    std::lock_guard<std::mutex> g(gLibLock);
    gStyles[source] = std::move(styles);
    return o;
}

jstring error(JNIEnv* env, const std::string& e) {
    {
        std::lock_guard<std::mutex> g(gLibLock);
        gLastError = e;
    }
    return jstr(env, "!" + e);
}

void setError(const std::string& e) {
    std::lock_guard<std::mutex> g(gLibLock);
    gLastError = e;
}

struct Mapped {
    const uint8_t* data = nullptr;
    size_t size = 0;
    int fd = -1;
    bool open(const std::string& path) {
        fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size <= 0) return false;
        size = static_cast<size_t>(st.st_size);
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) return false;
        data = static_cast<const uint8_t*>(p);
        return true;
    }
    ~Mapped() {
        if (data) munmap(const_cast<uint8_t*>(data), size);
        if (fd >= 0) close(fd);
    }
};

mm::Command make(int kind, int a = 0, int b = 0, int c = 0, int d = 0, float f = 0.f, void* p = nullptr) {
    mm::Command x;
    x.kind = static_cast<uint8_t>(kind);
    x.a = a;
    x.b = b;
    x.c = c;
    x.d = d;
    x.f = f;
    x.p = p;
    return x;
}

}  // namespace

#define JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_io_github_wael9912_midimaster_NativeEngine_##name

// ── stream and keyboard ──

JNI(jboolean, start)(JNIEnv*, jobject) { return engine().start() ? JNI_TRUE : JNI_FALSE; }
JNI(void, stop)(JNIEnv*, jobject) { engine().stop(); }

JNI(jboolean, openMidi)(JNIEnv* env, jobject, jobject midiDevice, jint port) {
    AMidiDevice* dev = nullptr;
    if (AMidiDevice_fromJava(env, midiDevice, &dev) != AMEDIA_OK || !dev) return JNI_FALSE;
    return engine().openMidi(dev, port) ? JNI_TRUE : JNI_FALSE;
}
JNI(void, closeMidi)(JNIEnv*, jobject) { engine().closeMidi(); }

JNI(jboolean, send)(JNIEnv*, jobject, jint kind, jint a, jint b, jint c, jint d, jfloat f) {
    return engine().send(make(kind, a, b, c, d, f)) ? JNI_TRUE : JNI_FALSE;
}

JNI(void, resetPeaks)(JNIEnv*, jobject) { engine().resetPeaks(); }

JNI(jdoubleArray, stats)(JNIEnv* env, jobject) {
    double v[mm::kStatCount];
    engine().stats(v);
    jdoubleArray arr = env->NewDoubleArray(mm::kStatCount);
    env->SetDoubleArrayRegion(arr, 0, mm::kStatCount, v);
    return arr;
}

// [held0, held1, held2, held3, chord, transport, beats × 1000, tempo, voices]
JNI(jintArray, state)(JNIEnv* env, jobject) {
    mm::Arranger& a = engine().arranger();
    jint v[9];
    for (int i = 0; i < 4; ++i) v[i] = static_cast<jint>(a.held[i].load());
    v[4] = a.chordState.load();
    v[5] = a.transportState.load();
    v[6] = static_cast<jint>(a.beats.load() * 1000.f);
    v[7] = a.tempoState.load();
    v[8] = a.voicesState.load();
    jintArray arr = env->NewIntArray(9);
    env->SetIntArrayRegion(arr, 0, 9, v);
    return arr;
}

JNI(void, drainRetired)(JNIEnv*, jobject) { engine().drainRetired(); }

JNI(jstring, lastError)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLibLock);
    return jstr(env, gLastError);
}

// ── SF2 banks ──

JNI(jlong, bankOpen)(JNIEnv* env, jobject, jstring path) {
    auto* b = new mm::Sf2Bank();
    std::string err;
    if (!b->open(str(env, path), &err)) {
        delete b;
        setError(err);
        return 0;
    }
    return reinterpret_cast<jlong>(b);
}

JNI(jstring, bankInfo)(JNIEnv* env, jobject, jlong h) {
    const auto* b = reinterpret_cast<const mm::Sf2Bank*>(h);
    if (!b) return jstr(env, "{}");
    std::string o = "{\"name\":" + q(b->name()) + ",\"presets\":[";
    for (size_t i = 0; i < b->presets().size(); ++i) {
        const auto& p = b->presets()[i];
        o += (i ? "," : "") + std::string("[") + std::to_string(p.bank) + "," + std::to_string(p.program) + "," + q(p.name) + "]";
    }
    return jstr(env, o + "]}");
}

JNI(void, bankLoadPreset)(JNIEnv*, jobject, jlong h, jint bank, jint program) {
    auto* b = reinterpret_cast<mm::Sf2Bank*>(h);
    if (b) b->loadPreset(b->findPreset(bank, program));
}

JNI(jlong, bankResident)(JNIEnv*, jobject, jlong h) {
    const auto* b = reinterpret_cast<const mm::Sf2Bank*>(h);
    return b ? static_cast<jlong>(b->residentBytes()) : 0;
}

JNI(void, bankAttach)(JNIEnv*, jobject, jint slot, jlong h) {
    engine().send(make(mm::Command::AddBank, slot, 0, 0, 0, 0, reinterpret_cast<void*>(h)));
}
JNI(void, bankRemove)(JNIEnv*, jobject, jint slot) { engine().send(make(mm::Command::RemoveBank, slot)); }
JNI(void, bankFree)(JNIEnv*, jobject, jlong h) { delete reinterpret_cast<mm::Sf2Bank*>(h); }

// ── ORG 24 sets ──

JNI(jlong, orgOpen)(JNIEnv* env, jobject, jstring path) {
    Mapped m;
    if (!m.open(str(env, path))) {
        setError("cannot open the file");
        return 0;
    }
    auto* set = new mm::OrgSet();
    std::string err;
    if (!mm::parseOrgSet(m.data, m.size, *set, true, &err)) {
        delete set;
        setError(err.empty() ? "notOrg" : err);
        return 0;
    }
    return reinterpret_cast<jlong>(set);
}

JNI(jstring, orgInfo)(JNIEnv* env, jobject, jlong h) {
    const auto* s = reinterpret_cast<const mm::OrgSet*>(h);
    if (!s) return jstr(env, "{}");
    std::string o = "{\"version\":" + q(s->version) + ",\"instruments\":[";
    for (size_t i = 0; i < s->instruments.size(); ++i)
        o += (i ? "," : "") + std::string("[") + q(s->instruments[i].name) + "," + (s->instruments[i].isKit ? "true" : "false") + "]";
    o += "],\"parts\":[";
    for (size_t i = 0; i < s->parts.size(); ++i) o += (i ? "," : "") + q(s->parts[i].name);
    return jstr(env, o + "]}");
}

JNI(void, orgAttach)(JNIEnv*, jobject, jlong h) { engine().send(make(mm::Command::SetOrg, 0, 0, 0, 0, 0, reinterpret_cast<void*>(h))); }
JNI(void, orgFree)(JNIEnv*, jobject, jlong h) { delete reinterpret_cast<mm::OrgSet*>(h); }

// ── styles ──

JNI(jstring, stylesAddPack)(JNIEnv* env, jobject, jstring source, jstring json) {
    std::vector<mm::Style> styles;
    std::string err;
    if (!mm::compilePack(str(env, json), styles, &err)) return error(env, err);
    return jstr(env, addStyles(str(env, source), std::move(styles)));
}

JNI(jstring, stylesAddSty)(JNIEnv* env, jobject, jstring source, jstring path, jstring id, jstring name) {
    Mapped m;
    if (!m.open(str(env, path))) return error(env, "cannot open the file");
    std::vector<mm::Style> styles;
    std::string err;
    if (!mm::convertSty(m.data, m.size, str(env, id), str(env, name), styles, &err)) return error(env, err);
    return jstr(env, addStyles(str(env, source), std::move(styles)));
}

JNI(jstring, stylesAddOrg)(JNIEnv* env, jobject, jstring source, jlong h, jstring id, jstring name, jint tempo) {
    const auto* set = reinterpret_cast<const mm::OrgSet*>(h);
    if (!set) return error(env, "no set");
    auto styles = mm::orgStyles(*set, str(env, id), str(env, name), tempo);
    return jstr(env, addStyles(str(env, source), std::move(styles)));
}

JNI(void, stylesRemove)(JNIEnv* env, jobject, jstring source) {
    std::lock_guard<std::mutex> g(gLibLock);
    gStyles.erase(str(env, source));
}

// The audio thread gets its own copy; the previous one comes back through drainRetired().
JNI(jboolean, styleSelect)(JNIEnv* env, jobject, jstring id) {
    const std::string want = str(env, id);
    mm::Style* copy = nullptr;
    {
        std::lock_guard<std::mutex> g(gLibLock);
        for (const auto& kv : gStyles) {
            for (const auto& st : kv.second)
                if (st.id == want) { copy = new mm::Style(st); break; }
            if (copy) break;
        }
    }
    if (!copy) return JNI_FALSE;
    if (!engine().send(make(mm::Command::SetStyle, 0, 0, 0, 0, 0, copy))) {
        delete copy;
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

JNI(jint, builtinPresetIndex)(JNIEnv* env, jobject, jstring id) { return mm::builtinPresetIndex(str(env, id).c_str()); }
