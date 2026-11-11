// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
// JNI for io.github.wael9912.midimaster.NativeEngine. Call these from ONE thread (the main thread): the command
// queue is single-producer.
#include <amidi/AMidi.h>
#include <jni.h>

#include "audio_engine.h"

namespace {
mm::AudioEngine& engine() {
    static mm::AudioEngine e;
    return e;
}
}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL Java_io_github_wael9912_midimaster_NativeEngine_start(JNIEnv*, jobject) {
    return engine().start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_github_wael9912_midimaster_NativeEngine_stop(JNIEnv*, jobject) { engine().stop(); }

JNIEXPORT jboolean JNICALL Java_io_github_wael9912_midimaster_NativeEngine_openMidi(JNIEnv* env, jobject, jobject midiDevice,
                                                                                    jint port) {
    AMidiDevice* dev = nullptr;
    if (AMidiDevice_fromJava(env, midiDevice, &dev) != AMEDIA_OK || !dev) return JNI_FALSE;
    return engine().openMidi(dev, port) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_github_wael9912_midimaster_NativeEngine_closeMidi(JNIEnv*, jobject) { engine().closeMidi(); }

JNIEXPORT void JNICALL Java_io_github_wael9912_midimaster_NativeEngine_noteOn(JNIEnv*, jobject, jint note, jint velocity) {
    engine().send({mm::Command::Kind::NoteOn, static_cast<uint8_t>(note), static_cast<uint8_t>(velocity)});
}

JNIEXPORT void JNICALL Java_io_github_wael9912_midimaster_NativeEngine_noteOff(JNIEnv*, jobject, jint note) {
    engine().send({mm::Command::Kind::NoteOff, static_cast<uint8_t>(note), 0});
}

JNIEXPORT void JNICALL Java_io_github_wael9912_midimaster_NativeEngine_allNotesOff(JNIEnv*, jobject) {
    engine().send({mm::Command::Kind::AllNotesOff});
}

JNIEXPORT void JNICALL Java_io_github_wael9912_midimaster_NativeEngine_setGain(JNIEnv*, jobject, jfloat gain) {
    engine().send({mm::Command::Kind::Gain, 0, 0, gain});
}

JNIEXPORT void JNICALL Java_io_github_wael9912_midimaster_NativeEngine_resetPeaks(JNIEnv*, jobject) { engine().resetPeaks(); }

JNIEXPORT jdoubleArray JNICALL Java_io_github_wael9912_midimaster_NativeEngine_stats(JNIEnv* env, jobject) {
    double v[mm::kStatCount];
    engine().stats(v);
    jdoubleArray arr = env->NewDoubleArray(mm::kStatCount);
    env->SetDoubleArrayRegion(arr, 0, mm::kStatCount, v);
    return arr;
}

}  // extern "C"
