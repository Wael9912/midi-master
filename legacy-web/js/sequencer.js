// Sample-accurate (clock-arithmetic) style sequencer with a look-ahead scheduler.
// Pure logic: it takes a clock { now() } and a sink { noteOn(ch, n, v, t, d) }.
// Times are in seconds of the clock; bars are chained by arithmetic, so there is
// no drift however long the style runs (see tests/sequencer.test.mjs).

import { transposeNote } from './chord.js';

const LOOKAHEAD = 0.2;       // seconds scheduled ahead of the clock (a busy UI frame must not starve a bar)
const TICK_MS = 25;          // scheduler timer period

export class Sequencer {
  constructor({ clock, sink, setTimer = (fn, ms) => setInterval(fn, ms), clearTimer = (id) => clearInterval(id) }) {
    this.clock = clock;
    this.sink = sink;
    this.setTimer = setTimer;
    this.clearTimer = clearTimer;
    this.style = null;
    this.tempo = 120;
    this.chord = null;          // { root, type }
    this.playing = false;
    this.part = 'mainA';        // the part being played now
    this.variation = 'A';       // A | B
    this.pending = null;        // 'fill' | 'ending' | 'intro' | null  (takes effect at the next bar)
    this.pendingVariation = null;
    this.syncArmed = false;
    this.mute = {};             // track name → true
    this.listeners = new Set();
    this._timer = null;
    this._barStart = 0;         // clock time of the current bar's first beat
    this._barIndex = 0;         // bar inside the current part
    this._eventIndex = 0;       // next event of the current part to schedule
    this._partStart = 0;        // clock time of the current part's first beat
    this._cycleBeats = 0;
  }

  on(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
  _emit(type, data) { for (const fn of this.listeners) fn(type, data); }

  setStyle(style) {
    const was = this.playing;
    if (was) this.stop();
    this.style = style;
    this.tempo = style.tempo;
    this.variation = 'A';
    this._emit('style', style);
    if (was) this.start();
  }

  setTempo(bpm) {
    this.tempo = Math.min(240, Math.max(40, Math.round(bpm)));
    this._emit('tempo', this.tempo);
  }

  setChord(chord) { this.chord = chord; this._emit('chord', chord); }

  get secondsPerBeat() { return 60 / this.tempo; }
  get barSeconds() { return this.secondsPerBeat * this.style.beatsPerBar; }

  /** Start on the next note in the chord zone instead of now. */
  armSyncStart(on = true) { this.syncArmed = on; this._emit('sync', on); }

  start({ intro = false } = {}) {
    if (!this.style || this.playing) return;
    this.playing = true;
    this.syncArmed = false;
    const now = this.clock.now() + 0.02;
    this._enterPart(intro ? 'intro' : this._mainName(), now);
    this._timer = this.setTimer(() => this._tick(), TICK_MS);
    this._emit('transport', { playing: true, part: this.part });
    this._tick();
  }

  stop() {
    if (!this.playing) return;
    this.playing = false;
    if (this._timer !== null) this.clearTimer(this._timer);
    this._timer = null;
    this.pending = null;
    this.sink.allOff && this.sink.allOff();
    this._emit('transport', { playing: false, part: null });
  }

  toggle() { this.playing ? this.stop() : this.start(); }

  /** Queue a fill; the fill of the CURRENT variation plays, then the main of the TARGET variation. */
  fill(toVariation = null) {
    if (!this.playing) { if (toVariation) this.setVariation(toVariation); return; }
    this.pendingVariation = toVariation || this.variation;
    this.pending = 'fill';
    this._emit('pending', this.pending);
  }

  setVariation(v) {
    if (!this.playing) { this.variation = v; this._emit('variation', v); return; }
    if (v !== this.variation) this.fill(v);
  }

  ending() {
    if (!this.playing) return;
    this.pending = 'ending';
    this._emit('pending', this.pending);
  }

  _mainName() { return this.variation === 'B' ? 'mainB' : 'mainA'; }

  _enterPart(name, at) {
    this.part = name;
    this._partStart = at;
    this._barStart = at;
    this._barIndex = 0;
    this._eventIndex = 0;
    this._emit('part', name);
  }

  /** Decide which part follows the one that just finished. */
  _nextPart() {
    const p = this.part;
    if (p === 'ending') return null;
    if (this.pending === 'ending') { this.pending = null; return 'ending'; }
    if (p === 'intro') return this._mainName();
    if (p === 'fillA' || p === 'fillB') {
      if (this.pendingVariation) { this.variation = this.pendingVariation; this.pendingVariation = null; this._emit('variation', this.variation); }
      return this._mainName();
    }
    if (this.pending === 'fill') { this.pending = null; return this.variation === 'B' ? 'fillB' : 'fillA'; }
    return this._mainName();
  }

  _tick() {
    if (!this.playing) return;
    const horizon = this.clock.now() + LOOKAHEAD;
    let guard = 0;
    while (guard++ < 256) {
      const part = this.style.parts[this.part];
      const events = part.events;
      const spb = this.secondsPerBeat;
      const barEndBeat = (this._barIndex + 1) * this.style.beatsPerBar;
      // Schedule this bar's events that fall before the horizon.
      while (this._eventIndex < events.length && events[this._eventIndex].t < barEndBeat - 1e-9) {
        const ev = events[this._eventIndex];
        const t = this._partStart + ev.t * spb;
        if (t > horizon) return;
        this._eventIndex++;
        this._play(ev, t, ev.d * spb);
      }
      const barEnd = this._partStart + barEndBeat * spb;
      if (barEnd > horizon) return;
      // Bar line reached. A queued fill or ending cuts a main part here, on the bar line.
      const inMain = this.part === 'mainA' || this.part === 'mainB';
      const interrupt = inMain && (this.pending === 'fill' || this.pending === 'ending');
      if (!interrupt && this._barIndex + 1 < part.bars) {
        this._barIndex++;
        this._barStart = barEnd;
        continue;
      }
      const next = this._nextPart();
      if (next === null) { this._stopAt(barEnd); return; }
      this._enterPart(next, barEnd);
    }
  }

  _stopAt(t) {
    const wait = Math.max(0, (t - this.clock.now()) * 1000);
    this.playing = false;
    if (this._timer !== null) this.clearTimer(this._timer);
    this._timer = null;
    this.pending = null;
    setTimeout(() => this._emit('transport', { playing: false, part: null }), wait);
  }

  _play(ev, t, d) {
    if (this.mute[ev.track]) return;
    const track = this.style.tracks[ev.track];
    let n = ev.n;
    if (track.transpose !== 'none') {
      if (!this.chord && (track.role === 'bass' || track.role === 'chord' || track.role === 'pad' || track.role === 'phrase')) {
        // No chord yet: play as written (C major) so the style is audible.
      }
      n = transposeNote(ev.n, track.transpose, this.chord);
    }
    this.sink.noteOn(track.channel, n, ev.v, t, d);
  }

  /** Position inside the bar, 0..1, for the beat display. */
  position() {
    if (!this.playing || !this.style) return { bar: 0, beat: 0, frac: 0 };
    const spb = this.secondsPerBeat;
    const elapsed = this.clock.now() - this._partStart;
    const beats = Math.max(0, elapsed / spb);
    const bpb = this.style.beatsPerBar;
    return { bar: Math.floor(beats / bpb), beat: Math.floor(beats % bpb), frac: (beats % 1) };
  }
}
