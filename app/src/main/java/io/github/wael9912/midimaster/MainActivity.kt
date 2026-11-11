// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import android.net.Uri
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity(), Importer {
    private lateinit var midi: MidiConnector
    private lateinit var app: AppState

    private val pickBanks = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) app.importBanks(uris.map { it to null })
    }
    private val pickBankFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
        if (tree != null) treeFiles(tree) { files -> app.importBanks(files.map { it.first to it.second }) }
    }
    private val pickStyles = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) app.importSty(uris.map { Triple(it, nameOf(it), "") })
    }
    private val pickStyleFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
        if (tree != null) treeFiles(tree) { files -> app.importSty(files) }
    }
    private val pickOrg = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> if (uri != null) app.importOrg(uri) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        midi = MidiConnector(this)
        app = AppState.get(this)
        app.importer = this
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = Pal.accent, background = Pal.bg, surface = Pal.panel)) {
                // The screen reads the engine's state (held keys, chord, transport, beat) 30 times a second; what the
                // audio thread has let go of is freed here, off the audio thread.
                LaunchedEffect(Unit) {
                    var n = 0
                    var lastMidi = -1.0
                    while (true) {
                        app.poll()
                        if (++n % 8 == 0) NativeEngine.drainRetired()
                        if (n % 300 == 0) logStats(lastMidi).let { lastMidi = it }
                        delay(33)
                    }
                }
                LaunchedEffect(app.keepAwake) {
                    if (app.keepAwake) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
                MainScreen(app, midi)
            }
        }
    }

    // The stream lives while the app is in front: it stays open and warm, so the first key never waits for it.
    override fun onStart() {
        super.onStart()
        NativeEngine.start()
        app.onStreamStarted()
        app.startUp()
        midi.startWatching()
    }

    override fun onStop() {
        midi.stopWatching()
        NativeEngine.cmd(NativeEngine.Cmd.PANIC)
        NativeEngine.stop()
        app.persist()
        super.onStop()
    }

    override fun onDestroy() {
        if (app.importer === this) app.importer = null
        super.onDestroy()
    }

    /** One line in `adb logcat -s MidiMaster` every 10 s while the keyboard is played: the audio path, checked without the owner. */
    private fun logStats(lastMidi: Double): Double {
        val s = NativeEngine.stats()
        val S = NativeEngine.Stat
        if (s[S.MIDI_MESSAGES] != lastMidi) {
            android.util.Log.i("MidiMaster", "stats: midi %d, hand-off avg %.2f / max %.2f ms, buffer %d/%d, latency %.1f ms, xruns %d, cpu peak %d %%, voices %d, dropped %d".format(
                s[S.MIDI_MESSAGES].toLong(), s[S.HANDOFF_AVG_MS], s[S.HANDOFF_MAX_MS], s[S.BUFFER].toInt(), s[S.BURST].toInt(), s[S.LATENCY_MS],
                s[S.XRUNS].toInt(), s[S.CPU_PEAK_PCT].toInt(), s[S.VOICES].toInt(), s[S.DROPPED].toInt()))
        }
        return s[S.MIDI_MESSAGES]
    }

    // ── Importer ──
    override fun banks() = pickBanks.launch(arrayOf("*/*"))
    override fun bankFolder() = pickBankFolder.launch(null)
    override fun styles() = pickStyles.launch(arrayOf("*/*"))
    override fun styleFolder() = pickStyleFolder.launch(null)
    override fun orgSet() = pickOrg.launch(arrayOf("*/*"))

    private fun nameOf(uri: Uri): String = contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
        ?.use { c -> if (c.moveToFirst()) c.getString(0) else null } ?: (uri.lastPathSegment ?: "file")

    private fun treeFiles(tree: Uri, then: (List<Triple<Uri, String, String>>) -> Unit) {
        lifecycleScope.launch {
            val files = withContext(Dispatchers.IO) { app.listTree(tree) }
            then(files)
        }
    }
}
