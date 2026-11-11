// ORG 24 / Sofeh Music Studio ".Set" reader → Midi Master styles + sampled instruments.
// Pure logic, no browser APIs (node runs it in the tests).
//
// Reverse-engineered 2026-10-20 from two sets (format versions 1.2.8 and 1.4.6):
//  • The file is a stream of objects tagged  int32 len | "Name\r<version>\r" .
//  • The first tags are the set's INSTRUMENTS (the tag is the instrument name).
//    After a header of varying length comes a chain of SAMPLE records:
//      u8 root note | 32-byte name block (0x20 + text) | u16 rate | u8 bits(16) | u8 channels |
//      u32 rate | u32 0 | u32 frames | u32 0 | u32 volume | u32 0 | u32 0 | u32 cents |
//      u32 loopFlag(2 = loop) | u32 loopStart | u32 loopEnd | 4×u32 0 | PCM int16 LE
//  • "Normal", "Master", "Up 1..3", "Low" are keyboard setups, then one "Rhythm" object:
//    eight TRACK records (Wave, Bass, Acc. 1–5, Drum), then PATTERN objects tagged with the
//    track name ("Bass", "Acc. 1", "Drum", "Percussion"): a 535-byte header whose first u32 is the
//    PART id and whose byte 6 is the track type, then NOTE rows of 24 bytes:
//      i32 kind (2 = on, 1 = off) | u32 id | i32 note | i32 velocity | i32 time | i32 duration
//    Times are ticks, 1000 ticks = one bar.
//  • A PART TABLE near the end: records  partId | ownerId | 32-byte name ("Variation 1", "Intro 1",
//    "Fill 1" …) | u8 type | 3 flags | u32 0 | u32 length in ticks | u32 link (next part) .

const TICKS_PER_BAR = 1000;
const TRACK_FOR_TYPE = { 8: 'bass', 9: 'drums', 10: 'perc', 11: 'acc1', 12: 'acc2', 13: 'acc3', 14: 'acc4', 15: 'acc5' };
const DALOKA_TRACK = {
  bass: { role: 'bass', channel: 2 }, acc1: { role: 'chord', channel: 3 }, acc2: { role: 'chord', channel: 6 },
  acc3: { role: 'pad', channel: 4 }, acc4: { role: 'phrase', channel: 5 }, acc5: { role: 'phrase', channel: 7 },
  drums: { role: 'drums', channel: 9 }, perc: { role: 'perc', channel: 10 },
};

const le32 = (b, i) => (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0;
const lei32 = (b, i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24);
const le16 = (b, i) => b[i] | (b[i + 1] << 8);
function txt(b, i, n) { let s = ''; for (let k = 0; k < n; k++) { const c = b[i + k]; if (!c) break; s += String.fromCharCode(c); } return s; }
function nameBlock(b, i) { return txt(b, i + 1, 31).trim(); }   // 0x20 + up to 31 chars

export function isOrgSet(bytes) { const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes); return txt(b, 0, 12) === 'Music Studio'; }

/** Every tagged object: { off (of the int32 len), name, len, body (offset of the first payload byte) }. */
export function scanTags(b, version) {
  const tags = [];
  const v = [...('\r' + version + '\r')].map((c) => c.charCodeAt(0));
  const n = b.length - v.length;
  outer: for (let i = 0; i < n; i++) {
    if (b[i] !== 13) continue;
    for (let k = 1; k < v.length; k++) if (b[i + k] !== v[k]) continue outer;
    // name runs back from i; the int32 before the name must equal name length + tag length
    for (let k = 0; k <= 40; k++) {
      const s = i - k;
      if (s < 4) break;
      const want = k + v.length;
      if (le32(b, s - 4) === want) {
        let ok = true;
        for (let q = s; q < i; q++) if (b[q] < 32 || b[q] > 126) { ok = false; break; }
        if (ok) { tags.push({ off: s - 4, name: txt(b, s, k), len: want, body: s + want }); }
        break;
      }
    }
  }
  return tags;
}

function sampleAt(b, o) {
  // o = offset of the root byte
  if (o + 97 > b.length || b[o + 1] !== 0x20) return null;
  const rate = le16(b, o + 33), bits = b[o + 35], channels = b[o + 36], rate2 = le32(b, o + 37);
  if (bits !== 16 || (channels !== 1 && channels !== 2) || rate !== rate2 || rate < 6000 || rate > 96000) return null;
  const frames = lei32(b, o + 45);
  if (frames <= 0 || o + 97 + frames * channels * 2 > b.length) return null;
  return {
    root: b[o], name: nameBlock(b, o + 1), rate, channels, frames,
    volume: lei32(b, o + 53), cents: lei32(b, o + 65), loop: lei32(b, o + 69) === 2, loopStart: lei32(b, o + 73), loopEnd: lei32(b, o + 77),
    dataOffset: o + 97, dataBytes: frames * channels * 2,
  };
}

