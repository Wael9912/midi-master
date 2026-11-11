// SPDX-License-Identifier: GPL-3.0-only
package io.github.wael9912.midimaster

import android.media.MediaCodec
import android.media.MediaDataSource
import android.media.MediaExtractor
import android.media.MediaFormat
import android.util.Log
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * SF3 (SoundFont with Ogg Vorbis samples, as MuseScore made it) → plain SF2, decoded once with Android's own Vorbis
 * decoder (MediaCodec), so the engine never decodes anything while it plays. Each SF3 sample is a complete Ogg
 * stream; its loop points are relative to the decoded sample. The rest of the bank (presets, instruments, zones) is
 * copied unchanged, the sample headers are rewritten, and the version becomes 2.01.
 */
object Sf3Converter {
    private const val TAG = "MidiMaster"

    private class Chunk(val id: String, val data: ByteArray)

    /** True when the file is an SF3 (version 3 or any compressed sample). */
    fun isSf3(file: File): Boolean = try {
        val b = file.readBytes()
        parse(b)?.let { (info, _, pdta) ->
            val ifil = info.firstOrNull { it.id == "ifil" }?.data
            val major = if (ifil != null && ifil.size >= 2) (ifil[0].toInt() and 255) or ((ifil[1].toInt() and 255) shl 8) else 2
            val shdr = pdta.firstOrNull { it.id == "shdr" }?.data
            major >= 3 || (shdr != null && (0 until shdr.size / 46).any { i -> (le16(shdr, i * 46 + 44) and 0x10) != 0 })
        } ?: false
    } catch (e: Exception) { false }

    /**
     * Converts [src] into [dst]. Samples are decoded on several threads (each with its own decoder) into one temporary
     * file per thread, then assembled in order. Returns false on any failure.
     */
    fun convert(src: ByteArray, dst: File, progress: (Float) -> Unit = {}): Boolean {
        val (info, sdta, pdta) = parse(src) ?: return false
        val smpl = sdta.firstOrNull { it.id == "smpl" }?.data ?: return false
        val shdr = pdta.firstOrNull { it.id == "shdr" }?.data?.copyOf() ?: return false
        val n = shdr.size / 46 - 1
        if (n <= 0) return false
        val dir = dst.parentFile ?: return false
        val threads = Runtime.getRuntime().availableProcessors().coerceIn(1, 4)
        val ranges = (0 until threads).map { k -> (k * n / threads) until ((k + 1) * n / threads) }
        val parts = ranges.indices.map { File(dir, dst.name + ".part$it") }
        val tmp = File(dir, dst.name + ".tmp")
        val frames = IntArray(n)
        val loops = LongArray(2 * n)
        val done = java.util.concurrent.atomic.AtomicInteger()
        val failed = java.util.concurrent.atomic.AtomicBoolean(false)
        try {
            val workers = ranges.mapIndexed { k, range ->
                Thread({
                    val decoder = Decoder()
                    try {
                        BufferedOutputStream(FileOutputStream(parts[k]), 1 shl 20).use { pcm ->
                            for (i in range) {
                                if (failed.get()) return@use
                                val o = i * 46
                                val start = le32(shdr, o + 20); val end = le32(shdr, o + 24)
                                val loopStart = le32(shdr, o + 28); val loopEnd = le32(shdr, o + 32)
                                val type = le16(shdr, o + 44)
                                val samples: ShortArray
                                if (type and 0x10 != 0) {
                                    if (end <= start || end > smpl.size) { failed.set(true); return@use }
                                    samples = decoder.decode(smpl.copyOfRange(start.toInt(), end.toInt())) ?: run { failed.set(true); return@use }
                                    loops[2 * i] = loopStart; loops[2 * i + 1] = loopEnd
                                } else {
                                    // An uncompressed sample inside an SF3: positions are sample points.
                                    val s0 = start.toInt(); val e0 = end.toInt()
                                    if (e0 <= s0 || e0 * 2 > smpl.size) { failed.set(true); return@use }
                                    samples = ShortArray(e0 - s0) { j -> le16s(smpl, (s0 + j) * 2) }
                                    loops[2 * i] = loopStart - start; loops[2 * i + 1] = loopEnd - start
                                }
                                val bytes = ByteBuffer.allocate((samples.size + 46) * 2).order(ByteOrder.LITTLE_ENDIAN)
                                for (v in samples) bytes.putShort(v)
                                pcm.write(bytes.array())  // includes the 46 zero frames SF2 asks for after each sample
                                frames[i] = samples.size
                                done.incrementAndGet()
                            }
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "SF3 decode thread failed", e)
                        failed.set(true)
                    } finally { decoder.release() }
                }, "mm-sf3-$k").apply { start() }
            }
            while (workers.any { it.isAlive }) {
                progress(done.get() * 0.98f / n)
                Thread.sleep(200)
            }
            workers.forEach { it.join() }
            if (failed.get()) return false
            var pos = 0L
            for (i in 0 until n) {
                val o = i * 46
                val f = frames[i].toLong()
                putLe32(shdr, o + 20, pos)
                putLe32(shdr, o + 24, pos + f)
                putLe32(shdr, o + 28, pos + loops[2 * i].coerceIn(0, f))
                putLe32(shdr, o + 32, pos + loops[2 * i + 1].coerceIn(0, f))
                putLe16(shdr, o + 44, le16(shdr, o + 44) and 0x10.inv())
                pos += f + 46
            }
            // RIFF: INFO (ifil 2.01) · sdta (smpl from the part files, in order) · pdta (shdr rewritten)
            val infoBody = info.map { if (it.id == "ifil") Chunk("ifil", byteArrayOf(2, 0, 1, 0)) else it }
            val pdtaBody = pdta.map { if (it.id == "shdr") Chunk("shdr", shdr) else it }
            val infoList = listBody("INFO", infoBody)
            val pdtaList = listBody("pdta", pdtaBody)
            val smplBytes = pos * 2
            val sdtaSize = 4 + 8 + smplBytes
            val riffSize = 4 + (8 + infoList.size) + (8 + sdtaSize) + (8 + pdtaList.size)
            BufferedOutputStream(FileOutputStream(tmp), 1 shl 20).use { out ->
                out.write(ascii("RIFF")); out.write(le32b(riffSize)); out.write(ascii("sfbk"))
                out.write(ascii("LIST")); out.write(le32b(infoList.size.toLong())); out.write(infoList)
                out.write(ascii("LIST")); out.write(le32b(sdtaSize)); out.write(ascii("sdta"))
                out.write(ascii("smpl")); out.write(le32b(smplBytes))
                for (p in parts) p.inputStream().use { it.copyTo(out, 1 shl 20) }
                out.write(ascii("LIST")); out.write(le32b(pdtaList.size.toLong())); out.write(pdtaList)
            }
            if (tmp.length() != 8 + riffSize) return false
            progress(1f)
            return tmp.renameTo(dst)
        } catch (e: Exception) {
            Log.w(TAG, "SF3 conversion failed", e)
            return false
        } finally {
            parts.forEach { it.delete() }
            tmp.delete()
        }
    }

