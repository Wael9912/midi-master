// Built-in Web Audio synth: melodic presets + a Sudanese percussion kit.
// Works out of the box with no sound bank; an SF2 can replace any channel.

export const SYNTH_PRESETS = {
  lead:      { label: 'Synth lead',  label_ar: 'ليد',     osc: [['sawtooth', 0], ['sawtooth', 8]],              cutoff: 2600, q: 2,   a: 0.01, d: 0.25, s: 0.7,  r: 0.15, fenv: 1800, vib: 5.5, vibDepth: 6,  level: 0.35 },
  oud:       { label: 'Oud',         label_ar: 'عود',     osc: [['triangle', 0], ['sawtooth', -3]],             cutoff: 2800, q: 1,   a: 0.003, d: 0.9, s: 0.0,  r: 0.12, fenv: 3500, vib: 0,   vibDepth: 0,  level: 0.5, pluck: true },
  tambour:   { label: 'Rababa / Tambour', label_ar: 'طمبور', osc: [['sawtooth', 0], ['square', 5]],           cutoff: 1600, q: 3,   a: 0.004, d: 1.1, s: 0.0,  r: 0.15, fenv: 2500, vib: 0,   vibDepth: 0,  level: 0.42, pluck: true },
  accordion: { label: 'Accordion',   label_ar: 'أكورديون', osc: [['square', 0], ['sawtooth', 4], ['square', -4]], cutoff: 3200, q: 0.7, a: 0.025, d: 0.1, s: 1.0, r: 0.1, fenv: 400,  vib: 5.2, vibDepth: 5,  level: 0.22 },
  sax:       { label: 'Sax',         label_ar: 'ساكس',    osc: [['sawtooth', 0], ['sawtooth', 3]],              cutoff: 1500, q: 3,   a: 0.035, d: 0.3, s: 0.85, r: 0.15, fenv: 2600, vib: 5,   vibDepth: 8,  level: 0.33 },
  strings:   { label: 'Strings',     label_ar: 'وتريات',  osc: [['sawtooth', -7], ['sawtooth', 0], ['sawtooth', 7]], cutoff: 2200, q: 0.5, a: 0.12, d: 0.3, s: 0.9, r: 0.5, fenv: 600, vib: 4.5, vibDepth: 4, level: 0.2 },
  brass:     { label: 'Brass',       label_ar: 'نفخ',     osc: [['sawtooth', 0], ['sawtooth', 6]],              cutoff: 1800, q: 2.5, a: 0.03, d: 0.3, s: 0.8,  r: 0.14, fenv: 3000, vib: 0,   vibDepth: 0,  level: 0.3 },
  piano:     { label: 'Piano',       label_ar: 'بيانو',   osc: [['triangle', 0], ['sine', 0.5]],                cutoff: 4500, q: 0.5, a: 0.003, d: 1.6, s: 0.15, r: 0.25, fenv: 3000, vib: 0,   vibDepth: 0,  level: 0.5, pluck: true },
  organ:     { label: 'Organ',       label_ar: 'أورغ',    osc: [['sine', 0], ['sine', 1200], ['sine', 1902], ['sine', 2400]], cutoff: 6000, q: 0.3, a: 0.01, d: 0.05, s: 1.0, r: 0.06, fenv: 0, vib: 0, vibDepth: 0, level: 0.22, organ: true },
  bass:      { label: 'Bass',        label_ar: 'باص',     osc: [['sine', 0], ['sawtooth', 0]],                  cutoff: 700,  q: 1.5, a: 0.005, d: 0.35, s: 0.6, r: 0.08, fenv: 900,  vib: 0,   vibDepth: 0,  level: 0.55 },
  flute:     { label: 'Flute',       label_ar: 'ناي',     osc: [['sine', 0], ['triangle', 0]],                  cutoff: 3000, q: 1,   a: 0.04, d: 0.2, s: 0.9,  r: 0.18, fenv: 500,  vib: 5,   vibDepth: 10, level: 0.4 },
};

const MAX_VOICES = 28;   // phones: fewer, cleaner voices beat a stuttering mix

function mtof(n) { return 440 * Math.pow(2, (n - 69) / 12); }

export class Synth {
  constructor(ctx, routeFor) {
    this.ctx = ctx;
    this.routeFor = routeFor;      // channel → destination AudioNode
    this.programs = {};            // channel → preset name
    this.voices = [];              // active melodic voices
    this.sustain = {};             // channel → bool
    this.bendSemis = {};           // channel → semitones
    this.noise = this._makeNoise();
  }

