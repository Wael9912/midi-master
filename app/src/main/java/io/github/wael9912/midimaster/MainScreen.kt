// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

object Pal {
    val bg = Color(0xFF14110F); val panel = Color(0xFF1F1A16); val panel2 = Color(0xFF2A231E); val line = Color(0xFF3A312A)
    val text = Color(0xFFF3EBE2); val muted = Color(0xFFA89A8C); val accent = Color(0xFFE0A63A); val accent2 = Color(0xFFC9452C)
    val green = Color(0xFF4FB37A); val dark = Color(0xFF1A1208)
}

sealed class Sheet {
    object Rhythm : Sheet()
    data class Sound(val ch: Int) : Sheet()
}

@Composable
fun MainScreen(app: AppState, midi: MidiConnector) {
    var sheet by remember { mutableStateOf<Sheet?>(null) }
    var settingsOpen by remember { mutableStateOf(false) }
    val rtl = app.lang == "ar"
    CompositionLocalProvider(LocalLayoutDirection provides if (rtl) LayoutDirection.Rtl else LayoutDirection.Ltr) {
        Box(Modifier.fillMaxSize().background(Pal.bg).safeDrawingPadding()) {
            Column(Modifier.fillMaxSize()) {
                TopBar(app, midi) { settingsOpen = true }
                app.preparing?.let { p ->
                    Column(Modifier.fillMaxWidth().background(Pal.panel2).padding(horizontal = 12.dp, vertical = 6.dp)) {
                        Text("${app.t("preparing")} ${(p * 100).toInt()} %", color = Pal.text, fontSize = 13.sp)
                        LinearProgressIndicator(progress = { p }, modifier = Modifier.fillMaxWidth().padding(top = 4.dp), color = Pal.accent, trackColor = Pal.line)
                    }
                }
                app.busy?.let { Text(it, color = Pal.muted, fontSize = 13.sp, modifier = Modifier.fillMaxWidth().background(Pal.panel2).padding(horizontal = 12.dp, vertical = 4.dp)) }
                BoxWithConstraints(Modifier.weight(1f).fillMaxWidth()) {
                    val wide = maxWidth > 700.dp
                    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 10.dp, vertical = 8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        if (wide) {
                            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) { RhythmPanel(app) { sheet = Sheet.Rhythm } }
                                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) { HandsPanel(app) { sheet = it } }
                            }
                        } else {
                            RhythmPanel(app) { sheet = Sheet.Rhythm }
                            HandsPanel(app) { sheet = it }
                        }
                    }
                }
                KeysRow(app)
            }
            app.toastText?.let { msg ->
                Box(Modifier.align(Alignment.BottomCenter).padding(bottom = 150.dp, start = 16.dp, end = 16.dp)
                    .clip(RoundedCornerShape(10.dp)).background(Color(0xEE2A231E)).border(1.dp, Pal.line, RoundedCornerShape(10.dp))
                    .padding(horizontal = 14.dp, vertical = 10.dp)) { Text(msg, color = Pal.text, fontSize = 14.sp, textAlign = TextAlign.Center) }
            }
        }
        sheet?.let { s -> ListSheet(app, s) { sheet = null } }
        if (settingsOpen) SettingsScreen(app, midi, openSound = { sheet = Sheet.Sound(it) }) { settingsOpen = false }
    }
}

