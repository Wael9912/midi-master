// Midi Master — app controller. Wires MIDI/screen keys → chord zone + voices,
// the sequencer transport, the two pickers (rhythm, sound), setups
// (registrations), imports and the UI.

import { createAudio } from './audio.js';
import { Engine, CH } from './engine.js';
import { Sequencer } from './sequencer.js';
import { MidiInput } from './midi.js';
import { STYLE_PACK } from './styles.js';
import { compileStyle, lintPack } from './styledsl.js';
import { convertStyle, sniffFormat } from './sty.js';
import { parseOrgSet, orgStyles, isOrgSet } from './orgset.js';
import { detectChord, chordName, NOTE_NAMES } from './chord.js';
import { orderStyles, stepStyle, styleGroups, soundGroups, flatKeys } from './catalog.js';
import { ListSheet } from './sheet.js';
import { t } from './i18n.js';
import * as store from './store.js';

export const VERSION = '0.5.1';

// GM programs to prefer for each built-in voice name when an SF2 is loaded.
const GM_FOR = { piano: 0, organ: 16, accordion: 21, oud: 24, tambour: 105, strings: 48, brass: 61, sax: 65, flute: 73, bass: 33, lead: 81 };
const TRACK_LABEL = { drums: 'drumsTrack', perc: 'percTrack', bass: 'bassTrack', chord: 'chordTrack', pad: 'padTrack', phrase: 'phraseTrack' };
const ROLE_OF_CH = { 0: 'upper', 1: 'lower', 2: 'bass', 3: 'chord', 4: 'pad', 5: 'phrase', 6: 'chord', 7: 'phrase', 9: 'drums', 10: 'perc' };

const $ = (id) => document.getElementById(id);

const settings = Object.assign({
  lang: (navigator.language || '').startsWith('ar') ? 'ar' : 'en',
  styleId: 'tumtum', tempo: null, transpose: 0, split: 54, chordMode: 'fingered',
  lowerOn: false, layer: false, keysShown: true, keepAwake: true, seenHint: false, showBuiltin: true, moreOpen: false,
  programs: {}, bus: { keys: 0.8, style: 0.8, drums: 0.8, master: 0.9 }, mute: {},
  regs: [null, null, null, null, null, null, null, null],
}, store.loadSettings());

let audio, engine, seq, midi, sheet;
let styles = [];                 // compiled styles (built-in + imported)
let importedPack = null;
let styFiles = [];               // imported Yamaha style files [{ name, folder, buf }]
let sf2Banks = [];               // loaded SF2 banks [{ name, buf }], in engine order
let orgSet = null;               // { name, buf, parsed } — the imported ORG 24 set
const held = new Map();          // raw note → channels it sounds on
const chordNotes = new Set();    // raw notes in the chord zone
let lastChord = null;
let deferredInstall = null;
let wakeLock = null;
const keyEls = new Map();        // note → on-screen key element
let sheetMode = null;            // 'rhythm' | { sound: ch } while the list sheet is open
let sheetStartBtn = null;        // the START/STOP mirrored into the rhythm list

// ───────────────────────── boot ─────────────────────────
function boot() {
  applyLang();
  $('gateBtn').addEventListener('click', startAudio, { once: true });
  $('langBtn').addEventListener('click', () => { settings.lang = settings.lang === 'ar' ? 'en' : 'ar'; persist(); applyLang(); if (engine) renderAll(); });
  $('version').textContent = VERSION;
  $('gateVersion').textContent = 'v' + VERSION;
  if ('serviceWorker' in navigator && location.protocol !== 'file:') {
    // A new build takes over as soon as it is installed: reload once so the phone never shows a stale version.
    let reloading = false;
    const hadController = !!navigator.serviceWorker.controller;   // first visit: the worker claiming the page is not an update
    navigator.serviceWorker.addEventListener('controllerchange', () => { if (hadController && !reloading) { reloading = true; location.reload(); } });
    navigator.serviceWorker.register('sw.js').then((reg) => { reg.update().catch(() => {}); }).catch(() => {});
  }
  window.addEventListener('beforeinstallprompt', (e) => { e.preventDefault(); deferredInstall = e; renderInstall(); });
  window.addEventListener('appinstalled', () => { deferredInstall = null; renderInstall(); });
  for (const id of ['installBtn', 'installBtn2']) $(id).addEventListener('click', install);
  $('closeInstallHelp').addEventListener('click', () => $('installHelp').close());
  renderInstall();
}

function isStandalone() { return (window.matchMedia && window.matchMedia('(display-mode: standalone)').matches) || window.navigator.standalone === true; }

function renderInstall() {
  const standalone = isStandalone();
  $('installBtn').hidden = standalone || location.protocol === 'file:';
  $('installBtn2').hidden = standalone;
  $('installBtn2').textContent = t(settings.lang, standalone ? 'installed' : 'install');
}

async function install() {
  if (deferredInstall) {
    deferredInstall.prompt();
    const { outcome } = await deferredInstall.userChoice;
    if (outcome === 'accepted') deferredInstall = null;
    renderInstall();
    return;
  }
  $('installHelp').showModal();
}

// A timer in a Worker keeps ticking when Chrome throttles the page's own timers
// (screen dim, another app in front); the sequencer only needs a steady nudge.
function workerTimer() {
  try {
    const src = 'let id=null;onmessage=e=>{if(e.data.cmd==="start"){clearInterval(id);id=setInterval(()=>postMessage(1),e.data.ms)}else{clearInterval(id);id=null}}';
    const w = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
    let cb = null;
    w.onmessage = () => { if (cb) cb(); };
    return {
      setTimer: (fn, ms) => { cb = fn; w.postMessage({ cmd: 'start', ms }); return 1; },
      clearTimer: () => { cb = null; w.postMessage({ cmd: 'stop' }); },
    };
  } catch (_) { return {}; }
}

