// Routes every channel to the built-in synth, one of the loaded SF2 banks, or the ORG set, by program.
// program: { kind: 'synth', name } | { kind: 'sf2', font, bank, program } | { kind: 'org', index }

import { Synth, SYNTH_PRESETS } from './synth.js';
import { SF2Player } from './sf2.js';
import { OrgSampler } from './orgsampler.js';

export const CH = { upper: 0, lower: 1, bass: 2, chord: 3, pad: 4, phrase: 5, drums: 9, perc: 10 };

export class Engine {
  constructor(audio) {
    this.audio = audio;
    this.synth = new Synth(audio.ctx, audio.routeFor);
    this.fonts = [];               // SF2Player per loaded bank, in load order
    this.org = new OrgSampler(audio.ctx, audio.routeFor);
    this.programs = {};
    for (const ch of Object.values(CH)) this.setProgram(ch, { kind: 'synth', name: ch === 9 || ch === 10 ? 'kit' : 'lead' });
  }

  get hasSF2() { return this.fonts.length > 0; }
  /** The first bank, for code that wants "the" SF2 (name, preset count). */
  get sf2() { return this.fonts[0] || null; }

  /** Add a bank (replacing one loaded from the same file name). Returns its index. */
  loadSF2(arrayBuffer, fileName) {
    const p = new SF2Player(this.audio.ctx, this.audio.routeFor);
    p.load(arrayBuffer, fileName);
    p.fileName = fileName;
    const i = this.fonts.findIndex((f) => f.fileName === fileName);
    if (i >= 0) { this.fonts[i].allOff(); this.fonts[i] = p; return i; }
    this.fonts.push(p);
    return this.fonts.length - 1;
  }

  removeSF2(index) {
    const f = this.fonts[index];
    if (!f) return;
    f.allOff();
    this.fonts.splice(index, 1);
    // Programs on later banks shift down; programs on the removed bank fall back to the synth.
    for (const [ch, p] of Object.entries(this.programs)) {
      if (p.kind !== 'sf2') continue;
      if (p.font === index) this.setProgram(+ch, { kind: 'synth', name: +ch === 9 || +ch === 10 ? 'kit' : 'lead' });
      else if (p.font > index) p.font--;
    }
  }

  clearSF2() { for (const f of this.fonts) f.allOff(); this.fonts = []; for (const [ch, p] of Object.entries(this.programs)) if (p.kind === 'sf2') this.setProgram(+ch, { kind: 'synth', name: +ch === 9 || +ch === 10 ? 'kit' : 'lead' }); }

  get hasOrg() { return this.org.loaded; }
  loadOrg(parsed, bytes, name) { this.org.load(parsed, bytes, name); }

  /** First bank that has (bank, program): { font, index } or null. */
  findPreset(bank, program) {
    for (let f = 0; f < this.fonts.length; f++) {
      const idx = this.fonts[f].findPreset(bank, program);
      if (idx >= 0) return { font: f, index: idx };
    }
    return null;
  }

  /** A drum kit from the banks: GM standard kit first, else any bank-128 preset. */
  findKit() {
    const std = this.findPreset(128, 0);
    if (std) return { kind: 'sf2', font: std.font, bank: 128, program: 0 };
    for (let f = 0; f < this.fonts.length; f++) {
      const p = this.fonts[f].presets.find((x) => x.bank === 128);
      if (p) return { kind: 'sf2', font: f, bank: 128, program: p.program };
    }
    return null;
  }

