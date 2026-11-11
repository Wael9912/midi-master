// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
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
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties

/**
 * The full-screen list behind a picker: a search box, group headers, the current item marked. A tap picks and the
 * list stays open, so several can be tried in a row. The rhythm list carries its own START / STOP.
 */
@Composable
fun ListSheet(app: AppState, sheet: Sheet, onClose: () -> Unit) {
    var query by remember { mutableStateOf("") }
    val rhythm = sheet is Sheet.Rhythm
    val ch = (sheet as? Sheet.Sound)?.ch ?: 0
    val groups = if (rhythm) {
        Catalog.styleGroups(app.orderedStyles(), app.lang, mapOf("yamaha" to app.t("yourRhythms"), "org" to app.t("orgGroup"),
            "sudan" to app.t("sudanese"), "west" to app.t("western")))
    } else {
        Catalog.soundGroups(app.programList(ch == AppState.DRUMS || ch == AppState.PERC), app.lang, app.soundLabels())
    }
    val active = if (rhythm) app.styleId else app.programs[ch]
    val shown = Catalog.filterGroups(groups, query)
    val title = when {
        rhythm -> app.t("rhythm")
        ch == AppState.UPPER -> app.t("voice")
        ch == AppState.LOWER -> app.t("lower")
        else -> "${app.t("soundFor")}: ${app.t(trackLabelKey(ch))}"
    }
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        CompositionLocalProvider(LocalLayoutDirection provides if (app.lang == "ar") LayoutDirection.Rtl else LayoutDirection.Ltr) {
            Column(Modifier.fillMaxSize().background(Pal.bg).safeDrawingPadding()) {
                Row(Modifier.fillMaxWidth().background(Pal.panel).padding(horizontal = 10.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(title, color = Pal.text, fontSize = 18.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f), maxLines = 1,
                        overflow = TextOverflow.Ellipsis)
                    if (rhythm) StartButton(app, Modifier.width(110.dp), compact = true)
                    Chip("✕ ${app.t("close")}", onClick = onClose)
                }
                OutlinedTextField(value = query, onValueChange = { query = it }, singleLine = true,
                    placeholder = { Text(app.t(if (rhythm) "searchRhythms" else "searchSounds"), color = Pal.muted) },
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 6.dp),
                    colors = OutlinedTextFieldDefaults.colors(focusedTextColor = Pal.text, unfocusedTextColor = Pal.text, focusedBorderColor = Pal.accent,
                        unfocusedBorderColor = Pal.line, cursorColor = Pal.accent))
                val state = rememberLazyListState()
                // Open on the current item.
                LaunchedEffect(Unit) {
                    var idx = 0
                    loop@ for (g in shown) {
                        idx++
                        for (it in g.items) { if (it.key == active) break@loop; idx++ }
                    }
                    if (idx > 3) state.scrollToItem(idx - 2)
                }
                if (shown.isEmpty()) Text(app.t("noMatch"), color = Pal.muted, modifier = Modifier.padding(16.dp))
                LazyColumn(state = state, modifier = Modifier.weight(1f).fillMaxWidth()) {
                    for (g in shown) {
                        item(key = "h:" + g.key) {
                            Text(g.title, color = Pal.accent, fontSize = 13.sp, fontWeight = FontWeight.Bold,
                                modifier = Modifier.fillMaxWidth().background(Pal.panel).padding(horizontal = 14.dp, vertical = 6.dp))
                        }
                        items(g.items, key = { g.key + "|" + it.key }) { it ->
                            val on = it.key == active
                            Row(Modifier.fillMaxWidth().background(if (on) Pal.panel2 else Pal.bg)
                                .clickable { if (rhythm) app.selectStyle(it.key) else app.pickSound(ch, it.key) }
                                .padding(horizontal = 14.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                                Text(if (on) "●  " else "    ", color = Pal.accent)
                                Text(it.label, color = if (on) Pal.accent else Pal.text, fontSize = 16.sp, modifier = Modifier.weight(1f), maxLines = 1,
                                    overflow = TextOverflow.Ellipsis)
                                if (it.meta.isNotEmpty()) Text(it.meta, color = Pal.muted, fontSize = 12.sp)
                            }
                        }
                    }
                }
            }
        }
    }
}

fun trackLabelKey(ch: Int) = when (ch) {
    AppState.BASS -> "bassTrack"; AppState.CHORD, 6 -> "chordTrack"; AppState.PAD -> "padTrack"; AppState.PHRASE, 7 -> "phraseTrack"
    AppState.DRUMS -> "drumsTrack"; AppState.PERC -> "percTrack"; else -> "voice"
}
