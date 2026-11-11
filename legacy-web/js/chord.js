// Chord detection and chord-relative transposition. Pure logic, no browser APIs.

export const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

// Chord types: intervals from the root, and how a pattern note written over
// C major (pitch class 0..11) is re-mapped when the chord is of this type.
// The map keys are pitch classes of the written note; values are the interval
// above the chord root that should sound.
export const CHORD_TYPES = {
  maj:  { label: '',     intervals: [0, 4, 7],      map: { 0: 0, 2: 2, 4: 4, 5: 5, 7: 7, 9: 9, 10: 10, 11: 11 } },
  min:  { label: 'm',    intervals: [0, 3, 7],      map: { 0: 0, 2: 2, 4: 3, 5: 5, 7: 7, 9: 8, 10: 10, 11: 10 } },
  dom7: { label: '7',    intervals: [0, 4, 7, 10],  map: { 0: 0, 2: 2, 4: 4, 5: 5, 7: 7, 9: 9, 10: 10, 11: 10 } },
  maj7: { label: 'maj7', intervals: [0, 4, 7, 11],  map: { 0: 0, 2: 2, 4: 4, 5: 5, 7: 7, 9: 9, 10: 11, 11: 11 } },
  min7: { label: 'm7',   intervals: [0, 3, 7, 10],  map: { 0: 0, 2: 2, 4: 3, 5: 5, 7: 7, 9: 8, 10: 10, 11: 10 } },
  sus4: { label: 'sus4', intervals: [0, 5, 7],      map: { 0: 0, 2: 2, 4: 5, 5: 5, 7: 7, 9: 9, 10: 10, 11: 10 } },
  dim:  { label: 'dim',  intervals: [0, 3, 6],      map: { 0: 0, 2: 2, 4: 3, 5: 5, 7: 6, 9: 9, 10: 9, 11: 9 } },
  aug:  { label: 'aug',  intervals: [0, 4, 8],      map: { 0: 0, 2: 2, 4: 4, 5: 5, 7: 8, 9: 9, 10: 10, 11: 11 } },
};

// Fingered-mode templates, tried in this order (most specific first).
// Each entry: [type, pitch-class set relative to root]. Subsets without the
// fifth are accepted for 7th chords so three-finger voicings work.
const TEMPLATES = [
  ['dom7', [0, 4, 7, 10]], ['maj7', [0, 4, 7, 11]], ['min7', [0, 3, 7, 10]],
  ['dom7', [0, 4, 10]],    ['maj7', [0, 4, 11]],    ['min7', [0, 3, 10]],
  ['maj',  [0, 4, 7]],     ['min',  [0, 3, 7]],     ['sus4', [0, 5, 7]],
  ['dim',  [0, 3, 6]],     ['aug',  [0, 4, 8]],
  ['maj',  [0, 4]],        ['min',  [0, 3]],        ['maj',  [0, 7]],
];

function sameSet(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

export function chordName(chord) {
  if (!chord) return '—';
  return NOTE_NAMES[chord.root] + CHORD_TYPES[chord.type].label;
}

/**
 * Fingered detection: the held MIDI notes (any octave) form a pitch-class set
 * that must equal one template. Returns { root, type, bass } or null.
 */
export function detectFingered(notes) {
  if (!notes || notes.length === 0) return null;
  const sorted = [...notes].sort((a, b) => a - b);
  const pcs = [...new Set(sorted.map((n) => ((n % 12) + 12) % 12))].sort((a, b) => a - b);
  if (pcs.length === 1) return { root: pcs[0], type: 'maj', bass: sorted[0] };
  const bassPc = ((sorted[0] % 12) + 12) % 12;
  // Prefer a root equal to the lowest note, then the other candidates.
  const roots = [bassPc, ...pcs.filter((p) => p !== bassPc)];
  for (const [type, tpl] of TEMPLATES) {
    if (tpl.length !== pcs.length) continue;
    for (const root of roots) {
      const rel = pcs.map((p) => (p - root + 12) % 12).sort((a, b) => a - b);
      if (sameSet(rel, tpl)) return { root, type, bass: sorted[0] };
    }
  }
  return null;
}

/**
 * One-finger (easy) mode, Yamaha-like:
 *  one key            → major
 *  root + a key 1..2 semitones below  → 7th   (white key below)
 *  root + a key 3 semitones below     → minor (black-ish key below)
 *  root + both                        → minor 7th
 * The highest held note is the root.
 */
export function detectOneFinger(notes) {
  if (!notes || notes.length === 0) return null;
  const sorted = [...notes].sort((a, b) => b - a);
  const rootNote = sorted[0];
  const root = ((rootNote % 12) + 12) % 12;
  if (sorted.length === 1) return { root, type: 'maj', bass: rootNote };
  const below = sorted.slice(1).map((n) => rootNote - n);
  const has7 = below.some((d) => d === 1 || d === 2);
  const hasMin = below.some((d) => d === 3 || d === 4);
  if (has7 && hasMin) return { root, type: 'min7', bass: rootNote };
  if (hasMin) return { root, type: 'min', bass: rootNote };
  if (has7) return { root, type: 'dom7', bass: rootNote };
  return detectFingered(notes);
}

export function detectChord(notes, mode = 'fingered') {
  return mode === 'single' ? detectOneFinger(notes) : detectFingered(notes);
}

/**
 * Transpose a pattern note (written over C major) for the active chord.
 *  'none'  → unchanged (drums, percussion)
 *  'root'  → shifted by the chord root, kept within ±6 semitones
 *  'chord' → pitch class re-mapped to the chord type, then shifted by the root
 * The root shift is folded to the nearest direction so bass lines stay in range.
 */
export function transposeNote(note, rule, chord) {
  if (rule === 'none' || !chord) return note;
  let shift = chord.root; // 0..11
  if (shift > 6) shift -= 12;
  if (rule === 'root') return note + shift;
  const pc = ((note % 12) + 12) % 12;
  const map = CHORD_TYPES[chord.type].map;
  const interval = map[pc] !== undefined ? map[pc] : pc;
  return note - pc + interval + shift;
}