async function keepAwake() {
  if (!settings.keepAwake || wakeLock || !('wakeLock' in navigator)) return;
  try {
    wakeLock = await navigator.wakeLock.request('screen');
    wakeLock.addEventListener('release', () => { wakeLock = null; });
  } catch (_) { wakeLock = null; }
}
async function releaseAwake() { try { if (wakeLock) await wakeLock.release(); } catch (_) { /* ignore */ } wakeLock = null; }

async function startAudio() {
  audio = createAudio();
  await audio.resume();
  engine = new Engine(audio);
  seq = new Sequencer({
    clock: { now: () => audio.ctx.currentTime },
    sink: { noteOn: (ch, n, v, t0, d) => engine.noteOn(ch, n, v, t0, d), allOff: () => engine.allOff() },
    ...workerTimer(),
  });
  seq.on(onSeqEvent);
  for (const [k, v] of Object.entries(settings.bus)) (k === 'master' ? audio.setMaster(v) : audio.setBus(k, v));
  seq.mute = settings.mute || {};
  sheet = new ListSheet($('listSheet'));
  $('listSheet').addEventListener('close', () => { sheetMode = null; sheetStartBtn = null; });

  await loadStoredFiles();
  buildStyles();
  selectStyle(settings.styleId, { keepPrograms: Object.keys(settings.programs).length > 0 });
  if (settings.tempo) seq.setTempo(settings.tempo);
  restorePrograms();

  midi = new MidiInput({
    onNote: ({ note, vel, on }) => (on ? noteOn(note, vel) : noteOff(note)),
    onSustain: (on) => { engine.setSustain(CH.upper, on); engine.setSustain(CH.lower, on); },
    onBend: (semis) => { engine.setBend(CH.upper, semis); engine.setBend(CH.lower, semis); },
    onAllOff: () => panic(),
    onState: (list) => renderMidi(list),
  });
  $('midiBtn').addEventListener('click', connectMidi);
  connectMidi(true);

  bindUI();
  renderAll();
  $('gate').remove();
  requestAnimationFrame(beatLoop);
  keepAwake();
  // Coming back from another app: audio context, wake lock and MIDI all need a nudge.
  document.addEventListener('visibilitychange', () => { if (!document.hidden) { audio.resume(); keepAwake(); connectMidi(true); } });
  audio.ctx.addEventListener('statechange', () => { if (audio.ctx.state !== 'running' && !document.hidden) audio.resume(); });
  document.addEventListener('pointerdown', () => { if (audio.ctx.state !== 'running') audio.resume(); }, { passive: true });
  if (!settings.seenHint) { toast(t(settings.lang, 'firstHint'), 6000); settings.seenHint = true; persist(); }
}

async function connectMidi(silent = false) {
  if (!midi.supported) { if (silent !== true) toast(t(settings.lang, 'midiUnsupported'), 5000); renderMidi([]); return; }
  try { renderMidi(await midi.connect()); }
  catch (err) { if (silent !== true) toast(`${t(settings.lang, 'error')}: ${err.message}`); renderMidi([]); }
}

async function loadStoredFiles() {
  try {
    let banks = await store.getFile('sf2banks');
    if (!banks) {
      // 0.4 kept a single bank under 'sf2': carry it over.
      const one = await store.getFile('sf2');
      if (one && one.buf) { banks = [one]; try { await store.putFile('sf2banks', banks); await store.deleteFile('sf2'); } catch (_) { /* keep going */ } }
    }
    for (const b of banks || []) {
      try { engine.loadSF2(b.buf, b.name); sf2Banks.push(b); } catch (err) { console.warn('bank skipped', b.name, err.message); }
    }
  } catch (_) { /* no IndexedDB */ }
  try { importedPack = await store.getFile('styles'); } catch (_) { importedPack = null; }
  try { styFiles = (await store.getFile('styfiles')) || []; } catch (_) { styFiles = []; }
  try {
    const o = await store.getFile('orgset');
    if (o && o.buf) loadOrg(o.name, o.buf);
  } catch (err) { console.warn('org set not loaded', err); }
}

// ───────────────────────── styles ─────────────────────────
function buildStyles() {
  styles = STYLE_PACK.map(compileStyle);
  if (importedPack && !lintPack(importedPack).length) {
    for (const src of importedPack) {
      const st = compileStyle(src);
      st.imported = true;
      const i = styles.findIndex((s) => s.id === st.id);
      if (i >= 0) styles[i] = st; else styles.push(st);
    }
  }
  for (const f of styFiles) {
    try {
      const base = f.name.replace(/\.[^.]+$/, '');
      const id = 'sty:' + (f.folder ? f.folder + '/' : '') + base;
      for (const st of convertStyle(new Uint8Array(f.buf), { id, name: base })) { st.folder = f.folder || ''; styles.push(st); }
    } catch (err) { console.warn('style file skipped', f.name, err.message); }
  }
  if (orgSet && orgSet.parsed) {
    const base = orgSet.name.replace(/\.[^.]+$/, '');
    for (const st of orgStyles(orgSet.parsed, { id: 'org:' + base, name: base, tempo: settings.orgTempo || 165 })) styles.push(st);
  }
}

function loadOrg(name, buf) {
  const bytes = new Uint8Array(buf);
  const parsed = parseOrgSet(bytes);
  orgSet = { name, buf, parsed };
  engine.loadOrg(parsed, bytes, name);
}

