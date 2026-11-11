// Yamaha style file (.sty / .prs / .sst, SFF1 and SFF2) → a compiled Midi Master style.
// Pure logic, no browser APIs (runs under node for the tests).
//
// A style file is a Standard MIDI File type 0 whose single track is cut into
// sections by marker meta-events ("SInt", "Main A", "Fill In AA", "Intro A",
// "Ending A" …), followed by Yamaha chunks; the first, CASM, holds per-channel
// Ctab records: source channel → destination channel, the chord the pattern was
// recorded over (root + type), the Note Transposition Rule and Table.
// Reference: jososoft.dk "Style CASM Section Format" + "CASM Format 1 / 2".
//
// Mapping to Midi Master: destination channels 9–16 (0-based 8–15) are
//   8 rhythm sub → perc (10), 9 rhythm main → drums (9), 10 bass → 2,
//   11 chord 1 → 3, 12 chord 2 → 6, 13 pad → 4, 14 phrase 1 → 5, 15 phrase 2 → 7.
// Notes are shifted so the pattern reads over C, then the sequencer re-maps them
// per chord with transposeNote(): NTT bypass → no transposition, anything else → 'chord'.

export const DEST_TO_TRACK = {
  8: { name: 'perc', role: 'perc', channel: 10 },
  9: { name: 'drums', role: 'drums', channel: 9 },
  10: { name: 'bass', role: 'bass', channel: 2 },
  11: { name: 'chord', role: 'chord', channel: 3 },
  12: { name: 'chord2', role: 'chord', channel: 6 },
  13: { name: 'pad', role: 'pad', channel: 4 },
  14: { name: 'phrase', role: 'phrase', channel: 5 },
  15: { name: 'phrase2', role: 'phrase', channel: 7 },
};

// Yamaha source-chord types (Ctab byte 19), in the order of the format reference.
export const YAMAHA_CHORD_TYPES = ['Maj', 'Maj6', 'Maj7', 'M7#11', 'Madd9', 'M7(9)', 'M6(9)', 'aug', 'm', 'm6', 'm7', 'm7b5', 'm(9)', 'm7(9)', 'm7(11)', 'mM7', 'mM7(9)', 'dim', 'dim7', '7', '7sus', '7b5', '7(9)', '7(#11)', '7(13)', '7(b9)', '7(b13)', '7(#9)', 'M7aug', '7aug', '1+8', '1+5', 'sus4', '1+2+5'];

function readVar(b, i) {
  let v = 0;
  for (;;) {
    const c = b[i++];
    v = (v << 7) | (c & 0x7f);
    if (!(c & 0x80)) return [v, i];
  }
}
const u32 = (b, i) => ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
const u16 = (b, i) => (b[i] << 8) | b[i + 1];
const ascii = (b, i, n) => { let s = ''; for (let k = 0; k < n; k++) s += String.fromCharCode(b[i + k]); return s; };

/** What kind of keyboard file is this? 'yamaha' | 'org24' | 'korg' | 'unknown'. */
export function sniffFormat(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const head = ascii(b, 0, Math.min(24, b.length));
  if (head.startsWith('MThd')) return 'yamaha';           // Yamaha SFF style (or a plain MIDI file)
  if (head.startsWith('Music Studio')) return 'org24';     // Sofeh ORG 24 / Music Studio .Set / .Instrument
  if (head.startsWith('OC31') || head.startsWith('KORG') || head.startsWith('STY1')) return 'korg';
  return 'unknown';
}