@Composable
private fun TopBar(app: AppState, midi: MidiConnector, onSettings: () -> Unit) {
    Row(Modifier.fillMaxWidth().background(Pal.panel).padding(horizontal = 10.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(app.t("app"), color = Pal.accent, fontWeight = FontWeight.Bold, fontSize = 18.sp, modifier = Modifier.weight(1f), maxLines = 1)
        val dev = midi.devices.firstOrNull { it.info.id == midi.connectedId }
        Chip(dev?.name ?: app.t("midiNone"), dot = if (dev != null) Pal.green else Color(0xFF666666), modifier = Modifier.widthIn(max = 150.dp)) {
            midi.devices.firstOrNull()?.let { midi.connect(it) }
        }
        Chip(app.t("language")) { app.lang = if (app.lang == "ar") "en" else "ar"; app.persist() }
        Chip("⚙", onClick = onSettings)
    }
}

@Composable
fun Chip(label: String, modifier: Modifier = Modifier, dot: Color? = null, on: Boolean = false, onClick: () -> Unit) {
    Row(modifier.heightIn(min = 36.dp).clip(RoundedCornerShape(50)).background(if (on) Pal.accent else Pal.panel2)
        .border(1.dp, if (on) Pal.accent else Pal.line, RoundedCornerShape(50)).clickable(onClick = onClick).padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        dot?.let { Box(Modifier.size(8.dp).clip(CircleShape).background(it)) }
        Text(label, color = if (on) Pal.dark else Pal.text, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
fun Btn(label: String, modifier: Modifier = Modifier, on: Boolean = false, pending: Boolean = false, small: Boolean = false, minHeight: Dp = 44.dp,
        onClick: () -> Unit) {
    val bg = when { pending -> Pal.accent; on -> Pal.green; else -> Pal.panel2 }
    val fg = if (on || pending) Pal.dark else Pal.text
    Box(modifier.heightIn(min = minHeight).clip(RoundedCornerShape(10.dp)).background(bg).border(1.dp, if (on || pending) bg else Pal.line, RoundedCornerShape(10.dp))
        .clickable(onClick = onClick).padding(horizontal = 8.dp, vertical = 6.dp), contentAlignment = Alignment.Center) {
        Text(label, color = fg, fontSize = if (small) 13.sp else 15.sp, fontWeight = FontWeight.SemiBold, textAlign = TextAlign.Center, maxLines = 2)
    }
}

@Composable
private fun PickerRow(label: String, name: String, meta: String, onPrev: () -> Unit, onNext: () -> Unit, onOpen: () -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
        Btn("‹", Modifier.width(52.dp).height(64.dp), onClick = onPrev)
        Column(Modifier.weight(1f).heightIn(min = 64.dp).clip(RoundedCornerShape(10.dp)).background(Pal.panel).border(1.dp, Pal.line, RoundedCornerShape(10.dp))
            .clickable(onClick = onOpen).padding(horizontal = 12.dp, vertical = 6.dp), verticalArrangement = Arrangement.Center) {
            Text(label, color = Pal.muted, fontSize = 12.sp)
            Text(name, color = Pal.text, fontSize = 18.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (meta.isNotEmpty()) Text(meta, color = Pal.muted, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Btn("›", Modifier.width(52.dp).height(64.dp), onClick = onNext)
    }
}

@Composable
private fun RhythmPanel(app: AppState, openList: () -> Unit) {
    val st = app.currentStyle()
    val group = when (st?.group) {
        "yamaha" -> st.folder.ifEmpty { app.t("yourRhythms") }
        "org" -> app.t("orgGroup")
        "sudan" -> app.t("sudanese")
        else -> app.t("western")
    }
    PickerRow(app.t("rhythm"), st?.let { if (app.lang == "ar") it.nameAr else it.name } ?: "—", st?.let { "$group · ${it.meterNum}/${it.meterDen}" } ?: "",
        onPrev = { app.stepStyle(-1) }, onNext = { app.stepStyle(1) }, onOpen = openList)
    val playing = app.playing
    val p = if (playing) app.part else -1
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Btn(app.t("intro"), Modifier.weight(1f), on = p == 0) { app.intro() }
        Btn(app.t("mainA"), Modifier.weight(1f), on = if (playing) p == 1 else app.variation == 0,
            pending = app.pendingVariation == 0 && app.variation != 0) { app.variation(0) }
        Btn(app.t("mainB"), Modifier.weight(1f), on = if (playing) p == 2 else app.variation == 1,
            pending = app.pendingVariation == 1 && app.variation != 1) { app.variation(1) }
        Btn(app.t("fill"), Modifier.weight(1f), on = p == 3 || p == 4, pending = app.pending == 1) { app.fill() }
        Btn(app.t("ending"), Modifier.weight(1f), on = p == 5, pending = app.pending == 2) { app.ending() }
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        StartButton(app, Modifier.weight(1.4f))
        Column(Modifier.weight(1f)) {
            Text(app.t("tempo"), color = Pal.muted, fontSize = 12.sp)
            Stepper(app.tempoLabel(), { app.tempoStep(-1) }, { app.tempoStep(1) })
        }
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Btn(app.t("sync"), Modifier.weight(1f), on = app.syncArmed, small = true) { app.toggleSync() }
        Btn(app.t(if (app.chordMode == "single") "single" else "fingered"), Modifier.weight(1f), small = true) { app.toggleChordMode() }
    }
}

@Composable
fun StartButton(app: AppState, modifier: Modifier = Modifier, compact: Boolean = false) {
    val playing = app.playing
    val bg = if (playing) Pal.green else Pal.accent2
    Column(modifier.heightIn(min = if (compact) 40.dp else 72.dp).clip(RoundedCornerShape(12.dp)).background(bg).clickable { app.startStop() }.padding(6.dp),
        horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        Text(app.t(if (playing) "stop" else "start"), color = if (playing) Color(0xFF06110A) else Color.White, fontSize = if (compact) 15.sp else 22.sp,
            fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
        if (!compact) {
            val st = app.currentStyle()
            val pulses = if (st == null) 4 else if (st.meterDen == 8) maxOf(1, st.meterNum / 3) else st.meterNum
            val bpb = st?.beatsPerBar ?: 4.0
            val beats = app.engineState[6] / 1000.0
            val barFrac = (beats % bpb) / bpb
            val pulse = (barFrac * pulses).toInt()
            val pfrac = barFrac * pulses - pulse
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(top = 4.dp)) {
                for (i in 0 until pulses) {
                    val lit = playing && i == pulse && pfrac < 0.5
                    Box(Modifier.size(10.dp).clip(CircleShape).background(if (lit) (if (i == 0) Pal.accent else Color.White) else Color(0x55000000)))
                }
            }
        }
    }
}

@Composable
fun Stepper(value: String, onMinus: () -> Unit, onPlus: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Btn("−", Modifier.width(46.dp), onClick = onMinus)
        Text(value, color = Pal.text, fontSize = 17.sp, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center, modifier = Modifier.widthIn(min = 56.dp))
        Btn("+", Modifier.width(46.dp), onClick = onPlus)
    }
}

private val NOTE_NAMES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
fun noteLabel(n: Int) = NOTE_NAMES[n % 12] + (n / 12 - 1)

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun HandsPanel(app: AppState, open: (Sheet) -> Unit) {
    PickerRow(app.t("voice"), app.programLabel(AppState.UPPER), app.programMeta(AppState.UPPER),
        onPrev = { app.stepSound(AppState.UPPER, -1) }, onNext = { app.stepSound(AppState.UPPER, 1) }, onOpen = { open(Sheet.Sound(AppState.UPPER)) })
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Column(Modifier.weight(1f).clip(RoundedCornerShape(10.dp)).background(Pal.panel).padding(horizontal = 12.dp, vertical = 6.dp)) {
            Text(app.t("chord"), color = Pal.muted, fontSize = 12.sp)
            Text(app.chordLabel, color = Pal.accent, fontSize = 22.sp, fontWeight = FontWeight.Bold)
        }
        Column {
            Text(app.t("transpose"), color = Pal.muted, fontSize = 12.sp)
            Stepper((if (app.transpose > 0) "+" else "") + app.transpose, { app.changeTranspose(app.transpose - 1) }, { app.changeTranspose(app.transpose + 1) })
        }
    }
    Btn("${app.t("more")} ${if (app.moreOpen) "▴" else "▾"}", Modifier.fillMaxWidth(), on = app.moreOpen, minHeight = 40.dp) {
        app.moreOpen = !app.moreOpen; app.persist()
    }
    if (app.moreOpen) {
        PickerRow(app.t("lower"), app.programLabel(AppState.LOWER), app.programMeta(AppState.LOWER),
            onPrev = { app.stepSound(AppState.LOWER, -1) }, onNext = { app.stepSound(AppState.LOWER, 1) }, onOpen = { open(Sheet.Sound(AppState.LOWER)) })
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Btn(app.t("lowerOn"), Modifier.weight(1f), on = app.lowerOn, small = true) { app.toggleLowerOn() }
            Btn(app.t("layer"), Modifier.weight(1f), on = app.layer, small = true) { app.toggleLayer() }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(app.t("split"), color = Pal.muted, fontSize = 13.sp, modifier = Modifier.weight(1f))
            Stepper(noteLabel(app.split), { app.changeSplit(app.split - 1) }, { app.changeSplit(app.split + 1) })
        }
        Text(app.t("registrations"), color = Pal.muted, fontSize = 13.sp)
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            for (i in 0 until 8) {
                val filled = app.regs[i] != null
                val active = app.activeReg == i
                Box(Modifier.weight(1f).height(44.dp).clip(RoundedCornerShape(8.dp))
                    .background(if (active) Pal.accent else if (filled) Pal.panel2 else Pal.panel)
                    .border(1.dp, if (filled) Pal.accent else Pal.line, RoundedCornerShape(8.dp))
                    .combinedClickable(onClick = { app.loadReg(i) }, onLongClick = { app.saveReg(i) }), contentAlignment = Alignment.Center) {
                    Text("${i + 1}", color = if (active) Pal.dark else Pal.text, fontWeight = FontWeight.Bold)
                }
            }
        }
        Text(app.t("holdToSave"), color = Pal.muted, fontSize = 12.sp)
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Btn(app.t("panic"), Modifier.weight(1f), small = true) { app.panic() }
            Btn(app.t(if (app.keysShown) "hideKeys" else "showKeys"), Modifier.weight(1f), small = true) { app.keysShown = !app.keysShown; app.persist() }
        }
    }
}