// GM program number → the closest built-in synth preset (used when no SF2 is loaded).
function synthForGM(gm) {
  if (gm < 8) return 'piano';
  if (gm < 16) return 'piano';
  if (gm === 21 || gm === 22 || gm === 23) return 'accordion';
  if (gm < 24) return 'organ';
  if (gm < 32) return 'oud';
  if (gm < 40) return 'bass';
  if (gm < 48) return 'strings';
  if (gm < 56) return 'strings';
  if (gm < 64) return 'brass';
  if (gm < 72) return 'sax';
  if (gm < 80) return 'flute';
  if (gm < 96) return 'lead';
  if (gm === 104 || gm === 105 || gm === 106 || gm === 107) return 'tambour';
  return 'lead';
}
function programForGM(gm) {
  const hit = engine.findPreset(0, gm);
  if (hit) return { kind: 'sf2', font: hit.font, bank: 0, program: gm };
  return { kind: 'synth', name: synthForGM(gm) };
}

/** The rhythm list as the player sees it: own files first, built-ins last (or hidden). */
function orderedStyles() {
  const o = orderStyles(styles, { showBuiltin: settings.showBuiltin });
  return o.length ? o : orderStyles(styles, { showBuiltin: true });   // never an empty list
}

function currentStyle() { return styles.find((s) => s.id === settings.styleId) || styles[0]; }

function selectStyle(id, { keepPrograms = false } = {}) {
  const st = styles.find((s) => s.id === id) || orderedStyles()[0] || styles[0];
  settings.styleId = st.id;
  seq.setStyle(st);
  settings.tempo = st.tempo;
  if (!keepPrograms) applyStyleVoices(st);
  persist();
  renderRhythm();
  renderStatus();
  renderSound();
  if (sheetMode === 'rhythm') sheet.setActive(st.id);
}

/** Pick each style voice from the banks when one has the GM program, else the built-in. */
function programFor(voiceName) {
  const gm = GM_FOR[voiceName];
  if (gm !== undefined) { const hit = engine.findPreset(0, gm); if (hit) return { kind: 'sf2', font: hit.font, bank: 0, program: gm }; }
  return { kind: 'synth', name: voiceName in GM_FOR ? voiceName : 'lead' };
}

function applyStyleVoices(st) {
  const v = st.voices || {};
  for (const role of ['upper', 'lower', 'bass', 'chord', 'pad', 'phrase']) {
    const prog = programFor(v[role] || (role === 'bass' ? 'bass' : role === 'upper' ? 'lead' : 'strings'));
    engine.setProgram(CH[role], prog);
    settings.programs[CH[role]] = engine.programKey(CH[role]);
  }
  // An ORG set style plays the set's own instruments on every track.
  for (const tr of Object.values(st.tracks || {})) {
    if (tr.orgInstrument !== undefined && engine.setProgram(tr.channel, { kind: 'org', index: tr.orgInstrument })) settings.programs[tr.channel] = engine.programKey(tr.channel);
  }
  if (st.org) { persist(); return; }
  // A Yamaha style brings its own GM programs per part (chord 2 and phrase 2 on channels 6 / 7).
  for (const [ch, gm] of Object.entries(st.gmPrograms || {})) {
    engine.setProgram(+ch, programForGM(gm));
    settings.programs[ch] = engine.programKey(+ch);
  }
  const kit = engine.findKit() || { kind: 'synth', name: 'kit' };
  for (const ch of [CH.drums, CH.perc]) {
    engine.setProgram(ch, kit);
    settings.programs[ch] = engine.programKey(ch);
  }
}

function restorePrograms() {
  for (const [ch, key] of Object.entries(settings.programs)) {
    if (!engine.setProgram(+ch, Engine.parseKey(key))) {
      engine.setProgram(+ch, +ch === 9 || +ch === 10 ? (engine.findKit() || { kind: 'synth', name: 'kit' }) : { kind: 'synth', name: 'lead' });
      settings.programs[ch] = engine.programKey(+ch);
    }
  }
}

// ───────────────────────── keys ─────────────────────────
function noteOn(note, vel) {
  if (held.has(note)) noteOff(note);
  const chans = [];
  if (note < settings.split) {
    chordNotes.add(note);
    const chord = detectChord([...chordNotes], settings.chordMode);
    if (chord) { lastChord = chord; seq.setChord(chord); }
    if (seq.syncArmed && !seq.playing) seq.start();
    if (settings.lowerOn) chans.push(CH.lower);
  } else {
    chans.push(CH.upper);
    if (settings.layer) chans.push(CH.lower);
  }
  const n = note + settings.transpose;
  for (const ch of chans) engine.noteOn(ch, n, vel);
  held.set(note, chans.map((ch) => [ch, n]));
  renderKey(note, true);
}

function noteOff(note) {
  chordNotes.delete(note);
  const chans = held.get(note);
  if (chans) for (const [ch, n] of chans) engine.noteOff(ch, n);
  held.delete(note);
  renderKey(note, false);
}

function panic() {
  engine.allOff();
  held.clear();
  chordNotes.clear();
  for (const k of keyEls.values()) k.classList.remove('on');
}

// ───────────────────────── sequencer events ─────────────────────────
function onSeqEvent(type, data) {
  if (type === 'transport' || type === 'part' || type === 'pending' || type === 'variation' || type === 'sync') renderTransport();
  if (type === 'chord' || type === 'tempo') renderStatus();
  if (type === 'transport' && data && data.playing) keepAwake();
}

