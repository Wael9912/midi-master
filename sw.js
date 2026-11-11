// Offline cache for the app shell. Bump VERSION on every release.
const VERSION = 'midimaster-0.5.1';
const FILES = [
  './', './index.html', './manifest.webmanifest', './css/app.css',
  './js/app.js', './js/audio.js', './js/catalog.js', './js/chord.js', './js/engine.js', './js/i18n.js', './js/midi.js',
  './js/sequencer.js', './js/sf2.js', './js/sheet.js', './js/store.js', './js/styledsl.js', './js/styles.js', './js/sty.js', './js/orgset.js', './js/orgsampler.js', './js/synth.js',
  './icons/icon-192.png', './icons/icon-512.png',
];

self.addEventListener('install', (e) => {
  // cache: 'reload' bypasses the HTTP cache (GitHub Pages serves 10-minute max-age), so a new VERSION gets new files.
  e.waitUntil(caches.open(VERSION).then((c) => c.addAll(FILES.map((f) => new Request(f, { cache: 'reload' })))).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys().then((keys) => Promise.all(keys.filter((k) => k !== VERSION).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});

self.addEventListener('fetch', (e) => {
  if (e.request.method !== 'GET') return;
  e.respondWith(
    caches.match(e.request, { ignoreSearch: true }).then((hit) => hit || fetch(e.request).then((res) => {
      if (res.ok && new URL(e.request.url).origin === location.origin) {
        const copy = res.clone();
        caches.open(VERSION).then((c) => c.put(e.request, copy));
      }
      return res;
    }).catch(() => caches.match('./index.html'))),
  );
});