/** Parse the SMF0 part: markers, tempo, time signature, program changes, notes. */
export function parseSMF(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const kind = sniffFormat(b);
  if (kind !== 'yamaha') throw new Error(kind === 'org24' ? 'org24' : kind === 'korg' ? 'korg' : 'notYamaha');
  const division = u16(b, 12);
  if (ascii(b, 14, 4) !== 'MTrk') throw new Error('not a Yamaha style (no MTrk)');
  const trackLen = u32(b, 18);
  const end = 22 + trackLen;
  let i = 22, t = 0, status = null;
  let tempo = 120, meter = [4, 4];
  const markers = [];
  const programs = {};   // channel → { bank, program }
  const bank = {};
  const notes = [];      // { t (ticks), ch, n, v, d (ticks) }
  const open = new Map(); // ch:note → note record
  const closeNote = (ch, n, at) => { const k = ch * 128 + n; const rec = open.get(k); if (rec) { rec.d = Math.max(1, at - rec.t); open.delete(k); } };
  while (i < end) {
    let d; [d, i] = readVar(b, i); t += d;
    const s = b[i];
    if (s === 0xff) {
      const type = b[i + 1]; let len; [len, i] = readVar(b, i + 2);
      const data = b.subarray(i, i + len); i += len;
      if (type === 0x06) markers.push({ t, name: ascii(data, 0, len).trim() });
      else if (type === 0x51) tempo = Math.round(60e6 / ((data[0] << 16) | (data[1] << 8) | data[2]));
      else if (type === 0x58) meter = [data[0], 2 ** data[1]];
      else if (type === 0x2f) break;
      continue;
    }
    if (s === 0xf0 || s === 0xf7) { let len; [len, i] = readVar(b, i + 1); i += len; continue; }
    if (s & 0x80) { status = s; i++; }
    const kind = status & 0xf0, ch = status & 0x0f;
    if (kind === 0x90 && b[i + 1] > 0) {
      closeNote(ch, b[i], t);
      const rec = { t, ch, n: b[i], v: b[i + 1], d: division / 4 };
      notes.push(rec); open.set(ch * 128 + b[i], rec); i += 2;
    } else if (kind === 0x80 || kind === 0x90) { closeNote(ch, b[i], t); i += 2; }
    else if (kind === 0xb0) { if (b[i] === 0) bank[ch] = (bank[ch] || 0) | (b[i + 1] << 7); else if (b[i] === 32) bank[ch] = (bank[ch] || 0) | b[i + 1]; i += 2; }
    else if (kind === 0xc0) { programs[ch] = { bank: bank[ch] || 0, program: b[i] }; i += 1; }
    else if (kind === 0xa0 || kind === 0xe0) i += 2;
    else if (kind === 0xd0) i += 1;
    else throw new Error(`bad MIDI status ${s.toString(16)} at ${i}`);
  }
  for (const rec of open.values()) rec.d = Math.max(1, t - rec.t);
  return { division, tempo, meter, markers, programs, notes, midiEnd: end, sff2: markers.some((m) => m.name === 'SFF2') };
}

/** Parse the CASM chunk: Ctab records for the CSEG that covers `partName` (else the first). */
export function parseCASM(bytes, midiEnd, sff2, partName = 'Main A') {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  let p = midiEnd;
  while (p + 8 <= b.length && ascii(b, p, 4) !== 'CASM') p += 8 + u32(b, p + 4);
  if (p + 8 > b.length) return null;
  const casmEnd = p + 8 + u32(b, p + 4);
  p += 8;
  let first = null;
  while (p + 8 <= casmEnd && ascii(b, p, 4) === 'CSEG') {
    const segEnd = p + 8 + u32(b, p + 4);
    let q = p + 8;
    let parts = [];
    const ctabs = {};
    while (q + 8 <= segEnd) {
      const id = ascii(b, q, 4), len = u32(b, q + 4), d = b.subarray(q + 8, q + 8 + len);
      if (id === 'Sdec') parts = ascii(d, 0, len).split(',').map((s) => s.trim());
      else if (id === 'Ctab' || id === 'Ctb2') {
        const rec = { src: d[0], name: ascii(d, 1, 8).trim(), dest: d[9], root: d[18], type: d[19] };
        if (id === 'Ctb2' || sff2) { rec.ntr = d[26]; rec.ntt = d[27] & 0x7f; }   // Format 2: low/mid limits (2 bytes), low section (6), then middle section
        else { rec.ntr = d[20]; rec.ntt = d[21]; }
        ctabs[rec.src] = rec;
      }
      q += 8 + len;
    }
    const seg = { parts, ctabs };
    if (!first) first = seg;
    if (parts.includes(partName)) return seg;
    p = segEnd;
  }
  return first;
}

// Yamaha part name → Midi Master part, for the A/B pair and the C/D pair.
const PART_MAP = {
  ab: { intro: ['Intro A', 'Intro B', 'Intro C'], mainA: ['Main A'], mainB: ['Main B', 'Main A'], fillA: ['Fill In AA', 'Fill In AB'], fillB: ['Fill In BB', 'Fill In BA', 'Fill In AA'], ending: ['Ending A', 'Ending B', 'Ending C'] },
  cd: { intro: ['Intro B', 'Intro C', 'Intro A'], mainA: ['Main C'], mainB: ['Main D', 'Main C'], fillA: ['Fill In CC'], fillB: ['Fill In DD', 'Fill In CC'], ending: ['Ending B', 'Ending C', 'Ending A'] },
};