// ───────────────────────── UI binding ─────────────────────────
function bindUI() {
  document.querySelectorAll('[data-act]').forEach((b) => b.addEventListener('click', () => act(b.dataset.act)));
  document.querySelectorAll('[data-nav]').forEach((b) => b.addEventListener('click', () => nav(b.dataset.nav)));
  $('startBtn').addEventListener('click', () => { if (seq.playing) seq.stop(); else seq.start(); });
  $('introBtn').addEventListener('click', () => { if (!seq.playing) seq.start({ intro: true }); });
  $('mainABtn').addEventListener('click', () => seq.setVariation('A'));
  $('mainBBtn').addEventListener('click', () => seq.setVariation('B'));
  $('fillBtn').addEventListener('click', () => seq.fill());
  $('endingBtn').addEventListener('click', () => seq.ending());
  $('syncBtn').addEventListener('click', () => seq.armSyncStart(!seq.syncArmed));
  $('lowerOnBtn').addEventListener('click', () => { settings.lowerOn = !settings.lowerOn; persist(); renderSound(); });
  $('layerBtn').addEventListener('click', () => { settings.layer = !settings.layer; persist(); renderSound(); });
  $('chordModeBtn').addEventListener('click', () => { settings.chordMode = settings.chordMode === 'fingered' ? 'single' : 'fingered'; persist(); renderSound(); });
  $('panicBtn').addEventListener('click', panic);
  $('keysToggle').addEventListener('click', () => { settings.keysShown = !settings.keysShown; persist(); renderKeyboard(); });
  $('awakeBtn').addEventListener('click', () => { settings.keepAwake = !settings.keepAwake; persist(); settings.keepAwake ? keepAwake() : releaseAwake(); renderSettings(); });
  $('moreBtn').addEventListener('click', () => { settings.moreOpen = !settings.moreOpen; persist(); renderMore(); });
  $('builtinBtn').addEventListener('click', () => { settings.showBuiltin = !settings.showBuiltin; persist(); renderSettings(); renderRhythm(); });

  // The pickers.
  $('rhythmBtn').addEventListener('click', openRhythmList);
  $('soundBtn').addEventListener('click', () => openSoundList(CH.upper));
  $('lowerBtn').addEventListener('click', () => openSoundList(CH.lower));

  $('settingsBtn').addEventListener('click', () => { renderSettings(); $('settings').showModal(); });
  $('closeSettings').addEventListener('click', () => $('settings').close());
  $('sf2File').addEventListener('change', (e) => importSF2([...e.target.files], e.target));
  $('sf2Folder').addEventListener('change', (e) => importSF2([...e.target.files].filter((f) => /\.sf2$/i.test(f.name)), e.target));
  $('styleFile').addEventListener('change', importStyles);
  $('styleExport').addEventListener('click', () => { const o = $('styleExportOut'); o.hidden = !o.hidden; o.textContent = JSON.stringify(STYLE_PACK, null, 1); });
  $('styleRemove').addEventListener('click', async () => { await store.deleteFile('styles'); importedPack = null; buildStyles(); selectStyle(settings.styleId); renderSettings(); });
  $('styFile').addEventListener('change', (e) => importSty([...e.target.files], e.target));
  $('styFolder').addEventListener('change', (e) => importSty([...e.target.files].filter((f) => /\.(sty|prs|sst|bcs|pst)$/i.test(f.name)), e.target));
  $('orgFile').addEventListener('change', importOrg);
  $('orgRemove').addEventListener('click', async () => { await store.deleteFile('orgset'); orgSet = null; engine.org.unload(); buildStyles(); selectStyle(settings.styleId); renderSettings(); renderSound(); });
  $('styRemove').addEventListener('click', async () => { await store.deleteFile('styfiles'); styFiles = []; buildStyles(); selectStyle(settings.styleId); renderSettings(); });
  document.querySelectorAll('input[data-bus]').forEach((r) => r.addEventListener('input', () => {
    const v = +r.value; settings.bus[r.dataset.bus] = v; persist();
    r.dataset.bus === 'master' ? audio.setMaster(v) : audio.setBus(r.dataset.bus, v);
  }));

  // Registrations: tap = load, hold = save.
  const regs = $('regBtns');
  regs.innerHTML = '';
  for (let i = 0; i < 8; i++) {
    const b = document.createElement('button');
    b.className = 'btn'; b.textContent = String(i + 1); b.dataset.i = i;
    let timer = null, longed = false;
    const down = () => { longed = false; timer = setTimeout(() => { longed = true; saveReg(i); }, 600); };
    const up = () => { clearTimeout(timer); if (!longed) loadReg(i); };
    b.addEventListener('pointerdown', down);
    b.addEventListener('pointerup', up);
    b.addEventListener('pointerleave', () => clearTimeout(timer));
    b.addEventListener('contextmenu', (e) => e.preventDefault());
    regs.appendChild(b);
  }
  buildKeyboard();
  // Computer keyboard for desktop testing: a s d f g h j k = C D E F G A B C (upper), z x c v b n m = lower octave.
  const map = { z: 48, s: 49, x: 50, d: 51, c: 52, v: 53, g: 54, b: 55, h: 56, n: 57, j: 58, m: 59, q: 60, '2': 61, w: 62, '3': 63, e: 64, r: 65, '5': 66, t: 67, '6': 68, y: 69, '7': 70, u: 71, i: 72 };
  window.addEventListener('keydown', (e) => {
    if (e.repeat || e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
    if (map[e.key] !== undefined) noteOn(map[e.key], 100);
    if (e.key === ' ') { e.preventDefault(); seq.toggle(); }
    if (e.key === 'ArrowDown') nav('style+'); if (e.key === 'ArrowUp') nav('style-');
    if (e.key === 'ArrowRight') nav('sound+'); if (e.key === 'ArrowLeft') nav('sound-');
  });
  window.addEventListener('keyup', (e) => { if (map[e.key] !== undefined) noteOff(map[e.key]); });
}

function act(a) {
  const step = seq.style.meter[1] === 8 ? 3 : 2;   // 6/8: ±2 on the dotted-quarter display
  if (a === 'tempo+') seq.setTempo(seq.tempo + step);
  if (a === 'tempo-') seq.setTempo(seq.tempo - step);
  if (a.startsWith('tempo')) settings.tempo = seq.tempo;
  if (a === 'trans+') settings.transpose = Math.min(12, settings.transpose + 1);
  if (a === 'trans-') settings.transpose = Math.max(-12, settings.transpose - 1);
  if (a === 'split+') settings.split = Math.min(84, settings.split + 1);
  if (a === 'split-') settings.split = Math.max(36, settings.split - 1);
  persist();
  renderStatus();
  renderKeyboard();
}

/** ‹ › on the two pickers: step through the same order the lists show. */
function nav(a) {
  const dir = a.endsWith('+') ? 1 : -1;
  if (a.startsWith('style')) {
    const next = stepStyle(orderedStyles(), settings.styleId, dir);
    if (next) selectStyle(next.id);
  } else {
    const keys = flatKeys(soundGroups(engine.programList(false), { lang: settings.lang }));
    if (!keys.length) return;
    const i = keys.indexOf(engine.programKey(CH.upper));
    pickSound(CH.upper, keys[(i + dir + keys.length) % keys.length]);
  }
}

function setChannelProgram(ch, key) {
  engine.setProgram(ch, Engine.parseKey(key));
  settings.programs[ch] = engine.programKey(ch);
  persist();
}

/** A sound chosen from a list or an arrow: set it, show it, and let it be heard. */
function pickSound(ch, key) {
  setChannelProgram(ch, key);
  renderSound();
  if (sheetMode && sheetMode.sound === ch) sheet.setActive(engine.programKey(ch));
  if ($('settings').open) renderPartVoices();
  if (!seq.playing && held.size === 0) {
    const n = ch === CH.drums || ch === CH.perc ? 63 : ch === CH.bass ? 48 : 60;
    engine.noteOn(ch, n, 96, audio.ctx.currentTime, 0.6);
  }
}

// ───────────────────────── the pickers ─────────────────────────
function openRhythmList() {
  const labels = { yamaha: t(settings.lang, 'yourRhythms'), org: t(settings.lang, 'orgGroup'), sudan: t(settings.lang, 'sudanese'), west: t(settings.lang, 'western') };
  const groups = styleGroups(orderedStyles(), { lang: settings.lang, labels });
  sheetStartBtn = document.createElement('button');
  sheetStartBtn.className = 'btn start';
  sheetStartBtn.addEventListener('click', () => { if (seq.playing) seq.stop(); else seq.start(); });
  sheetMode = 'rhythm';
  sheet.open({
    title: t(settings.lang, 'pickRhythm'), groups, active: settings.styleId, placeholder: t(settings.lang, 'searchRhythms'), hint: t(settings.lang, 'noMatch'),
    actions: [sheetStartBtn],
    onPick: (id) => selectStyle(id),
  });
  renderTransport();
}

function openSoundList(ch) {
  const forDrums = ch === CH.drums || ch === CH.perc;
  const labels = { kits: t(settings.lang, 'kits'), bank: t(settings.lang, 'bank'), org: t(settings.lang, 'setGroup'), synth: t(settings.lang, 'builtinRhythms') };
  const groups = soundGroups(engine.programList(forDrums), { lang: settings.lang, labels });
  const role = ROLE_OF_CH[ch];
  const title = ch === CH.upper ? t(settings.lang, 'voice') : ch === CH.lower ? t(settings.lang, 'lower') : `${t(settings.lang, 'soundFor')}: ${t(settings.lang, TRACK_LABEL[role] || role)}`;
  sheetMode = { sound: ch };
  sheet.open({
    title, groups, active: engine.programKey(ch), placeholder: t(settings.lang, 'searchSounds'),
    hint: engine.hasSF2 ? t(settings.lang, 'noMatch') : t(settings.lang, 'noBanksHint'),
    onPick: (key) => pickSound(ch, key),
  });
}

// ───────────────────────── registrations ─────────────────────────
function snapshot() {
  return { styleId: settings.styleId, tempo: seq.tempo, transpose: settings.transpose, split: settings.split, chordMode: settings.chordMode,
    lowerOn: settings.lowerOn, layer: settings.layer, programs: { ...settings.programs }, bus: { ...settings.bus }, mute: { ...settings.mute } };
}

function saveReg(i) {
  settings.regs[i] = snapshot();
  settings.activeReg = i;
  persist();
  renderRegs();
  toast(`${t(settings.lang, 'saved')} ${i + 1}`);
}

function loadReg(i) {
  const r = settings.regs[i];
  if (!r) return;
  Object.assign(settings, { transpose: r.transpose, split: r.split, chordMode: r.chordMode, lowerOn: r.lowerOn, layer: r.layer, programs: { ...r.programs }, bus: { ...r.bus }, mute: { ...r.mute } });
  settings.activeReg = i;
  selectStyle(r.styleId, { keepPrograms: true });
  seq.setTempo(r.tempo); settings.tempo = r.tempo;
  restorePrograms();
  seq.mute = settings.mute;
  for (const [k, v] of Object.entries(settings.bus)) (k === 'master' ? audio.setMaster(v) : audio.setBus(k, v));
  persist();
  renderAll();
  toast(`${t(settings.lang, 'loaded')} ${i + 1}`);
}

// ───────────────────────── imports ─────────────────────────
async function importSF2(files, input) {
  if (!files.length) { if (input) input.value = ''; return; }
  const added = [], bad = [];
  for (const file of files) {
    try {
      const buf = await file.arrayBuffer();
      engine.loadSF2(buf, file.name);
      sf2Banks = sf2Banks.filter((b) => b.name !== file.name);
      sf2Banks.push({ name: file.name, buf });
      added.push(file.name);
    } catch (err) { bad.push(`${file.name}: ${err.message}`); }
  }
  // The engine keeps banks in load order; a re-import of a known name replaced it in place, so mirror that order.
  sf2Banks.sort((a, b) => engine.fonts.findIndex((f) => f.fileName === a.name) - engine.fonts.findIndex((f) => f.fileName === b.name));
  try { await store.putFile('sf2banks', sf2Banks); } catch (_) { toast(t(settings.lang, 'orgNotStored'), 6000); }
  if (added.length) applyStyleVoices(currentStyle());
  persist();
  renderSettings(); renderSound();
  if (bad.length) toast(`${t(settings.lang, 'error')}: ${bad.slice(0, 2).join(' · ')}`, 7000);
  else toast(`${t(settings.lang, 'imported')}: ${added.length} ${t(settings.lang, 'banksLoaded')}`);
  if (input) input.value = '';
}

async function removeSF2(index) {
  engine.removeSF2(index);
  sf2Banks.splice(index, 1);
  try { await store.putFile('sf2banks', sf2Banks); } catch (_) { /* ignore */ }
  for (const ch of Object.keys(settings.programs)) settings.programs[ch] = engine.programKey(+ch);
  persist();
  renderSettings(); renderSound();
}

async function importStyles(e) {
  const file = e.target.files[0];
  if (!file) return;
  try {
    const pack = JSON.parse(await file.text());
    const list = Array.isArray(pack) ? pack : [pack];
    const problems = lintPack(list);
    if (problems.length) { toast(`${t(settings.lang, 'lintErrors')}: ${problems.slice(0, 3).join(' · ')}`, 7000); return; }
    importedPack = list;
    await store.putFile('styles', list);
    buildStyles();
    selectStyle(list[0].id);
    renderSettings();
    toast(`${t(settings.lang, 'imported')}: ${list.map((s) => s.name).join(', ')}`);
  } catch (err) {
    toast(`${t(settings.lang, 'error')}: ${err.message}`, 5000);
  }
  e.target.value = '';
}

/** The folder a picked file came from (folder import), else ''. */
function folderOf(file) {
  const rel = file.webkitRelativePath || '';
  const parts = rel.split('/');
  return parts.length > 1 ? parts.slice(0, -1).join('/') : '';
}

async function importSty(files, input) {
  if (!files.length) { if (input) input.value = ''; return; }
  const added = [], bad = [];
  for (const file of files) {
    try {
      const buf = await file.arrayBuffer();
      convertStyle(new Uint8Array(buf), { id: 'x', name: 'x' });   // parse once so a bad file is refused here
      const folder = folderOf(file);
      styFiles = styFiles.filter((f) => !(f.name === file.name && (f.folder || '') === folder));
      styFiles.push({ name: file.name, folder, buf });
      added.push({ name: file.name.replace(/\.[^.]+$/, ''), folder });
    } catch (err) { bad.push(`${file.name}: ${t(settings.lang, 'fmt_' + err.message) !== 'fmt_' + err.message ? t(settings.lang, 'fmt_' + err.message) : err.message}`); }
  }
  try { await store.putFile('styfiles', styFiles); } catch (_) { toast(t(settings.lang, 'orgNotStored'), 6000); }
  buildStyles();
  if (added.length) selectStyle('sty:' + (added[0].folder ? added[0].folder + '/' : '') + added[0].name);
  renderSettings();
  if (bad.length) toast(`${t(settings.lang, 'imported')}: ${added.length} · ${t(settings.lang, 'skipped')} ${bad.length} — ${bad.slice(0, 2).join(' · ')}`, 9000);
  else toast(`${t(settings.lang, 'imported')}: ${added.length} ${t(settings.lang, 'rhythmFiles')}`);
  if (input) input.value = '';
}

async function importOrg(e) {
  const file = e.target.files[0];
  if (!file) return;
  toast(t(settings.lang, 'orgLoading'), 20000);
  try {
    const buf = await file.arrayBuffer();
    if (!isOrgSet(buf)) {
      const kind = sniffFormat(new Uint8Array(buf));
      throw new Error(kind === 'yamaha' ? 'fmt_isYamaha' : 'fmt_notOrg');
    }
    loadOrg(file.name, buf);
    try { await store.putFile('orgset', { name: file.name, buf }); } catch (_) { toast(t(settings.lang, 'orgNotStored'), 6000); }
    buildStyles();
    const first = styles.find((s) => s.org);
    if (first) selectStyle(first.id);
    renderSettings(); renderSound();
    toast(`${t(settings.lang, 'imported')}: ${file.name} — ${orgSet.parsed.instruments.length} ${t(settings.lang, 'instruments')}`, 5000);
  } catch (err) {
    const key = String(err.message || err);
    toast(`${t(settings.lang, 'error')}: ${t(settings.lang, key) !== key ? t(settings.lang, key) : key}`, 8000);
  }
  e.target.value = '';
}

// ───────────────────────── rendering ─────────────────────────
function applyLang() {
  const lang = settings.lang;
  document.documentElement.lang = lang;
  document.documentElement.dir = lang === 'ar' ? 'rtl' : 'ltr';
  document.querySelectorAll('[data-i18n]').forEach((el) => { el.textContent = t(lang, el.dataset.i18n); });
  renderInstall();
}

function renderAll() { renderRhythm(); renderStatus(); renderTransport(); renderSound(); renderRegs(); renderKeyboard(); renderMore(); }

function renderRhythm() {
  const st = currentStyle();
  if (!st) return;
  $('rhythmName').textContent = settings.lang === 'ar' ? st.name_ar : st.name;
  const grp = st.group === 'yamaha' ? (st.folder || t(settings.lang, 'yourRhythms')) : st.group === 'org' ? t(settings.lang, 'orgGroup') : t(settings.lang, st.group === 'sudan' ? 'sudanese' : 'western');
  $('rhythmMeta').textContent = `${grp} · ${st.meter[0]}/${st.meter[1]}`;
}

function renderMore() {
  $('more').hidden = !settings.moreOpen;
  $('moreBtn').classList.toggle('on', !!settings.moreOpen);
}

function noteLabel(n) { return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1); }

