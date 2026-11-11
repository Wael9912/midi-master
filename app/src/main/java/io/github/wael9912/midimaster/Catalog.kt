// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import java.text.Collator
import java.util.Locale

/** A compiled style as the UI sees it (the notes stay in the engine). */
data class StyleMeta(
    val id: String, val name: String, val nameAr: String, val group: String, val tempo: Int,
    val meterNum: Int, val meterDen: Int, val beatsPerBar: Double, val tracks: List<TrackMeta>,
    val voices: Map<String, String>, val gm: Map<Int, Int>, val folder: String = "",
)

data class TrackMeta(val name: String, val role: String, val channel: Int, val orgInstrument: Int)

/** One selectable sound. key: "synth:<id>", "synth:kit", "sf2:<bank file>:<bank>:<program>", "org:<index>". */
data class ProgramItem(
    val kind: String, val key: String, val label: String, val labelAr: String,
    val bankFile: String = "", val bankName: String = "", val bank: Int = 0, val program: Int = 0, val index: Int = 0,
)

data class ListItem(val key: String, val label: String, val meta: String)
data class ListGroup(val key: String, val title: String, val items: List<ListItem>)

/** Ordering, grouping and search for the two pickers, ported from legacy-web/js/catalog.js. */
object Catalog {
    private val families = listOf(
        "Piano" to "بيانو", "Chromatic percussion" to "إيقاع نغمي", "Organ" to "أورغ", "Guitar" to "جيتار",
        "Bass" to "باص", "Strings" to "وتريات", "Ensemble" to "مجموعات", "Brass" to "نفخ نحاسي",
        "Reed" to "ناي وساكس", "Pipe" to "فلوت", "Synth lead" to "ليد", "Synth pad" to "خلفيات",
        "Synth effects" to "مؤثرات سينث", "Ethnic" to "آلات شرقية", "Percussive" to "إيقاعية", "Sound effects" to "مؤثرات صوتية",
    )

    fun gmFamily(program: Int, lang: String = "en"): String {
        val f = families[(program / 8).coerceIn(0, 15)]
        return if (lang == "ar") f.second else f.first
    }

    private val groupOrder = listOf("yamaha", "org", "sudan", "west")
    private val collator: Collator = Collator.getInstance(Locale.ROOT).apply { strength = Collator.PRIMARY }

    /** Text order that reads numbers as numbers ("s2" before "s10"), case-insensitive. */
    val natural = Comparator<String> { a, b ->
        val ra = Regex("\\d+|\\D+").findAll(a).map { it.value }.toList()
        val rb = Regex("\\d+|\\D+").findAll(b).map { it.value }.toList()
        for (i in 0 until minOf(ra.size, rb.size)) {
            val x = ra[i]; val y = rb[i]
            val c = if (x[0].isDigit() && y[0].isDigit()) (x.toBigInteger()).compareTo(y.toBigInteger()) else collator.compare(x, y)
            if (c != 0) return@Comparator c
        }
        ra.size - rb.size
    }

    /** The player's own files first (folders by name, then loose files), then the ORG set, then the built-ins. */
    fun orderStyles(styles: List<StyleMeta>, showBuiltin: Boolean): List<StyleMeta> {
        fun rank(s: StyleMeta) = groupOrder.indexOf(s.group).let { if (it < 0) 99 else it }
        val yamahaCmp = Comparator<StyleMeta> { a, b ->
            val fa = a.folder; val fb = b.folder
            if (fa.isEmpty() != fb.isEmpty()) return@Comparator if (fa.isNotEmpty()) -1 else 1
            val c = collator.compare(fa, fb)
            if (c != 0) c else natural.compare(a.name, b.name)
        }
        return styles.withIndex()
            .filter { showBuiltin || !(it.value.group == "sudan" || it.value.group == "west") }
            .sortedWith { x, y ->
                val r = rank(x.value) - rank(y.value)
                when {
                    r != 0 -> r
                    x.value.group == "yamaha" -> yamahaCmp.compare(x.value, y.value)
                    else -> x.index - y.index
                }
            }
            .map { it.value }
    }

    fun stepStyle(ordered: List<StyleMeta>, id: String, dir: Int): StyleMeta? {
        if (ordered.isEmpty()) return null
        val i = ordered.indexOfFirst { it.id == id }
        if (i < 0) return ordered[0]
        return ordered[(i + dir + ordered.size) % ordered.size]
    }

    fun tempoLabel(meterDen: Int, tempo: Int) = if (meterDen == 8) "♩. ${Math.round(tempo * 2 / 3.0)}" else "♩ $tempo"

    fun styleGroups(ordered: List<StyleMeta>, lang: String, labels: Map<String, String>): List<ListGroup> {
        val groups = LinkedHashMap<String, Pair<String, MutableList<ListItem>>>()
        val folders = ordered.filter { it.group == "yamaha" }.map { it.folder }.toSet()
        for (st in ordered) {
            var key = st.group
            var title = labels[st.group] ?: st.group
            if (st.group == "yamaha" && folders.size > 1) {
                key = "yamaha:" + st.folder
                title = st.folder.ifEmpty { labels["yamaha"] ?: "Styles" }
            }
            groups.getOrPut(key) { title to mutableListOf() }.second +=
                ListItem(st.id, if (lang == "ar") st.nameAr else st.name, "${st.meterNum}/${st.meterDen} · ${tempoLabel(st.meterDen, st.tempo)}")
        }
        return groups.map { (k, v) -> ListGroup(k, v.first, v.second) }
    }

    /** Banks' presets grouped by bank file and GM family; the set's instruments; the built-ins last. */
    fun soundGroups(programs: List<ProgramItem>, lang: String, labels: Map<String, String>): List<ListGroup> {
        val groups = LinkedHashMap<String, Pair<String, MutableList<ListItem>>>()
        fun add(key: String, title: String, item: ListItem) { groups.getOrPut(key) { title to mutableListOf() }.second += item }
        for (p in programs) {
            val label = if (lang == "ar") p.labelAr else p.label
            when (p.kind) {
                "sf2" -> {
                    val fam = when (p.bank) {
                        128 -> labels["kits"] ?: "Drum kits"
                        0 -> gmFamily(p.program, lang)
                        else -> "${labels["bank"] ?: "Bank"} ${p.bank}"
                    }
                    add("sf2:${p.bankFile}:${if (p.bank == 0) (p.program / 8).toString() else "b${p.bank}"}", "${p.bankName} · $fam",
                        ListItem(p.key, label, "${p.bank}:${p.program}"))
                }
                "org" -> add("org", labels["org"] ?: "Set", ListItem(p.key, label, ""))
                else -> add("synth", labels["synth"] ?: "Built-in", ListItem(p.key, label, ""))
            }
        }
        val list = groups.map { (k, v) -> ListGroup(k, v.first, v.second) }.toMutableList()
        val synth = list.indexOfFirst { it.key == "synth" }
        if (synth >= 0) list.add(list.removeAt(synth))
        return list
    }

    fun flatKeys(groups: List<ListGroup>) = groups.flatMap { g -> g.items.map { it.key } }

    fun filterGroups(groups: List<ListGroup>, query: String): List<ListGroup> {
        val q = query.trim().lowercase()
        if (q.isEmpty()) return groups
        val words = q.split(Regex("\\s+"))
        fun hit(s: String) = s.lowercase().let { t -> words.all { t.contains(it) } }
        return groups.map { g -> if (hit(g.title)) g else g.copy(items = g.items.filter { hit("${it.label} ${it.meta}") }) }
            .filter { it.items.isNotEmpty() }
    }
}
