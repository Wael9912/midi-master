// A full-screen list sheet (rhythms, sounds): search box, sticky group headers,
// one row per item, the active row marked. Tapping a row picks it and the sheet
// stays open so the player can go through several in a row; ✕ / Back closes it.

import { filterGroups } from './catalog.js';

export class ListSheet {
  constructor(dialog) {
    this.el = dialog;
    this.title = dialog.querySelector('.sheet-title');
    this.actions = dialog.querySelector('.sheet-actions');
    this.search = dialog.querySelector('input[type=search]');
    this.body = dialog.querySelector('.list-body');
    this.empty = dialog.querySelector('.list-empty');
    this.groups = [];
    this.active = null;
    this.onPick = null;
    this.rows = new Map();
    dialog.querySelector('.sheet-close').addEventListener('click', () => this.close());
    dialog.addEventListener('click', (e) => { if (e.target === dialog) this.close(); });   // backdrop
    this.search.addEventListener('input', () => this.render());
  }

  /** open({ title, groups, active, onPick, actions: [HTMLElement], hint }) */
  open({ title, groups, active, onPick, actions = [], hint = '', placeholder = '' }) {
    this.title.textContent = title;
    this.groups = groups;
    this.active = active;
    this.onPick = onPick;
    this.actions.innerHTML = '';
    for (const a of actions) this.actions.appendChild(a);
    this.search.value = '';
    this.search.placeholder = placeholder;
    this.empty.textContent = hint;
    this.render();
    if (!this.el.open) this.el.showModal();
    const row = this.rows.get(this.active);
    if (row) row.scrollIntoView({ block: 'center' });
  }

  close() { if (this.el.open) this.el.close(); }
  get isOpen() { return this.el.open; }

  setActive(key) {
    const prev = this.rows.get(this.active);
    if (prev) { prev.classList.remove('on'); prev.setAttribute('aria-selected', 'false'); }
    this.active = key;
    const row = this.rows.get(key);
    if (row) { row.classList.add('on'); row.setAttribute('aria-selected', 'true'); }
  }

  render() {
    const shown = filterGroups(this.groups, this.search.value);
    this.body.innerHTML = '';
    this.rows.clear();
    let count = 0;
    for (const g of shown) {
      const h = document.createElement('div');
      h.className = 'list-group';
      h.textContent = g.title;
      this.body.appendChild(h);
      for (const it of g.items) {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'list-row' + (it.key === this.active ? ' on' : '');
        b.setAttribute('role', 'option');
        b.setAttribute('aria-selected', it.key === this.active ? 'true' : 'false');
        b.innerHTML = `<span class="list-label"></span><span class="list-meta"></span><span class="list-check">✓</span>`;
        b.querySelector('.list-label').textContent = it.label;
        b.querySelector('.list-meta').textContent = it.meta || '';
        b.addEventListener('click', () => { this.setActive(it.key); if (this.onPick) this.onPick(it.key); });
        this.body.appendChild(b);
        this.rows.set(it.key, b);
        count++;
      }
    }
    this.empty.hidden = count > 0;
  }
}
