// node tests/run.mjs — engine logic tests (no browser needed).
import { detectChord, transposeNote, chordName, CHORD_TYPES } from '../js/chord.js';
import { compileStyle, lintPack, noteNumber } from '../js/styledsl.js';
import { STYLE_PACK } from '../js/styles.js';
import { Sequencer } from '../js/sequencer.js';
import { parseSMF, parseCASM, convertStyle } from '../js/sty.js';
import { parseOrgSet, orgStyles, assignInstruments, isOrgSet } from '../js/orgset.js';
import { orderStyles, stepStyle, styleGroups, soundGroups, flatKeys, filterGroups, gmFamily } from '../js/catalog.js';

let pass = 0, fail = 0;
const ok = (cond, msg) => { if (cond) pass++; else { fail++; console.log('FAIL', msg); } };
const eq = (a, b, msg) => ok(JSON.stringify(a) === JSON.stringify(b), `${msg}: got ${JSON.stringify(a)} want ${JSON.stringify(b)}`);

// ── chord detection: every type in all 12 keys, fingered, three voicings ──
for (let root = 0; root < 12; root++) {
  for (const [type, def] of Object.entries(CHORD_TYPES)) {
    const base = 48 + root;
    const rootPos = def.intervals.map((i) => base + i);
    const c1 = detectChord(rootPos, 'fingered');
    eq(c1 && [c1.root, c1.type], [root, type], `fingered ${chordName({ root, type })} root position`);
    // first inversion (lowest note raised an octave)
    const inv = [...rootPos.slice(1), rootPos[0] + 12];
    const c2 = detectChord(inv, 'fingered');
    if (type === 'aug') ok(c2 && c2.type === 'aug', 'aug inversion is still aug (symmetric chord)');
    else eq(c2 && [c2.root, c2.type], [root, type], `fingered ${chordName({ root, type })} 1st inversion`);
    // octave-doubled root
    const c3 = detectChord([...rootPos, base + 12], 'fingered');
    eq(c3 && [c3.root, c3.type], [root, type], `fingered ${chordName({ root, type })} doubled root`);
  }
  // one-finger mode
  const r = 48 + root;
  eq(detectChord([r], 'single').type, 'maj', 'single: 1 key = major');
  eq(detectChord([r, r - 3], 'single').type, 'min', 'single: black key below = minor');
  eq(detectChord([r, r - 2], 'single').type, 'dom7', 'single: white key below = 7th');
  eq(detectChord([r, r - 2, r - 3], 'single').type, 'min7', 'single: both = m7');
  eq(detectChord([r], 'single').root, root, 'single: root');
}
ok(detectChord([], 'fingered') === null, 'empty = null');
ok(detectChord([60, 61], 'fingered') === null, 'C + C# = no chord');

// ── transposition ──
eq(transposeNote(36, 'none', { root: 7, type: 'maj' }), 36, 'none keeps');
eq(transposeNote(36, 'root', { root: 7, type: 'maj' }), 31, 'root G folds down');
eq(transposeNote(36, 'root', { root: 2, type: 'maj' }), 38, 'root D goes up');
eq(transposeNote(52, 'chord', { root: 0, type: 'min' }), 51, 'E over C minor = Eb');
eq(transposeNote(52, 'chord', { root: 9, type: 'min' }), 48, 'E over A minor = C');
eq(transposeNote(55, 'chord', { root: 0, type: 'dim' }), 54, 'G over C dim = Gb');
eq(transposeNote(59, 'chord', { root: 0, type: 'dom7' }), 58, 'B over C7 = Bb');
eq(transposeNote(59, 'chord', { root: 0, type: 'maj7' }), 59, 'B over Cmaj7 = B');
eq(transposeNote(52, 'chord', { root: 0, type: 'sus4' }), 53, 'E over Csus4 = F');