/** Tempo as a player reads it: quarter BPM, or the dotted-quarter value for 6/8. */
function tempoLabel() {
  if (seq.style.meter[1] === 8) return `♩. ${Math.round(seq.tempo * 2 / 3)}`;
  return String(seq.tempo);
}

function renderStatus() {
  $('tempoVal').textContent = tempoLabel();
  $('transVal').textContent = (settings.transpose > 0 ? '+' : '') + settings.transpose;
  $('splitVal').textContent = noteLabel(settings.split);
  $('chordVal').textContent = chordName(seq.chord);
}

function renderTransport() {
  const p = seq.playing ? seq.part : null;
  const set = (id, on, pending = false) => { const b = $(id); b.classList.toggle('on', !!on); b.classList.toggle('pending', !!pending); };
  set('introBtn', p === 'intro');
  set('mainABtn', seq.playing ? p === 'mainA' : seq.variation === 'A', seq.pendingVariation === 'A' && seq.variation !== 'A');
  set('mainBBtn', seq.playing ? p === 'mainB' : seq.variation === 'B', seq.pendingVariation === 'B' && seq.variation !== 'B');
  set('fillBtn', p === 'fillA' || p === 'fillB', seq.pending === 'fill');
  set('endingBtn', p === 'ending', seq.pending === 'ending');
  set('syncBtn', seq.syncArmed);
  const s = $('startBtn');
  s.classList.toggle('on', seq.playing);
  $('startLabel').textContent = t(settings.lang, seq.playing ? 'stop' : 'start');
  if (sheetStartBtn) { sheetStartBtn.textContent = t(settings.lang, seq.playing ? 'stop' : 'start'); sheetStartBtn.classList.toggle('on', seq.playing); }
  if (!seq.playing) $('beat').querySelectorAll('i').forEach((i) => i.classList.remove('on'));
}

