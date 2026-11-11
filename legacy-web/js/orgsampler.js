// Sampler for the instruments of an ORG 24 / Music Studio .Set (see orgset.js).
// Each instrument is a multisample: the sample whose root is nearest the played
// note is pitched to it. PCM is decoded lazily, per sample, the first time it sounds.

const ORG_MAX_VOICES = 48;

export class OrgSampler {
  constructor(ctx, routeFor) {
    this.ctx = ctx;
    this.routeFor = routeFor;
    this.set = null;            // parsed set (orgset.js)
    this.bytes = null;          // Uint8Array of the whole file
    this.name = '';
    this.buffers = new Map();   // dataOffset → AudioBuffer
    this.programs = {};         // channel → instrument index
    this.voices = [];
    this.sustain = {};
    this.bendSemis = {};
  }

  load(parsed, bytes, name = 'set') { this.set = parsed; this.bytes = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes); this.name = name; this.buffers.clear(); }
  unload() { this.set = null; this.bytes = null; this.buffers.clear(); this.programs = {}; }
  get loaded() { return !!this.set; }
  get instruments() { return this.set ? this.set.instruments : []; }

  setProgram(ch, index) { this.programs[ch] = index; }

  _buffer(s) {
    let buf = this.buffers.get(s.dataOffset);
    if (buf) return buf;
    buf = this.ctx.createBuffer(s.channels, s.frames, s.rate);
    const b = this.bytes;
    for (let c = 0; c < s.channels; c++) {
      const data = buf.getChannelData(c);
      let o = s.dataOffset + c * 2;
      const step = s.channels * 2;
      for (let i = 0; i < s.frames; i++, o += step) {
        const v = b[o] | (b[o + 1] << 8);
        data[i] = (v & 0x8000 ? v - 0x10000 : v) / 32768;
      }
    }
    this.buffers.set(s.dataOffset, buf);
    return buf;
  }

  _pick(inst, note) {
    let best = null, bd = 1e9;
    for (const s of inst.samples) { const d = Math.abs(s.root - note); if (d < bd) { bd = d; best = s; } }
    return best;
  }

  noteOn(ch, note, vel, when = this.ctx.currentTime, dur = null) {
    const inst = this.set && this.set.instruments[this.programs[ch]];
    if (!inst) return;
    const s = this._pick(inst, note);
    if (!s) return;
    const ctx = this.ctx;
    const buf = this._buffer(s);
    const src = ctx.createBufferSource();
    src.buffer = buf;
    const cents = s.cents > -100 && s.cents < 100 ? s.cents : 0;
    const baseRate = Math.pow(2, (note - s.root + cents / 100) / 12);
    src.playbackRate.value = baseRate * Math.pow(2, (this.bendSemis[ch] || 0) / 12);
    if (s.loop && s.loopEnd > s.loopStart && s.loopEnd <= s.frames) { src.loop = true; src.loopStart = s.loopStart / s.rate; src.loopEnd = s.loopEnd / s.rate; }
    const amp = ctx.createGain();
    const v = Math.max(0.05, vel / 127);
    const peak = (0.3 + 0.7 * v) * Math.min(1.5, (s.volume || 100) / 100);
    amp.gain.setValueAtTime(peak, when);
    src.connect(amp).connect(this.routeFor(ch));
    src.start(when);
    if (!src.loop) src.stop(when + buf.duration / baseRate + 0.05);
    const voice = { ch, note, src, amp, baseRate, released: false, sustained: false };
    src.onended = () => { const i = this.voices.indexOf(voice); if (i >= 0) this.voices.splice(i, 1); try { amp.disconnect(); } catch (_) { /* ignore */ } };
    this.voices.push(voice);
    while (this.voices.length > ORG_MAX_VOICES) this._release(this.voices.shift(), ctx.currentTime, true);
    if (dur !== null) this._release(voice, when + dur);
  }

  noteOff(ch, note, when = this.ctx.currentTime) {
    for (const v of this.voices) if (v.ch === ch && v.note === note && !v.released) { if (this.sustain[ch]) v.sustained = true; else this._release(v, when); }
  }

  setSustain(ch, on) {
    this.sustain[ch] = on;
    if (!on) { const now = this.ctx.currentTime; for (const v of this.voices) if (v.ch === ch && v.sustained && !v.released) this._release(v, now); }
  }

  setBend(ch, semis) {
    this.bendSemis[ch] = semis;
    const now = this.ctx.currentTime, f = Math.pow(2, semis / 12);
    for (const v of this.voices) if (v.ch === ch && !v.released) v.src.playbackRate.setTargetAtTime(v.baseRate * f, now, 0.005);
  }

  allOff(ch = null) { const now = this.ctx.currentTime; for (const v of this.voices) if (ch === null || v.ch === ch) this._release(v, now, true); }

  _release(v, when, fast = false) {
    if (!v || v.released) return;
    v.released = true;
    const r = fast ? 0.03 : 0.14;
    const g = v.amp.gain;
    if (typeof g.cancelAndHoldAtTime === 'function') g.cancelAndHoldAtTime(when); else g.cancelScheduledValues(when);
    g.setTargetAtTime(0.0001, when, r / 4);
    try { v.src.stop(when + r + 0.05); } catch (_) { /* already stopped */ }
  }
}
