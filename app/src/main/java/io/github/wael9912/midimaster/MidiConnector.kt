// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Finds MIDI keyboards and hands the open device to the native engine, which reads it inside the audio callback.
 * Android's MIDI service opens class-compliant USB keyboards itself, so no USB permission dialog is needed.
 * All calls and callbacks run on the main thread.
 */
class MidiConnector(context: Context) {
    data class Device(val info: MidiDeviceInfo, val name: String)

    private val manager = context.getSystemService(MidiManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var open: MidiDevice? = null

    var devices by mutableStateOf<List<Device>>(emptyList()); private set
    var connectedId by mutableStateOf<Int?>(null); private set

    private val callback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(device: MidiDeviceInfo) { refresh(); if (connectedId == null) autoConnect() }
        override fun onDeviceRemoved(device: MidiDeviceInfo) {
            if (device.id == connectedId) disconnect()
            refresh()
        }
    }

    fun startWatching() {
        if (Build.VERSION.SDK_INT >= 33) {
            manager.registerDeviceCallback(MidiManager.TRANSPORT_MIDI_BYTE_STREAM, { handler.post(it) }, callback)
        } else {
            @Suppress("DEPRECATION") manager.registerDeviceCallback(callback, handler)
        }
        refresh()
        autoConnect()
    }

    fun stopWatching() {
        manager.unregisterDeviceCallback(callback)
        disconnect()
    }

    private fun refresh() {
        val infos: Collection<MidiDeviceInfo> = if (Build.VERSION.SDK_INT >= 33) {
            manager.getDevicesForTransport(MidiManager.TRANSPORT_MIDI_BYTE_STREAM)
        } else {
            @Suppress("DEPRECATION") manager.devices.toList()
        }
        // Only devices that send notes to us (they have an output port).
        devices = infos.filter { it.outputPortCount > 0 }
            .sortedBy { if (it.type == MidiDeviceInfo.TYPE_USB) 0 else 1 }
            .map { Device(it, nameOf(it)) }
    }

    private fun autoConnect() { devices.firstOrNull()?.let { connect(it) } }

    fun connect(d: Device) {
        if (d.info.id == connectedId) return
        disconnect()
        manager.openDevice(d.info, { dev ->
            if (dev == null) { Log.w(TAG, "openDevice failed: ${d.name}"); return@openDevice }
            if (NativeEngine.openMidi(dev, 0)) {
                open = dev
                connectedId = d.info.id
                Log.i(TAG, "connected ${d.name}")
            } else {
                dev.close()
            }
        }, handler)
    }

    fun disconnect() {
        if (open == null) return
        NativeEngine.closeMidi()  // release the native port first, then the Java device
        open?.close()
        open = null
        connectedId = null
    }

    private fun nameOf(info: MidiDeviceInfo): String {
        val p = info.properties
        return p.getString(MidiDeviceInfo.PROPERTY_PRODUCT)
            ?: p.getString(MidiDeviceInfo.PROPERTY_NAME)
            ?: "MIDI ${info.id}"
    }

    private companion object { const val TAG = "MidiMaster" }
}