function soundMeta(ch) {
  const p = engine.programs[ch];
  if (!p) return '';
  if (p.kind === 'sf2') { const f = engine.fonts[p.font]; return `${f ? f.name : ''} · ${p.bank}:${p.program}`; }
  if (p.kind === 'org') return t(settings.lang, 'setGroup');
  return t(settings.lang, 'builtinRhythms');
}

function renderSound() {
  $('soundName').textContent = engine.programLabel(CH.upper, settings.lang) || '—';
  $('soundMeta').textContent = soundMeta(CH.upper);
  $('lowerName').textContent = engine.programLabel(CH.lower, settings.lang) || '—';
  $('lowerOnBtn').classList.toggle('on', settings.lowerOn);
  $('layerBtn').classList.toggle('on', settings.layer);
  const cm = $('chordModeBtn');
  cm.classList.toggle('on', true);
  cm.textContent = t(settings.lang, settings.chordMode === 'single' ? 'single' : 'fingered');
}

function renderRegs() {
  $('regBtns').querySelectorAll('button').forEach((b, i) => {
    b.classList.toggle('filled', !!settings.regs[i]);
    b.classList.toggle('on', settings.activeReg === i);
  });
}

function renderMidi(list) {
  const btn = $('midiBtn');
  btn.classList.toggle('on', list.length > 0);
  $('midiLabel').textContent = list.length ? list.map((d) => d.name).join(', ') : t(settings.lang, midi.supported ? 'midiConnect' : 'midiNone');
  $('midiInfo').textContent = list.length ? list.map((d) => `${d.manufacturer} ${d.name}`.trim()).join(' · ') : (midi.supported ? t(settings.lang, 'midiNone') : t(settings.lang, 'midiUnsupported'));
}

