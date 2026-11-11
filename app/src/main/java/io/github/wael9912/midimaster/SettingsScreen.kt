// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import io.github.wael9912.midimaster.NativeEngine.Stat
import kotlinx.coroutines.delay

/** The file pickers live in the activity (they need its result launchers). */
interface Importer {
    fun banks(); fun bankFolder(); fun styles(); fun styleFolder(); fun orgSet()
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SettingsScreen(app: AppState, midi: MidiConnector, openSound: (Int) -> Unit, onClose: () -> Unit) {
    val importer = app.importer
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        CompositionLocalProvider(LocalLayoutDirection provides if (app.lang == "ar") LayoutDirection.Rtl else LayoutDirection.Ltr) {
            Column(Modifier.fillMaxSize().background(Pal.bg).safeDrawingPadding()) {
                Row(Modifier.fillMaxWidth().background(Pal.panel).padding(horizontal = 10.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(app.t("settings"), color = Pal.text, fontSize = 18.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                    Chip("✕ ${app.t("close")}", onClick = onClose)
                }
                Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Card(app.t("keyboardDevice")) {
                        if (midi.devices.isEmpty()) Muted(app.t("midiNone"))
                        midi.devices.forEach { d ->
                            val on = d.info.id == midi.connectedId
                            Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).clickable { midi.connect(d) }.padding(8.dp),
                                verticalAlignment = Alignment.CenterVertically) {
                                Text(if (on) "●" else "○", color = if (on) Pal.green else Pal.muted)
                                Text("  ${d.name}", color = Pal.text, modifier = Modifier.weight(1f))
                                Muted(app.t(if (on) "connected" else "tapToConnect"))
                            }
                        }
                    }
                    Card(app.t("sounds")) {
                        for (b in app.banks.sortedBy { if (it.builtin) 99 else it.slot }) {
                            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                                Column(Modifier.weight(1f)) {
                                    Text(b.name + if (b.builtin) " (${app.t("builtinBank")})" else "", color = Pal.text)
                                    Muted("${b.presets.size} ${app.t("presets")} · ${b.file}")
                                }
                                if (!b.builtin) Chip("✕ ${app.t("remove")}") { app.removeBank(b.file) }
                            }
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            Btn(app.t("importSF2"), Modifier.weight(1f), small = true) { importer?.banks() }
                            Btn(app.t("importSF2Folder"), Modifier.weight(1f), small = true) { importer?.bankFolder() }
                        }
                        Muted(app.t("sf2Help"))
                    }
                    Card(app.t("yamahaStyles")) {
                        if (app.styFiles.isEmpty()) Muted(app.t("noSty"))
                        else {
                            val folders = app.styFiles.map { it.folder }.filter { it.isNotEmpty() }.distinct()
                            Text("${app.styFiles.size} ${app.t("rhythmFiles")}" + if (folders.isNotEmpty()) " (${app.t("fromFolder")} ${folders.joinToString(", ")})" else "",
                                color = Pal.text)
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            Btn(app.t("importSty"), Modifier.weight(1f), small = true) { importer?.styles() }
                            Btn(app.t("importStyFolder"), Modifier.weight(1f), small = true) { importer?.styleFolder() }
                        }
                        if (app.styFiles.isNotEmpty()) Btn(app.t("removeSty"), Modifier.fillMaxWidth(), small = true) { app.removeAllSty() }
                        Muted(app.t("yamahaHelp"))
                    }
                    Card(app.t("orgGroup")) {
                        val o = app.org
                        if (o == null) Muted(app.t("noOrg"))
                        else {
                            Text("${o.file} (v${o.version}) — ${o.instruments.size} ${app.t("instruments")}", color = Pal.text)
                            Muted(o.instruments.joinToString(", ") { it.first })
                            if (o.parts.isNotEmpty()) Muted("${app.t("rhythmParts")}: ${o.parts.distinct().joinToString(", ")}")
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            Btn(app.t("importOrg"), Modifier.weight(1f), small = true) { importer?.orgSet() }
                            if (o != null) Btn(app.t("removeOrg"), Modifier.weight(1f), small = true) { app.removeOrg() }
                        }
                        Muted(app.t("orgHelp"))
                    }
                    Card(app.t("rhythm")) {
                        Btn(app.t(if (app.showBuiltin) "showBuiltin" else "hideBuiltin"), Modifier.fillMaxWidth(), on = app.showBuiltin, small = true) {
                            app.showBuiltin = !app.showBuiltin; app.persist()
                        }
                        Text(app.t("partSounds"), color = Pal.muted, fontSize = 13.sp)
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            for (ch in listOf(AppState.BASS, AppState.CHORD, AppState.PAD, AppState.PHRASE, AppState.DRUMS, AppState.PERC)) {
                                Column(Modifier.width(160.dp).clip(RoundedCornerShape(8.dp)).background(Pal.panel2).border(1.dp, Pal.line, RoundedCornerShape(8.dp))
                                    .clickable { openSound(ch) }.padding(8.dp)) {
                                    Muted(app.t(trackLabelKey(ch)))
                                    Text(app.programLabel(ch), color = Pal.text, maxLines = 1)
                                }
                            }
                        }
                        app.currentStyle()?.let { st ->
                            Text(app.t("tracks"), color = Pal.muted, fontSize = 13.sp)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                                for (tr in st.tracks) {
                                    val label = app.t("${tr.role}Track").let { if (it.endsWith("Track")) tr.name else it }
                                    Btn("$label${if (tr.name.last().isDigit()) " " + tr.name.last() else ""}", on = app.mute[tr.name] != true, small = true) {
                                        app.toggleMute(tr.name)
                                    }
                                }
                            }
                        }
                    }
                    Card(app.t("mixer")) {
                        val b = app.bus
                        Level(app.t("keys"), b.keys) { app.updateBus(b.copy(keys = it)) }
                        Level(app.t("styleVol"), b.style) { app.updateBus(b.copy(style = it)) }
                        Level(app.t("drumsVol"), b.drums) { app.updateBus(b.copy(drums = it)) }
                        Level(app.t("reverb"), b.reverb, max = 1f) { app.updateBus(b.copy(reverb = it)) }
                        Level(app.t("master"), b.master, max = 1.5f) { app.updateBus(b.copy(master = it)) }
                    }
                    Card(app.t("settings")) {
                        Btn(app.t("keepAwake"), Modifier.fillMaxWidth(), on = app.keepAwake, small = true) { app.keepAwake = !app.keepAwake; app.persist() }
                        Btn(app.t("language"), Modifier.fillMaxWidth(), small = true) { app.lang = if (app.lang == "ar") "en" else "ar"; app.persist() }
                    }
                    AudioPathCard(app)
                    Card(app.t("about")) {
                        Text("${app.t("app")} ${BuildConfig.VERSION_NAME}", color = Pal.text, fontWeight = FontWeight.Bold)
                        Muted(app.t("aboutText"))
                        var showLic by remember { mutableStateOf(false) }
                        Btn(app.t("licences"), Modifier.fillMaxWidth(), small = true) { showLic = !showLic }
                        if (showLic) {
                            val ctx = LocalContext.current
                            val text = remember { runCatching { ctx.assets.open("licenses/NOTICES.txt").bufferedReader().readText() }.getOrDefault("") }
                            Text(text, color = Pal.muted, fontSize = 11.sp)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun Card(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Pal.panel).border(1.dp, Pal.line, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(title, color = Pal.accent, fontWeight = FontWeight.Bold, fontSize = 15.sp)
        content()
    }
}

@Composable
private fun Muted(s: String) = Text(s, color = Pal.muted, fontSize = 13.sp)

@Composable
private fun Level(label: String, value: Float, max: Float = 1.2f, onChange: (Float) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = Pal.text, fontSize = 14.sp, modifier = Modifier.width(90.dp))
        Slider(value = value, onValueChange = onChange, valueRange = 0f..max, modifier = Modifier.weight(1f),
            colors = SliderDefaults.colors(thumbColor = Pal.accent, activeTrackColor = Pal.accent, inactiveTrackColor = Pal.line))
        Text("${(value * 100).toInt()}", color = Pal.muted, fontSize = 12.sp, modifier = Modifier.width(36.dp))
    }
}