  /** All selectable programs, each with its storage key: the banks' presets, the set's instruments, the built-ins. */
  programList(forDrums = false) {
    const out = [];
    this.fonts.forEach((f, fi) => {
      for (const p of f.presets) {
        if (forDrums ? p.bank !== 128 : p.bank === 128) continue;
        out.push({ kind: 'sf2', font: fi, fontName: f.name, bank: p.bank, program: p.program, key: `sf2:${fi}:${p.bank}:${p.program}`, label: p.name, label_ar: p.name });
      }
    });
    if (this.org.loaded) {
      for (const inst of this.org.instruments) {
        if (forDrums !== !!inst.isKit) continue;
        out.push({ kind: 'org', index: inst.index, key: `org:${inst.index}`, label: inst.name, label_ar: inst.name });
      }
    }
    if (forDrums) out.push({ kind: 'synth', name: 'kit', key: 'synth:kit', label: 'Built-in kit', label_ar: 'الطقم المدمج' });
    else for (const [name, p] of Object.entries(SYNTH_PRESETS)) out.push({ kind: 'synth', name, key: `synth:${name}`, label: p.label, label_ar: p.label_ar });
    return out;
  }

  setProgram(ch, prog) {
    if (prog.kind === 'org') {
      if (!this.org.loaded || !this.org.instruments[prog.index]) return false;
      this.org.setProgram(ch, prog.index);
    } else if (prog.kind === 'sf2') {
      const f = this.fonts[prog.font];
      if (!f) return false;
      const idx = f.findPreset(prog.bank, prog.program);
      if (idx < 0) return false;
      f.setProgram(ch, idx);
    } else {
      this.synth.setProgram(ch, prog.name);
    }
    this.programs[ch] = { ...prog };
    return true;
  }

  programKey(ch) {
    const p = this.programs[ch];
    return p ? Engine.keyOf(p) : 'synth:lead';
  }

  static keyOf(p) {
    return p.kind === 'sf2' ? `sf2:${p.font}:${p.bank}:${p.program}` : p.kind === 'org' ? `org:${p.index}` : `synth:${p.name}`;
  }

  /** 'sf2:font:bank:program' (or the older 'sf2:bank:program' = bank file 0), 'org:index', 'synth:name'. */
  static parseKey(key) {
    const parts = String(key).split(':');
    if (parts[0] === 'sf2') return parts.length >= 4 ? { kind: 'sf2', font: +parts[1], bank: +parts[2], program: +parts[3] } : { kind: 'sf2', font: 0, bank: +parts[1], program: +parts[2] };
    if (parts[0] === 'org') return { kind: 'org', index: +parts[1] };
    return { kind: 'synth', name: parts[1] || 'lead' };
  }

  /** Label of the program on a channel, for the main screen. */
  programLabel(ch, lang = 'en') {
    const p = this.programs[ch];
    if (!p) return '';
    if (p.kind === 'sf2') { const f = this.fonts[p.font]; const i = f ? f.findPreset(p.bank, p.program) : -1; return i >= 0 ? f.presets[i].name : ''; }
    if (p.kind === 'org') { const inst = this.org.instruments[p.index]; return inst ? inst.name : ''; }
    const sp = SYNTH_PRESETS[p.name];
    return sp ? (lang === 'ar' ? sp.label_ar : sp.label) : (p.name === 'kit' ? (lang === 'ar' ? 'الطقم المدمج' : 'Built-in kit') : p.name);
  }

  _inst(ch) {
    const p = this.programs[ch];
    const k = p && p.kind;
    if (k === 'sf2') return this.fonts[p.font] || this.synth;
    return k === 'org' ? this.org : this.synth;
  }

  noteOn(ch, n, v, t, d = null) { if (n >= 0 && n <= 127) this._inst(ch).noteOn(ch, n, v, t, d); }
  noteOff(ch, n, t) { this._inst(ch).noteOff(ch, n, t); }
  setSustain(ch, on) { this.synth.setSustain(ch, on); for (const f of this.fonts) f.setSustain(ch, on); this.org.setSustain(ch, on); }
  setBend(ch, semis) { this.synth.setBend(ch, semis); for (const f of this.fonts) f.setBend(ch, semis); this.org.setBend(ch, semis); }
  allOff(ch = null) { this.synth.allOff(ch); for (const f of this.fonts) f.allOff(ch); this.org.allOff(ch); }
}