function renderPartVoices() {
  const pv = $('partVoices');
  pv.innerHTML = '';
  for (const role of ['bass', 'chord', 'pad', 'phrase', 'drums', 'perc']) {
    const b = document.createElement('button');
    b.className = 'pickbtn';
    b.innerHTML = '<label></label><strong></strong>';
    b.querySelector('label').textContent = t(settings.lang, TRACK_LABEL[role]);
    b.querySelector('strong').textContent = engine.programLabel(CH[role], settings.lang) || '—';
    b.addEventListener('click', () => openSoundList(CH[role]));
    pv.appendChild(b);
  }
}

function renderSettings() {
  const lang = settings.lang;
  const sl = $('sf2List');
  sl.innerHTML = '';
  if (!engine.fonts.length) { const p = document.createElement('p'); p.className = 'muted'; p.textContent = t(lang, 'noSF2'); sl.appendChild(p); }
  engine.fonts.forEach((f, i) => {
    const row = document.createElement('div');
    row.className = 'frow';
    row.innerHTML = '<span></span><small></small><button class="chip" type="button">✕</button>';
    row.querySelector('span').textContent = f.name;
    row.querySelector('small').textContent = `${f.presets.length} ${t(lang, 'presets')}`;
    const x = row.querySelector('button');
    x.setAttribute('aria-label', t(lang, 'remove'));
    x.addEventListener('click', () => removeSF2(i));
    sl.appendChild(row);
  });
  if (orgSet && orgSet.parsed) {
    const pr = orgSet.parsed;
    $('orgInfo').textContent = `${orgSet.name} (v${pr.version}) — ${pr.instruments.length} ${t(lang, 'instruments')}: ${pr.instruments.map((i) => i.name).join(', ')}` + (pr.rhythm ? ` · ${t(lang, 'rhythmParts')}: ${pr.rhythm.parts.map((p) => p.name).join(', ') || '—'}` : '');
  } else $('orgInfo').textContent = t(lang, 'noOrg');
  $('orgRemove').hidden = !orgSet;
  $('styleRemove').hidden = !importedPack;
  $('styRemove').hidden = !styFiles.length;
  if (styFiles.length) {
    const folders = [...new Set(styFiles.map((f) => f.folder).filter(Boolean))];
    $('styInfo').textContent = `${styFiles.length} ${t(lang, 'rhythmFiles')}` + (folders.length ? ` (${t(lang, 'fromFolder')} ${folders.join(', ')})` : '');
  } else $('styInfo').textContent = t(lang, 'noSty');
  const bb = $('builtinBtn');
  bb.classList.toggle('on', !!settings.showBuiltin);
  bb.textContent = t(lang, settings.showBuiltin ? 'showBuiltin' : 'hideBuiltin');
  renderPartVoices();
  const tm = $('trackMutes');
  tm.innerHTML = '';
  for (const trackName of Object.keys(currentStyle().tracks)) {
    const b = document.createElement('button');
    b.className = 'btn tog' + (settings.mute[trackName] ? '' : ' on');
    b.textContent = t(lang, TRACK_LABEL[currentStyle().tracks[trackName].role] || trackName);
    b.addEventListener('click', () => { settings.mute[trackName] = !settings.mute[trackName]; seq.mute = settings.mute; persist(); renderSettings(); });
    tm.appendChild(b);
  }
  document.querySelectorAll('input[data-bus]').forEach((r) => { r.value = settings.bus[r.dataset.bus]; });
  $('latencyVal').textContent = audio.latencyMs();
  $('awakeBtn').classList.toggle('on', !!settings.keepAwake);
  $('awakeBtn').hidden = !('wakeLock' in navigator);
  renderInstall();
  renderMidi(midi.list());
}

