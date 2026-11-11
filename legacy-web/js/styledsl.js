// Style DSL → compiled style. Pure logic, no browser APIs.
//
// A style pack is JSON. Each style:
// {
//   id, name, name_ar, tempo (quarter-note BPM), meter: [4,4], steps: 16 (per bar),
//   voices: { upper, lower, bass, chord, pad, phrase },      // default sounds
//   tracks: { drums: {...}, perc: {...}, bass: {...}, chord: {...}, pad: {...}, phrase: {...} },
//   parts: { intro, mainA, mainB, fillA, fillB, ending }      // each: { bars, <trackName>: grid }
// }
// A track: { role, channel, transpose: 'none'|'root'|'chord', gate?: 0.9 }
// A grid for drums/perc: { "<drum>": "x..x..x." } one string per drum sound,
//   'X' accent, 'x' hit, 'o' soft, '.' rest. Shorter strings repeat.
// A grid for pitched tracks: a string of space-separated step tokens:
//   "C2 . . C2:2 G2+B2 ." — note[:steps] with '+' for several notes, '.' rest,
//   '-' = tie (extends the previous token). Written over C major.
//
// compileStyle() turns this into parts of { bars, events: [{track, t, n, v, d}] }
// where t and d are in quarter-note beats from the start of the part.

export const DRUM_NOTES = {
  // GM kit numbers, so an imported GM SoundFont (bank 128) plays the right sound;
  // the built-in kit (synth.js) synthesises the same numbers.
  D: 63,   // daloka / bangaz "dum"   (GM open high conga)
  T: 60,   // daloka / bangaz "tak"   (GM high bongo)
  M: 64,   // mambo, the big bangaz   (GM low conga)
  K: 36,   // kick / tum-tum deep drum
  A: 38,   // snare
  L: 41,   // nugara / tabl low       (GM low floor tom)
  H: 48,   // nugara / tabl high      (GM hi-mid tom)
  C: 39,   // hand clap (الصفقة)
  J: 70,   // shaker / kashkoush      (GM maracas)
  W: 76,   // shatam sticks           (GM hi wood block)
  R: 54,   // riq                     (GM tambourine)
  S: 42,   // closed hat
  O: 46,   // open hat
  B: 56,   // cowbell / bell
  Z: 49,   // crash
  N: 45,   // tom
};