// ── style DSL ──
eq(noteNumber('C4'), 60, 'C4 = 60');
eq(noteNumber('A0'), 21, 'A0 = 21');
eq(noteNumber('Bb2'), 46, 'Bb2 = 46');
const problems = lintPack(STYLE_PACK);
eq(problems, [], 'built-in pack lints clean');
eq(STYLE_PACK.length, 12, 'twelve built-in styles');
eq(STYLE_PACK.filter((s) => s.group === 'sudan').map((s) => s.id), ['tumtum', 'daloka', 'mardoum', 'jarari', 'karachi', 'dalib', 'haqiba'], 'the Sudanese set, in keyboard order');
ok(STYLE_PACK.every((s) => s.group === 'sudan' || s.group === 'west'), 'every style has a group');
const byId = (id) => compileStyle(STYLE_PACK.find((s) => s.id === id));
// Tum Tum: a triple rhythm — dum tak dum, two on the mambo — with claps in 3 against the 2 (the 3:2 cross-rhythm).
const tumtum = byId('tumtum');
eq(tumtum.meter, [6, 8], 'Tum Tum is 6/8');
eq(tumtum.parts.mainA.beats, 6, 'Tum Tum main A = 2 bars of 6/8');
const bar1 = (st, drum) => st.parts.mainA.events.filter((e) => e.n === drum && e.t < st.beatsPerBar).map((e) => +e.t.toFixed(3));
eq(bar1(tumtum, 63), [0, 1], 'tum tum: dum on 1 and 3 (eighths)');
eq(bar1(tumtum, 60), [0.5], 'tum tum: tak on the 2nd eighth');
eq(bar1(tumtum, 64), [1.5, 2], 'tum tum: two mambo strokes after dum tak dum');
eq(bar1(tumtum, 39), [0, 1, 2], 'tum tum: three claps per bar against the two-pulse drum');
// Mardoum / Jarari are 6/8 (fast / slow); Jarari is the softer one.
eq(byId('mardoum').meter, [6, 8], 'Mardoum is 6/8');
eq(byId('jarari').meter, [6, 8], 'Jarari is 6/8');
ok(byId('mardoum').tempo > byId('jarari').tempo + 30, 'Mardoum is much faster than Jarari');
// Karachi: 2/4, starts on TAK (tak-tak-dum), not on dum.
const karachi = byId('karachi');
eq(karachi.meter, [2, 4], 'Karachi is 2/4');
eq(bar1(karachi, 60), [0, 0.5], 'karachi: tak tak');
eq(bar1(karachi, 63), [1], 'karachi: then dum');
// Daloka / Sayra: 4/4 with the mambo on 1 and 3 and claps on 2 and 4.
const daloka = byId('daloka');
eq(daloka.meter, [4, 4], 'Daloka is 4/4');
eq(bar1(daloka, 64), [0, 2], 'daloka: mambo on 1 and 3');
eq(bar1(daloka, 39), [1, 3], 'daloka: claps on 2 and 4');
const tum = daloka;                      // 4/4 style for the sequencer tests below
const pop = byId('pop');
ok(pop.parts.mainA.events.some((e) => e.track === 'drums' && e.n === 36 && e.t === 0), 'pop: kick on beat 1');
ok(tum.parts.mainA.events.every((e) => e.t < 8), 'no event past part end');
const six = compileStyle(STYLE_PACK.find((s) => s.id === 'six8'));
eq(six.beatsPerBar, 3, '6/8 = 3 quarter beats per bar');
eq(six.steps, 12, '6/8 = 12 steps per bar');
const bal = compileStyle(STYLE_PACK.find((s) => s.id === 'ballad'));
const longBass = bal.parts.mainA.events.find((e) => e.track === 'bass' && e.t === 0);
ok(longBass && Math.abs(longBass.d - 8 * 0.25 * 0.8) < 1e-9, 'C2:8 lasts 8 steps × gate');
ok(lintPack([{ id: 'x', name: 'x', tracks: { drums: { role: 'drums', channel: 9, transpose: 'none' } }, parts: {} }]).length > 0, 'lint catches missing parts');

