// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.wael9912.midimaster.NativeEngine.Cmd
import io.github.wael9912.midimaster.NativeEngine.Prog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/** One loaded SF2 bank: slot 0 is the bundled FluidR3Mono, imported banks take 1–15. */
data class BankInfo(val slot: Int, val file: String, val path: String, val name: String, val presets: List<Triple<Int, Int, String>>,
                    val builtin: Boolean, val handle: Long)

data class StyFile(val path: String, val folder: String, val name: String)
data class OrgInfo(val file: String, val path: String, val version: String, val instruments: List<Pair<String, Boolean>>, val parts: List<String>, val handle: Long)
data class Bus(val keys: Float = 0.8f, val style: Float = 0.8f, val drums: Float = 0.8f, val master: Float = 0.9f, val reverb: Float = 0.5f)

/** A saved setup (the web app's "registration"). */
data class Reg(val styleId: String, val tempo: Int, val transpose: Int, val split: Int, val chordMode: String, val lowerOn: Boolean,
               val layer: Boolean, val programs: Map<Int, String>, val bus: Bus, val mute: Map<String, Boolean>)

/**
 * The app controller: what legacy-web/js/app.js did, minus the page. It owns the settings, the library (banks,
 * rhythm files, the ORG set, the compiled styles) and turns taps into engine commands. Loading runs on one worker
 * thread; engine commands are sent from the main thread, in order (one job at a time).
 */
