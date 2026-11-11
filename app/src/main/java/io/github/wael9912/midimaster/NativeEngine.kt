// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import android.media.midi.MidiDevice

/** The C++ engine (engine/android). Call from the main thread only: the command queue is single-producer. */
object NativeEngine {
    init { System.loadLibrary("midimaster") }

    external fun start(): Boolean
    external fun stop()
    external fun openMidi(device: MidiDevice, port: Int): Boolean
    external fun closeMidi()
    external fun noteOn(note: Int, velocity: Int)
    external fun noteOff(note: Int)
    external fun allNotesOff()
    external fun setGain(gain: Float)
    external fun resetPeaks()
    external fun stats(): DoubleArray

    /** Indices into [stats]; mirrors enum Stat in audio_engine.h. */
    object Stat {
        const val RUNNING = 0; const val SAMPLE_RATE = 1; const val BURST = 2; const val BUFFER = 3
        const val CAPACITY = 4; const val EXCLUSIVE = 5; const val LOW_LATENCY = 6; const val MMAP = 7
        const val LATENCY_MS = 8; const val XRUNS = 9; const val VOICES = 10; const val MIDI_MESSAGES = 11
        const val HANDOFF_AVG_MS = 12; const val HANDOFF_MAX_MS = 13; const val CPU_PEAK_PCT = 14; const val MIDI_OPEN = 15
        const val COUNT = 16
    }
}