@Composable
private fun AudioPathCard(app: AppState) {
    var s by remember { mutableStateOf(DoubleArray(Stat.COUNT)) }
    LaunchedEffect(Unit) { while (true) { s = NativeEngine.stats(); delay(250) } }
    Card(app.t("audioPath")) {
        if (s[Stat.RUNNING] < 1) { Text(app.t("streamOff"), color = Color(0xFFFF7043)); return@Card }
        val sr = s[Stat.SAMPLE_RATE]
        fun ms(frames: Double) = if (sr > 0) "%.1f ms".format(frames * 1000 / sr) else "–"
        Line(app.t("sampleRate"), "${sr.toInt()} Hz")
        Line(app.t("burst"), "${s[Stat.BURST].toInt()} (${ms(s[Stat.BURST])})")
        Line(app.t("buffer"), "${s[Stat.BUFFER].toInt()} / ${s[Stat.CAPACITY].toInt()} (${ms(s[Stat.BUFFER])})")
        Line(app.t("mode"), listOf(if (s[Stat.LOW_LATENCY] > 0) "low-latency" else "NOT low-latency", if (s[Stat.EXCLUSIVE] > 0) "exclusive" else "shared",
            if (s[Stat.MMAP] > 0) "MMAP" else "legacy").joinToString(" · "))
        Line(app.t("outputLatency"), if (s[Stat.LATENCY_MS] >= 0) "%.1f ms".format(s[Stat.LATENCY_MS]) else "–")
        Line(app.t("midiHandoff"), "%.2f / %.2f ms".format(s[Stat.HANDOFF_AVG_MS], s[Stat.HANDOFF_MAX_MS]))
        Line(app.t("cpuPeak"), "${s[Stat.CPU_PEAK_PCT].toInt()} %")
        Line(app.t("xruns"), "${s[Stat.XRUNS].toInt()}")
        Line(app.t("voicesN"), "${s[Stat.VOICES].toInt()}")
        Line(app.t("midiMessages"), "${s[Stat.MIDI_MESSAGES].toLong()}")
        Line(app.t("memory"), "%.0f MB".format(app.banks.sumOf { NativeEngine.bankResident(it.handle) } / 1e6))
        Btn(app.t("resetPeaks"), small = true) { NativeEngine.resetPeaks() }
    }
}

@Composable
private fun Line(label: String, value: String) {
    Row(Modifier.fillMaxWidth()) {
        Text(label, color = Pal.muted, fontSize = 14.sp, modifier = Modifier.weight(1f))
        Text(value, color = Pal.text, fontSize = 14.sp)
    }
}