const NOTE_RE = /^([A-Ga-g])([#b]?)(-?\d)$/;
const PC = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

export function noteNumber(token) {
  const m = NOTE_RE.exec(token);
  if (!m) throw new Error(`bad note token "${token}"`);
  let n = PC[m[1].toUpperCase()] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0);
  return n + (parseInt(m[3], 10) + 1) * 12;
}

function velocityFor(ch) {
  if (ch === 'X') return 118;
  if (ch === 'x') return 96;
  if (ch === 'o') return 64;
  return 0;
}

function drumEvents(trackName, grid, totalSteps, stepBeats) {
  const events = [];
  for (const [drum, pattern] of Object.entries(grid)) {
    const note = DRUM_NOTES[drum];
    if (note === undefined) throw new Error(`unknown drum "${drum}"`);
    const pat = pattern.replace(/\s+/g, '');
    if (!pat.length) continue;
    for (let s = 0; s < totalSteps; s++) {
      const v = velocityFor(pat[s % pat.length]);
      if (v) events.push({ track: trackName, t: s * stepBeats, n: note, v, d: stepBeats * 0.5 });
    }
  }
  return events;
}

function pitchedEvents(trackName, grid, totalSteps, stepBeats, gate) {
  const events = [];
  const tokens = grid.trim().split(/\s+/).filter(Boolean);
  if (!tokens.length) return events;
  // Expand ties so each token knows its own length.
  let s = 0;
  let last = null;
  while (s < totalSteps) {
    const tok = tokens[s % tokens.length];
    if (tok === '-') {
      if (last) last.forEach((e) => (e.d += stepBeats));
      s += 1;
      continue;
    }
    if (tok === '.') { last = null; s += 1; continue; }
    const [notesPart, lenPart] = tok.split(':');
    const len = lenPart ? parseInt(lenPart, 10) : 1;
    const vel = notesPart.endsWith('!') ? 112 : notesPart.endsWith('?') ? 60 : 88;
    const clean = notesPart.replace(/[!?]$/, '');
    const notes = clean.split('+').map(noteNumber);
    last = notes.map((n) => ({ track: trackName, t: s * stepBeats, n, v: vel, d: stepBeats * len * gate }));
    events.push(...last);
    s += len;
  }
  return events;
}

export function beatsPerBar(meter) {
  return (meter[0] * 4) / meter[1];
}

export function compileStyle(src) {
  const meter = src.meter || [4, 4];
  const steps = src.steps || (meter[1] === 8 ? meter[0] * 2 : meter[0] * 4);
  const bpb = beatsPerBar(meter);
  const stepBeats = bpb / steps;
  const style = {
    id: src.id,
    name: src.name,
    name_ar: src.name_ar || src.name,
    tempo: src.tempo || 120,
    meter,
    steps,
    beatsPerBar: bpb,
    voices: src.voices || {},
    tracks: src.tracks,
    parts: {},
    group: src.group || 'west',
    source: src,
  };
  for (const partName of ['intro', 'mainA', 'mainB', 'fillA', 'fillB', 'ending']) {
    const part = src.parts[partName];
    if (!part) throw new Error(`${src.id}: missing part ${partName}`);
    const bars = part.bars || 1;
    const totalSteps = bars * steps;
    const events = [];
    for (const [trackName, grid] of Object.entries(part)) {
      if (trackName === 'bars') continue;
      const track = src.tracks[trackName];
      if (!track) throw new Error(`${src.id}/${partName}: unknown track ${trackName}`);
      if (track.role === 'drums' || track.role === 'perc') {
        events.push(...drumEvents(trackName, grid, totalSteps, stepBeats));
      } else {
        events.push(...pitchedEvents(trackName, grid, totalSteps, stepBeats, track.gate ?? 0.9));
      }
    }
    events.sort((a, b) => a.t - b.t);
    style.parts[partName] = { bars, beats: bars * bpb, events };
  }
  return style;
}

/** Validate a style pack (array of style sources). Returns a list of problems. */
export function lintPack(pack) {
  const problems = [];
  if (!Array.isArray(pack)) return ['pack must be an array of styles'];
  const ids = new Set();
  pack.forEach((src, i) => {
    const tag = src.id || `#${i}`;
    if (!src.id) problems.push(`${tag}: missing id`);
    if (ids.has(src.id)) problems.push(`${tag}: duplicate id`);
    ids.add(src.id);
    if (!src.name) problems.push(`${tag}: missing name`);
    if (!src.tracks || !Object.keys(src.tracks).length) problems.push(`${tag}: no tracks`);
    for (const [name, tr] of Object.entries(src.tracks || {})) {
      if (!['drums', 'perc', 'bass', 'chord', 'pad', 'phrase'].includes(tr.role)) problems.push(`${tag}/${name}: bad role ${tr.role}`);
      if (!['none', 'root', 'chord'].includes(tr.transpose)) problems.push(`${tag}/${name}: bad transpose ${tr.transpose}`);
      if (typeof tr.channel !== 'number') problems.push(`${tag}/${name}: channel must be a number`);
    }
    if (src.tempo && (src.tempo < 40 || src.tempo > 240)) problems.push(`${tag}: tempo out of 40..240`);
    try {
      const st = compileStyle(src);
      for (const [p, part] of Object.entries(st.parts)) {
        if (!part.events.length) problems.push(`${tag}/${p}: part has no events`);
        for (const e of part.events) if (e.t >= part.beats) problems.push(`${tag}/${p}: event past the end`);
      }
    } catch (err) {
      problems.push(`${tag}: ${err.message}`);
    }
  });
  return problems;
}
