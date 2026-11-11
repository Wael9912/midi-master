// SoundFont 2 loader + sampler on Web Audio. Covers the generators that matter
// for playback: key/vel ranges, root key, tuning, loop modes, the volume
// envelope, attenuation and pan. Preset-level generators add to instrument
// ones per the SF2 spec. Modulators are ignored.

const G = {
  startAddrsOffset: 0, endAddrsOffset: 1, startloopAddrsOffset: 2, endloopAddrsOffset: 3,
  startAddrsCoarseOffset: 4, endAddrsCoarseOffset: 12, pan: 17,
  delayVolEnv: 33, attackVolEnv: 34, holdVolEnv: 35, decayVolEnv: 36, sustainVolEnv: 37, releaseVolEnv: 38,
  instrument: 41, keyRange: 43, velRange: 44, startloopAddrsCoarseOffset: 45, keynum: 46, velocity: 47,
  initialAttenuation: 48, endloopAddrsCoarseOffset: 50, coarseTune: 51, fineTune: 52, sampleID: 53,
  sampleModes: 54, scaleTuning: 56, exclusiveClass: 57, overridingRootKey: 58,
};
// Generators that are relative when set on a preset zone (added to the instrument value).
const ADDITIVE = new Set([G.pan, G.delayVolEnv, G.attackVolEnv, G.holdVolEnv, G.decayVolEnv, G.sustainVolEnv,
  G.releaseVolEnv, G.initialAttenuation, G.coarseTune, G.fineTune, G.scaleTuning]);

const SF2_MAX_VOICES = 64;

