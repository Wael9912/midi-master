# Midi Master (formerly Daloka) — Sudanese arranger keyboard (browser app)

## Goal
Free, open-source, offline web app: a USB-MIDI keyboard (first target: YM928) → sound module + arranger with Sudanese and Western styles. Runs in Chrome on Android as a PWA. Arabic (RTL) + English UI. The owner wants it **simple and light**: no framework, no bundler, no npm dependencies.

## Architecture rules (do not break)
- Plain ES modules in `js/`, loaded straight by the browser. No build step. No external scripts or CDNs (the app must work offline and the service worker caches everything).
- Audio is Web Audio only. The sequencer schedules by AudioContext time with a look-ahead timer (`sequencer.js`); bars are chained by arithmetic, never by `setTimeout` accumulation. Keep `tests/run.mjs`'s 10,000-bar drift test green.
- Engine logic (`chord.js`, `styledsl.js`, `styles.js`, `sequencer.js`, `sty.js`, `orgset.js`, `catalog.js`) must stay free of browser APIs so `node tests/run.mjs` runs them.
- MIDI goes `midi.js` → `app.js` `noteOn/noteOff` → `engine.js`. Nothing else handles notes.
- `engine.js` routes each channel (0 upper, 1 lower, 2 bass, 3 chord, 4 pad, 5 phrase, 9 drums, 10 perc) to the built-in synth or the loaded SF2.
- Style patterns are written over C major and re-mapped per chord by `transposeNote()`; `'none'` for drums, `'root'` for plain shifts, `'chord'` for bass/chord/pad/phrase.
- The app is named **Midi Master** (Arabic ميدي ماستر). The storage names stay `daloka.v1` (localStorage) and `daloka` (IndexedDB) so phones keep their setups and banks: never rename them. The rhythm *Daloka · Sayra* keeps its name. The repo and the Pages URL are still `daloka` until the owner decides (see `docs/PLAN_MIDI_MASTER.md`).
- Persisted state: `localStorage` for settings + 8 setups, IndexedDB for the SF2 banks (`sf2banks`, an array), the Yamaha style files (`styfiles`, each with its `folder`), the ORG set and the imported style pack.
- The main screen is two pickers (rhythm, right-hand sound) + transport + keys pinned to the bottom. Lists go through `sheet.js` (one `ListSheet`, `#listSheet`); their order, grouping and search live in `catalog.js` (pure, tested). Keep the phone layout first: nothing on the main screen may push the keys below the fold at 390×844.

## Definition of done for any task
- `node tests/run.mjs` passes.
- The page loads with no console errors (`./serve.sh`, open in Chrome, check devtools) and on an Android phone in Chrome.
- Bump `VERSION` in `sw.js` (and `js/app.js`) when any cached file changes, or phones keep the old build.
- `README.md` updated if behaviour changed; `docs/CHANGELOG.md` gets a line.

## Do not
- Add paid or closed-source dependencies, or any network calls.
- Bundle samples without a license entry in `CREDITS.md`.
- Put browser APIs into the engine modules listed above.