// ── sequencer: part chaining, fills, ending, zero drift ──
function makeSeq() {
  let now = 0;
  const played = [];
  const timers = [];
  const seq = new Sequencer({
    clock: { now: () => now },
    sink: { noteOn: (ch, n, v, t, d) => played.push({ ch, n, v, t, d }), allOff: () => {} },
    setTimer: (fn) => { timers.push(fn); return timers.length; },
    clearTimer: () => { timers.length = 0; },
  });
  const advance = (dt) => { now += dt; for (const f of [...timers]) f(); };
  return { seq, played, advance, now: () => now };
}
{
  const { seq, played, advance } = makeSeq();
  seq.setStyle(tum);
  seq.setTempo(120);               // 0.5 s per beat, 2 s per bar, mainA = 4 s
  seq.setChord({ root: 7, type: 'maj' });
  seq.start();
  const parts = [];
  seq.on((type, d) => { if (type === 'part') parts.push(d); });
  seq.fill('B');
  for (let i = 0; i < 400; i++) advance(0.05);   // 20 s
  eq(parts.slice(0, 3), ['fillA', 'mainB', 'mainB'], 'fill then main B, then main B repeats');
  const bass = played.filter((e) => e.ch === 2);
  ok(bass.length > 0 && bass.every((e) => [31, 38, 33, 35, 42].includes(e.n)), 'bass follows G major (root folded down)');
  seq.ending();
  for (let i = 0; i < 100; i++) advance(0.05);
  ok(parts.includes('ending') && !seq.playing, 'ending stops the sequencer');
}
{
  // Drift: 10,000 bars of main A chained by arithmetic must land exactly on bar boundaries.
  const { seq, played, advance } = makeSeq();
  seq.setStyle(pop);
  seq.setTempo(240);               // 1 s per bar
  seq.start();
  const total = 10000;
  for (let i = 0; i < total * 20 + 40; i++) advance(0.05);
  const t0 = played[0].t;
  const kicks = played.filter((e) => e.ch === 9 && e.n === 36 && Math.abs((e.t - t0) - Math.round(e.t - t0)) < 1e-6);
  const expected = kicks.filter((e) => e.t - t0 <= total).length;
  ok(expected === total + 1, `kick lands on every bar line over ${total} bars (${expected})`);
  let maxErr = 0;
  for (const e of played) maxErr = Math.max(maxErr, Math.abs((e.t - t0) * 16 - Math.round((e.t - t0) * 16)));
  ok(maxErr < 1e-6, `max timing error ${maxErr} s`);
}
{
  const { seq, played, advance } = makeSeq();
  seq.setStyle(tum);
  seq.armSyncStart(true);
  advance(1);
  ok(played.length === 0 && !seq.playing, 'sync start waits');
  seq.start();
  ok(seq.playing && !seq.syncArmed, 'start clears sync arm');
}

