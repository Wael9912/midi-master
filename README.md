# Midi Master (ميدي ماستر)

A free, open-source Android app that turns a USB-MIDI keyboard into a low-latency **sound module and arranger** with Sudanese and Western rhythms. Its first target is the Youngmei YM-928 on a phone, over a USB OTG cable. It works offline, in Arabic and English. Formerly *Daloka*, a browser app; that version is frozen in [`legacy-web/`](legacy-web/README.md).

**Status: 0.7.0, a complete arranger (milestones M2–M4).**
- **Sounds:** the bundled FluidR3Mono GM bank (195 sounds), your own SF2 / SF3 banks, the ORG 24 set's sampled instruments, and the built-in voices and Sudanese drum kit.
- **Rhythms:** the 12 built-in Sudanese and Western rhythms, your Yamaha `.sty` files, and the ORG 24 set's rhythm. Intro, A / B, fill, ending and sync start, with the chord from the left hand.
- **Screen:** two pickers with searchable lists, the transport, keys pinned to the bottom, 8 setups, a mixer, and the *Audio path* card.

Everything that makes sound runs in one C++ audio callback, including the keyboard (read with AMidi), the chord detector, the sequencer and the instruments. A key press never passes through Java/Kotlin. Measured on the Nothing Phone (3a) with the YM-928:
- stream: 48 kHz, 96-frame burst, 192-frame buffer, exclusive · low-latency · MMAP;
- MIDI hand-off 1.1 ms on average, output latency about 8 ms, no glitches;
- a full rhythm uses 2–4 % of the audio callback's time.

## Install
APK builds will be on GitHub Releases. Until then, build it yourself (below), or download the `midi-master-debug-apk` artifact from the latest CI run. Plug the keyboard in with a USB OTG cable and open the app: it connects to the first MIDI keyboard it finds, and no USB permission dialog is needed. On the first start the app unpacks its sound bank once (under a minute); the built-in sounds play meanwhile.

## Use
- Left of the red line on the keys (the split point): play a chord, and the rhythm follows it. Right of it: the melody.
- Pick a rhythm and a sound with ‹ › or by tapping the name: a searchable list opens. START, INTRO, A, B, FILL and END work as on an arranger keyboard. *Start with first chord* waits for your left hand.
- *More*: left-hand sound, left hand also sounds, two sounds layered, split point, the 8 setups (tap to load, hold to save), all notes off.
- Settings (⚙): add SF2 banks, Yamaha rhythm files (single files or a whole folder) and an ORG 24 set. Also there: the sounds of each rhythm part, muting tracks, the mixer and reverb, and the audio path.

## Build
Needs JDK 17, the Android SDK (platform 36), NDK 29.0.14206865 and CMake 3.31.6.
```
./gradlew assembleDebug                                                       # app/build/outputs/apk/debug/app-debug.apk
cmake -S engine -B build/engine && cmake --build build/engine && build/engine/mm_tests   # engine tests on the desktop
node legacy-web/tests/run.mjs                                                 # reference web engine tests
node legacy-web/tools/dump_fixtures.mjs                                       # regenerate the fixtures the C++ ports are tested against
```
`MM_TEST_SF2=/path/bank.sf2 build/engine/mm_tests` also plays every preset of a real bank and prints the mix levels and per-block render times. The same binary, built with the NDK (`-DMM_TESTS_ON_DEVICE=ON`), runs on the phone over adb with `MM_ONLY_PERF=1`.

## Layout
- `app/`: Kotlin + Jetpack Compose.
  - `AppState.kt`: the controller (settings, setups, library, imports).
  - `MainScreen.kt`, `ListSheet.kt`, `SettingsScreen.kt`: the screens.
  - `Sf3Converter.kt`: SF3 → SF2 with the phone's own decoder.
- `engine/core/`: portable C++17, tested on the desktop.
  - `arranger` (keys → split / chord / channels, mixer);
  - `sequencer`, `chord`, `styledsl`, `sty`, `orgset`: ported from the web app and checked against its output;
  - `sf2` + `sf2_synth` (SoundFont player), `builtin` (synth voices and drum kit), `org_sampler`, `reverb`.
- `engine/android/`: Oboe + AMidi + JNI.
- `engine/tests/`: desktop tests and the JSON fixtures dumped from the web engine.
- `legacy-web/`: the old browser app, the reference for the ports.
- `docs/`: plan, research, changelog, content sources.

## License
Code: **GPL-3.0-only**. Built-in styles: CC BY-SA 4.0. The FluidR3Mono bank: MIT. Third-party parts and their licences: `CREDITS.md`, and in the app under Settings → About.
