// Web MIDI input: every connected input (USB-MIDI via OTG on Android Chrome).
// Fires { note, vel, on, ch } for keys; sustain, pitch bend and program change.

export class MidiInput {
  constructor(handlers) {
    this.h = handlers;           // { onNote, onSustain, onBend, onProgram, onState }
    this.access = null;
    this.inputs = new Map();
  }

  get supported() { return typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess; }

  async connect() {
    if (!this.supported) throw new Error('nomidi');
    this.access = await navigator.requestMIDIAccess({ sysex: false });
    this.access.onstatechange = () => this._refresh();
    this._refresh();
    return this.list();
  }

  list() {
    return [...this.inputs.values()].map((i) => ({ id: i.id, name: i.name || 'MIDI', manufacturer: i.manufacturer || '' }));
  }

  _refresh() {
    if (!this.access) return;
    for (const input of this.access.inputs.values()) {
      if (!this.inputs.has(input.id)) {
        input.onmidimessage = (e) => this._message(e.data);
        this.inputs.set(input.id, input);
      }
    }
    for (const id of [...this.inputs.keys()]) {
      if (!this.access.inputs.has(id)) this.inputs.delete(id);
    }
    this.h.onState && this.h.onState(this.list());
  }

  _message(d) {
    if (!d || d.length === 0) return;
    const status = d[0] & 0xf0;
    const ch = d[0] & 0x0f;
    switch (status) {
      case 0x90:
        this.h.onNote && this.h.onNote({ note: d[1], vel: d[2], on: d[2] > 0, ch });
        break;
      case 0x80:
        this.h.onNote && this.h.onNote({ note: d[1], vel: 0, on: false, ch });
        break;
      case 0xb0:
        if (d[1] === 64) this.h.onSustain && this.h.onSustain(d[2] >= 64, ch);
        else if (d[1] === 123 || d[1] === 120) this.h.onAllOff && this.h.onAllOff(ch);
        else this.h.onControl && this.h.onControl(d[1], d[2], ch);
        break;
      case 0xe0: {
        const v = ((d[2] << 7) | d[1]) - 8192;
        this.h.onBend && this.h.onBend((v / 8192) * 2, ch); // ±2 semitones
        break;
      }
      case 0xc0:
        this.h.onProgram && this.h.onProgram(d[1], ch);
        break;
      default:
        break;
    }
    this.h.onAny && this.h.onAny(d);
  }
}