// ── Yamaha style import: a synthetic SFF1 file (SMF0 + markers + CASM) ──
function vlq(n) { const out = [n & 0x7f]; n >>= 7; while (n) { out.unshift((n & 0x7f) | 0x80); n >>= 7; } return out; }
function be32(n) { return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]; }
function makeSty({ root = 0, ntt = 1 } = {}) {
  const div = 480;
  const ev = [];
  const push = (dt, ...bytes) => ev.push(...vlq(dt), ...bytes);
  const marker = (dt, name) => push(dt, 0xff, 0x06, name.length, ...[...name].map((c) => c.charCodeAt(0)));
  marker(0, 'SFF1'); marker(0, 'SInt');
  push(0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20);          // 120 bpm
  push(0, 0xff, 0x58, 4, 4, 2, 24, 8);               // 4/4
  push(0, 0xc0 | 10, 33);                             // bass program 33 on channel 11
  push(0, 0xc0 | 11, 21);                             // chord 1 program 21 (accordion)
  marker(div * 4, 'Main A');                          // 1 bar: kick on 1 and 3, bass root+5th, chord on 2 and 4
  push(0, 0x99, 36, 100); push(0, 0x9a, 36 + root, 90);                                   // beat 1: kick + bass root
  push(div / 2, 0x89, 36, 0); push(div / 2, 0x8a, 36 + root, 0);
  push(0, 0x9b, 60 + root, 80); push(0, 0x9b, 64 + root, 80); push(0, 0x9b, 67 + root, 80); // beat 2: chord
  push(div / 2, 0x8b, 60 + root, 0); push(0, 0x8b, 64 + root, 0); push(0, 0x8b, 67 + root, 0);
  push(div / 2, 0x99, 36, 100); push(0, 0x9a, 43 + root, 90);                              // beat 3: kick + bass fifth
  push(div / 2, 0x89, 36, 0); push(0, 0x8a, 43 + root, 0);
  push(div / 2, 0x9b, 60 + root, 80); push(div / 2, 0x8b, 60 + root, 0);                   // beat 4: chord
  marker(div / 2, 'Fill In AA');
  push(0, 0x99, 38, 100); push(div * 4, 0x89, 38, 0);
  marker(0, 'Ending A');
  push(0, 0x99, 49, 100); push(div * 4, 0x89, 49, 0);
  push(0, 0xff, 0x2f, 0);
  const smf = [...'MThd'].map((c) => c.charCodeAt(0)).concat(be32(6), [0, 0, 0, 1, div >> 8, div & 255], [...'MTrk'].map((c) => c.charCodeAt(0)), be32(ev.length), ev);
  const ctab = (src, name, dest, nttv) => { const n = name.padEnd(8).slice(0, 8); return [...'Ctab'].map((c) => c.charCodeAt(0)).concat(be32(27), [src, ...[...n].map((c) => c.charCodeAt(0)), dest, 0, 0x0f, 0xff, 0x07, 0xff, 0xff, 0xff, 0xff, root, 2, 0, nttv, 6, 0, 127, 1, 0]); };
  const sdec = [...'Sdec'].map((c) => c.charCodeAt(0)).concat(be32(6), [...'Main A'].map((c) => c.charCodeAt(0)));
  const cseg = sdec.concat(ctab(9, 'Rhythm2', 9, 0), ctab(10, 'Bass', 10, ntt), ctab(11, 'Chord1', 11, 2));
  const casmBody = [...'CSEG'].map((c) => c.charCodeAt(0)).concat(be32(cseg.length), cseg);
  const casm = [...'CASM'].map((c) => c.charCodeAt(0)).concat(be32(casmBody.length), casmBody);
  return new Uint8Array(smf.concat(casm));
}
{
  const bytes = makeSty();
  const smf = parseSMF(bytes);
  eq(smf.tempo, 120, 'sty: tempo meta');
  eq(smf.meter, [4, 4], 'sty: time signature');
  eq(smf.markers.map((m) => m.name), ['SFF1', 'SInt', 'Main A', 'Fill In AA', 'Ending A'], 'sty: markers');
  eq(smf.programs[10].program, 33, 'sty: program change read');
  const casm = parseCASM(bytes, smf.midiEnd, false);
  eq(casm.parts, ['Main A'], 'sty: Sdec parts');
  eq([casm.ctabs[10].dest, casm.ctabs[10].ntt, casm.ctabs[11].ntt], [10, 1, 2], 'sty: Ctab dest + NTT');
  const [st] = convertStyle(bytes, { id: 'sty:test', name: 'Test' });
  eq(Object.keys(st.parts).sort(), ['ending', 'fillA', 'fillB', 'intro', 'mainA', 'mainB'], 'sty: all six arranger parts exist');
  eq(st.parts.mainA.bars, 1, 'sty: Main A = 1 bar');
  eq(st.parts.mainB.events.length, st.parts.mainA.events.length, 'sty: missing Main B falls back to Main A');
  eq(st.parts.fillA.events.map((e) => e.n), [38], 'sty: Fill In AA → fillA');
  eq(st.parts.ending.events.map((e) => e.n), [49], 'sty: Ending A → ending');
  eq(st.parts.fillB.events.length, 1, 'sty: missing Fill In BB falls back to Fill In AA');
  eq(st.parts.intro.events.length, st.parts.mainA.events.filter((e) => e.t < 4).length, 'sty: missing intro = one bar of main');
  const bass = st.parts.mainA.events.filter((e) => e.track === 'bass').map((e) => [e.t, e.n]);
  eq(bass, [[0, 36], [2, 43]], 'sty: bass notes on beats 1 and 3, ticks → beats');
  eq(st.tracks.drums.transpose, 'none', 'sty: rhythm channel is never transposed');
  eq(st.tracks.bass.transpose, 'chord', 'sty: bass follows the chord');
  eq(st.gmPrograms, { 2: 33, 3: 21 }, 'sty: GM programs land on the arranger channels');
  ok(!st.tracks.pad && !st.tracks.phrase, 'sty: silent channels are dropped');
  // Recorded over F (root 5): the pattern is shifted back to C before the chord re-map.
  const [stF] = convertStyle(makeSty({ root: 5 }), { id: 'f', name: 'F' });
  eq(stF.parts.mainA.events.filter((e) => e.track === 'bass').map((e) => e.n), [36, 43], 'sty: source root F folded back to C');
  // The imported style runs in the sequencer and follows a chord.
  const { seq, played, advance } = makeSeq();
  seq.setStyle(st); seq.setTempo(120); seq.setChord({ root: 7, type: 'min' }); seq.start();
  for (let i = 0; i < 100; i++) advance(0.05);
  const bassPlayed = played.filter((e) => e.ch === 2).map((e) => e.n);
  ok(bassPlayed.length >= 2 && bassPlayed.every((n) => [31, 38].includes(n)), `sty: imported bass follows G minor (${bassPlayed.slice(0, 4)})`);
  ok(played.some((e) => e.ch === 3 && e.n % 12 === 10), 'sty: chord track plays the minor third (Bb over Gm)');
  ok(convertStyle(makeSty({ ntt: 0 }), { id: 'b', name: 'b' })[0].tracks.bass.transpose === 'none', 'sty: NTT bypass → no transposition');
  let threw = false; try { convertStyle(new Uint8Array([1, 2, 3, 4]), {}); } catch (_) { threw = true; }
  ok(threw, 'sty: garbage is refused');
}

