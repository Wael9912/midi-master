# Midi Master — plan (2026-11-11)

Midi Master (formerly Daloka) turns the owner's Youngmei **YM-928** (61 velocity keys, USB-MIDI out) on an Android phone into a sound module + arranger. The owner hears a **noticeable delay from key to sound** on the phone speaker and on an external speaker, and wants **several virtual instruments in one app**. This plan comes from a code audit and three research passes on 2026-11-11. Findings and sources are in `docs/RESEARCH_2026-11.md`.

Next chat: read this file, get the owner's answers to §1, then start Phase 0.

## Where the delay comes from

| Source | Delay | Fixable how |
| --- | --- | --- |
| Bluetooth speaker (A2DP: SBC / AAC / LDAC) | **+100–300 ms** | Not from the app. Use a wired output (see D1). |
| Chrome's audio path (shared stream, 128-frame quantum + FIFO, MIDI on the page's main thread) | **20–35 ms floor** on a good phone, wired | Native app only (Oboe + AMidi): 10–20 ms. |
| Arranger look-ahead (`sequencer.js:8`, `LOOKAHEAD = 0.2`) | **175–200 ms** of the old chord after a chord change | Shorter look-ahead + re-schedule on chord change. |
| Slow attacks on strings / flute / sax / accordion / brass (`synth.js:5–15, 89–91`) | 13–51 ms before the note is heard | ≤ 10 ms attack on the key channels. |
| Leading silence in ORG drum samples | ~17 ms (median, "drums 1", "ZOL") | Start offset past the silence, found at load. |
| Chrome stops the output after 30 s of silence; the first note restarts it | tens of ms (likely, not measured) | Keep-alive signal (inaudible constant source). |
| `DynamicsCompressorNode` limiter (`audio.js:9–15`) | **6 ms on every note** (measured, Chromium `kPreDelay`) | WaveShaper soft clip (no look-ahead). |
| PCM decode on the first note of each sample (`orgsampler.js:28–44`, `sf2.js:142–156`) | 1–15 ms, once per sample | Decode in `setProgram` / idle time. |
| JS per note, main-thread jitter | < 1 ms, 1–5 ms jitter | Fine. |

The note path itself is clean: no await, timer or look-ahead on played keys. On a fast voice the code adds ~6–11 ms over Chrome's floor.

## 1. Owner decisions (ask first)

- **D1 — The speaker.** How is the external speaker connected: Bluetooth, cable, or the keyboard's own speakers? If Bluetooth, that alone is 100–300 ms and no app can remove it. The phone has no 3.5 mm jack and its USB-C port holds the keyboard. Recommended wired setup: a **powered USB-C OTG hub with a headphone jack (or hub + USB-C DAC dongle)**. Plug the keyboard into the hub, and run a cable from the jack to the YM-928's AUX/audio-in, or to a powered speaker. Android routes audio to a USB DAC automatically. MIDI plus a DAC on one hub is very likely to work, but it needs testing on this phone.
- **D2 — Go native.** **Recommended:** an Android app (Kotlin shell + C++ engine on Oboe/AAudio, MIDI read with AMidi inside the audio callback), installed as an APK from GitHub Releases. The web app stays as the free "try it in the browser" version. This is the only route under 15 ms.
- **D3 — Repo and URL.** Keep the repo `Wael9912/daloka` and the Pages URL `wael9912.github.io/daloka/` until the APK ships. Renaming the repo breaks the Pages URL, and with it the PWA already installed on the phone. When the APK ships, rename the repo to `midi-master`, then publish a one-file redirect at the old URL.
- **D4 — License.** Make it **GPL-3.0-only**: Polly and Felucca are GPL-3.0-only. MIT / Apache / BSD / LGPL / MPL parts are all compatible. GPL-2.0-*only* code (check TimGM6mb) is not.
- **D5 — The bundled sound bank.** Recommended: **FluidR3Mono_GM.sf3** (14 MB, MIT) inside the APK, so it plays good GM sounds out of the box. GeneralUser GS 2.0 (31 MB, its own permissive licence) is an option. Imports stay as today.
- **D6 — The name.** Check the Play Store for an existing "MIDI Master" before publishing there. This doesn't matter for GitHub releases.