function sectionsOf(smf) {
  const out = {};
  const ms = smf.markers.filter((m) => !['SFF1', 'SFF2', 'SInt'].includes(m.name));
  const lastTick = smf.notes.reduce((m, n) => Math.max(m, n.t + n.d), 0);
  ms.forEach((m, k) => { out[m.name] = { start: m.t, end: k + 1 < ms.length ? ms[k + 1].t : Math.max(lastTick, m.t + smf.division) }; });
  return out;
}

/**
 * Convert a parsed style file into one or two compiled Midi Master styles
 * ({ id, name, tempo, meter, tracks, parts: { bars, beats, events } }).
 */
export function convertStyle(bytes, { id = 'sty', name = 'Style' } = {}) {
  const smf = parseSMF(bytes);
  const casm = parseCASM(bytes, smf.midiEnd, smf.sff2) || { ctabs: {} };
  const sections = sectionsOf(smf);
  const meter = smf.meter;
  const beatsPerBar = (meter[0] * 4) / meter[1];
  const ticksPerBeat = smf.division;

  // Channel plan: source channel → track + transposition, from the Ctabs (default: channel = destination).
  const plan = {};
  for (let ch = 8; ch <= 15; ch++) plan[ch] = { dest: ch, root: 0, type: 2, ntr: ch <= 9 ? 1 : 0, ntt: ch <= 9 ? 0 : 1 };
  for (const rec of Object.values(casm.ctabs)) plan[rec.src] = rec;
  const tracks = {};
  const gmPrograms = {};
  for (const [src, rec] of Object.entries(plan)) {
    const tr = DEST_TO_TRACK[rec.dest];
    if (!tr) continue;
    const isDrum = tr.role === 'drums' || tr.role === 'perc';
    tracks[tr.name] = { role: tr.role, channel: tr.channel, transpose: isDrum || rec.ntt === 0 ? 'none' : 'chord', gate: 1, src: +src, shift: isDrum ? 0 : (rec.root > 6 ? 12 - rec.root : -rec.root) };
    const pg = smf.programs[+src];
    if (pg && !isDrum) gmPrograms[tr.channel] = pg.program;
  }
  const bySrc = {};
  for (const [tname, tr] of Object.entries(tracks)) bySrc[tr.src] = tname;

  const build = (map, suffix) => {
    if (!sections[map.mainA[0]]) return null;
    const parts = {};
    for (const part of ['mainA', 'mainB', 'intro', 'fillA', 'fillB', 'ending']) {
      const candidates = map[part];
      const secName = candidates.find((c) => sections[c]);
      const sec = secName ? sections[secName] : null;
      const events = [];
      let bars;
      if (sec) {
        bars = Math.max(1, Math.round((sec.end - sec.start) / (ticksPerBeat * beatsPerBar)));
        for (const n of smf.notes) {
          if (n.t < sec.start || n.t >= sec.end) continue;
          const tname = bySrc[n.ch];
          if (!tname) continue;
          events.push({ track: tname, t: (n.t - sec.start) / ticksPerBeat, n: n.n + tracks[tname].shift, v: n.v, d: n.d / ticksPerBeat });
        }
      } else {
        // Missing part: a fill becomes one bar of the main, an intro/ending one bar of the main with a cut.
        const src = parts.mainA;
        bars = 1;
        for (const e of src.events) if (e.t < beatsPerBar) events.push({ ...e });
      }
      events.sort((a, b) => a.t - b.t);
      parts[part] = { bars, beats: bars * beatsPerBar, events };
    }
    const used = {};
    for (const [tname, tr] of Object.entries(tracks)) if (Object.values(parts).some((pt) => pt.events.some((e) => e.track === tname))) used[tname] = tr;
    return {
      id: id + suffix, name: name + (suffix ? ' (C/D)' : ''), name_ar: name + (suffix ? ' (C/D)' : ''), group: 'yamaha', imported: true, yamaha: true,
      tempo: smf.tempo, meter, steps: meter[1] === 8 ? meter[0] * 2 : meter[0] * 4, beatsPerBar, voices: {}, tracks: used, gmPrograms, parts,
      sections: Object.keys(sections),
    };
  };
  const out = [];
  const ab = build(PART_MAP.ab, '');
  if (ab) out.push(ab);
  const cd = build(PART_MAP.cd, '-cd');
  if (cd) out.push(cd);
  if (!out.length) throw new Error('no Main A section');
  return out;
}