// ── ORG 24 .Set reader: a synthetic set with one instrument (2 samples) and a rhythm (2 parts) ──
function le32(n) { return [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255]; }
function tag(name, ver = '1.2.8') { const s = [...(name + '\r' + ver + '\r')].map((c) => c.charCodeAt(0)); return [...le32(s.length), ...s]; }
function nameBlock(nm) { const out = [0x20]; for (let i = 0; i < 31; i++) out.push(i < nm.length ? nm.charCodeAt(i) : 0); return out; }
function sample(root, nm, frames, { loop = false, channels = 1 } = {}) {
  const hdr = [...le32(0), ...le32(frames), ...le32(0), ...le32(100), ...le32(0), ...le32(0), ...le32(0), ...le32(loop ? 2 : 0), ...le32(0), ...le32(frames - 1), ...le32(0), ...le32(0), ...le32(0), ...le32(0)];
  const pcm = []; for (let i = 0; i < frames * channels; i++) { const v = Math.round(Math.sin(i / 5) * 8000); pcm.push(v & 255, (v >> 8) & 255); }
  return [root, ...nameBlock(nm), 48000 & 255, 48000 >> 8, 16, channels, ...le32(48000), ...hdr, ...pcm];
}
function row(note, vel, t, d) { return [...le32(2), ...le32(0x1234), ...le32(note), ...le32(vel), ...le32(t), ...le32(d), ...le32(1), ...le32(0x1234), ...le32(note), ...le32(-1 >>> 0), ...le32(t + d), ...le32(-1 >>> 0)]; }
function pattern(trackName, partId, type, rows) { const h = new Array(535).fill(0); le32(partId).forEach((v, i) => { h[i] = v; }); h[4] = 1; h[6] = type; h[12] = 3; return [...tag(trackName), ...h, ...rows.flat()]; }
function partRec(id, nm, type, len, link) { return [...le32(id), ...le32(0x219af380), ...nameBlock(nm), type, 1, 1, 1, ...le32(0), ...le32(len), ...le32(link), ...le32(0xff), ...le32(0), ...le32(0x71), ...le32(1)]; }
function makeSet() {
  const head = [...'Music Studio Music'].map((c) => c.charCodeAt(0)); while (head.length < 36) head.push(0); head.push(...le32(7), ...[...'\r1.2.8\r'].map((c) => c.charCodeAt(0))); while (head.length < 420) head.push(0);
  const trackRec = () => new Array(2508).fill(0);
  const bytes = [
    ...head,
    ...tag('Oud Test'), ...new Array(130).fill(0), ...sample(60, 'c4', 300), ...sample(72, 'c5', 200, { loop: true }),
    ...tag('Kit Test'), ...new Array(130).fill(0), ...sample(36, 'kick', 100), ...sample(38, 'snare', 100), ...sample(42, 'hat', 50), ...sample(49, 'tak', 50), ...sample(50, 'dum', 50), ...sample(51, 'clap', 50), ...sample(52, 'x', 50),
    ...tag('Normal'), ...new Array(240).fill(0), ...tag('Master'), ...new Array(240).fill(0),
    ...tag('Rhythm'), ...trackRec(), ...tag('Wave'), ...trackRec(), ...tag('Bass'), ...trackRec(), ...tag('Acc. 1'), ...trackRec(), ...tag('Drum'), ...trackRec(),
    ...pattern('Bass', 0x1111, 8, [row(36, 120, 0, 250), row(43, 120, 500, 250)]),
    ...pattern('Acc. 1', 0x1111, 11, [row(60, 100, 0, 200), row(64, 100, 0, 200), row(67, 100, 0, 200)]),
    ...pattern('Drum', 0x1111, 9, [row(36, 127, 0, 100), row(38, 110, 500, 100)]),
    ...pattern('Bass', 0x2222, 8, [row(36, 120, 0, 250)]),
    ...pattern('Drum', 0x3333, 9, [row(49, 127, 0, 100), row(49, 127, 250, 100), row(49, 127, 500, 100), row(49, 127, 750, 100)]),
    ...new Array(64).fill(0),
    ...partRec(0x1111, 'Variation 1', 0x20, 2000, 0), ...partRec(0x2222, 'Variation 2', 0x21, 1000, 0), ...partRec(0x3333, 'Fill 1', 0x30, 1000, 0xfffffffe), ...partRec(0x9999, 'Intro 1', 0x10, 1000, 0x1111),
    ...new Array(64).fill(0),
  ];
  return new Uint8Array(bytes);
}
{
  const bytes = makeSet();
  ok(isOrgSet(bytes), 'org: header recognised');
  const p = parseOrgSet(bytes);
  eq(p.version, '1.2.8', 'org: version read');
  eq(p.instruments.map((i) => [i.name, i.samples.length, i.isKit]), [['Oud Test', 2, false], ['Kit Test', 7, true]], 'org: instruments + sample chains walked, kit detected');
  const oud = p.instruments[0];
  eq(oud.samples.map((s) => [s.root, s.name, s.frames, s.loop, s.rate]), [[60, 'c4', 300, false, 48000], [72, 'c5', 200, true, 48000]], 'org: sample header fields');
  eq(bytes[oud.samples[0].dataOffset] | (bytes[oud.samples[0].dataOffset + 1] << 8), Math.round(Math.sin(0) * 8000) & 0xffff, 'org: PCM starts right after the 97-byte record head');
  eq(Object.keys(p.rhythm.tracks).sort(), ['acc1', 'bass', 'drums'], 'org: track records (Wave ignored)');
  eq(p.rhythm.patterns.map((x) => [x.track, x.partId, x.rows.length]), [['bass', 0x1111, 2], ['acc1', 0x1111, 3], ['drums', 0x1111, 2], ['bass', 0x2222, 1], ['drums', 0x3333, 4]], 'org: patterns by part and track with note rows');
  eq(p.rhythm.parts.map((x) => [x.name, x.lengthTicks, x.type]), [['Variation 1', 2000, 0x20], ['Variation 2', 1000, 0x21], ['Fill 1', 1000, 0x30]], 'org: part table (only parts that have patterns)');
  eq(assignInstruments(p), { bass: 1, acc1: 0, drums: 1 }, 'org: chords → the oud multisample (60–72), bass (36/43) and drums → the kit whose roots cover them');
  const [st] = orgStyles(p, { id: 'org:t', name: 'T' });
  eq([st.parts.mainA.bars, st.parts.mainB.bars, st.parts.fillA.bars, st.parts.intro.bars], [2, 1, 1, 2], 'org: 1000 ticks = one bar; missing intro falls back to Variation 1');
  const bass = st.parts.mainA.events.filter((e) => e.track === 'bass').map((e) => [e.t, e.n]);
  eq(bass, [[0, 36], [1.5, 43]], 'org: ticks → beats of 6/8 (500 ticks = 1.5 beats)');
  eq(st.tracks.drums.transpose, 'none', 'org: drums never transposed');
  eq(st.tracks.acc1.orgInstrument, 0, 'org: track carries its set instrument');
  ok(st.parts.fillA.events.every((e) => e.n === 49) && st.parts.fillA.events.length === 4, 'org: Fill 1 → fillA');
  const { seq, played, advance } = makeSeq();
  seq.setStyle(st); seq.setTempo(120); seq.setChord({ root: 2, type: 'min' }); seq.start();
  for (let i = 0; i < 80; i++) advance(0.05);
  ok(played.some((e) => e.ch === 3 && e.n % 12 === 5), 'org: chord track follows D minor (F natural)');
  ok(played.some((e) => e.ch === 9 && e.n === 36), 'org: kick plays unchanged on the drum channel');
  let threw = false; try { parseOrgSet(new Uint8Array([77, 84, 104, 100])); } catch (_) { threw = true; }
  ok(threw, 'org: a non-set is refused');
}

