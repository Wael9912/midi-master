// Pure helpers for the two pickers (rhythm list, sound list): ordering, grouping,
// search. No browser APIs (runs under node for the tests).

/** GM program (0–127) → family name, for grouping a bank's presets. */
export const GM_FAMILIES = [
  ['Piano', 'بيانو'], ['Chromatic percussion', 'إيقاع نغمي'], ['Organ', 'أورغ'], ['Guitar', 'جيتار'],
  ['Bass', 'باص'], ['Strings', 'وتريات'], ['Ensemble', 'مجموعات'], ['Brass', 'نفخ نحاسي'],
  ['Reed', 'ناي وساكس'], ['Pipe', 'فلوت'], ['Synth lead', 'ليد'], ['Synth pad', 'خلفيات'],
  ['Synth effects', 'مؤثرات سينث'], ['Ethnic', 'آلات شرقية'], ['Percussive', 'إيقاعية'], ['Sound effects', 'مؤثرات صوتية'],
];
export function gmFamily(program, lang = 'en') {
  const f = GM_FAMILIES[Math.max(0, Math.min(15, Math.floor(program / 8)))];
  return lang === 'ar' ? f[1] : f[0];
}

/** Rhythm list order: the player's own files first, then the ORG set, then the built-ins. */
export const GROUP_ORDER = ['yamaha', 'org', 'sudan', 'west'];

export function orderStyles(styles, { showBuiltin = true } = {}) {
  const rank = (s) => { const i = GROUP_ORDER.indexOf(s.group); return i < 0 ? 99 : i; };
  // Own files: folders alphabetically, then the files picked one by one (no folder), each by name.
  const cmpYamaha = (a, b) => {
    const fa = a.folder || '', fb = b.folder || '';
    if (!fa !== !fb) return fa ? -1 : 1;
    return fa.localeCompare(fb, undefined, { sensitivity: 'base' }) || a.name.localeCompare(b.name, undefined, { sensitivity: 'base', numeric: true });
  };
  return styles
    .filter((s) => showBuiltin || !(s.group === 'sudan' || s.group === 'west'))
    .map((s, i) => ({ s, i }))
    .sort((a, b) => rank(a.s) - rank(b.s) || (a.s.group === 'yamaha' ? cmpYamaha(a.s, b.s) : a.i - b.i))
    .map((x) => x.s);
}

/** The neighbour of `id` in the ordered list, wrapping; null when the list is empty. */
export function stepStyle(ordered, id, dir) {
  if (!ordered.length) return null;
  const i = ordered.findIndex((s) => s.id === id);
  if (i < 0) return ordered[0];
  return ordered[(i + dir + ordered.length) % ordered.length];
}

/** Rhythm list groups: [{ key, title, items: [{ key, label, meta }] }]. */
export function styleGroups(ordered, { lang = 'en', labels = {} } = {}) {
  const groups = [];
  const byKey = new Map();
  const yamahaFolders = new Set(ordered.filter((s) => s.group === 'yamaha').map((s) => s.folder || ''));
  for (const st of ordered) {
    let gkey = st.group, title = labels[st.group] || st.group;
    if (st.group === 'yamaha' && yamahaFolders.size > 1) { gkey = 'yamaha:' + (st.folder || ''); title = st.folder || labels.yamaha || 'Styles'; }
    let g = byKey.get(gkey);
    if (!g) { g = { key: gkey, title, items: [] }; byKey.set(gkey, g); groups.push(g); }
    const tempo = st.meter[1] === 8 ? `♩. ${Math.round(st.tempo * 2 / 3)}` : `♩ ${st.tempo}`;
    g.items.push({ key: st.id, label: lang === 'ar' ? st.name_ar : st.name, meta: `${st.meter[0]}/${st.meter[1]} · ${tempo}` });
  }
  return groups;
}

/**
 * Sound list groups from an engine program list (engine.programList()).
 * SF2 presets are grouped by bank file and GM family; the set's instruments and the
 * built-in presets keep their own groups.
 */
export function soundGroups(programs, { lang = 'en', labels = {} } = {}) {
  const groups = [];
  const byKey = new Map();
  const add = (gkey, title, item) => {
    let g = byKey.get(gkey);
    if (!g) { g = { key: gkey, title, items: [] }; byKey.set(gkey, g); groups.push(g); }
    g.items.push(item);
  };
  for (const p of programs) {
    const label = lang === 'ar' ? p.label_ar : p.label;
    if (p.kind === 'sf2') {
      const fam = p.bank === 128 ? (labels.kits || 'Drum kits') : p.bank === 0 ? gmFamily(p.program, lang) : `${labels.bank || 'Bank'} ${p.bank}`;
      add(`sf2:${p.font}:${p.bank === 0 ? Math.floor(p.program / 8) : 'b' + p.bank}`, `${p.fontName} · ${fam}`, { key: p.key, label, meta: `${p.bank}:${p.program}` });
    } else if (p.kind === 'org') add('org', labels.org || 'Set', { key: p.key, label, meta: '' });
    else add('synth', labels.synth || 'Built-in', { key: p.key, label, meta: '' });
  }
  // Built-in presets last: the sampled sounds are what the player went looking for.
  const synth = groups.find((g) => g.key === 'synth');
  if (synth) { groups.splice(groups.indexOf(synth), 1); groups.push(synth); }
  return groups;
}

/** Flat ordered keys of a group list (for prev / next). */
export function flatKeys(groups) {
  const out = [];
  for (const g of groups) for (const it of g.items) out.push(it.key);
  return out;
}

/** Case-insensitive search over label + meta + group title; empty query keeps everything. */
export function filterGroups(groups, query) {
  const q = (query || '').trim().toLowerCase();
  if (!q) return groups;
  const words = q.split(/\s+/);
  const hit = (s) => { const t = s.toLowerCase(); return words.every((w) => t.includes(w)); };
  return groups
    .map((g) => ({ ...g, items: hit(g.title) ? g.items : g.items.filter((it) => hit(`${it.label} ${it.meta}`)) }))
    .filter((g) => g.items.length);
}
