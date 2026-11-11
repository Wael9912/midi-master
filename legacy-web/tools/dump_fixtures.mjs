// SPDX-License-Identifier: GPL-3.0-only
// Dumps the reference (web) engine's results to JSON fixtures that the C++ tests in engine/tests/ must reproduce.
//   node legacy-web/tools/dump_fixtures.mjs [path/to/own.Set ...]
// Writes engine/tests/fixtures/*.json (+ the synthetic .sty / .Set files) and app/src/main/assets/styles.json (the
// built-in style pack as JSON, compiled by the C++ engine at run time). Real files given on the command line (the
// owner's .Set / .sty files) are dumped to build/fixtures-local/, which is not committed.
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { dirname, join, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { detectChord, transposeNote, CHORD_TYPES } from '../js/chord.js';
import { compileStyle } from '../js/styledsl.js';
import { STYLE_PACK } from '../js/styles.js';
import { Sequencer } from '../js/sequencer.js';
import { convertStyle, sniffFormat } from '../js/sty.js';
import { parseOrgSet, orgStyles, assignInstruments } from '../js/orgset.js';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');
const fx = join(root, 'engine', 'tests', 'fixtures');
const local = join(root, 'build', 'fixtures-local');
mkdirSync(fx, { recursive: true });
const out = (name, obj) => writeFileSync(join(fx, name), JSON.stringify(obj) + '\n');
const r6 = (x) => Math.round(x * 1e6) / 1e6;

// Deterministic PRNG (mulberry32) so the fixtures never change between runs.
function rng(seed) {
  return () => {
    seed |= 0; seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ── the built-in style pack, as the app's asset ──
const assets = join(root, 'app', 'src', 'main', 'assets');
mkdirSync(assets, { recursive: true });
writeFileSync(join(assets, 'styles.json'), JSON.stringify(STYLE_PACK, null, 1) + '\n');

// ── chords ──
{
  const cases = [];
  const add = (notes, mode) => { const c = detectChord(notes, mode); cases.push([notes, mode, c ? [c.root, c.type, c.bass] : null]); };
  for (let r = 0; r < 12; r++) {
    for (const def of Object.values(CHORD_TYPES)) {
      const base = 48 + r;
      const pos = def.intervals.map((i) => base + i);
      for (const mode of ['fingered', 'single']) {
        add(pos, mode);
        add([...pos.slice(1), pos[0] + 12], mode);
        add([...pos.slice(2), pos[0] + 12, pos[1] + 12], mode);
        add([...pos, base + 12], mode);
        add([pos[0] - 12, ...pos.slice(1)], mode);
      }
    }
    for (const mode of ['fingered', 'single']) { add([48 + r], mode); add([48 + r, 45 + r], mode); add([48 + r, 46 + r], mode); add([48 + r, 46 + r, 45 + r], mode); add([48 + r, 47 + r], mode); }
  }
  const rand = rng(1);
  for (let i = 0; i < 3000; i++) {
    const k = 1 + Math.floor(rand() * 5);
    const set = new Set();
    while (set.size < k) set.add(36 + Math.floor(rand() * 25));
    add([...set], rand() < 0.5 ? 'fingered' : 'single');
  }
  add([], 'fingered'); add([], 'single');
  out('chords.json', cases);
}

// ── transposition ──
{
  const types = Object.keys(CHORD_TYPES);
  const table = [];
  for (const rule of ['none', 'root', 'chord']) {
    for (let r = 0; r < 12; r++) for (const type of types) {
      const row = [];
      for (let n = 0; n <= 127; n++) row.push(transposeNote(n, rule, { root: r, type }));
      table.push([rule, r, type, row]);
    }
  }
  out('transpose.json', { types, table });
}

// ── compiled built-in styles ──
const compiled = STYLE_PACK.map(compileStyle);
const styleJson = (st) => ({
  id: st.id, tempo: st.tempo, meter: st.meter, steps: st.steps, beatsPerBar: st.beatsPerBar,
  tracks: Object.fromEntries(Object.entries(st.tracks).map(([k, t]) => [k, [t.role, t.channel, t.transpose]])),
  parts: Object.fromEntries(Object.entries(st.parts).map(([k, p]) => [k, { bars: p.bars, beats: p.beats, events: p.events.map((e) => [e.track, r6(e.t), e.n, e.v, r6(e.d)]) }])),
});
out('styles_compiled.json', compiled.map(styleJson));

// ── sequencer: scripted runs. Commands carry the time at which the C++ sequencer (no look-ahead) must apply them so
// it decides the same as the JS one: the JS scheduler is 0.2 s ahead of the clock, so a command issued at clock time T
// takes effect for events after T + 0.2. Times in the fixture are relative to the sequencer start (JS start = now + 0.02).
const LOOKAHEAD = 0.2, START = 0.02, STEP = 0.005;
function runSeq(style, { tempo, chord = null, script = [], seconds = 20 }) {
  let now = 0;
  const played = [];
  const timers = [];
  const seq = new Sequencer({
    clock: { now: () => now },
    sink: { noteOn: (ch, n, v, t, d) => played.push([ch, n, v, r6(t - START), r6(d)]), allOff: () => {} },
    setTimer: (fn) => { timers.push(fn); return 1; },
    clearTimer: () => { timers.length = 0; },
  });
  seq.setStyle(style);
  seq.setTempo(tempo);
  if (chord) seq.setChord(chord);
  const parts = [];
  seq.on((type, d) => { if (type === 'part') parts.push([d, r6(seq._partStart - START)]); });
  seq.start();
  const applied = [];
  const queue = [...script].sort((a, b) => a.at - b.at);
  const steps = Math.round(seconds / STEP);
  for (let i = 1; i <= steps; i++) {
    now = i * STEP;
    while (queue.length && queue[0].at <= now + 1e-9) {
      const c = queue.shift();
      if (c.cmd === 'chord') seq.setChord(c.chord);
      else if (c.cmd === 'fill') seq.fill(c.to || null);
      else if (c.cmd === 'variation') seq.setVariation(c.v);
      else if (c.cmd === 'ending') seq.ending();
      else if (c.cmd === 'mute') seq.mute = { ...seq.mute, [c.track]: c.on };
      applied.push({ ...c, at: r6(now + LOOKAHEAD - START) });
    }
    for (const f of [...timers]) f();
  }
  return { tempo, chord: chord ? [chord.root, chord.type] : null, script: applied, parts, played, playing: seq.playing };
}
{
  const byId = (id) => compiled.find((s) => s.id === id);
  const runs = [];
  // A command at time T of the clock, placed so that T + 0.2 falls between two 32nd-note grid points (no event exactly
  // on the decision time).
  const at = (style, tempo, beat) => START + beat * 60 / tempo - LOOKAHEAD + 60 / tempo / 16;
  for (const st of compiled) {
    const tempo = st.tempo;
    const bpb = st.beatsPerBar;
    runs.push({ style: st.id, ...runSeq(st, { tempo, chord: { root: 7, type: 'maj' }, seconds: 12 }) });
    runs.push({ style: st.id, ...runSeq(st, {
      tempo, chord: { root: 2, type: 'min' }, seconds: 14 + 8 * bpb * 60 / tempo,
      script: [
        { at: at(st, tempo, bpb * 1 + 0.5), cmd: 'chord', chord: { root: 5, type: 'dom7' } },
        { at: at(st, tempo, bpb * 2 + 0.25), cmd: 'fill', to: 'B' },
        { at: at(st, tempo, bpb * 4 + 1), cmd: 'chord', chord: { root: 9, type: 'min7' } },
        { at: at(st, tempo, bpb * 5 + 0.5), cmd: 'mute', track: Object.keys(st.tracks)[0], on: true },
        { at: at(st, tempo, bpb * 6 + 0.5), cmd: 'variation', v: 'A' },
        { at: at(st, tempo, bpb * 7 + 0.5), cmd: 'mute', track: Object.keys(st.tracks)[0], on: false },
        { at: at(st, tempo, bpb * 9 + 0.5), cmd: 'fill' },
        { at: at(st, tempo, bpb * 11 + 0.75), cmd: 'chord', chord: { root: 11, type: 'dim' } },
        { at: at(st, tempo, bpb * 12 + 0.5), cmd: 'ending' },
      ],
    }) });
  }
  // Tempo extremes on a 4/4 and a 6/8 style.
  runs.push({ style: 'pop', ...runSeq(byId('pop'), { tempo: 240, chord: { root: 4, type: 'aug' }, seconds: 10 }) });
  runs.push({ style: 'tumtum', ...runSeq(byId('tumtum'), { tempo: 40, chord: { root: 0, type: 'sus4' }, seconds: 30 }) });
  runs.push({ style: 'daloka', ...runSeq(byId('daloka'), { tempo: 104, chord: null, seconds: 10 }) });
  out('sequencer.json', runs);
}

// ── Yamaha .sty: synthetic files (same builder as tests/run.mjs) ──
function vlq(n) { const o = [n & 0x7f]; n >>= 7; while (n) { o.unshift((n & 0x7f) | 0x80); n >>= 7; } return o; }
function be32(n) { return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]; }
const asc = (s) => [...s].map((c) => c.charCodeAt(0));
function makeSty({ root = 0, ntt = 1, sff2 = false, withCD = false } = {}) {
  const div = 480;
  const ev = [];
  const push = (dt, ...bytes) => ev.push(...vlq(dt), ...bytes);
  const marker = (dt, name) => push(dt, 0xff, 0x06, name.length, ...asc(name));
  marker(0, sff2 ? 'SFF2' : 'SFF1'); marker(0, 'SInt');
  push(0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20);
  push(0, 0xff, 0x58, 4, 4, 2, 24, 8);
  push(0, 0xb0 | 10, 0, 0); push(0, 0xb0 | 10, 32, 0); push(0, 0xc0 | 10, 33);
  push(0, 0xc0 | 11, 21);
  push(0, 0xc0 | 13, 48);
  marker(div * 4, 'Main A');
  push(0, 0x99, 36, 100); push(0, 0x9a, 36 + root, 90);
  push(div / 2, 0x89, 36, 0); push(div / 2, 0x8a, 36 + root, 0);
  push(0, 0x9b, 60 + root, 80); push(0, 0x9b, 64 + root, 80); push(0, 0x9b, 67 + root, 80);
  push(div / 2, 0x8b, 60 + root, 0); push(0, 0x8b, 64 + root, 0); push(0, 0x8b, 67 + root, 0);
  push(div / 2, 0x99, 36, 100); push(0, 0x9a, 43 + root, 90);
  push(div / 2, 0x89, 36, 0); push(0, 0x8a, 43 + root, 0);
  push(div / 2, 0x9b, 60 + root, 80); push(div / 2, 0x8b, 60 + root, 0);
  marker(div / 2, 'Main B');
  push(0, 0x99, 36, 110); push(0, 0x9d, 72 + root, 70); push(div, 0x89, 36, 0); push(div * 3, 0x8d, 72 + root, 0);
  marker(0, 'Fill In AA');
  push(0, 0x99, 38, 100); push(div * 4, 0x89, 38, 0);
  if (withCD) {
    marker(0, 'Main C');
    push(0, 0x99, 42, 90); push(0, 0x9a, 38 + root, 90); push(div * 2, 0x89, 42, 0); push(0, 0x8a, 38 + root, 0);
    push(div * 2, 0x99, 42, 90); push(div, 0x89, 42, 0);
    marker(div, 'Fill In CC');
    push(0, 0x99, 49, 100); push(div * 4, 0x89, 49, 0);
  }
  marker(0, 'Ending A');
  push(0, 0x99, 49, 100); push(0, 0x9a, 36 + root, 100); push(div * 4, 0x89, 49, 0);
  push(div * 4, 0xff, 0x2f, 0);   // the bass note is never released: closed at the end of the track
  const smf = asc('MThd').concat(be32(6), [0, 0, 0, 1, div >> 8, div & 255], asc('MTrk'), be32(ev.length), ev);
  const ctab = (src, name, dest, nttv) => {
    const n = name.padEnd(8).slice(0, 8);
    const head = [src, ...asc(n), dest, 0, 0x0f, 0xff, 0x07, 0xff, 0xff, 0xff, 0xff, root, 2];
    const body = sff2 || nttv === 'f2'
      ? head.concat([0, 0, 0, 0, 0, 0, 1, (nttv === 'f2' ? 1 : nttv) | 0x80, 0, 127, 0, 0, 0, 0, 0, 0])
      : head.concat([0, nttv, 6, 0, 127, 1, 0]);
    return asc(sff2 ? 'Ctb2' : 'Ctab').concat(be32(body.length), body);
  };
  const sdec = asc('Sdec').concat(be32(withCD ? 13 : 6), asc(withCD ? 'Main A,Main B' : 'Main A'));
  const cseg = sdec.concat(ctab(9, 'Rhythm2', 9, 0), ctab(10, 'Bass', 10, ntt), ctab(11, 'Chord1', 11, 2), ctab(13, 'Pad', 13, 1));
  const casmBody = asc('CSEG').concat(be32(cseg.length), cseg);
  const casm = asc('CASM').concat(be32(casmBody.length), casmBody);
  return new Uint8Array(smf.concat(casm));
}
const styJson = (st) => ({
  id: st.id, name: st.name, tempo: st.tempo, meter: st.meter, beatsPerBar: st.beatsPerBar, sections: st.sections || null,
  gmPrograms: st.gmPrograms || {},
  tracks: Object.fromEntries(Object.entries(st.tracks).map(([k, t]) => [k, [t.role, t.channel, t.transpose, t.orgInstrument ?? -1]])),
  parts: Object.fromEntries(Object.entries(st.parts).map(([k, p]) => [k, { bars: p.bars, beats: p.beats, events: p.events.map((e) => [e.track, r6(e.t), e.n, e.v, r6(e.d)]) }])),
});
{
  const cases = [];
  const variants = [['plain', {}], ['rootF', { root: 5 }], ['rootA', { root: 9 }], ['bypass', { ntt: 0 }], ['sff2', { sff2: true }], ['cd', { withCD: true }]];
  for (const [name, opts] of variants) {
    const bytes = makeSty(opts);
    writeFileSync(join(fx, `synth_${name}.sty`), bytes);
    cases.push({ file: `synth_${name}.sty`, format: sniffFormat(bytes), styles: convertStyle(bytes, { id: 'sty:' + name, name }).map(styJson) });
  }
  out('sty.json', cases);
}

// ── ORG 24 .Set: a synthetic set (same builder as tests/run.mjs) ──
function le32(n) { return [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255]; }
function tag(name, ver = '1.2.8') { const s = asc(name + '\r' + ver + '\r'); return [...le32(s.length), ...s]; }
function nameBlock(nm) { const o = [0x20]; for (let i = 0; i < 31; i++) o.push(i < nm.length ? nm.charCodeAt(i) : 0); return o; }
function sample(rootNote, nm, frames, { loop = false, channels = 1, lead = 0, cents = 0, volume = 100 } = {}) {
  const hdr = [...le32(0), ...le32(frames), ...le32(0), ...le32(volume), ...le32(0), ...le32(0), ...le32(cents >>> 0), ...le32(loop ? 2 : 0), ...le32(loop ? 10 : 0), ...le32(frames - 1), ...le32(0), ...le32(0), ...le32(0), ...le32(0)];
  const pcm = [];
  for (let i = 0; i < frames * channels; i++) { const f = Math.floor(i / channels); const v = f < lead ? 0 : Math.round(Math.sin(f / 5) * 8000); pcm.push(v & 255, (v >> 8) & 255); }
  return [rootNote, ...nameBlock(nm), 48000 & 255, 48000 >> 8, 16, channels, ...le32(48000), ...hdr, ...pcm];
}
function row(note, vel, t, d) { return [...le32(2), ...le32(0x1234), ...le32(note), ...le32(vel), ...le32(t), ...le32(d), ...le32(1), ...le32(0x1234), ...le32(note), ...le32(-1 >>> 0), ...le32(t + d), ...le32(-1 >>> 0)]; }
function pattern(trackName, partId, type, rows) { const h = new Array(535).fill(0); le32(partId).forEach((v, i) => { h[i] = v; }); h[4] = 1; h[6] = type; h[12] = 3; return [...tag(trackName), ...h, ...rows.flat()]; }
function partRec(id, nm, type, len, link) { return [...le32(id), ...le32(0x219af380), ...nameBlock(nm), type, 1, 1, 1, ...le32(0), ...le32(len), ...le32(link), ...le32(0xff), ...le32(0), ...le32(0x71), ...le32(1)]; }
function makeSet() {
  const head = asc('Music Studio Music'); while (head.length < 36) head.push(0); head.push(...le32(7), ...asc('\r1.2.8\r')); while (head.length < 420) head.push(0);
  const trackRec = () => new Array(2508).fill(0);
  return new Uint8Array([
    ...head,
    ...tag('Oud Test'), ...new Array(130).fill(0), ...sample(60, 'c4', 300, { lead: 40 }), ...sample(72, 'c5', 200, { loop: true, cents: -20 }),
    ...tag('Kit Test'), ...new Array(130).fill(0), ...sample(36, 'kick', 100), ...sample(38, 'snare', 100), ...sample(42, 'hat', 50), ...sample(49, 'tak', 50), ...sample(50, 'dum', 50), ...sample(51, 'clap', 50), ...sample(52, 'x', 50, { channels: 2, volume: 140 }),
    ...tag('Normal'), ...new Array(240).fill(0), ...tag('Master'), ...new Array(240).fill(0),
    ...tag('Rhythm'), ...trackRec(), ...tag('Wave'), ...trackRec(), ...tag('Bass'), ...trackRec(), ...tag('Acc. 1'), ...trackRec(), ...tag('Drum'), ...trackRec(),
    ...pattern('Bass', 0x1111, 8, [row(36, 120, 0, 250), row(43, 200, 500, 250)]),
    ...pattern('Acc. 1', 0x1111, 11, [row(60, 100, 0, 200), row(64, 100, 0, 200), row(67, 100, 0, 200), row(62, 90, 2100, 100)]),
    ...pattern('Drum', 0x1111, 9, [row(36, 127, 0, 100), row(38, 110, 500, 100)]),
    ...pattern('Bass', 0x2222, 8, [row(36, 120, 0, 250)]),
    ...pattern('Drum', 0x3333, 9, [row(49, 127, 0, 100), row(49, 127, 250, 100), row(49, 127, 500, 100), row(49, 127, 750, 100)]),
    ...pattern('Drum', 0x4444, 9, [row(51, 100, 0, 1)]),
    ...new Array(64).fill(0),
    ...partRec(0x1111, 'Variation 1', 0x20, 2000, 0), ...partRec(0x2222, 'Variation 2', 0x21, 1000, 0), ...partRec(0x3333, 'Fill 1', 0x30, 1000, 0xfffffffe), ...partRec(0x9999, 'Intro 1', 0x10, 1000, 0x1111), ...partRec(0x4444, 'Variation 3', 0x22, 1400, 0),
    ...new Array(64).fill(0),
  ]);
}
function orgJson(p, styles) {
  return {
    version: p.version,
    instruments: p.instruments.map((i) => ({ name: i.name, isKit: i.isKit, samples: i.samples.map((s) => [s.root, s.name, s.rate, s.channels, s.frames, s.volume, s.cents, s.loop, s.loopStart, s.loopEnd, s.dataOffset]) })),
    rhythm: p.rhythm && {
      tracks: Object.fromEntries(Object.entries(p.rhythm.tracks).map(([k, t]) => [k, [t.instrumentHint, t.instrumentHint2]])),
      patterns: p.rhythm.patterns.map((x) => [x.partId, x.track, x.type, x.instrumentHint, x.rows.map((r) => [r.note, r.vel, r.t, r.d])]),
      parts: p.rhythm.parts.map((x) => [x.id, x.name, x.type, x.lengthTicks, x.link]),
    },
    assign: assignInstruments(p),
    styles: styles.map(styJson),
  };
}
{
  const bytes = makeSet();
  writeFileSync(join(fx, 'synth.set'), bytes);
  const p = parseOrgSet(bytes);
  out('orgset.json', orgJson(p, orgStyles(p, { id: 'org:synth', name: 'Synth', tempo: 165 })));
}

// ── the owner's own files (local only) ──
for (const path of process.argv.slice(2)) {
  mkdirSync(local, { recursive: true });
  const bytes = new Uint8Array(readFileSync(path));
  const kind = sniffFormat(bytes);
  const name = basename(path).replace(/\.[^.]+$/, '');
  let obj;
  if (kind === 'org24') { const p = parseOrgSet(bytes); obj = { file: path, kind, ...orgJson(p, orgStyles(p, { id: 'org:' + name, name, tempo: 165 })) }; }
  else if (kind === 'yamaha') obj = { file: path, kind, styles: convertStyle(bytes, { id: 'sty:' + name, name }).map(styJson) };
  else { console.log('skipped', path, kind); continue; }
  writeFileSync(join(local, name + '.json'), JSON.stringify(obj) + '\n');
  console.log('local fixture', join(local, name + '.json'));
}
console.log('fixtures written to', fx);