function samplesBetween(b, from, to) {
  const out = [];
  let o = null;
  for (let q = from; q < Math.min(from + 4000, to - 97); q++) if (sampleAt(b, q)) { o = q; break; }
  while (o !== null && o < to - 97) {
    const s = sampleAt(b, o);
    if (!s) break;
    out.push(s);
    o = s.dataOffset + s.dataBytes;
  }
  return out;
}

function noteRows(b, o, limit) {
  const rows = [];
  while (o + 24 <= limit) {
    const kind = lei32(b, o), note = lei32(b, o + 8), vel = lei32(b, o + 12), t = lei32(b, o + 16), d = lei32(b, o + 20);
    if (!(kind === 1 || kind === 2) || note < 0 || note > 127 || t < 0 || t > 1e7) break;
    if (kind === 2 && (vel <= 0 || vel > 255 || d <= 0)) break;
    if (kind === 2) rows.push({ note, vel, t, d });
    o += 24;
  }
  return rows;
}

/** Parse a whole .Set. Returns instruments, the rhythm (tracks, parts, patterns) and the setup names. */
export function parseOrgSet(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (!isOrgSet(b)) throw new Error('org24');
  let head = ''; for (let i = 0; i < Math.min(120, b.length); i++) head += String.fromCharCode(b[i] || 32);
  const ver = /\r(\d+\.\d+\.\d+)\r/.exec(head);
  if (!ver) throw new Error('org24');
  const tags = scanTags(b, ver[1]);
  const idx = (name) => tags.findIndex((t) => t.name === name);
  const rhythmTag = idx('Rhythm');
  const setupNames = new Set(['Normal', 'Master', 'Up 1', 'Up 2', 'Up 3', 'Low']);
  // Instruments: every tag before the setups / Rhythm whose region holds a sample chain.
  const instruments = [];
  const limitIdx = tags.findIndex((t) => setupNames.has(t.name) || t.name === 'Rhythm');
  const endOfInstruments = limitIdx >= 0 ? tags[limitIdx].off : b.length;
  for (let k = 0; k < (limitIdx >= 0 ? limitIdx : tags.length); k++) {
    const t = tags[k];
    const next = k + 1 < tags.length ? tags[k + 1].off : endOfInstruments;
    const samples = samplesBetween(b, t.body, next);
    if (samples.length) instruments.push({ index: instruments.length, name: t.name, samples, isKit: samples.length >= 6 && samples.every((s) => !s.loop) && new Set(samples.map((s) => s.root)).size === samples.length && samples.some((s) => s.root < 48) });
  }
  // Rhythm
  let rhythm = null;
  if (rhythmTag >= 0) {
    const tracks = {};
    const patterns = [];
    const trackNames = { Bass: 'bass', 'Acc. 1': 'acc1', 'Acc. 2': 'acc2', 'Acc. 3': 'acc3', 'Acc. 4': 'acc4', 'Acc. 5': 'acc5', Drum: 'drums', Percussion: 'perc' };
    let seenPatternArea = false;
    for (let k = rhythmTag + 1; k < tags.length; k++) {
      const t = tags[k];
      const next = k + 1 < tags.length ? tags[k + 1].off : b.length;
      const tr = trackNames[t.name];
      if (!tr) continue;
      if (!seenPatternArea && !tracks[tr] && next - t.body > 2000) {
        tracks[tr] = { instrumentHint: b[t.body + 153], instrumentHint2: b[t.body + 12] };
        continue;
      }
      seenPatternArea = true;
      const partId = le32(b, t.body);
      const type = le16(b, t.body + 6);
      const rows = noteRows(b, t.body + 535, next);
      if (rows.length) patterns.push({ partId, track: TRACK_FOR_TYPE[type] || tr, type, instrumentHint: b[t.body + 12], rows });
    }
    // Part table: records "partId | owner | nameBlock | type ..." — find them by the known part names.
    const parts = [];
    const names = ['Variation 1', 'Variation 2', 'Variation 3', 'Variation 4', 'Intro 1', 'Intro 2', 'Intro 3', 'Fill 1', 'Fill 2', 'Fill 3', 'Fill 4', 'Break', 'Ending 1', 'Ending 2', 'Ending 3'];
    const wanted = new Set(patterns.map((p) => p.partId));
    const start = tags[rhythmTag].off;
    for (let o = start; o < b.length - 60; o++) {
      if (b[o] !== 0x20 || b[o + 1] < 65 || b[o + 1] > 90) continue;
      const nm = txt(b, o + 1, 31);
      if (!names.includes(nm)) continue;
      const partId = le32(b, o - 8);
      if (!wanted.has(partId)) continue;
      parts.push({ id: partId, name: nm, type: b[o + 32], lengthTicks: lei32(b, o + 40), link: le32(b, o + 44) });
    }
    rhythm = { tracks, patterns, parts, ticksPerBar: TICKS_PER_BAR };
  }
  return { version: ver[1], instruments, rhythm, tagCount: tags.length };
}