class AppState(private val ctx: Context) {
    companion object {
        @Volatile private var instance: AppState? = null
        fun get(ctx: Context): AppState = instance ?: synchronized(this) { instance ?: AppState(ctx.applicationContext).also { instance = it } }

        const val UPPER = 0; const val LOWER = 1; const val BASS = 2; const val CHORD = 3; const val PAD = 4; const val PHRASE = 5
        const val DRUMS = 9; const val PERC = 10
        val ROLE_CH = linkedMapOf("upper" to UPPER, "lower" to LOWER, "bass" to BASS, "chord" to CHORD, "pad" to PAD, "phrase" to PHRASE)
        // GM programs to prefer for each built-in voice name when a bank has them.
        val GM_FOR = mapOf("piano" to 0, "organ" to 16, "accordion" to 21, "oud" to 24, "tambour" to 105, "strings" to 48,
            "brass" to 61, "sax" to 65, "flute" to 73, "bass" to 33, "lead" to 81)
        val BUILTIN = listOf(
            "lead" to ("Synth lead" to "ليد"), "oud" to ("Oud" to "عود"), "tambour" to ("Rababa / Tambour" to "طمبور"),
            "accordion" to ("Accordion" to "أكورديون"), "sax" to ("Sax" to "ساكس"), "strings" to ("Strings" to "وتريات"),
            "brass" to ("Brass" to "نفخ"), "piano" to ("Piano" to "بيانو"), "organ" to ("Organ" to "أورغ"), "bass" to ("Bass" to "باص"),
            "flute" to ("Flute" to "ناي"), "epiano" to ("Electric piano" to "بيانو كهربائي"),
        )
        const val BUNDLED_FILE = "FluidR3Mono_GM.sf2"
        const val BUNDLED_VERSION = 1  // bump when the bundled bank or its conversion changes
        private const val TAG = "MidiMaster"
        val STY_EXT = Regex("\\.(sty|prs|sst|bcs|pst)$", RegexOption.IGNORE_CASE)
        val BANK_EXT = Regex("\\.(sf2|sf3)$", RegexOption.IGNORE_CASE)
    }

    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "mm-loader").apply { priority = Thread.MIN_PRIORITY } }
        .asCoroutineDispatcher()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val serial = Mutex()
    private fun job(block: suspend () -> Unit) = scope.launch { serial.withLock { try { block() } catch (e: Exception) { Log.e(TAG, "job failed", e); toast("${t("error")}: ${e.message}") } } }
    private suspend fun <T> onWorker(f: () -> T): T = withContext(worker) { f() }

    // ───────── settings (persisted) ─────────
    var lang by mutableStateOf("en")
    var styleId by mutableStateOf("tumtum")
    var transpose by mutableIntStateOf(0)
    var split by mutableIntStateOf(54)
    var chordMode by mutableStateOf("fingered")
    var lowerOn by mutableStateOf(false)
    var layer by mutableStateOf(false)
    var keysShown by mutableStateOf(true)
    var keepAwake by mutableStateOf(true)
    var showBuiltin by mutableStateOf(true)
    var moreOpen by mutableStateOf(false)
    var bus by mutableStateOf(Bus())
    val programs = mutableStateMapOf<Int, String>()
    val mute = mutableStateMapOf<String, Boolean>()
    val regs = mutableStateListOf<Reg?>().apply { repeat(8) { add(null) } }
    var activeReg by mutableIntStateOf(-1)
    var orgTempo = 165
    private var seenHint = false
    private var savedTempo = 0
    private var bundledVersion = 0
    private var bankVoices = false  // the rhythm's sounds were picked once with the bundled bank present

    // ───────── library ─────────
    var banks by mutableStateOf<List<BankInfo>>(emptyList()); private set
    var styFiles by mutableStateOf<List<StyFile>>(emptyList()); private set
    var org by mutableStateOf<OrgInfo?>(null); private set
    var styles by mutableStateOf<List<StyleMeta>>(emptyList()); private set
    private val styleSources = LinkedHashMap<String, List<StyleMeta>>()

    // ───────── live ─────────
    var engineState by mutableStateOf(IntArray(9)); private set
    var preparing by mutableStateOf<Float?>(null); private set
    var busy by mutableStateOf<String?>(null); private set
    var toastText by mutableStateOf<String?>(null); private set
    var ready by mutableStateOf(false); private set
    private var toastSeq = 0

    var importer: Importer? = null

    fun t(key: String) = Strings.t(lang, key)

    fun toast(msg: String, ms: Long = 2500) {
        val id = ++toastSeq
        toastText = msg
        scope.launch { delay(ms); if (toastSeq == id) toastText = null }
    }

    // ───────── live engine state, polled by the screen ─────────
    fun poll() { engineState = NativeEngine.state() }
    val playing get() = engineState[5] and 1 == 1
    val part get() = (engineState[5] shr 1) and 7
    val pending get() = (engineState[5] shr 4) and 3
    val pendingVariation get() = ((engineState[5] shr 6) and 3) - 1
    val variation get() = (engineState[5] shr 8) and 1
    val syncArmed get() = (engineState[5] shr 9) and 1 == 1
    val engineTempo get() = engineState[7]
    fun isHeld(note: Int) = note in 0..127 && (engineState[note shr 5] ushr (note and 31)) and 1 == 1
    val chordLabel: String get() {
        val c = engineState[4]
        if (c == 0) return "—"
        val names = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
        val labels = arrayOf("", "m", "7", "maj7", "m7", "sus4", "dim", "aug")
        return names[(c and 255) - 1] + labels[(c shr 8).coerceIn(0, 7)]
    }

    // ───────── start-up ─────────
    private var started = false
    private val filesDir get() = ctx.filesDir

    fun startUp() {
        if (started) return
        started = true
        loadSettings()
        if (!seenHint) { seenHint = true; scope.launch { delay(800); toast(t("firstHint"), 6000) } }
        job {
            sendKeyboardSettings()
            sendMixer()
            // Rhythms first: the built-in sounds already play while the banks load.
            val pack = onWorker { ctx.assets.open("styles.json").bufferedReader().readText() }
            addStyleSource("builtin", onWorker { NativeEngine.stylesAddPack("builtin", pack) }, "")
            for (f in styFiles) {
                val r = onWorker { NativeEngine.stylesAddSty("sty:${f.path}", f.path, styId(f), f.name) }
                if (!r.startsWith("!")) addStyleSource("sty:${f.path}", r, f.folder)
            }
            org?.let { o -> openOrg(o.path, o.file, select = false) }
            rebuildStyles()
            val firstRun = programs.isEmpty()
            selectStyleNow(styleId, keepPrograms = !firstRun)
            if (savedTempo > 0) NativeEngine.cmd(Cmd.TEMPO, savedTempo)
            // Imported banks, then the bundled one (converted from SF3 on the first start only).
            val files = settingsBanks
            for (file in files) {
                val path = File(File(filesDir, "banks"), file).path
                if (!File(path).exists()) continue
                openBank(path, file, builtin = false)
            }
            ensureBundled()
            if (firstRun || !bankVoices) {
                currentStyle()?.let { applyStyleVoices(it) }
                bankVoices = banks.any { it.builtin }
            } else restorePrograms()
            ready = true
            persist()
        }
    }

    private var settingsBanks: List<String> = emptyList()

    private suspend fun ensureBundled() {
        val dir = File(filesDir, "bundled").apply { mkdirs() }
        val sf2 = File(dir, BUNDLED_FILE)
        if (!sf2.exists() || bundledVersion != BUNDLED_VERSION) {
            preparing = 0f
            val ok = onWorker {
                val bytes = ctx.assets.open("FluidR3Mono_GM.sf3").use { it.readBytes() }
                Sf3Converter.convert(bytes, sf2) { p -> scope.launch { preparing = p } }
            }
            preparing = null
            if (!ok) { toast("${t("error")}: FluidR3Mono", 6000); return }
            bundledVersion = BUNDLED_VERSION
            persist()
        }
        openBank(sf2.path, BUNDLED_FILE, builtin = true)
    }

    /** Opens a bank, loads the samples of the programs that use it, then hands it to the engine. */
    private suspend fun openBank(path: String, file: String, builtin: Boolean): Boolean {
        val handle = onWorker { NativeEngine.bankOpen(path) }
        if (handle == 0L) { toast("${t("error")}: $file — ${errorText(NativeEngine.lastError())}", 6000); return false }
        val info = JSONObject(onWorker { NativeEngine.bankInfo(handle) })
        val pa = info.getJSONArray("presets")
        val presets = (0 until pa.length()).map { pa.getJSONArray(it).let { r -> Triple(r.getInt(0), r.getInt(1), r.getString(2)) } }
        val existing = banks.firstOrNull { it.file == file }
        val slot = when {
            builtin -> 0
            existing != null -> existing.slot
            else -> (1..15).firstOrNull { s -> banks.none { it.slot == s } } ?: run { NativeEngine.bankFree(handle); toast("16 banks max"); return false }
        }
        val name = if (builtin) "FluidR3 Mono" else info.optString("name").ifBlank { file.substringBeforeLast('.') }
        val bank = BankInfo(slot, file, path, name, presets, builtin, handle)
        // Samples first, so the engine never meets a preset with nothing loaded.
        onWorker { for ((ch, key) in programs.toMap()) parseKey(key)?.let { if (it.kind == "sf2" && it.bankFile == file) NativeEngine.bankLoadPreset(handle, it.bank, it.program) } }
        NativeEngine.bankAttach(slot, handle)  // replaces (and retires) a bank in the same slot
        banks = banks.filter { it.slot != slot } + bank
        return true
    }

    // ───────── styles ─────────
    private fun styId(f: StyFile) = "sty:" + (if (f.folder.isNotEmpty()) f.folder + "/" else "") + f.name

    private fun addStyleSource(source: String, json: String, folder: String) {
        if (json.startsWith("!")) { Log.w(TAG, "style source $source: $json"); return }
        val a = JSONArray(json)
        styleSources[source] = (0 until a.length()).map { parseStyle(a.getJSONObject(it), folder) }
    }

    private fun parseStyle(o: JSONObject, folder: String): StyleMeta {
        val tr = o.getJSONArray("tracks")
        val voices = o.getJSONObject("voices")
        val gm = o.getJSONObject("gm")
        return StyleMeta(
            id = o.getString("id"), name = o.getString("name"), nameAr = o.getString("nameAr"), group = o.getString("group"),
            tempo = o.getInt("tempo"), meterNum = o.getJSONArray("meter").getInt(0), meterDen = o.getJSONArray("meter").getInt(1),
            beatsPerBar = o.getDouble("beatsPerBar"),
            tracks = (0 until tr.length()).map { tr.getJSONArray(it).let { r -> TrackMeta(r.getString(0), r.getString(1), r.getInt(2), r.getInt(3)) } },
            voices = voices.keys().asSequence().associateWith { voices.getString(it) },
            gm = gm.keys().asSequence().associate { it.toInt() to gm.getInt(it) },
            folder = folder,
        )
    }

    private fun rebuildStyles() { styles = styleSources.values.flatten() }

    fun orderedStyles(): List<StyleMeta> = Catalog.orderStyles(styles, showBuiltin).ifEmpty { Catalog.orderStyles(styles, true) }
    fun currentStyle(): StyleMeta? = styles.firstOrNull { it.id == styleId } ?: styles.firstOrNull()

    fun selectStyle(id: String) = job { selectStyleNow(id, keepPrograms = false) }

    private suspend fun selectStyleNow(id: String, keepPrograms: Boolean) {
        val st = styles.firstOrNull { it.id == id } ?: orderedStyles().firstOrNull() ?: return
        styleId = st.id
        NativeEngine.styleSelect(st.id)
        sendMuteMask(st)
        if (!keepPrograms) applyStyleVoices(st)
        persist()
    }

    fun stepStyle(dir: Int) { Catalog.stepStyle(orderedStyles(), styleId, dir)?.let { selectStyle(it.id) } }

    private fun sendMuteMask(st: StyleMeta) {
        var lo = 0; var hi = 0
        st.tracks.forEachIndexed { i, tr -> if (mute[tr.name] == true) { if (i < 32) lo = lo or (1 shl i) else if (i < 64) hi = hi or (1 shl (i - 32)) } }
        NativeEngine.cmd(Cmd.MUTE_MASK, lo, hi)
    }

    fun toggleMute(track: String) {
        mute[track] = !(mute[track] ?: false)
        currentStyle()?.let { sendMuteMask(it) }
        persist()
    }

    // ───────── sounds ─────────
    data class Key(val kind: String, val id: String = "", val bankFile: String = "", val bank: Int = 0, val program: Int = 0, val index: Int = 0)

    fun parseKey(key: String): Key? {
        val p = key.split(':')
        return when (p[0]) {
            "synth" -> Key("synth", p.getOrElse(1) { "lead" })
            "sf2" -> if (p.size >= 4) Key("sf2", bankFile = p.subList(1, p.size - 2).joinToString(":"), bank = p[p.size - 2].toIntOrNull() ?: 0,
                program = p.last().toIntOrNull() ?: 0) else null
            "org" -> Key("org", index = p.getOrElse(1) { "0" }.toIntOrNull() ?: 0)
            else -> null
        }
    }

    /** Banks in the order a rhythm looks for its sounds: the player's own first, the bundled one last. */
    private fun searchBanks() = banks.filter { !it.builtin }.sortedBy { it.slot } + banks.filter { it.builtin }

    private fun findPreset(bank: Int, program: Int): BankInfo? = searchBanks().firstOrNull { b -> b.presets.any { it.first == bank && it.second == program } }

    private fun findKit(): String? {
        findPreset(128, 0)?.let { return "sf2:${it.file}:128:0" }
        for (b in searchBanks()) b.presets.firstOrNull { it.first == 128 }?.let { return "sf2:${b.file}:128:${it.second}" }
        return null
    }

    private fun programFor(voice: String): String {
        GM_FOR[voice]?.let { gm -> findPreset(0, gm)?.let { return "sf2:${it.file}:0:$gm" } }
        return "synth:" + (if (BUILTIN.any { it.first == voice }) voice else "lead")
    }

    private fun synthForGM(gm: Int) = when {
        gm < 16 -> "piano"
        gm in 21..23 -> "accordion"
        gm < 24 -> "organ"
        gm < 32 -> "oud"
        gm < 40 -> "bass"
        gm < 56 -> "strings"
        gm < 64 -> "brass"
        gm < 72 -> "sax"
        gm < 80 -> "flute"
        gm in 104..107 -> "tambour"
        else -> "lead"
    }

    private fun programForGM(gm: Int): String = findPreset(0, gm)?.let { "sf2:${it.file}:0:$gm" } ?: "synth:${synthForGM(gm)}"

    private suspend fun applyStyleVoices(st: StyleMeta) {
        for ((role, ch) in ROLE_CH) {
            val voice = st.voices[role] ?: when (role) { "bass" -> "bass"; "upper" -> "lead"; else -> "strings" }
            applyProgram(ch, programFor(voice))
        }
        if (org != null) for (tr in st.tracks) if (tr.orgInstrument >= 0) applyProgram(tr.channel, "org:${tr.orgInstrument}")
        if (st.group != "org") {
            for ((ch, gm) in st.gm) applyProgram(ch, programForGM(gm))
            val kit = findKit() ?: "synth:kit"
            applyProgram(DRUMS, kit)
            applyProgram(PERC, kit)
        }
        persist()
    }

    private suspend fun restorePrograms() { for ((ch, key) in programs.toMap()) applyProgram(ch, key) }

    private fun fallbackKey(ch: Int) = if (ch == DRUMS || ch == PERC) "synth:kit" else "synth:lead"

    /** Loads the program's samples (worker), then sets it on the channel (main). */
    private suspend fun applyProgram(ch: Int, key: String) {
        val k = parseKey(key) ?: return applyProgram(ch, fallbackKey(ch))
        when (k.kind) {
            "sf2" -> {
                val b = banks.firstOrNull { it.file == k.bankFile } ?: return applyProgram(ch, fallbackKey(ch))
                if (b.presets.none { it.first == k.bank && it.second == k.program }) return applyProgram(ch, fallbackKey(ch))
                onWorker { NativeEngine.bankLoadPreset(b.handle, k.bank, k.program) }
                NativeEngine.cmd(Cmd.SET_PROGRAM, ch, Prog.SF2, b.slot, (k.bank shl 8) or k.program)
            }
            "org" -> {
                val o = org ?: return applyProgram(ch, fallbackKey(ch))
                if (k.index !in o.instruments.indices) return applyProgram(ch, fallbackKey(ch))
                NativeEngine.cmd(Cmd.SET_PROGRAM, ch, Prog.ORG, k.index)
            }
            else -> if (k.id == "kit") NativeEngine.cmd(Cmd.SET_PROGRAM, ch, Prog.KIT)
            else NativeEngine.cmd(Cmd.SET_PROGRAM, ch, Prog.BUILTIN, NativeEngine.builtinPresetIndex(k.id).coerceAtLeast(0))
        }
        programs[ch] = key
    }

    /** A sound chosen from a list or an arrow: set it and let it be heard. */
    fun pickSound(ch: Int, key: String) = job {
        applyProgram(ch, key)
        persist()
        if (!playing) NativeEngine.cmd(Cmd.PREVIEW, ch, if (ch == DRUMS || ch == PERC) 63 else if (ch == BASS) 48 else 60)
    }

    fun stepSound(ch: Int, dir: Int) {
        val keys = Catalog.flatKeys(Catalog.soundGroups(programList(ch == DRUMS || ch == PERC), lang, soundLabels()))
        if (keys.isEmpty()) return
        val i = keys.indexOf(programs[ch])
        pickSound(ch, keys[(i + dir + keys.size) % keys.size])
    }

    fun programList(forDrums: Boolean): List<ProgramItem> {
        val out = ArrayList<ProgramItem>()
        for (b in searchBanks()) for ((bank, program, name) in b.presets) {
            if (forDrums != (bank == 128)) continue
            out += ProgramItem("sf2", "sf2:${b.file}:$bank:$program", name, name, b.file, if (b.builtin) "${b.name} (${t("builtinBank")})" else b.name, bank, program)
        }
        org?.let { o -> o.instruments.forEachIndexed { i, (name, kit) -> if (forDrums == kit) out += ProgramItem("org", "org:$i", name, name, index = i) } }
        if (forDrums) out += ProgramItem("synth", "synth:kit", "Built-in kit", "الطقم المدمج")
        else for ((id, l) in BUILTIN) out += ProgramItem("synth", "synth:$id", l.first, l.second)
        return out
    }

    fun soundLabels() = mapOf("kits" to t("kits"), "bank" to t("bank"), "org" to t("setGroup"), "synth" to t("builtinRhythms"))

    fun programLabel(ch: Int): String {
        val k = parseKey(programs[ch] ?: return "—") ?: return "—"
        return when (k.kind) {
            "sf2" -> banks.firstOrNull { it.file == k.bankFile }?.presets?.firstOrNull { it.first == k.bank && it.second == k.program }?.third ?: "—"
            "org" -> org?.instruments?.getOrNull(k.index)?.first ?: "—"
            else -> if (k.id == "kit") (if (lang == "ar") "الطقم المدمج" else "Built-in kit")
            else BUILTIN.firstOrNull { it.first == k.id }?.second?.let { if (lang == "ar") it.second else it.first } ?: k.id
        }
    }

    fun programMeta(ch: Int): String {
        val k = parseKey(programs[ch] ?: return "") ?: return ""
        return when (k.kind) {
            "sf2" -> "${banks.firstOrNull { it.file == k.bankFile }?.name ?: ""} · ${k.bank}:${k.program}"
            "org" -> t("setGroup")
            else -> t("builtinRhythms")
        }
    }

    // ───────── transport and keys ─────────
    fun startStop() = NativeEngine.cmd(if (playing) Cmd.STOP else Cmd.START, 0)
    fun intro() { if (!playing) NativeEngine.cmd(Cmd.START, 1) }
    fun variation(v: Int) { NativeEngine.cmd(Cmd.VARIATION, v) }
    fun fill() = NativeEngine.cmd(Cmd.FILL, -1)
    fun ending() = NativeEngine.cmd(Cmd.ENDING)
    fun toggleSync() = NativeEngine.cmd(Cmd.SYNC, if (syncArmed) 0 else 1)
    fun panic() = NativeEngine.cmd(Cmd.PANIC)
    fun keyOn(note: Int, vel: Int = 100) = NativeEngine.cmd(Cmd.KEY_ON, note, vel)
    fun keyOff(note: Int) = NativeEngine.cmd(Cmd.KEY_OFF, note)

    fun tempoStep(dir: Int) {
        val st = currentStyle() ?: return
        val step = if (st.meterDen == 8) 3 else 2
        val tempo = (engineTempo + dir * step).coerceIn(40, 240)
        NativeEngine.cmd(Cmd.TEMPO, tempo)
        savedTempo = tempo
        persist()
    }

    fun tempoLabel(): String {
        val st = currentStyle() ?: return "${engineTempo}"
        return if (st.meterDen == 8) "♩. ${Math.round(engineTempo * 2 / 3.0)}" else "$engineTempo"
    }

    fun changeTranspose(v: Int) { transpose = v.coerceIn(-12, 12); NativeEngine.cmd(Cmd.TRANSPOSE, transpose); persist() }
    fun changeSplit(v: Int) { split = v.coerceIn(36, 84); NativeEngine.cmd(Cmd.SPLIT, split); persist() }
    fun toggleChordMode() { chordMode = if (chordMode == "single") "fingered" else "single"; NativeEngine.cmd(Cmd.CHORD_MODE, if (chordMode == "single") 1 else 0); persist() }
    fun toggleLowerOn() { lowerOn = !lowerOn; NativeEngine.cmd(Cmd.LOWER_ON, if (lowerOn) 1 else 0); persist() }
    fun toggleLayer() { layer = !layer; NativeEngine.cmd(Cmd.LAYER, if (layer) 1 else 0); persist() }
    fun updateBus(b: Bus) { bus = b; sendMixer(); persist() }

    private fun sendKeyboardSettings() {
        NativeEngine.cmd(Cmd.SPLIT, split)
        NativeEngine.cmd(Cmd.TRANSPOSE, transpose)
        NativeEngine.cmd(Cmd.CHORD_MODE, if (chordMode == "single") 1 else 0)
        NativeEngine.cmd(Cmd.LOWER_ON, if (lowerOn) 1 else 0)
        NativeEngine.cmd(Cmd.LAYER, if (layer) 1 else 0)
    }

    private fun sendMixer() {
        NativeEngine.cmd(Cmd.BUS, 0, f = bus.keys)
        NativeEngine.cmd(Cmd.BUS, 1, f = bus.style)
        NativeEngine.cmd(Cmd.BUS, 2, f = bus.drums)
        NativeEngine.cmd(Cmd.BUS, 3, f = bus.master)
        NativeEngine.cmd(Cmd.REVERB, f = bus.reverb)
    }

    /** The stream was (re)opened: the engine kept its programs and style, the keyboard settings are sent again. */
    fun onStreamStarted() { if (started) { sendKeyboardSettings(); sendMixer() } }

    // ───────── setups ─────────
    fun saveReg(i: Int) {
        regs[i] = Reg(styleId, engineTempo, transpose, split, chordMode, lowerOn, layer, programs.toMap(), bus, mute.toMap())
        activeReg = i
        persist()
        toast("${t("saved")} ${i + 1}")
    }

    fun loadReg(i: Int) {
        val r = regs[i] ?: return
        job {
            transpose = r.transpose; split = r.split; chordMode = r.chordMode; lowerOn = r.lowerOn; layer = r.layer
            bus = r.bus
            mute.clear(); mute.putAll(r.mute)
            programs.clear(); programs.putAll(r.programs)
            activeReg = i
            sendKeyboardSettings()
            sendMixer()
            selectStyleNow(r.styleId, keepPrograms = true)
            NativeEngine.cmd(Cmd.TEMPO, r.tempo)
            savedTempo = r.tempo
            restorePrograms()
            persist()
            toast("${t("loaded")} ${i + 1}")
        }
    }

    // ───────── imports ─────────
    private fun displayName(uri: Uri): String {
        ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0)?.let { return it }
        }
        return uri.lastPathSegment?.substringAfterLast('/') ?: "file"
    }

    private fun copyIn(uri: Uri, dest: File) {
        dest.parentFile?.mkdirs()
        ctx.contentResolver.openInputStream(uri)!!.use { input -> dest.outputStream().use { input.copyTo(it, 1 shl 20) } }
    }

    /** Every file under a picked folder: (uri, name, sub-folder path relative to the picked folder's parent). */
    fun listTree(tree: Uri): List<Triple<Uri, String, String>> {
        val out = ArrayList<Triple<Uri, String, String>>()
        val rootId = DocumentsContract.getTreeDocumentId(tree)
        fun walk(docId: String, folder: String, depth: Int) {
            if (depth > 8) return
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId)
            ctx.contentResolver.query(children, arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE), null, null, null)?.use { c ->
                while (c.moveToNext()) {
                    val id = c.getString(0); val name = c.getString(1) ?: continue; val mime = c.getString(2) ?: ""
                    if (mime == DocumentsContract.Document.MIME_TYPE_DIR) walk(id, if (folder.isEmpty()) name else "$folder/$name", depth + 1)
                    else out += Triple(DocumentsContract.buildDocumentUriUsingTree(tree, id), name, folder)
                }
            }
        }
        val rootName = ctx.contentResolver.query(DocumentsContract.buildDocumentUriUsingTree(tree, rootId),
            arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null)?.use { c -> if (c.moveToFirst()) c.getString(0) else null } ?: "folder"
        walk(rootId, rootName, 0)
        return out
    }

    fun importBanks(files: List<Pair<Uri, String?>>) = job {
        val list = files.map { (u, n) -> u to (n ?: displayName(u)) }.filter { BANK_EXT.containsMatchIn(it.second) }
        if (list.isEmpty()) return@job
        var added = 0
        val bad = ArrayList<String>()
        for ((uri, name) in list) {
            busy = "$name…"
            val ok = onWorker {
                val dir = File(filesDir, "banks")
                val raw = File(dir, name)
                copyIn(uri, raw)
                if (Sf3Converter.isSf3(raw)) {
                    val sf2 = File(dir, name.substringBeforeLast('.') + ".sf2")
                    val bytes = raw.readBytes()
                    raw.delete()
                    Sf3Converter.convert(bytes, sf2)
                } else true
            }
            val file = if (name.endsWith(".sf3", true)) name.substringBeforeLast('.') + ".sf2" else name
            if (!ok) { bad += "$name: ${t("fmt_sf3")}"; continue }
            if (openBank(File(File(filesDir, "banks"), file).path, file, builtin = false)) {
                added++
                settingsBanks = settingsBanks.filter { it != file } + file
            } else bad += name
        }
        busy = null
        if (added > 0) currentStyle()?.let { applyStyleVoices(it) }
        persist()
        if (bad.isNotEmpty()) toast("${t("error")}: ${bad.take(2).joinToString(" · ")}", 7000)
        else toast("${t("imported")}: $added ${t("banksLoaded")}")
    }

    fun removeBank(file: String) = job {
        val b = banks.firstOrNull { it.file == file && !it.builtin } ?: return@job
        NativeEngine.bankRemove(b.slot)  // the engine stops its voices and hands it back to be freed
        banks = banks.filter { it.slot != b.slot }
        settingsBanks = settingsBanks.filter { it != file }
        for ((ch, key) in programs.toMap()) if (parseKey(key)?.bankFile == file) applyProgram(ch, fallbackKey(ch))
        onWorker { File(File(filesDir, "banks"), file).delete() }
        persist()
    }

    fun importSty(files: List<Triple<Uri, String, String>>) = job {
        val list = files.filter { STY_EXT.containsMatchIn(it.second) }
        if (list.isEmpty()) return@job
        var firstId: String? = null
        var added = 0
        val bad = ArrayList<String>()
        for ((uri, name, folder) in list) {
            busy = name
            val dest = File(File(File(filesDir, "styles"), folder.replace("..", "_")), name)
            val f = StyFile(dest.path, folder, name.substringBeforeLast('.'))
            val r = onWorker { copyIn(uri, dest); NativeEngine.stylesAddSty("sty:${f.path}", f.path, styId(f), f.name) }
            if (r.startsWith("!")) { onWorker { dest.delete() }; bad += "$name: ${errorText(r.substring(1))}"; continue }
            addStyleSource("sty:${f.path}", r, folder)
            styFiles = styFiles.filter { it.path != f.path } + f
            if (firstId == null) firstId = styId(f)
            added++
        }
        busy = null
        rebuildStyles()
        firstId?.let { selectStyleNow(it, keepPrograms = false) }
        persist()
        if (bad.isNotEmpty()) toast("${t("imported")}: $added · ${t("skipped")} ${bad.size} — ${bad.take(2).joinToString(" · ")}", 9000)
        else toast("${t("imported")}: $added ${t("rhythmFiles")}")
    }

    fun removeAllSty() = job {
        for (f in styFiles) { NativeEngine.stylesRemove("sty:${f.path}"); styleSources.remove("sty:${f.path}") }
        onWorker { File(filesDir, "styles").deleteRecursively() }
        styFiles = emptyList()
        rebuildStyles()
        selectStyleNow(styleId, keepPrograms = currentStyle()?.id == styleId)
        persist()
    }

    fun importOrg(uri: Uri) = job {
        val name = displayName(uri)
        busy = t("orgLoading")
        val dest = File(File(filesDir, "org"), name)
        onWorker { File(filesDir, "org").deleteRecursively(); copyIn(uri, dest) }
        val ok = openOrg(dest.path, name, select = true)
        busy = null
        if (!ok) { onWorker { dest.delete() }; return@job }
        persist()
        toast("${t("imported")}: $name — ${org?.instruments?.size ?: 0} ${t("instruments")}", 5000)
    }

    private suspend fun openOrg(path: String, file: String, select: Boolean): Boolean {
        val handle = onWorker { NativeEngine.orgOpen(path) }
        if (handle == 0L) {
            val e = NativeEngine.lastError()
            toast("${t("error")}: $file — ${errorText(if (e == "org24") "notOrg" else e)}", 8000)
            if (!select) org = null
            return false
        }
        val info = JSONObject(onWorker { NativeEngine.orgInfo(handle) })
        val ia = info.getJSONArray("instruments")
        val pa = info.getJSONArray("parts")
        NativeEngine.orgAttach(handle)  // the engine retires the previous set
        org = OrgInfo(file, path, info.optString("version"), (0 until ia.length()).map { ia.getJSONArray(it).let { r -> r.getString(0) to r.getBoolean(1) } },
            (0 until pa.length()).map { pa.getString(it) }, handle)
        val base = file.substringBeforeLast('.')
        addStyleSource("org", onWorker { NativeEngine.stylesAddOrg("org", handle, "org:$base", base, orgTempo) }, "")
        rebuildStyles()
        if (select) styles.firstOrNull { it.group == "org" }?.let { selectStyleNow(it.id, keepPrograms = false) }
        return true
    }

    fun removeOrg() = job {
        if (org == null) return@job
        NativeEngine.orgAttach(0)
        NativeEngine.stylesRemove("org")
        styleSources.remove("org")
        org = null
        rebuildStyles()
        for ((ch, key) in programs.toMap()) if (key.startsWith("org:")) applyProgram(ch, fallbackKey(ch))
        onWorker { File(filesDir, "org").deleteRecursively() }
        selectStyleNow(styleId, keepPrograms = currentStyle()?.id == styleId)
        persist()
    }

    fun errorText(code: String): String {
        val k = "fmt_$code"
        val s = t(k)
        return if (s != k) s else code
    }

    // ───────── persistence ─────────
    private val settingsFile get() = File(filesDir, "settings.json")

    fun persist() {
        val o = JSONObject()
        o.put("lang", lang); o.put("styleId", styleId); o.put("tempo", savedTempo); o.put("transpose", transpose); o.put("split", split)
        o.put("chordMode", chordMode); o.put("lowerOn", lowerOn); o.put("layer", layer); o.put("keysShown", keysShown)
        o.put("keepAwake", keepAwake); o.put("showBuiltin", showBuiltin); o.put("moreOpen", moreOpen); o.put("seenHint", seenHint)
        o.put("programs", JSONObject(programs.mapKeys { it.key.toString() }))
        o.put("bus", busJson(bus))
        o.put("mute", JSONObject(mute.toMap()))
        o.put("regs", JSONArray(regs.map { r -> r?.let { regJson(it) } ?: JSONObject.NULL }))
        o.put("activeReg", activeReg); o.put("orgTempo", orgTempo); o.put("bundledVersion", bundledVersion); o.put("bankVoices", bankVoices)
        o.put("banks", JSONArray(settingsBanks))
        o.put("styFiles", JSONArray(styFiles.map { JSONObject().put("path", it.path).put("folder", it.folder).put("name", it.name) }))
        o.put("org", org?.let { JSONObject().put("path", it.path).put("file", it.file) } ?: JSONObject.NULL)
        val tmp = File(filesDir, "settings.json.tmp")
        tmp.writeText(o.toString())
        tmp.renameTo(settingsFile)
    }

    private fun busJson(b: Bus) = JSONObject().put("keys", b.keys.toDouble()).put("style", b.style.toDouble()).put("drums", b.drums.toDouble())
        .put("master", b.master.toDouble()).put("reverb", b.reverb.toDouble())

    private fun busOf(o: JSONObject?) = if (o == null) Bus() else Bus(o.optDouble("keys", 0.8).toFloat(), o.optDouble("style", 0.8).toFloat(),
        o.optDouble("drums", 0.8).toFloat(), o.optDouble("master", 0.9).toFloat(), o.optDouble("reverb", 0.5).toFloat())

    private fun regJson(r: Reg) = JSONObject().put("styleId", r.styleId).put("tempo", r.tempo).put("transpose", r.transpose).put("split", r.split)
        .put("chordMode", r.chordMode).put("lowerOn", r.lowerOn).put("layer", r.layer)
        .put("programs", JSONObject(r.programs.mapKeys { it.key.toString() })).put("bus", busJson(r.bus)).put("mute", JSONObject(r.mute))

    private fun regOf(o: JSONObject) = Reg(o.optString("styleId"), o.optInt("tempo", 120), o.optInt("transpose"), o.optInt("split", 54),
        o.optString("chordMode", "fingered"), o.optBoolean("lowerOn"), o.optBoolean("layer"),
        o.optJSONObject("programs")?.let { p -> p.keys().asSequence().associate { it.toInt() to p.getString(it) } } ?: emptyMap(),
        busOf(o.optJSONObject("bus")),
        o.optJSONObject("mute")?.let { m -> m.keys().asSequence().associateWith { m.getBoolean(it) } } ?: emptyMap())

    private fun loadSettings() {
        val sysAr = java.util.Locale.getDefault().language == "ar"
        lang = if (sysAr) "ar" else "en"
        val o = try { JSONObject(settingsFile.readText()) } catch (e: Exception) { return }
        lang = o.optString("lang", lang); styleId = o.optString("styleId", styleId); savedTempo = o.optInt("tempo", 0)
        transpose = o.optInt("transpose", 0); split = o.optInt("split", 54); chordMode = o.optString("chordMode", "fingered")
        lowerOn = o.optBoolean("lowerOn"); layer = o.optBoolean("layer"); keysShown = o.optBoolean("keysShown", true)
        keepAwake = o.optBoolean("keepAwake", true); showBuiltin = o.optBoolean("showBuiltin", true); moreOpen = o.optBoolean("moreOpen")
        seenHint = o.optBoolean("seenHint"); activeReg = o.optInt("activeReg", -1); orgTempo = o.optInt("orgTempo", 165)
        bundledVersion = o.optInt("bundledVersion", 0)
        bankVoices = o.optBoolean("bankVoices", false)
        o.optJSONObject("programs")?.let { p -> for (k in p.keys()) programs[k.toInt()] = p.getString(k) }
        bus = busOf(o.optJSONObject("bus"))
        o.optJSONObject("mute")?.let { m -> for (k in m.keys()) mute[k] = m.getBoolean(k) }
        o.optJSONArray("regs")?.let { a -> for (i in 0 until minOf(8, a.length())) regs[i] = a.optJSONObject(i)?.let { regOf(it) } }
        o.optJSONArray("banks")?.let { a -> settingsBanks = (0 until a.length()).map { a.getString(it) } }
        o.optJSONArray("styFiles")?.let { a ->
            styFiles = (0 until a.length()).map { a.getJSONObject(it).let { f -> StyFile(f.getString("path"), f.optString("folder"), f.getString("name")) } }
                .filter { File(it.path).exists() }
        }
        o.optJSONObject("org")?.let { if (File(it.getString("path")).exists()) org = OrgInfo(it.getString("file"), it.getString("path"), "", emptyList(), emptyList(), 0) }
    }
}
