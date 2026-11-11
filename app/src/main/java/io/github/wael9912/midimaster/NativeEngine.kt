// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import android.media.midi.MidiDevice

/**
 * The C++ engine (engine/android/jni_bridge.cpp).
 *  • [send], [bankAttach], [bankRemove], [orgAttach], [styleSelect]: MAIN thread only (single-producer queue).
 *  • [bankOpen], [bankLoadPreset], [orgOpen], styles*: the one worker thread ([Library.worker]).
 */
object NativeEngine {
    init { System.loadLibrary("midimaster") }

    external fun start(): Boolean
    external fun stop()
    external fun openMidi(device: MidiDevice, port: Int): Boolean
    external fun closeMidi()
    external fun send(kind: Int, a: Int, b: Int, c: Int, d: Int, f: Float): Boolean
    external fun resetPeaks()
    external fun stats(): DoubleArray
    external fun state(): IntArray
    external fun drainRetired()
    external fun lastError(): String

    external fun bankOpen(path: String): Long
    external fun bankInfo(handle: Long): String
    external fun bankLoadPreset(handle: Long, bank: Int, program: Int)
    external fun bankResident(handle: Long): Long
    external fun bankAttach(slot: Int, handle: Long)
    external fun bankRemove(slot: Int)
    external fun bankFree(handle: Long)

    external fun orgOpen(path: String): Long
    external fun orgInfo(handle: Long): String
    external fun orgAttach(handle: Long)
    external fun orgFree(handle: Long)

    external fun stylesAddPack(source: String, json: String): String
    external fun stylesAddSty(source: String, path: String, id: String, name: String): String
    external fun stylesAddOrg(source: String, handle: Long, id: String, name: String, tempo: Int): String
    external fun stylesRemove(source: String)
    external fun styleSelect(id: String): Boolean
    external fun builtinPresetIndex(id: String): Int

    fun cmd(kind: Int, a: Int = 0, b: Int = 0, c: Int = 0, d: Int = 0, f: Float = 0f) = send(kind, a, b, c, d, f)

    /** Mirrors Command::Kind in engine/core/arranger.h. */
    object Cmd {
        const val KEY_ON = 0; const val KEY_OFF = 1; const val SUSTAIN = 2; const val BEND = 3; const val PANIC = 4
        const val SET_PROGRAM = 5; const val PREVIEW = 6; const val ADD_BANK = 7; const val REMOVE_BANK = 8
        const val SET_ORG = 9; const val SET_STYLE = 10; const val START = 11; const val STOP = 12; const val FILL = 13
        const val VARIATION = 14; const val ENDING = 15; const val SYNC = 16; const val TEMPO = 17; const val SPLIT = 18
        const val TRANSPOSE = 19; const val CHORD_MODE = 20; const val LOWER_ON = 21; const val LAYER = 22
        const val MUTE_MASK = 23; const val BUS = 24; const val REVERB = 25
    }

    /** Mirrors Program::Kind. */
    object Prog { const val BUILTIN = 0; const val KIT = 1; const val SF2 = 2; const val ORG = 3 }

    /** Indices into [stats]; mirrors enum Stat in audio_engine.h. */
    object Stat {
        const val RUNNING = 0; const val SAMPLE_RATE = 1; const val BURST = 2; const val BUFFER = 3
        const val CAPACITY = 4; const val EXCLUSIVE = 5; const val LOW_LATENCY = 6; const val MMAP = 7
        const val LATENCY_MS = 8; const val XRUNS = 9; const val VOICES = 10; const val MIDI_MESSAGES = 11
        const val HANDOFF_AVG_MS = 12; const val HANDOFF_MAX_MS = 13; const val CPU_PEAK_PCT = 14; const val MIDI_OPEN = 15
        const val DROPPED = 16; const val COUNT = 17
    }
}
