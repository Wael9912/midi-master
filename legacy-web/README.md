# Midi Master 0.5.1 — the browser version (frozen reference)

> Frozen on 2026-11-11 when Midi Master went native (see the repo `README.md`). GitHub Pages is off; paths below are relative to this folder.

**Midi Master** (ميدي ماستر, formerly *Daloka*) turns a USB-MIDI keyboard (first target: the YM928) plugged into an Android phone into a Sudanese and Western arranger keyboard. It is a plain web app: no install step, no app store, no build tools, no dependencies. It works offline once opened, and can be installed to the home screen as a PWA.

- **Sound module:** a built-in synth (oud, tambour, accordion, sax, strings, brass, piano, organ, bass, lead, flute) and a Sudanese percussion kit; import any `.sf2` sound bank for real sampled instruments.
- **Arranger:** the left hand (below the split point) plays chords; drums, percussion, bass, chord and pad parts follow them.
- **Sudanese set:** Tum Tum (6/8), Daloka · Sayra (4/4), Mardoum (6/8), Jarari (6/8), Karachi (2/4), Dalib, Haqiba — built from published descriptions of each rhythm (`docs/CONTENT.md` has the sources). **Western:** Pop, Rock, Ballad, Reggae, Slow 6/8. Each has intro, main A/B, fill, ending.
- **Live controls:** start/stop, start-with-first-chord, fill, A/B, tempo (dotted-quarter display for 6/8), transpose, split point, eight saved setups.
- **Two pickers, phone first:** *Rhythm* and *Right-hand sound* sit at the top of the screen with ‹ › arrows; tap the name for a full-screen list with search and group headers (your rhythm files first, grouped by folder; sounds grouped by bank and family). Picking keeps the list open so you can go through several; the rhythm list has its own START. The keys stay pinned to the bottom.
- **Stays up on stage:** the screen stays on while the app is open (Wake Lock), the sequencer clock runs in a Worker so Chrome's timer throttling cannot stall a bar, and audio/MIDI/wake lock re-arm when you switch back to the app.
- **Open content:** import an **ORG 24 set (`.set`)** for its sampled Sudanese instruments and its rhythm, your own `.sf2` banks, style packs (`.json`) and **Yamaha style files** (`.sty` / `.prs` / `.sst` from any PSR / Tyros / Genos, SFF1 or SFF2); export the built-in styles to edit them.
- **Arabic (RTL) and English.**

## Use it on Android

Web MIDI and the install prompt need a secure page (HTTPS, or `localhost`). Three ways:

1. **Hosted (easiest):** open **https://wael9912.github.io/daloka/** in **Chrome**, tap **Install app** in the top bar (or ⋮ → *Install app* / *Add to Home screen*). Plug the keyboard in with a USB OTG cable, open the app, tap **Connect keyboard**.
2. **From your Mac on the same Wi-Fi, over HTTPS:** run `./serve.sh https` (needs `mkcert` once), then open `https://<mac-ip>:8443` on the phone and accept the certificate.
3. **USB cable + adb:** `adb reverse tcp:8787 tcp:8787`, then `./serve.sh` on the Mac and open `http://localhost:8787` on the phone (localhost counts as secure).

Without a keyboard the on-screen keys work everywhere (touch, multi-finger). On a computer, `a s d f g h j k` play the upper octave, `z x c v b n m` the lower, space = start/stop.

## Playing