    // ── one Ogg Vorbis stream → 16-bit mono PCM ──
    private class Decoder {
        private var codec: MediaCodec? = null
        private var mime: String? = null

        fun decode(ogg: ByteArray): ShortArray? {
            val ex = MediaExtractor()
            try {
                ex.setDataSource(ByteSource(ogg))
                if (ex.trackCount < 1) return null
                val fmt = ex.getTrackFormat(0)
                val m = fmt.getString(MediaFormat.KEY_MIME) ?: return null
                ex.selectTrack(0)
                val c = codec?.takeIf { mime == m } ?: run {
                    codec?.release()
                    MediaCodec.createDecoderByType(m).also { codec = it; mime = m }
                }
                c.configure(fmt, null, null, 0)
                c.start()
                val out = ShortList()
                val info = MediaCodec.BufferInfo()
                var inputDone = false
                var channels = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT).coerceAtLeast(1)
                var guard = 0
                var outputDone = false
                while (!outputDone && guard++ < 10_000_000) {
                    if (!inputDone) {
                        val ii = c.dequeueInputBuffer(2_000)
                        if (ii >= 0) {
                            val buf = c.getInputBuffer(ii)!!
                            val size = ex.readSampleData(buf, 0)
                            if (size < 0) {
                                c.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                                inputDone = true
                            } else {
                                c.queueInputBuffer(ii, 0, size, ex.sampleTime, 0)
                                ex.advance()
                            }
                        }
                    }
                    // Drain everything that is ready; wait only when the input is finished.
                    var oi = c.dequeueOutputBuffer(info, if (inputDone) 5_000 else 0)
                    while (oi >= 0 || oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                            channels = c.outputFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT).coerceAtLeast(1)
                        } else {
                            val buf = c.getOutputBuffer(oi)!!.order(ByteOrder.LITTLE_ENDIAN)
                            buf.position(info.offset)
                            buf.limit(info.offset + info.size)
                            val sb = buf.asShortBuffer()
                            val count = sb.remaining() / channels
                            for (k in 0 until count) out.add(sb.get(k * channels))
                            c.releaseOutputBuffer(oi, false)
                            if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) { outputDone = true; break }
                        }
                        oi = c.dequeueOutputBuffer(info, 0)
                    }
                }
                c.stop()
                return out.toArray()
            } catch (e: Exception) {
                Log.w(TAG, "Vorbis decode failed", e)
                codec?.release(); codec = null; mime = null
                return null
            } finally {
                ex.release()
            }
        }

        fun release() { codec?.release(); codec = null }
    }

    private class ByteSource(private val b: ByteArray) : MediaDataSource() {
        override fun readAt(position: Long, buffer: ByteArray, offset: Int, size: Int): Int {
            if (position >= b.size) return -1
            val n = minOf(size.toLong(), b.size - position).toInt()
            System.arraycopy(b, position.toInt(), buffer, offset, n)
            return n
        }
        override fun getSize(): Long = b.size.toLong()
        override fun close() {}
    }

    private class ShortList {
        private var a = ShortArray(1 shl 14)
        private var n = 0
        fun add(v: Short) { if (n == a.size) a = a.copyOf(a.size * 2); a[n++] = v }
        fun toArray(): ShortArray = a.copyOf(n)
    }

    // ── RIFF helpers ──
    private fun parse(b: ByteArray): Triple<List<Chunk>, List<Chunk>, List<Chunk>>? {
        if (b.size < 12 || String(b, 0, 4, Charsets.US_ASCII) != "RIFF" || String(b, 8, 4, Charsets.US_ASCII) != "sfbk") return null
        val lists = HashMap<String, List<Chunk>>()
        var p = 12
        while (p + 8 <= b.size) {
            val id = String(b, p, 4, Charsets.US_ASCII)
            val size = le32(b, p + 4).toInt()
            if (id == "LIST" && p + 12 <= b.size) {
                val kind = String(b, p + 8, 4, Charsets.US_ASCII)
                val sub = ArrayList<Chunk>()
                var q = p + 12
                val end = minOf(b.size, p + 8 + size)
                while (q + 8 <= end) {
                    val cid = String(b, q, 4, Charsets.US_ASCII)
                    val cs = le32(b, q + 4).toInt()
                    if (q + 8 + cs > b.size) break
                    sub += Chunk(cid, b.copyOfRange(q + 8, q + 8 + cs))
                    q += 8 + cs + (cs and 1)
                }
                lists[kind] = sub
            }
            p += 8 + size + (size and 1)
        }
        return Triple(lists["INFO"] ?: emptyList(), lists["sdta"] ?: return null, lists["pdta"] ?: return null)
    }

    private fun listBody(kind: String, chunks: List<Chunk>): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        out.write(ascii(kind))
        for (c in chunks) {
            out.write(ascii(c.id)); out.write(le32b(c.data.size.toLong())); out.write(c.data)
            if (c.data.size and 1 == 1) out.write(0)
        }
        return out.toByteArray()
    }

    private fun ascii(s: String) = s.toByteArray(Charsets.US_ASCII)
    private fun le16(b: ByteArray, i: Int) = (b[i].toInt() and 255) or ((b[i + 1].toInt() and 255) shl 8)
    private fun le16s(b: ByteArray, i: Int) = le16(b, i).toShort()
    private fun le32(b: ByteArray, i: Int): Long =
        ((b[i].toLong() and 255) or ((b[i + 1].toLong() and 255) shl 8) or ((b[i + 2].toLong() and 255) shl 16) or ((b[i + 3].toLong() and 255) shl 24))
    private fun le32b(v: Long) = byteArrayOf(v.toByte(), (v shr 8).toByte(), (v shr 16).toByte(), (v shr 24).toByte())
    private fun putLe32(b: ByteArray, i: Int, v: Long) { b[i] = v.toByte(); b[i + 1] = (v shr 8).toByte(); b[i + 2] = (v shr 16).toByte(); b[i + 3] = (v shr 24).toByte() }
    private fun putLe16(b: ByteArray, i: Int, v: Int) { b[i] = v.toByte(); b[i + 1] = (v shr 8).toByte() }
}