// ───────────────────────── on-screen keyboard ─────────────────────────
const KB_LOW = 48, KB_HIGH = 72; // C3..C5
function buildKeyboard() {
  const kb = $('keyboard');
  kb.innerHTML = '';
  keyEls.clear();
  const whites = [];
  for (let n = KB_LOW; n <= KB_HIGH; n++) if (![1, 3, 6, 8, 10].includes(n % 12)) whites.push(n);
  const w = 100 / whites.length;
  for (const n of whites) {
    const k = document.createElement('div');
    k.className = 'key'; k.dataset.note = n; k.textContent = n % 12 === 0 ? noteLabel(n) : '';
    kb.appendChild(k); keyEls.set(n, k);
  }
  for (let n = KB_LOW; n <= KB_HIGH; n++) {
    if (![1, 3, 6, 8, 10].includes(n % 12)) continue;
    const idx = whites.filter((x) => x < n).length;
    const k = document.createElement('div');
    k.className = 'key black'; k.dataset.note = n;
    k.style.left = `${idx * w - w * 0.3}%`; k.style.width = `${w * 0.6}%`;
    kb.appendChild(k); keyEls.set(n, k);
  }
  const active = new Map(); // pointerId → note
  const keyAt = (x, y) => { const el = document.elementFromPoint(x, y); return el && el.classList.contains('key') ? +el.dataset.note : null; };
  kb.addEventListener('pointerdown', (e) => { const n = keyAt(e.clientX, e.clientY); if (n === null) return; e.preventDefault(); active.set(e.pointerId, n); noteOn(n, 100); });
  kb.addEventListener('pointermove', (e) => {
    if (!active.has(e.pointerId)) return;
    const n = keyAt(e.clientX, e.clientY);
    const prev = active.get(e.pointerId);
    if (n !== null && n !== prev) { noteOff(prev); active.set(e.pointerId, n); noteOn(n, 100); }
  });
  const end = (e) => { if (!active.has(e.pointerId)) return; noteOff(active.get(e.pointerId)); active.delete(e.pointerId); };
  kb.addEventListener('pointerup', end);
  kb.addEventListener('pointercancel', end);
  kb.addEventListener('pointerleave', end);
}

function renderKeyboard() {
  const kb = $('keyboard');
  kb.classList.toggle('hidden', !settings.keysShown);
  $('keysToggle').textContent = t(settings.lang, settings.keysShown ? 'hideKeys' : 'showKeys');
  for (const [n, k] of keyEls) {
    k.classList.toggle('lower', n < settings.split);
    k.classList.toggle('split', n === settings.split);
  }
}

function renderKey(note, on) {
  const k = keyEls.get(note);
  if (k) k.classList.toggle('on', on);
}

function beatLoop() {
  if (seq && seq.playing) {
    const pos = seq.position();
    const m = seq.style.meter;
    const pulses = m[1] === 8 ? m[0] / 3 : m[0];              // 6/8 pulses twice a bar, 4/4 four times
    const barFrac = (pos.beat + pos.frac) / seq.style.beatsPerBar;
    const pulse = Math.floor(barFrac * pulses);
    const pfrac = barFrac * pulses - pulse;
    const leds = $('beat').querySelectorAll('i');
    leds.forEach((led, i) => {
      led.hidden = i >= pulses;
      led.classList.toggle('on', i === pulse && pfrac < 0.5);
      led.classList.toggle('one', i === 0);
    });
  }
  requestAnimationFrame(beatLoop);
}

// ───────────────────────── misc ─────────────────────────
let toastTimer = null;
function toast(msg, ms = 2500) {
  const el = $('toast');
  el.textContent = msg; el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.hidden = true; }, ms);
}

function persist() { store.saveSettings(settings); }

boot();
