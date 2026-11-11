# Changelog

## 0.6.0 — 2026-11-11 · native app, milestone M1 (first sound)
- **Midi Master is now a native Android app** (owner decisions, `docs/PLAN_MIDI_MASTER.md` §0). Kotlin + Jetpack Compose UI, C++17 engine on **Oboe** (LowLatency, Exclusive, float, native rate, LatencyTuner, ADPF hint) with **AMidi read inside the audio callback**, so a key press never passes through Java/Kotlin. minSdk 29, package `io.github.wael9912.midimaster`.
- M1 screen: the keyboard (auto-connects to the first USB MIDI device, reconnects on plug-in), an *Audio path* card (sample rate, burst, buffer, low-latency / exclusive / MMAP, output-latency estimate, MIDI hand-off avg/max, audio CPU peak, glitches, voices), volume, 8 test pads, all-notes-off. English + Arabic.
- Sound: a two-operator FM test electric piano with a 1 ms attack, 32 voices, sustain pedal (CC64), and a soft clip on the master in place of a look-ahead limiter.
- Engine core (`engine/core/`) builds and is tested on the desktop: first audible sample ≤ 1 ms after note-on at 44.1/48/96 kHz for every velocity and register, zero allocations on the audio path, release, sustain, voice stealing, sample bounds, MIDI parser (running status, real-time bytes, SysEx, split chunks), SPSC queue.
- The web app moved to `legacy-web/` as the frozen reference (its 453 tests still pass there). GitHub Pages was switched off and the repo renamed to `Wael9912/midi-master`. CI now runs the web tests, the engine tests and the APK build.
- Not yet tried on the phone with the YM-928. The owner will judge latency by ear once the app is complete; no OboeTester measurement.
- **Next chat: the whole app.** M2 (FluidSynth + FluidR3Mono, the ported built-in voices, SF2 / ORG import), M3 (the arranger in C++, tested against the JS fixtures), M4 (the main screen in Compose, Arabic + English, 8 setups).

## 0.5.1 — 2026-11-11
- **Renamed to Midi Master** (ميدي ماستر): title, home-screen name, start screen, install help, messages, one-file bundles (`dist/midi-master*.html`). Saved setups, banks and rhythm files carry over (the storage names did not change). The Sudanese rhythm *Daloka · Sayra* keeps its name.

## 0.5.0 — 2026-10-22
- **The screen rebuilt for a phone.** Two pickers at the top of the screen: *Rhythm* and *Right-hand sound*, each with ‹ › arrows to step and a tap on the name to open a full-screen list (`js/sheet.js`): search box, group headers, the current item marked, and a tap picks while the list stays open so several can be tried in a row; the rhythm list carries its own START/STOP. The keyboard is pinned to the bottom; the panels above it scroll if they must. Transport: INTRO · A · B · FILL · END on one row, START with the beat lights inside it, tempo beside it. Chord and transpose below; split point, left-hand sound, layer, setups under *More*. Two columns on a tablet, a short layout in landscape.
- **Your rhythm files come first.** Imported Yamaha styles head the rhythm list, grouped by folder (Settings → *Add a folder of rhythms* takes a whole folder; non-style files are skipped), then the ORG set, then the built-in Sudanese and Western sets, which can be hidden (*Show built-in rhythms*). Order and grouping in `js/catalog.js`.
- **Several SF2 banks at once.** Settings → *Add SF2 banks* / *Add a folder of banks*; each bank is listed with its sound count and a remove button, all are kept offline, and every bank's presets appear in the sound lists grouped by bank and GM family. Style voices take the first bank that has the GM program; drums take the first bank with a kit. A bank loaded by 0.4 is carried over. Program keys are now `sf2:<bank file>:<bank>:<program>`.
- **Rhythm-part sounds** (bass, chord, pad, phrase, drums, percussion) open the same list from Settings; picking a sound plays a short preview when nothing else is sounding.
- The built-in kit now covers the whole GM percussion map (toms, rides, timbales, agogo, cabasa, claves, triangle, guiro…), so an imported Yamaha or ORG rhythm played without a sound bank no longer turns every unknown stroke into a muffled tom.
- Fix: a held key in the chord zone did not light up (the zone colour painted over the key colour).