  setProgram(ch, name) { this.programs[ch] = SYNTH_PRESETS[name] ? name : 'lead'; }

  _makeNoise() {
    const len = this.ctx.sampleRate * 2;
    const buf = this.ctx.createBuffer(1, len, this.ctx.sampleRate);
    const data = buf.getChannelData(0);
    for (let i = 0; i < len; i++) data[i] = Math.random() * 2 - 1;
    return buf;
  }

  noteOn(ch, note, vel, when = this.ctx.currentTime, dur = null) {
    if (ch === 9 || ch === 10) return this._drum(ch, note, vel, when);
    const preset = SYNTH_PRESETS[this.programs[ch] || 'lead'];
    const ctx = this.ctx;
    const dest = this.routeFor(ch);
    const v = Math.max(0.05, vel / 127);
    const freq = mtof(note);

    if (this.voices.length >= MAX_VOICES) this._release(this.voices.shift(), ctx.currentTime);

    const amp = ctx.createGain();
    const filter = ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.Q.value = preset.q;
    const cut = Math.min(18000, preset.cutoff * (0.5 + v));
    filter.frequency.setValueAtTime(cut + preset.fenv * v, when);
    filter.frequency.setTargetAtTime(cut, when + preset.a, Math.max(0.03, preset.d / 3));

    const oscs = [];
    let lfo = null;
    if (preset.vib) {
      lfo = ctx.createOscillator();
      lfo.frequency.value = preset.vib;
      const lfoGain = ctx.createGain();
      lfoGain.gain.value = preset.vibDepth;
      lfo.connect(lfoGain);
      oscs.lfoGain = lfoGain;
      lfo.start(when);
    }
    const bend = (this.bendSemis[ch] || 0) * 100;
    preset.osc.forEach(([type, detune], i) => {
      const o = ctx.createOscillator();
      o.type = type;
      o.frequency.value = freq;
      o.detune.value = detune + bend;
      const g = ctx.createGain();
      g.gain.value = preset.organ ? 1 / (i + 1) : 1 / preset.osc.length;
      o.connect(g).connect(filter);
      if (oscs.lfoGain) oscs.lfoGain.connect(o.detune);
      o.start(when);
      oscs.push(o);
    });
    filter.connect(amp).connect(dest);

    const peak = preset.level * (0.35 + 0.65 * v);   // soft keys still speak
    const g = amp.gain;
    g.setValueAtTime(0.0001, when);
    g.linearRampToValueAtTime(peak, when + preset.a);
    g.setTargetAtTime(Math.max(0.0001, peak * preset.s), when + preset.a, Math.max(0.01, preset.d / 3));

    const voice = { ch, note, oscs, lfo, amp, filter, preset, released: false, sustained: false, at: when };
    this.voices.push(voice);
    if (dur !== null) this._release(voice, when + dur);
    return voice;
  }