## 2. Phase 0 — measure, and fix the web app (one session, ships as 0.6.0)

**Measure first** (the owner, 15 minutes):
1. Read the latency value in Settings (the app shows Chrome's `baseLatency + outputLatency`). Around 10 ms means the phone is on the low-latency path; ≥ 40 ms means the phone or Chrome is the limit.
2. Play the same note through the phone speaker and through the external speaker. Whatever goes away on the phone speaker is the Bluetooth/speaker share.
3. Install **OboeTester** (Play Store) → *Tap to Tone* with the YM-928 as the MIDI input, on each output. This is the native floor on this phone. Note the burst size and whether MMAP/exclusive is granted.
4. Optional, exact: record key click + sound with a second phone at 48 kHz, 10 notes, measure in Audacity.

**Web fixes** (all small, keep `node tests/run.mjs` green):
1. Replace the compressor with a WaveShaper `tanh` soft clip (`oversample: 'none'`). Saves 6 ms on everything.
2. Attack ≤ 10 ms for the upper/lower voices (channels 0–1). Keep long attacks for the style pad parts.
3. `LOOKAHEAD` 0.2 → ~0.07 s. On `setChord`, cancel the not-yet-started transposing notes and schedule them again. This needs `noteOn` to return its voices, so `SF2Player.noteOn` must return them too. Sync-start 20 → 5 ms.
4. Pre-decode the selected instrument's PCM in `setProgram`, the rest in `requestIdleCallback`.
5. Trim leading silence: at load, store the first frame above −50 dB, then `start(when, offset)`.
6. Keep-alive: a `ConstantSourceNode` at 1e-6 into the master, so Chrome never suspends the output.
7. Diagnostics row in Settings: `baseLatency`, `outputLatency`, MIDI hand-off (`performance.now() - e.timeStamp`), burst estimate.
8. **MIDI out test:** if `access.outputs` lists the YM-928, add "send a test note to the keyboard". If it plays, the keyboard's own sounds become a zero-latency fallback.
9. A test in `tests/run.mjs` for each fix that can be tested without a browser: re-scheduling on chord change, the trim-offset finder, the attack caps. Golden-audio checks come in Phase 1.

## 3. Phase 1 — the native engine (Android)

Goal: **≤ 15 ms MIDI-to-sound** on the phone speaker or wired, with no MIDI note ever passing through JS.

- **Layout:** an `android/` folder in this repo. Kotlin app, C++17 engine (`libmidimaster.so`). A WebView shows the existing UI from the APK's assets, copied in at build time. The web app at the repo root stays deployable to Pages.
- **Audio:** Oboe (Apache-2.0) with:
  - `PerformanceMode::LowLatency`, `SharingMode::Exclusive` (fall back to Shared), `Usage::Game`;
  - float output, the device's native rate (never force 44.1 kHz), data callback, a buffer of 2 bursts with LatencyTuner;
  - no audio-effect session;
  - ADPF performance hint on;
  - stream kept open and warm while the app is in front;
  - re-open on route change (plugging in a DAC, Bluetooth).
- **MIDI:** Android MIDI API to open the device, then **AMidi polled at the top of every audio callback** (non-blocking). Notes reach the synth within one burst.
- **Real-time rules:** no allocation, lock, I/O, logging or JNI in the callback. The UI talks to the audio thread through lock-free SPSC queues. Instruments are built on a worker thread, swapped in with an atomic pointer, and freed on the worker.
- **Sounds, first cut:**
  - a port of `synth.js`'s voices and the ORG sampler (`orgsampler.js`) to C++;
  - FluidSynth (LGPL-2.1, used as a library from our own callback, not its Oboe driver: that driver defaults to Shared mode, no performance mode, 44.1 kHz) for SF2/SF3.
- **Bridge:** `addJavascriptInterface` / `WebMessage` for settings, program changes, style data and the beat lights only. When native, the web app turns its own Web MIDI and Web Audio off; a `window.MidiMasterNative` check keeps the browser build unchanged. Native forwards note events to JS asynchronously for key highlighting and chord display.
- **Arranger in Phase 1:** stays in JS and sends timestamped events (native audio clock, ~70 ms ahead) over the bridge. Phase 3 moves it native.
- **Reference code to read and borrow from:**
  - **einhander/piano**: GPL-3.0-only, Kotlin + C++17 + Oboe + FluidSynth, built for USB keyboards. The closest existing app.
  - the NDK `native-midi` sample (AMidi);
  - the Oboe samples (hello-oboe, MegaDrone, DrumThumper);
  - **GrooveForge** (MIT): FluidSynth + AAudio, a rack of slots, microtonal tuning tables.
- **Exit criteria:**
  - OboeTester or a recording shows ≤ 15 ms on the phone speaker;
  - 30 minutes of style playback with no xruns;
  - the browser build still passes its tests;
  - a CI build of the APK.

## 4. Phase 2 — the instrument rack (several virtual instruments)

The model to copy is **Felucca's `engine_t`** (one table entry per engine: parameter descriptions, presets, `note_on`, `render`), plus **CLAP-style sample-stamped events**:

```
Instrument { prepare(sampleRate, maxBlock, maxVoices); process(events, n, L, R, frames);
             setTuning(cents[128]); allNotesOff(); activeVoices(); params(); saveState(); loadState(); }
```

- **Adapters** for each engine's own block size (Surge 32, msfa 64, Plaits 12), using small internal FIFOs.
- **Rack:** 16 fixed slots, plus a routing table from MIDI channel / arranger part / key range to slots:
  - parts: Upper 1, Upper 2 (layer), Lower, Bass, Chord, Pad, Phrase, Drums, Percussion;
  - per slot: gain / pan / reverb and chorus sends, transpose, octave;
  - one shared reverb and chorus, a soft-clip master, denormals flushed to zero.
- **Budget:** a voice cap per slot plus a global steal policy that takes released voices first. Each slot's CPU is timed in the callback. Above ~65 % of the deadline, shed voices, then effects. A CPU meter per slot in the UI.
- **Presets:** each preset is `{engine, state, name, category, gmProgram}`. A **Performance** saves the whole rack (the 8 setups grow into this, like Registration Memory).
- **Engines, in build order** (all GPL-3.0-compatible):

| # | Engine | What it gives | Licence |
| --- | --- | --- | --- |
| 1 | FluidSynth | SF2/SF3, GM, the bundled bank, octave tuning | LGPL-2.1 |
| 2 | Built-in (ported from `synth.js`) + ORG sampler | today's sounds + ORG 24 sets | ours |
| 3 | msfa / Synth_Dexed | DX7 FM (EPs, bells, basses), loads `.syx` banks; very low CPU, no JUCE | Apache-2.0 / GPL-3 |
| 4 | Felucca-Engines | analog, FM, phase distortion, chip, organ, grain, physical, drums (plain C) | **MIT** |
| 5 | Plaits | 24 models in one engine (VA, FM, wavetable, chords, modal/string physical models, 808-style drums, speech); float pitch, so microtones work | MIT |
| 6 | sfizz | SFZ multisamples: oud, darbuka, converted ORG sets; Scala tuning | BSD-2 |
| 7 | X0X (Polly's author) | synthesized 909 / 808 kits + two 303s, no samples needed | GPL-3.0-only |
| later | setBfree (tonewheel organ + Leslie), ymfm (OPL/OPN), Surge XT (needs a wrapper; its patch loading locks) | | GPL-2+ / BSD-3 / GPL-3 |

The FM-1 firmware code (Felucca, X0X, sloopDX, Melodee) is plain C fixed at 44.1 kHz with global state. Build each project as its own `.so` or with name prefixes, because they share engine file names. Regenerate the tables for 48 kHz (Felucca-Engines can) or resample. Skip Vital and Odin 2 (too heavy on a phone) and anything JUCE-bound unless its DSP comes out cleanly (JUCE 8 is AGPL).

## 5. Phase 3 — the arranger goes native, and Arabic tuning

- **Real-time arranger in C++:**
  - **Chord detection:** borrow from **Polly** (`chords.c`: 26 chord types, inversions, slash chords).
  - **Transposition:** Polly's `xpose.c` rules (bypass / parallel / chord 1 / chord 2 / bass). They match Yamaha's CASM note-transposition rule (NTR) and table (NTT) closely, so `.sty` playback gets closer to a real PSR.
  - **Chord changes** take effect immediately: not-yet-started notes are re-transposed inside the audio thread.
- **Styles stay parsed in JS** (`sty.js`, `orgset.js`, `styledsl.js`) and are compiled to one flat event list handed to native once. The node tests stay. The C++ sequencer gets tests on the same fixtures, including the 10,000-bar drift test.
- **References:**
  - JJazzLab's YamJJazz (LGPL-2.1, Java) for SFF1/SFF2 details we skip today;
  - MMA (GPL-2) and Polly's text style format for writing Sudanese rhythms by hand.
- **Maqam / quarter tones:**
  - An "Arabic scale" control like an arranger keyboard has: tap a key to lower it by ¼ tone, with saved presets (Rast, Bayati, Saba, Hijaz, Sikah…).
  - Implemented as per-note cent tables pushed to every slot: FluidSynth octave tuning; direct float pitch in Plaits and Faust engines; channel rotation + pitch bend as the fallback.
  - Melodee's 70 scale tables (maqam models) are a reference.
- **Sudanese sounds:** the `daloka-sudan.sf2` task in `docs/CONTENT.md` stays. Sources:
  - VCSL (CC0): darbuka, frame drums;
  - the Musical Artifacts "Persian instruments" SF2 (Free Art License; check sample origin);
  - the owner's own recordings;
  - Faust physical models for oud / qanun / ney if no clean samples turn up.

## 6. Phase 4 — the screen (inspired by Felucca [Salt] Studio)

- Keep the phone-first rule: two pickers + transport + keys pinned to the bottom, nothing pushes the keys below the fold at 390×844.
- The sound picker becomes **engine-aware**. One list with search, grouped by engine then category; each entry has a small engine badge (SF2, FM, Analog, Sample, Drum).
- An **instrument card** for the selected part. Felucca-style engines share the same parameter description, so one card draws any engine's 8 knobs or sliders.
- A **Rack** sheet: parts as rows with engine, preset, volume, split / layer, and the CPU meter.
- Optional extras from Salt Studio: oscilloscope strip, themes, drag-to-reorder.

## 7. Quality and prevention (from the start, not after)

- **CI** (GitHub Actions):
  - node tests;
  - C++ unit tests on desktop (the engine builds without Android);
  - an APK build;
  - golden-audio offline renders per engine (fixed MIDI in, compare RMS / spectrum within a tolerance);
  - a **latency regression check**: the first audible sample after note-on is ≤ 1 ms for every key-channel preset. This catches slow attacks and leading silence.
- **Real-time safety:** a debug build counts allocations and locks inside the callback (a malloc hook) and fails the test if any happen.
- **On the phone:** each release gets the Phase 0 measurement (OboeTester tap-to-tone + one recording), logged in `docs/CHANGELOG.md`.
- **Licences:**
  - keep the SPDX and copyright headers;
  - ship the licence texts and the Apache NOTICE files (Oboe, msfa, Sonivox if used);
  - credit every author in About and `CREDITS.md`;
  - CC-BY attribution for any groove dataset;
  - never bundle QY70 ROM data or Melodee's CZ-1 / Prophet factory presets.

## 8. What stays true from the current rules

The current rules still apply:

- free, open source, offline;
- no paid or closed dependencies, no network calls;
- Arabic RTL + English;
- storage names `daloka.v1` / `daloka` never change (the rename already kept them);
- engine logic stays testable without a browser or a phone.