function readString(dv, off, len) {
  let s = '';
  for (let i = 0; i < len; i++) {
    const c = dv.getUint8(off + i);
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s;
}

function tc2s(tc) { return Math.pow(2, tc / 1200); }

export function parseSF2(arrayBuffer) {
  const dv = new DataView(arrayBuffer);
  if (readString(dv, 0, 4) !== 'RIFF' || readString(dv, 8, 4) !== 'sfbk') throw new Error('Not an SF2 file');
  const chunks = {};
  let smpl = null;
  let info = {};
  let p = 12;
  const end = 8 + dv.getUint32(4, true);
  while (p + 8 <= end) {
    const id = readString(dv, p, 4);
    const size = dv.getUint32(p + 4, true);
    if (id === 'LIST') {
      const kind = readString(dv, p + 8, 4);
      let q = p + 12;
      const listEnd = p + 8 + size;
      while (q + 8 <= listEnd) {
        const cid = readString(dv, q, 4);
        const csize = dv.getUint32(q + 4, true);
        if (kind === 'sdta' && cid === 'smpl') smpl = new Int16Array(arrayBuffer, q + 8, csize / 2);
        else if (kind === 'pdta') chunks[cid] = { off: q + 8, size: csize };
        else if (kind === 'INFO' && (cid === 'INAM' || cid === 'ICRD' || cid === 'IENG' || cid === 'ICOP')) info[cid] = readString(dv, q + 8, csize);
        q += 8 + csize + (csize & 1);
      }
    }
    p += 8 + size + (size & 1);
  }
  if (!smpl) throw new Error('SF2 has no sample data');
  const need = ['phdr', 'pbag', 'pgen', 'inst', 'ibag', 'igen', 'shdr'];
  for (const n of need) if (!chunks[n]) throw new Error(`SF2 missing ${n}`);

  const rec = (name, size, fn) => {
    const { off, size: total } = chunks[name];
    const out = [];
    for (let o = off; o + size <= off + total; o += size) out.push(fn(o));
    return out;
  };
  const phdr = rec('phdr', 38, (o) => ({ name: readString(dv, o, 20), preset: dv.getUint16(o + 20, true), bank: dv.getUint16(o + 22, true), bagNdx: dv.getUint16(o + 24, true) }));
  const pbag = rec('pbag', 4, (o) => ({ genNdx: dv.getUint16(o, true) }));
  const pgen = rec('pgen', 4, (o) => ({ op: dv.getUint16(o, true), lo: dv.getUint8(o + 2), hi: dv.getUint8(o + 3), amt: dv.getInt16(o + 2, true) }));
  const inst = rec('inst', 22, (o) => ({ name: readString(dv, o, 20), bagNdx: dv.getUint16(o + 20, true) }));
  const ibag = rec('ibag', 4, (o) => ({ genNdx: dv.getUint16(o, true) }));
  const igen = rec('igen', 4, (o) => ({ op: dv.getUint16(o, true), lo: dv.getUint8(o + 2), hi: dv.getUint8(o + 3), amt: dv.getInt16(o + 2, true) }));
  const shdr = rec('shdr', 46, (o) => ({
    name: readString(dv, o, 20), start: dv.getUint32(o + 20, true), end: dv.getUint32(o + 24, true),
    startLoop: dv.getUint32(o + 28, true), endLoop: dv.getUint32(o + 32, true), sampleRate: dv.getUint32(o + 36, true),
    originalPitch: dv.getUint8(o + 40), pitchCorrection: dv.getInt8(o + 41), sampleType: dv.getUint16(o + 44, true),
  }));

  const zonesOf = (bags, gens, from, to) => {
    const zones = [];
    for (let b = from; b < to; b++) {
      const z = { gens: {}, keyLo: 0, keyHi: 127, velLo: 0, velHi: 127 };
      for (let g = bags[b].genNdx; g < bags[b + 1].genNdx; g++) {
        const gen = gens[g];
        if (gen.op === G.keyRange) { z.keyLo = gen.lo; z.keyHi = gen.hi; }
        else if (gen.op === G.velRange) { z.velLo = gen.lo; z.velHi = gen.hi; }
        else z.gens[gen.op] = gen.amt;
      }
      zones.push(z);
    }
    return zones;
  };

  const instruments = inst.slice(0, -1).map((it, i) => {
    const zones = zonesOf(ibag, igen, it.bagNdx, inst[i + 1].bagNdx);
    let global = null;
    if (zones.length && zones[0].gens[G.sampleID] === undefined) global = zones.shift();
    return { name: it.name, global, zones: zones.filter((z) => z.gens[G.sampleID] !== undefined) };
  });

  const presets = phdr.slice(0, -1).map((ph, i) => {
    const zones = zonesOf(pbag, pgen, ph.bagNdx, phdr[i + 1].bagNdx);
    let global = null;
    if (zones.length && zones[0].gens[G.instrument] === undefined) global = zones.shift();
    return { name: ph.name, bank: ph.bank, program: ph.preset, global, zones: zones.filter((z) => z.gens[G.instrument] !== undefined) };
  }).filter((p) => p.zones.length);
  presets.sort((a, b) => a.bank - b.bank || a.program - b.program);

  return { info, smpl, shdr, instruments, presets };
}

export class SF2Player {
  constructor(ctx, routeFor) {
    this.ctx = ctx;
    this.routeFor = routeFor;
    this.font = null;
    this.name = '';
    this.buffers = new Map();   // sample index → AudioBuffer
    this.programs = {};         // channel → preset index
    this.voices = [];
    this.sustain = {};
    this.bendSemis = {};
  }

  load(arrayBuffer, name = 'soundfont') {
    this.font = parseSF2(arrayBuffer);
    this.name = this.font.info.INAM || name;
    this.buffers.clear();
    return this.font.presets.map((p, i) => ({ index: i, name: p.name, bank: p.bank, program: p.program }));
  }

  get presets() { return this.font ? this.font.presets : []; }

  setProgram(ch, presetIndex) { this.programs[ch] = presetIndex; }

  findPreset(bank, program) {
    if (!this.font) return -1;
    return this.font.presets.findIndex((p) => p.bank === bank && p.program === program);
  }

  _buffer(idx, startOff, endOff) {
    const key = `${idx}:${startOff}:${endOff}`;
    let b = this.buffers.get(key);
    if (b) return b;
    const s = this.font.shdr[idx];
    const start = s.start + startOff;
    const end = Math.min(s.end + endOff, this.font.smpl.length);
    const len = Math.max(1, end - start);
    b = this.ctx.createBuffer(1, len, s.sampleRate || 44100);
    const data = b.getChannelData(0);
    const smpl = this.font.smpl;
    for (let i = 0; i < len; i++) data[i] = smpl[start + i] / 32768;
    this.buffers.set(key, b);
    return b;
  }

  noteOn(ch, note, vel, when = this.ctx.currentTime, dur = null) {
    const preset = this.font && this.font.presets[this.programs[ch]];
    if (!preset) return;
    const dest = this.routeFor(ch);
    const ctx = this.ctx;
    const started = [];
    for (const pz of preset.zones) {
      if (note < pz.keyLo || note > pz.keyHi || vel < pz.velLo || vel > pz.velHi) continue;
      const instr = this.font.instruments[pz.gens[G.instrument]];
      if (!instr) continue;
      for (const iz of instr.zones) {
        if (note < iz.keyLo || note > iz.keyHi || vel < iz.velLo || vel > iz.velHi) continue;
        const gen = (op, dflt) => {
          let v = iz.gens[op] !== undefined ? iz.gens[op] : (instr.global && instr.global.gens[op] !== undefined ? instr.global.gens[op] : dflt);
          if (ADDITIVE.has(op)) {
            const pv = pz.gens[op] !== undefined ? pz.gens[op] : (preset.global && preset.global.gens[op] !== undefined ? preset.global.gens[op] : 0);
            v += pv;
          }
          return v;
        };
        const sIdx = iz.gens[G.sampleID];
        const s = this.font.shdr[sIdx];
        if (!s) continue;
        const startOff = gen(G.startAddrsOffset, 0) + gen(G.startAddrsCoarseOffset, 0) * 32768;
        const endOff = gen(G.endAddrsOffset, 0) + gen(G.endAddrsCoarseOffset, 0) * 32768;
        const buf = this._buffer(sIdx, startOff, endOff);
        let root = gen(G.overridingRootKey, -1);
        if (root < 0 || root > 127) root = s.originalPitch > 127 ? 60 : s.originalPitch;
        const keynum = gen(G.keynum, -1);
        const playNote = keynum >= 0 ? keynum : note;
        const cents = gen(G.coarseTune, 0) * 100 + gen(G.fineTune, 0) + s.pitchCorrection;
        const scale = gen(G.scaleTuning, 100) / 100;
        const rate = Math.pow(2, ((playNote - root) * scale + cents / 100) / 12);
        const modes = gen(G.sampleModes, 0);
        const loop = (modes & 1) === 1 || modes === 3;

        const src = ctx.createBufferSource();
        src.buffer = buf;
        src.playbackRate.value = rate * Math.pow(2, (this.bendSemis[ch] || 0) / 12);
        if (loop) {
          const ls = s.startLoop + gen(G.startloopAddrsOffset, 0) + gen(G.startloopAddrsCoarseOffset, 0) * 32768 - (s.start + startOff);
          const le = s.endLoop + gen(G.endloopAddrsOffset, 0) + gen(G.endloopAddrsCoarseOffset, 0) * 32768 - (s.start + startOff);
          if (le > ls && ls >= 0) { src.loop = true; src.loopStart = ls / buf.sampleRate; src.loopEnd = le / buf.sampleRate; }
        }
        const amp = ctx.createGain();
        const att = Math.max(0, gen(G.initialAttenuation, 0)) * 0.4; // FluidSynth scales attenuation by 0.4
        const velGain = (vel / 127) * (vel / 127);
        const peak = Math.pow(10, -att / 200) * velGain;
        const delay = Math.min(2, tc2s(gen(G.delayVolEnv, -12000)));
        const attack = Math.min(4, tc2s(gen(G.attackVolEnv, -12000)));
        const hold = Math.min(4, tc2s(gen(G.holdVolEnv, -12000)));
        const decay = Math.min(20, tc2s(gen(G.decayVolEnv, -12000)));
        const susCb = Math.min(1000, Math.max(0, gen(G.sustainVolEnv, 0)));
        const sustain = susCb >= 1000 ? 0.0001 : Math.pow(10, -susCb / 200);
        const release = Math.min(8, tc2s(gen(G.releaseVolEnv, -12000)));
        const t0 = when + delay;
        const g = amp.gain;
        g.setValueAtTime(0.0001, when);
        g.setValueAtTime(0.0001, t0);
        g.linearRampToValueAtTime(Math.max(0.0001, peak), t0 + attack);
        g.setValueAtTime(Math.max(0.0001, peak), t0 + attack + hold);
        g.setTargetAtTime(Math.max(0.0001, peak * sustain), t0 + attack + hold, Math.max(0.005, decay / 4));

        let node = amp;
        const pan = gen(G.pan, 0);
        if (pan !== 0 && ctx.createStereoPanner) {
          const pn = ctx.createStereoPanner();
          pn.pan.value = Math.max(-1, Math.min(1, pan / 500));
          amp.connect(pn);
          node = pn;
        }
        src.connect(amp);
        node.connect(dest);
        src.start(when);
        if (!loop) src.stop(when + buf.duration / rate + 0.05);
        const voice = { ch, note, src, amp, release, baseRate: rate, released: false, sustained: false, exclusive: gen(G.exclusiveClass, 0), instr };
        src.onended = () => {
          const i = this.voices.indexOf(voice);
          if (i >= 0) this.voices.splice(i, 1);
          try { amp.disconnect(); node.disconnect(); } catch (_) { /* ignore */ }
        };
        // Exclusive class (hi-hat choke etc.) inside the same instrument.
        if (voice.exclusive) {
          for (const v of this.voices) if (v.ch === ch && v.instr === instr && v.exclusive === voice.exclusive && !v.released) this._release(v, when, true);
        }
        this.voices.push(voice);
        started.push(voice);
      }
    }
    while (this.voices.length > SF2_MAX_VOICES) this._release(this.voices.shift(), ctx.currentTime, true);
    if (dur !== null) for (const v of started) this._release(v, when + dur);
  }

  noteOff(ch, note, when = this.ctx.currentTime) {
    for (const v of this.voices) {
      if (v.ch === ch && v.note === note && !v.released) {
        if (this.sustain[ch]) v.sustained = true; else this._release(v, when);
      }
    }
  }

  setSustain(ch, on) {
    this.sustain[ch] = on;
    if (!on) {
      const now = this.ctx.currentTime;
      for (const v of this.voices) if (v.ch === ch && v.sustained && !v.released) this._release(v, now);
    }
  }

  setBend(ch, semis) {
    this.bendSemis[ch] = semis;
    const now = this.ctx.currentTime;
    const f = Math.pow(2, semis / 12);
    for (const v of this.voices) if (v.ch === ch && !v.released) v.src.playbackRate.setTargetAtTime(v.baseRate * f, now, 0.005);
  }

  allOff(ch = null) {
    const now = this.ctx.currentTime;
    for (const v of this.voices) if (ch === null || v.ch === ch) this._release(v, now, true);
  }

  _release(v, when, fast = false) {
    if (!v || v.released) return;
    v.released = true;
    const r = fast ? 0.03 : v.release;
    const g = v.amp.gain;
    if (typeof g.cancelAndHoldAtTime === 'function') g.cancelAndHoldAtTime(when); else g.cancelScheduledValues(when);
    g.setTargetAtTime(0.0001, when, Math.max(0.005, r / 4));
    try { v.src.stop(when + r + 0.05); } catch (_) { /* already stopped */ }
  }
}