## 0.4.0 — 2026-10-20
- **ORG 24 / Music Studio `.Set` import** (`js/orgset.js` + `js/orgsampler.js`): the format was reverse-engineered from the owner's `Motaso.Set` (v1.2.8) and a Sofeh sample set (v1.4.6). Daloka reads the set's sampled instruments (16-bit PCM multisamples with root notes, loops and tuning) and plays them as voices and as the rhythm's tracks, and reads the rhythm (track records, patterns of 24-byte note rows per part, the part table) into a style: Intro 1 → intro, Variation 1/2 → A/B, Fill 1/2 → fills, Variation 3/4 as a second style. Settings → *ORG 24 set*; the set is kept in IndexedDB. `docs/CONTENT.md` has the byte layout.
- Unknown so far: the set's tempo (not found in the file; default ♩. 110, adjust by hand), the Wave (audio-loop) track, the rhythm's private percussion sounds (the Percussion track plays the best-fitting kit of the set), per-instrument envelopes.

## 0.3.1 — 2026-10-20
- The style picker accepts `.set` / `.mid` too and sniffs the real format: a Yamaha file under any name imports; an ORG 24 or Korg file is named as such in the error instead of a generic failure.

## 0.3.0 — 2026-10-20
- **Yamaha style import** (`js/sty.js`): any PSR / Tyros / Genos style file (.sty, .prs, .sst; SFF1 and SFF2) plays as a Daloka style — SMF0 sections by marker (Intro, Main A/B, Fill In AA/BB, Ending; Main C/D + Fill CC/DD become a second "(C/D)" style), CASM Ctab for the source chord and the transposition table, GM program changes on the part channels (chord 2 / phrase 2 on channels 6 / 7). Settings → *Import Yamaha styles*; files persist in IndexedDB; a "Yamaha styles" group on the main screen.
- ORG 24 (Sofeh) `.Set` files were examined and are a closed format (own binary, rhythms in a private block, no MIDI inside); they are not supported. Details in `docs/CONTENT.md`.
- Fix: the self-update reload from 0.2.1 also fired on a first visit; it now only reloads when an older worker was in control.

## 0.2.1 — 2026-10-20
- Updates reach the phone: a new build reloads the page once as soon as its service worker takes over, and the install fetch bypasses the HTTP cache; the version is shown on the start screen.

## 0.2.0 — 2026-10-20
- Sudanese set rebuilt from published descriptions (`docs/CONTENT.md` lists the sources): Tum Tum as the triple 6/8 with the «dum tak dum, two on the mambo» figure and claps in 3 against 2; Daloka · Sayra 4/4; Mardoum fast 6/8; Jarari slow 6/8; Karachi 2/4 starting on tak; Dalib with the tambour; Haqiba with the riq. Western: Pop, Rock, Ballad, Reggae, Slow 6/8.
- Drum map moved to GM numbers with the three bangaz (dum 63, tak 60, mambo 64), kashkoush 70, shatam 76, riq 54; a GM SoundFont now plays the right family of sound; the built-in kit synthesises the same numbers.
- Simpler screen: rhythms grouped Sudanese / Western, one big START, INTRO · A · B · FILL · END, tempo shown as ♩. for 6/8, right-hand sound and chord up front, everything else under "More"; a one-line hint under the keys; a first-run toast.
- Install: an **Install app** button in the top bar (uses the browser prompt, or shows the Chrome steps); manifest `id` / `display_override`.
- Screen stays on while the app is open (Wake Lock, toggle in Settings); audio, MIDI and wake lock re-arm on return to the app.
- Stability: the sequencer timer runs in a Worker (immune to tab-timer throttling), look-ahead 200 ms, 28-voice cap.
- Key feel: native sample rate (no resampler on Android), faster attacks on strings / sax / brass / accordion / flute, a gentler velocity curve, on-screen keys looked up from a map.

## 0.1.0 — 2026-10-20
- First build as a browser app (PWA): Web MIDI input, on-screen keys, built-in synth + Sudanese percussion kit, SF2 import, sequencer with intro / main A-B / fill A-B / ending / sync start, chord-following bass/chord/pad/phrase, 10 styles (Tum Tum, Daloka, Sayra, Haqiba, Mardoum, Pop, Rock, Ballad, Reggae, 6/8), 8 setups, Arabic/English, offline cache.