| Zone | What it does |
| --- | --- |
| Left of the split (default F#3) | Chord zone. Fingered: play the chord. One finger: root = major, root + white key below = 7th, root + black key below = minor. |
| Right of the split | Upper voice (+ Layer adds the lower voice on top). |
| **Start with first chord** | The rhythm starts on the next chord you play. |
| **FILL** | Plays the fill of the current variation on the next bar, then returns. |
| **A / B** | Switches variation through a fill. |
| **END** | Plays the ending on the next bar and stops. |
| Setups 1–8 | Tap to load, hold to save the whole setup (style, tempo, voices, split, mixer). |

## ORG 24 sets

⚙ → *Import ORG 24 set*: a Sofeh ORG 2024 / Music Studio `.Set` file (copy it to the phone first; a 200 MB set reads in about a second and is kept offline). Its instruments appear in every sound list as "… (set)" and its rhythm appears under *ORG 24 set* on the main screen: Intro 1 = INTRO, Variation 1/2 = A/B, Fill 1/2 = FILL, Variation 3/4 as a second entry. The file does not carry a tempo, so set it with the tempo buttons. If a track got the wrong instrument, change it under Sounds. The format is documented in `docs/CONTENT.md`.

## Yamaha styles

⚙ → *Add rhythm files* takes any number of `.sty` files, and *Add a folder of rhythms* a whole folder (copy them to the phone first; files that are not styles are skipped). Each gives a rhythm with Intro, Main A/B, Fill, Ending; a style with Main C/D also gives a second "(C/D)" entry. The CASM section decides how each part follows your chord. Your files come first in the rhythm list, one group per folder; the built-in sets can be hidden with *Show built-in rhythms*. Korg Pa `.STY` files are a closed format and cannot be imported (`docs/CONTENT.md`).

## Sound banks

The app ships with a synth so it plays out of the box. For sampled instruments, open ⚙ → *Add SF2 banks* (or *Add a folder of banks*) and pick any SoundFont 2 files (for example the free GeneralUser GS, plus any single-instrument banks). All of them are stored in the browser (IndexedDB), loaded on every start, and listed under Settings with a remove button. Every bank's presets appear in the sound lists, grouped by bank and GM family. Style voices prefer the first bank that has the GM program (oud → 24 nylon guitar, tambour → 105 banjo, accordion → 21, strings → 48, brass → 61, sax → 65, bass → 33); drums take the first bank with a kit (bank 128).

A Sudanese bank `daloka-sudan.sf2` (daloka, tabl, tum-tum, claps, oud, tambour, Sudanese lead) is the main non-code task; see `docs/CONTENT.md`.

## Style packs

A style pack is a JSON array of styles. Export the built-in pack from ⚙ → *Export built-in styles*, edit it, and import it back; an imported style with the same `id` replaces the built-in one. The format is documented at the top of `js/styledsl.js`: one text grid per track, written over C major. Example of a drum track and a bass track for one bar of 4/4 (16 steps):

```json
"mainA": {
  "bars": 1,
  "drums": { "K": "x..x..x.x..x..x.", "C": "....x.......x...", "S": "x.x.x.x.x.x.x.x." },
  "bass":  "C2 . . C2 . . G2 . C2 . . C2 . . G2 ."
}
```

Drum letters: `D` dum, `T` tak, `M` mambo (the three bangaz), `K` kick, `A` snare, `L`/`H` nugara low/high, `C` clap, `J` shaker, `W` shatam sticks, `R` riq, `S` hat, `O` open hat, `B` bell, `Z` crash, `N` tom. `x` hit, `X` accent, `o` soft. Pitched tokens: `C2`, `C3+E3+G3` (chord), `C2:4` (4 steps long), `.` rest, `-` tie, `!` accent, `?` soft.

Validate a pack from the command line:

```
node tools/style_lint.mjs my-pack.json
```

## Develop

```
./serve.sh            # http://localhost:8787
node tests/run.mjs    # engine tests: chord detection ×12 keys, DSL, the Sudanese set's metres and strokes, sequencer, 10,000-bar drift
```

Files: `index.html`, `css/app.css`, `js/` (`app.js` UI · `sheet.js` the list sheet · `catalog.js` list order, grouping, search · `engine.js` routing over the loaded banks · `synth.js` built-in sounds · `sf2.js` SoundFont loader · `sty.js` Yamaha styles · `orgset.js` + `orgsampler.js` ORG 24 sets · `sequencer.js` · `chord.js` · `styledsl.js` · `styles.js` · `midi.js` · `audio.js` · `store.js` · `i18n.js`), `sw.js` + `manifest.webmanifest` (PWA).

Latency: Chrome on Android gives roughly 20–35 ms key-to-sound on a good phone with the phone speaker or a cable, worse on cheap phones. A **Bluetooth speaker adds 100–300 ms** that no app can remove: use a cable (a powered USB-C hub with a headphone jack, into the keyboard's audio input or a powered speaker). The road to under 15 ms (a native Android engine) and to several virtual instruments in one app is in `docs/PLAN_MIDI_MASTER.md`; the findings behind it are in `docs/RESEARCH_2026-11.md`.

## License

Code: GPL-3.0. Styles and sounds: CC BY-SA 4.0. Every bundled sample must have an entry in `CREDITS.md`.