/** Pick, per track, an instrument of the set whose roots cover the pattern's notes. */
export function assignInstruments(parsed) {
  const { instruments, rhythm } = parsed;
  const choice = {};
  if (!rhythm) return choice;
  const notesOf = {};
  for (const p of rhythm.patterns) { (notesOf[p.track] = notesOf[p.track] || []).push(...p.rows.map((r) => r.note)); }
  const fits = (inst, notes) => {
    const rs = inst.samples.map((s) => s.root); const lo = Math.min(...rs), hi = Math.max(...rs);
    const inside = notes.filter((n) => n >= lo - 2 && n <= hi + 2).length / notes.length;
    const near = notes.filter((n) => n >= lo - 9 && n <= hi + 9).length / notes.length;
    return inside + 0.5 * near - 0.3 * ((hi - lo) / 127);   // a tight multisample beats a kit that spans the keyboard
  };
  for (const [track, notes] of Object.entries(notesOf)) {
    if (!notes.length) continue;
    const hint = rhythm.tracks[track] && rhythm.tracks[track].instrumentHint;
    const drumTrack = track === 'drums' || track === 'perc';
    const keyword = { bass: /bass|باص/i, drums: /drum|kit|درام|طقم/i, perc: /kit|perc|var|dal|tabl|bongo|daf|طبل|دلوك/i }[track];
    const candidates = instruments.filter((inst) => inst.name !== '-').map((inst) => ({
      inst,
      score: fits(inst, notes) + (inst.index === hint ? 0.5 : 0) + (drumTrack && inst.isKit ? 0.3 : 0) + (!drumTrack && inst.isKit ? -0.6 : 0)
        + (keyword && keyword.test(inst.name) ? 0.4 : 0) + (inst.samples.length >= 4 ? 0.1 : 0),
    }));
    candidates.sort((a, c) => c.score - a.score);
    if (candidates.length) choice[track] = candidates[0].inst.index;
  }
  return choice;
}

const PART_SETS = [
  { suffix: '', label: '', intro: ['Intro 1', 'Intro 2', 'Intro 3'], mainA: ['Variation 1'], mainB: ['Variation 2', 'Variation 1'], fillA: ['Fill 1', 'Fill 2'], fillB: ['Fill 2', 'Fill 1'], ending: ['Ending 1', 'Ending 2', 'Ending 3', 'Intro 2', 'Fill 1'] },
  { suffix: '-34', label: ' (3/4)', intro: ['Intro 2', 'Intro 3', 'Intro 1'], mainA: ['Variation 3'], mainB: ['Variation 4', 'Variation 3'], fillA: ['Fill 3', 'Fill 4', 'Fill 1'], fillB: ['Fill 4', 'Fill 3', 'Fill 2'], ending: ['Ending 2', 'Ending 3', 'Ending 1', 'Fill 3'] },
];

/**
 * Compiled Midi Master styles from a parsed set. meter: [6, 8] (1000 ticks = one bar, triplet feel) by default.
 * Pitched tracks are played as written (the set's own key) and re-mapped by the chord with 'chord'.
 */
export function orgStyles(parsed, { id = 'org', name = 'ORG set', meter = [6, 8], tempo = 100, instrumentChoice = null } = {}) {
  const r = parsed.rhythm;
  if (!r || !r.patterns.length) return [];
  const choice = instrumentChoice || assignInstruments(parsed);
  const beatsPerBar = (meter[0] * 4) / meter[1];
  const byName = {};
  for (const p of r.parts) byName[p.name] = p;
  const patsOf = (partId) => r.patterns.filter((p) => p.partId === partId);
  const tracks = {};
  for (const p of r.patterns) {
    const d = DALOKA_TRACK[p.track];
    if (d) tracks[p.track] = { role: d.role, channel: d.channel, transpose: d.role === 'drums' || d.role === 'perc' ? 'none' : 'chord', gate: 1, orgInstrument: choice[p.track] };
  }
  const build = (set) => {
    const mainPart = set.mainA.map((n) => byName[n]).find(Boolean);
    if (!mainPart) return null;
    const parts = {};
    for (const key of ['mainA', 'mainB', 'intro', 'fillA', 'fillB', 'ending']) {
      const part = set[key].map((n) => byName[n]).find(Boolean) || mainPart;
      const bars = Math.max(1, Math.round(part.lengthTicks / TICKS_PER_BAR));
      const events = [];
      for (const pat of patsOf(part.id)) {
        for (const row of pat.rows) {
          if (row.t >= part.lengthTicks) continue;
          events.push({ track: pat.track, t: (row.t / TICKS_PER_BAR) * beatsPerBar, n: row.note, v: Math.min(127, Math.round(row.vel > 127 ? row.vel / 2 : row.vel)), d: Math.max(0.05, (row.d / TICKS_PER_BAR) * beatsPerBar) });
        }
      }
      events.sort((a, c) => a.t - c.t);
      parts[key] = { bars, beats: bars * beatsPerBar, events };
    }
    return { id: id + set.suffix, name: name + set.label, name_ar: name + set.label, group: 'org', imported: true, org: true, tempo, meter, steps: meter[1] === 8 ? meter[0] * 2 : meter[0] * 4, beatsPerBar, voices: {}, tracks, parts, partNames: r.parts.map((p) => p.name) };
  };
  return PART_SETS.map(build).filter(Boolean);
}
