// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import io.github.wael9912.midimaster.NativeEngine.Stat

class MainActivity : ComponentActivity() {
    private lateinit var midi: MidiConnector

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)  // stays up on stage
        midi = MidiConnector(this)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(Modifier.fillMaxSize(), color = Color.Black) { M1Screen(midi) }
            }
        }
    }

    // The stream lives while the app is in front: it stays open and warm, so the first key never waits for it.
    override fun onStart() {
        super.onStart()
        NativeEngine.start()
        midi.startWatching()
    }

    override fun onStop() {
        midi.stopWatching()
        NativeEngine.stop()
        super.onStop()
    }
}

@Composable
private fun M1Screen(midi: MidiConnector) {
    var stats by remember { mutableStateOf(DoubleArray(Stat.COUNT)) }
    var volume by remember { mutableFloatStateOf(0.5f) }
    LaunchedEffect(Unit) {
        while (true) {
            stats = NativeEngine.stats()
            delay(250)
        }
    }
    Column(Modifier.fillMaxSize().safeDrawingPadding().padding(horizontal = 16.dp)) {
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(Modifier.padding(top = 8.dp), verticalAlignment = Alignment.Bottom) {
                Text(stringResource(R.string.app_name), fontSize = 24.sp, fontWeight = FontWeight.Bold)
                Text("  ${BuildConfig.VERSION_NAME}", color = Color.Gray)
            }
            Text(stringResource(R.string.sound_m1), color = Color.Gray, fontSize = 13.sp)
            KeyboardCard(midi)
            AudioCard(stats)
            Text("${stringResource(R.string.volume)}  ${(volume * 100).toInt()}%")
            Slider(value = volume, onValueChange = { volume = it; NativeEngine.setGain(it) })
        }
        Text(stringResource(R.string.test_pads), color = Color.Gray, fontSize = 13.sp, modifier = Modifier.padding(top = 8.dp))
        Pads()
        OutlinedButton(onClick = { NativeEngine.allNotesOff() }, modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
            Text(stringResource(R.string.all_notes_off))
        }
    }
}

@Composable
private fun KeyboardCard(midi: MidiConnector) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(stringResource(R.string.keyboard), fontWeight = FontWeight.Bold)
            if (midi.devices.isEmpty()) Text(stringResource(R.string.no_keyboard), color = Color.Gray)
            midi.devices.forEach { d ->
                val on = d.info.id == midi.connectedId
                Row(
                    Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).clickable { midi.connect(d) }.padding(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(if (on) "●" else "○", color = if (on) Color(0xFF4CAF50) else Color.Gray)
                    Text("  ${d.name}", Modifier.weight(1f))
                    Text(stringResource(if (on) R.string.connected else R.string.tap_to_connect), color = Color.Gray, fontSize = 12.sp)
                }
            }
        }
    }
}

@Composable
private fun AudioCard(s: DoubleArray) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(R.string.audio_path), fontWeight = FontWeight.Bold)
            if (s[Stat.RUNNING] < 1) { Text(stringResource(R.string.stream_off), color = Color(0xFFFF7043)); return@Column }
            val sr = s[Stat.SAMPLE_RATE]
            fun ms(frames: Double) = if (sr > 0) "%.1f ms".format(frames * 1000 / sr) else "–"
            Line(R.string.sample_rate, "${sr.toInt()} Hz")
            Line(R.string.burst, "${s[Stat.BURST].toInt()} (${ms(s[Stat.BURST])})")
            Line(R.string.buffer, "${s[Stat.BUFFER].toInt()} / ${s[Stat.CAPACITY].toInt()} (${ms(s[Stat.BUFFER])})")
            Line(R.string.mode, listOf(
                if (s[Stat.LOW_LATENCY] > 0) "low-latency" else "NOT low-latency",
                if (s[Stat.EXCLUSIVE] > 0) "exclusive" else "shared",
                if (s[Stat.MMAP] > 0) "MMAP" else "legacy",
            ).joinToString(" · "))
            Line(R.string.output_latency, if (s[Stat.LATENCY_MS] >= 0) "%.1f ms".format(s[Stat.LATENCY_MS]) else "–")
            Line(R.string.midi_handoff, "%.2f / %.2f ms".format(s[Stat.HANDOFF_AVG_MS], s[Stat.HANDOFF_MAX_MS]))
            Line(R.string.cpu_peak, "${s[Stat.CPU_PEAK_PCT].toInt()} %")
            Line(R.string.xruns, "${s[Stat.XRUNS].toInt()}")
            Line(R.string.voices, "${s[Stat.VOICES].toInt()}")
            Line(R.string.midi_messages, "${s[Stat.MIDI_MESSAGES].toLong()}")
            OutlinedButton(onClick = { NativeEngine.resetPeaks() }) { Text(stringResource(R.string.reset_peaks)) }
        }
    }
}

@Composable
private fun Line(label: Int, value: String) {
    Row(Modifier.fillMaxWidth()) {
        Text(stringResource(label), Modifier.weight(1f), color = Color.Gray, fontSize = 14.sp)
        Text(value, fontSize = 14.sp)
    }
}

/** One octave of white keys, C4–C5. Each pad is its own touch target, so several can be held at once. */
@Composable
private fun Pads() {
    val notes = listOf(60, 62, 64, 65, 67, 69, 71, 72)
    val names = listOf("C", "D", "E", "F", "G", "A", "B", "C")
    Row(Modifier.fillMaxWidth().height(96.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        notes.forEachIndexed { i, note ->
            var down by remember { mutableStateOf(false) }
            Box(
                Modifier.weight(1f).height(96.dp).clip(RoundedCornerShape(6.dp))
                    .background(if (down) Color(0xFF90CAF9) else Color(0xFFECEFF1))
                    .pointerInput(note) {
                        awaitEachGesture {
                            awaitFirstDown(requireUnconsumed = false)
                            down = true
                            NativeEngine.noteOn(note, 100)
                            waitForUpOrCancellation()
                            NativeEngine.noteOff(note)
                            down = false
                        }
                    },
                contentAlignment = Alignment.BottomCenter,
            ) { Text(names[i], color = Color.DarkGray, modifier = Modifier.padding(bottom = 6.dp)) }
        }
    }
}