// ── the pickers: order, grouping, search, stepping ──
{
  const mk = (id, group, extra = {}) => ({ id, name: id.toUpperCase(), name_ar: id, group, meter: [4, 4], tempo: 100, ...extra });
  const list = [mk('pop', 'west'), mk('tumtum', 'sudan', { meter: [6, 8], tempo: 156 }), mk('b', 'yamaha', { folder: 'Sudan' }), mk('loose', 'yamaha'), mk('a', 'yamaha', { folder: 'Sudan' }), mk('z', 'yamaha', { folder: 'Arabic' }), mk('set', 'org')];
  const ordered = orderStyles(list);
  eq(ordered.map((s) => s.id), ['z', 'a', 'b', 'loose', 'set', 'tumtum', 'pop'], 'picker: own files first (folders by name, then loose files), set, Sudanese, Western');
  eq(orderStyles(list, { showBuiltin: false }).map((s) => s.id), ['z', 'a', 'b', 'loose', 'set'], 'picker: built-ins can be hidden');
  eq(orderStyles([mk('s10', 'yamaha'), mk('s2', 'yamaha')]).map((s) => s.id), ['s2', 's10'], 'picker: numeric-aware name order');
  eq(stepStyle(ordered, 'pop', 1).id, 'z', 'picker: next wraps to the first');
  eq(stepStyle(ordered, 'z', -1).id, 'pop', 'picker: previous wraps to the last');
  eq(stepStyle(ordered, 'missing', 1).id, 'z', 'picker: an unknown id steps to the first');
  eq(stepStyle([], 'x', 1), null, 'picker: empty list → null');
  const groups = styleGroups(ordered, { labels: { yamaha: 'Yours', sudan: 'Sudanese', west: 'Western', org: 'Set' } });
  eq(groups.map((g) => [g.title, g.items.length]), [['Arabic', 1], ['Sudan', 2], ['Yours', 1], ['Set', 1], ['Sudanese', 1], ['Western', 1]], 'picker: a group per folder when there is more than one folder; loose files under the plain group');
  eq(groups[4].items[0].meta, '6/8 · ♩. 104', 'picker: 6/8 shows the dotted-quarter tempo');
  const one = styleGroups(orderStyles(list.filter((s) => s.folder !== 'Arabic' && s.id !== 'loose')), { labels: { yamaha: 'Yours' } });
  eq(one[0].title, 'Yours', 'picker: one folder → the plain "your rhythms" group');
  eq(filterGroups(groups, 'TUM').map((g) => g.title), ['Sudanese'], 'picker: search is case-insensitive over the label');
  eq(filterGroups(groups, 'sudan').map((g) => g.items.length), [2, 1], 'picker: a hit on the group title keeps the whole group');
  eq(filterGroups(groups, '  ').length, 6, 'picker: blank query keeps everything');
  eq(filterGroups(groups, 'nothing-here'), [], 'picker: no match → no groups');
  const progs = [
    { kind: 'synth', name: 'lead', key: 'synth:lead', label: 'Synth lead', label_ar: 'ليد' },
    { kind: 'sf2', font: 0, fontName: 'GM', bank: 0, program: 0, key: 'sf2:0:0:0', label: 'Piano', label_ar: 'Piano' },
    { kind: 'sf2', font: 0, fontName: 'GM', bank: 0, program: 24, key: 'sf2:0:0:24', label: 'Nylon', label_ar: 'Nylon' },
    { kind: 'sf2', font: 0, fontName: 'GM', bank: 8, program: 24, key: 'sf2:0:8:24', label: 'Ukulele', label_ar: 'Ukulele' },
    { kind: 'sf2', font: 1, fontName: 'Oud', bank: 0, program: 25, key: 'sf2:1:0:25', label: 'Oud', label_ar: 'Oud' },
    { kind: 'org', index: 2, key: 'org:2', label: 'Saz', label_ar: 'Saz' },
  ];
  const sg = soundGroups(progs, { labels: { bank: 'Bank', org: 'Set', synth: 'Built-in' } });
  eq(sg.map((g) => g.title), ['GM · Piano', 'GM · Guitar', 'GM · Bank 8', 'Oud · Guitar', 'Set', 'Built-in'], 'picker: sounds grouped by bank file and GM family, built-ins last');
  eq(flatKeys(sg), ['sf2:0:0:0', 'sf2:0:0:24', 'sf2:0:8:24', 'sf2:1:0:25', 'org:2', 'synth:lead'], 'picker: flat key order for the arrows');
  eq(gmFamily(0), 'Piano', 'gm family 0'); eq(gmFamily(127), 'Sound effects', 'gm family 127'); eq(gmFamily(105, 'ar'), 'آلات شرقية', 'gm family in Arabic');
}

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