  noteOff(ch, note, when = this.ctx.currentTime) {
    for (const v of this.voices) {
      if (v.ch === ch && v.note === note && !v.released) {
        if (this.sustain[ch]) v.sustained = true;
        else this._release(v, when);
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
    for (const v of this.voices) {
      if (v.ch !== ch) continue;
      v.preset.osc.forEach(([, detune], i) => v.oscs[i].detune.setTargetAtTime(detune + semis * 100, now, 0.005));
    }
  }

  allOff(ch = null) {
    const now = this.ctx.currentTime;
    for (const v of this.voices) if (ch === null || v.ch === ch) this._release(v, now, true);
  }

  _release(v, when, fast = false) {
    if (v.released) return;
    v.released = true;
    const r = fast ? 0.03 : v.preset.r;
    const g = v.amp.gain;
    if (typeof g.cancelAndHoldAtTime === 'function') g.cancelAndHoldAtTime(when);
    else g.cancelScheduledValues(when);
    g.setTargetAtTime(0.0001, when, r / 4);
    const stopAt = when + r + 0.05;
    for (const o of v.oscs) o.stop(stopAt);
    if (v.lfo) v.lfo.stop(stopAt);
    const self = this;
    v.oscs[0].onended = () => {
      const i = self.voices.indexOf(v);
      if (i >= 0) self.voices.splice(i, 1);
      try { v.amp.disconnect(); v.filter.disconnect(); } catch (_) { /* ignore */ }
    };
  }

  // ───────── Percussion kit ─────────
  _drum(ch, note, vel, when) {
    const v = Math.max(0.1, vel / 127);
    const dest = this.routeFor(ch);
    // The whole GM percussion map, so an imported Yamaha / ORG rhythm plays every stroke it has,
    // not a generic tom for anything the Sudanese kit did not name.
    switch (note) {
      case 35: return this._kick(dest, when, v, 120, 42, 0.5);         // acoustic bass drum
      case 36: return this._kick(dest, when, v, 150, 48, 0.45);        // kick / tum-tum deep drum
      case 38: return this._snare(dest, when, v);                      // snare
      case 40: return this._snare(dest, when, v * 0.9);                // electric snare
      case 63: return this._membrane(dest, when, v, 215, 0.26, 0.35);  // daloka / bangaz dum (open high conga)
      case 62: return this._membrane(dest, when, v * 0.8, 260, 0.12, 0.5);  // mute high conga
      case 60: return this._tak(dest, when, v, 1500, 0.08);            // daloka / bangaz tak (high bongo)
      case 61: return this._tak(dest, when, v, 1100, 0.1);             // low bongo
      case 64: return this._membrane(dest, when, v, 105, 0.42, 0.3);   // mambo (low conga)
      case 41: return this._membrane(dest, when, v, 120, 0.35, 0.5);   // nugara / tabl low (low floor tom)
      case 43: return this._membrane(dest, when, v, 140, 0.33, 0.5);   // high floor tom
      case 45: return this._membrane(dest, when, v, 170, 0.3, 0.45);   // low tom
      case 47: return this._membrane(dest, when, v, 210, 0.26, 0.45);  // low-mid tom
      case 48: return this._membrane(dest, when, v, 320, 0.18, 0.5);   // nugara / tabl high (hi-mid tom)
      case 50: return this._membrane(dest, when, v, 380, 0.16, 0.5);   // high tom
      case 65: return this._membrane(dest, when, v, 420, 0.14, 0.7);   // high timbale
      case 66: return this._membrane(dest, when, v, 300, 0.18, 0.7);   // low timbale
      case 39: return this._clap(dest, when, v);
      case 70: return this._shaker(dest, when, v, 0.05, 8000);         // kashkoush / maracas
      case 69: return this._shaker(dest, when, v, 0.08, 6500);         // cabasa
      case 82: return this._shaker(dest, when, v, 0.1, 7500);          // shaker
      case 42: return this._shaker(dest, when, v, 0.06, 7000);         // closed hat
      case 44: return this._shaker(dest, when, v * 0.7, 0.05, 7500);   // pedal hat
      case 46: return this._shaker(dest, when, v, 0.35, 6000);         // open hat
      case 51: return this._shaker(dest, when, v * 0.8, 0.6, 5000);    // ride
      case 59: return this._shaker(dest, when, v * 0.8, 0.5, 5500);    // ride 2
      case 53: return this._bell(dest, when, v * 0.7);                 // ride bell
      case 76: return this._tak(dest, when, v, 2300, 0.04);            // shatam sticks / hi wood block
      case 77: return this._tak(dest, when, v, 1700, 0.05);            // low wood block
      case 75: return this._tak(dest, when, v, 2500, 0.03);            // claves
      case 54: return this._tambourine(dest, when, v);                 // riq
      case 37: return this._tak(dest, when, v, 2600, 0.05);            // rim / side stick
      case 56: return this._bell(dest, when, v);                       // cowbell
      case 67: return this._bell(dest, when, v * 0.8);                 // high agogo
      case 68: return this._bell(dest, when, v * 0.7);                 // low agogo
      case 80: return this._tak(dest, when, v * 0.6, 6000, 0.06);      // mute triangle
      case 81: return this._shaker(dest, when, v * 0.6, 0.5, 9000);    // open triangle
      case 49: return this._shaker(dest, when, v, 1.2, 4000);          // crash
      case 57: return this._shaker(dest, when, v, 1.0, 4500);          // crash 2
      case 52: return this._shaker(dest, when, v, 0.9, 3500);          // china
      case 55: return this._shaker(dest, when, v, 0.5, 5000);          // splash
      case 73: case 74: return this._shaker(dest, when, v * 0.6, 0.12, 3000);  // guiro
      default: return note < 60 ? this._membrane(dest, when, v, 180, 0.2, 0.4) : this._tak(dest, when, v, 1800, 0.06);
    }
  }

  _snare(dest, when, v) {
    this._membrane(dest, when, v * 0.8, 190, 0.12, 0.2);
    this._noiseBurst(dest, when, 0.7 * v, 0.16, 1800, 'highpass', 0.7);
  }

  _tambourine(dest, when, v) {
    this._noiseBurst(dest, when, 0.45 * v, 0.1, 6500, 'highpass', 0.7);
    this._noiseBurst(dest, when + 0.02, 0.25 * v, 0.12, 9000, 'bandpass', 3);
  }

  _kick(dest, when, v, f0, f1, dur) {
    const ctx = this.ctx;
    const o = ctx.createOscillator();
    const g = ctx.createGain();
    o.type = 'sine';
    o.frequency.setValueAtTime(f0, when);
    o.frequency.exponentialRampToValueAtTime(f1, when + 0.09);
    g.gain.setValueAtTime(1.1 * v, when);
    g.gain.exponentialRampToValueAtTime(0.001, when + dur);
    o.connect(g).connect(dest);
    o.start(when); o.stop(when + dur + 0.02);
    // click
    this._noiseBurst(dest, when, 0.4 * v, 0.012, 3000, 'highpass');
  }

  _membrane(dest, when, v, f, dur, noiseMix) {
    const ctx = this.ctx;
    const o = ctx.createOscillator();
    const g = ctx.createGain();
    o.type = 'sine';
    o.frequency.setValueAtTime(f * 1.5, when);
    o.frequency.exponentialRampToValueAtTime(f, when + 0.03);
    g.gain.setValueAtTime(0.9 * v, when);
    g.gain.exponentialRampToValueAtTime(0.001, when + dur);
    o.connect(g).connect(dest);
    o.start(when); o.stop(when + dur + 0.02);
    this._noiseBurst(dest, when, noiseMix * v, dur * 0.4, f * 6, 'bandpass');
  }

  _tak(dest, when, v, f, dur) {
    this._noiseBurst(dest, when, 0.8 * v, dur, f, 'bandpass', 2);
    const ctx = this.ctx;
    const o = ctx.createOscillator();
    const g = ctx.createGain();
    o.type = 'triangle';
    o.frequency.setValueAtTime(f * 0.6, when);
    o.frequency.exponentialRampToValueAtTime(f * 0.35, when + dur);
    g.gain.setValueAtTime(0.35 * v, when);
    g.gain.exponentialRampToValueAtTime(0.001, when + dur);
    o.connect(g).connect(dest);
    o.start(when); o.stop(when + dur + 0.02);
  }

  _clap(dest, when, v) {
    for (let i = 0; i < 3; i++) this._noiseBurst(dest, when + i * 0.011, 0.5 * v, 0.02, 1800, 'bandpass', 1);
    this._noiseBurst(dest, when + 0.03, 0.6 * v, 0.14, 1600, 'bandpass', 1.2);
  }

  _shaker(dest, when, v, dur, f) {
    this._noiseBurst(dest, when, 0.35 * v, dur, f, 'highpass');
  }

  _bell(dest, when, v) {
    const ctx = this.ctx;
    for (const [ratio, lvl] of [[1, 0.6], [1.5, 0.3], [2.4, 0.2]]) {
      const o = ctx.createOscillator();
      const g = ctx.createGain();
      o.type = 'square';
      o.frequency.value = 560 * ratio;
      g.gain.setValueAtTime(lvl * v * 0.5, when);
      g.gain.exponentialRampToValueAtTime(0.001, when + 0.25);
      o.connect(g).connect(dest);
      o.start(when); o.stop(when + 0.3);
    }
  }

  _noiseBurst(dest, when, level, dur, freq, type, q = 0.8) {
    const ctx = this.ctx;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    src.loopEnd = 1.9;
    const f = ctx.createBiquadFilter();
    f.type = type;
    f.frequency.value = freq;
    f.Q.value = q;
    const g = ctx.createGain();
    g.gain.setValueAtTime(level, when);
    g.gain.exponentialRampToValueAtTime(0.001, when + dur);
    src.connect(f).connect(g).connect(dest);
    src.start(when, Math.random() * 1.5);
    src.stop(when + dur + 0.02);
  }
}