@Composable
private fun KeysRow(app: AppState) {
    Column(Modifier.fillMaxWidth().background(Pal.bg).padding(horizontal = 10.dp, vertical = 4.dp)) {
        Text(app.t("hint"), color = Pal.muted, fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
        if (app.keysShown) {
            Spacer(Modifier.height(4.dp))
            BoxWithConstraints(Modifier.fillMaxWidth()) {
                val h = if (maxWidth > 700.dp) 150.dp else 120.dp
                Keyboard(app, Modifier.fillMaxWidth().height(h))
            }
        }
    }
}

/** C3–C5. Several fingers at once; sliding a finger plays the keys it crosses. Always left-to-right, also in Arabic. */
@Composable
fun Keyboard(app: AppState, modifier: Modifier) {
    val low = 48; val high = 72
    val whites = (low..high).filter { it % 12 !in intArrayOf(1, 3, 6, 8, 10) }
    val blacks = (low..high).filter { it % 12 in intArrayOf(1, 3, 6, 8, 10) }
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
        BoxWithConstraints(modifier.clip(RoundedCornerShape(8.dp))
            .pointerInput(Unit) {
                val active = HashMap<PointerId, Int>()
                fun noteAt(p: Offset): Int? {
                    val w = size.width.toFloat(); val h = size.height.toFloat()
                    if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h) return null
                    val ww = w / whites.size
                    if (p.y < h * 0.6f) for (n in blacks) {
                        val idx = whites.count { it < n }
                        val left = idx * ww - ww * 0.3f
                        if (p.x >= left && p.x < left + ww * 0.6f) return n
                    }
                    return whites[(p.x / ww).toInt().coerceIn(0, whites.size - 1)]
                }
                awaitPointerEventScope {
                    while (true) {
                        val ev = awaitPointerEvent()
                        for (c in ev.changes) {
                            val prev = active[c.id]
                            if (c.pressed) {
                                val n = noteAt(c.position)
                                if (n != prev) {
                                    prev?.let { app.keyOff(it) }
                                    if (n != null) { app.keyOn(n); active[c.id] = n } else active.remove(c.id)
                                }
                                c.consume()
                            } else if (prev != null) {
                                app.keyOff(prev)
                                active.remove(c.id)
                            }
                        }
                    }
                }
            }) {
            val ww = maxWidth / whites.size
            Row(Modifier.fillMaxSize()) {
                for (n in whites) {
                    val held = app.isHeld(n)
                    val lower = n < app.split
                    Box(Modifier.weight(1f).fillMaxHeight().background(if (held) Pal.accent else if (lower) Color(0xFFE8DDD0) else Color(0xFFF4EFE6))
                        .border(0.5.dp, Color(0xFFBFB3A3)), contentAlignment = Alignment.BottomCenter) {
                        if (n % 12 == 0) Text(noteLabel(n), color = Color(0xFF777766), fontSize = 10.sp, modifier = Modifier.padding(bottom = 4.dp))
                        if (n == app.split) Box(Modifier.align(Alignment.CenterStart).width(3.dp).fillMaxHeight().background(Pal.accent2))
                    }
                }
            }
            for (n in blacks) {
                val idx = whites.count { it < n }
                Box(Modifier.offset(x = ww * idx - ww * 0.3f).width(ww * 0.6f).fillMaxHeight(0.6f).clip(RoundedCornerShape(bottomStart = 4.dp, bottomEnd = 4.dp))
                    .background(if (app.isHeld(n)) Pal.accent2 else Color(0xFF1C1917)))
                if (n == app.split) Box(Modifier.offset(x = ww * idx - ww * 0.3f).width(3.dp).fillMaxHeight().background(Pal.accent2))
            }
        }
    }
}

