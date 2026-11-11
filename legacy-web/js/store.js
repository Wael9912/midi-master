// Persistence: registrations + settings in localStorage, sound banks and
// style packs in IndexedDB (so an imported SF2 survives a reload, offline).

const LS_KEY = 'daloka.v1';

export function loadSettings() {
  try { return JSON.parse(localStorage.getItem(LS_KEY) || '{}'); } catch (_) { return {}; }
}

export function saveSettings(obj) {
  try { localStorage.setItem(LS_KEY, JSON.stringify(obj)); } catch (_) { /* quota or private mode */ }
}

function db() {
  return new Promise((resolve, reject) => {
    if (!('indexedDB' in window)) return reject(new Error('noidb'));
    const req = indexedDB.open('daloka', 1);
    req.onupgradeneeded = () => {
      const d = req.result;
      if (!d.objectStoreNames.contains('files')) d.createObjectStore('files');
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

export async function putFile(key, value) {
  const d = await db();
  return new Promise((resolve, reject) => {
    const tx = d.transaction('files', 'readwrite');
    tx.objectStore('files').put(value, key);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

export async function getFile(key) {
  const d = await db();
  return new Promise((resolve, reject) => {
    const tx = d.transaction('files', 'readonly');
    const req = tx.objectStore('files').get(key);
    req.onsuccess = () => resolve(req.result || null);
    req.onerror = () => reject(req.error);
  });
}

export async function deleteFile(key) {
  const d = await db();
  return new Promise((resolve, reject) => {
    const tx = d.transaction('files', 'readwrite');
    tx.objectStore('files').delete(key);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}
