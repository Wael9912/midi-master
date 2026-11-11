# Midi Master — Sudanese arranger keyboard (native Android app)

## Goal
Free, open-source (GPL-3.0-only), offline Android app. A USB-MIDI keyboard (first target: the owner's Youngmei YM-928 on a Nothing Phone (3a)) becomes a low-latency sound module and arranger with Sudanese and Western styles. Arabic (RTL) + English UI. Target: **≤ 15 ms from key to sound** on the phone speaker. Distributed as an APK on GitHub Releases only (no Play Store).

**Read `docs/PLAN_MIDI_MASTER.md` §0 first.** It holds the owner's decisions of 2026-11-11 and the milestones M1–M6. The latest `docs/CHANGELOG.md` entry says where work stopped.

## Layout
- `app/`: Kotlin + Jetpack Compose (no WebView).
  - `AppState.kt`: the controller (port of `app.js`): settings JSON in `filesDir`, the 8 setups, the library (banks, rhythm files, ORG set), imports through SAF, picking the rhythm's sounds.
  - `MainScreen.kt` / `ListSheet.kt` / `SettingsScreen.kt`: the screens. `Strings.kt`: EN/AR (in code, so the language switches live).
  - `Catalog.kt`: order, grouping and search (port of `catalog.js`). `Sf3Converter.kt`: SF3 → SF2 with MediaCodec.
  - `MidiConnector.kt`: MIDI device discovery. The JNI surface is `NativeEngine.kt` (send commands from the main thread only).
- `engine/core/`: pure C++17 with no Android headers, built and tested on the desktop.
  - `arranger.*`: keys → split / chord zone / channels, routing to the instruments, buses, reverb, soft clip. The one object on the audio thread.
  - `sequencer`, `chord`, `styledsl`, `sty`, `orgset`: the ports, checked against the JS fixtures.
  - `sf2` (lazy-loading bank) + `sf2_synth` (SoundFont voices), `builtin` (synth.js voices + kit, incl. the M1 FM piano), `org_sampler`, `reverb.h`, `dsp.h`, `json` (style packs only, never on the audio thread).
- `engine/android/`: Oboe output stream + AMidi input (`audio_engine.cpp`), and the JNI (`jni_bridge.cpp`, which also keeps the style library).
- `app/src/main/assets/`: `styles.json` (the built-in pack, written by `legacy-web/tools/dump_fixtures.mjs`), `FluidR3Mono_GM.sf3` (unpacked to `filesDir/bundled/` once), `licenses/NOTICES.txt` (shown in About).
- `legacy-web/`: the old browser app (0.5.1), **frozen**. It is the reference implementation, and its node tests and parsers are the oracle for the C++ ports. Do not develop it further. GitHub Pages is off.

## Real-time rules (do not break)
- MIDI from the keyboard is read with **AMidi inside the audio callback**. Key presses never pass through Kotlin/Java.
- In the audio callback, never: allocate memory, take a lock, do I/O, log, call JNI, or wait. The UI talks to the audio thread through `SpscQueue` (single producer = the main thread). Stats come back through atomics.
- The stream is opened LowLatency + Exclusive, float, at the device's native rate (never force 44.1 kHz), with `LatencyTuner` and the ADPF hint. It stays open while the app is in front and reopens on a route change (`onErrorAfterClose`).
- Key-part sounds: the first audible sample comes ≤ 1 ms after note-on (the test enforces this). No look-ahead limiter on the master: `Synth::softClip`.
- Heavy work (loading banks, building instruments) happens on a worker thread. It is swapped in with an atomic pointer and freed off the audio thread.
- Banks, ORG sets and styles reach the audio thread as `Command` pointers; the ones it lets go come back through `Arranger::retired()` and are deleted in `NativeEngine.drainRetired()` (main thread).
- SF2 samples load lazily per preset (`Sf2Bank::loadPreset`, worker); the audio thread skips a zone whose sample pointer is not published yet. Always load a program's preset before `SET_PROGRAM` (`AppState.applyProgram` does).
- FluidSynth is not used: building it was blocked in the 0.7.0 session. The SF2 player is our own (`sf2_synth.cpp`). Do not add a Vorbis decoder: SF3 goes through Android's MediaCodec.

## Ported logic is tested against the web app
When porting `chord.js`, the sequencer, `styledsl.js` / `styles.js`, `sty.js` or `orgset.js`:
1. Dump the JS results to JSON fixtures with a tool in `legacy-web/tools/`.
2. Make the C++ tests in `engine/tests/` reproduce them exactly.
3. Keep the 10,000-bar drift test.

## Build and test
```
export JAVA_HOME=/opt/homebrew/opt/openjdk@17 ANDROID_HOME=/opt/homebrew/share/android-commandlinetools
CM=$ANDROID_HOME/cmake/3.31.6/bin
$CM/cmake -S engine -B build/engine -G Ninja -DCMAKE_MAKE_PROGRAM=$CM/ninja && $CM/cmake --build build/engine && build/engine/mm_tests
MM_TEST_SF2=<decoded FluidR3Mono .sf2> build/engine/mm_tests   # + every preset of the real bank, mix levels, render times
node legacy-web/tests/run.mjs                     # reference engine (453 tests)
node legacy-web/tools/dump_fixtures.mjs ~/Downloads/Motaso.Set   # fixtures (+ the owner's set, local only, build/fixtures-local)
./gradlew assembleDebug                           # APK (NDK 29.0.14206865, CMake 3.31.6)
$ANDROID_HOME/platform-tools/adb install -r app/build/outputs/apk/debug/app-debug.apk
```
The Mac has 8 GB of RAM, so the Gradle limits in `gradle.properties` are measured values: keep them. The phone is usually on Wi-Fi adb at `192.168.100.14:5555`. It is the owner's everyday phone, which also runs the Memo robot app: install over adb, but don't launch apps on it without asking.

## Definition of done for any task
- The desktop engine tests, the legacy node tests and `./gradlew assembleDebug` pass. CI (`.github/workflows/ci.yml`) runs all three.
- Anything on the audio path is checked on the phone by you, not the owner: the stream line and the `stats:` line (every 10 s while the keyboard is played: MIDI hand-off, latency, xruns, CPU peak) in `adb logcat -s MidiMaster`, and the app's *Audio path* card. For render cost, build the tests with the NDK (`-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=29 -DANDROID_STL=c++_static -DMM_TESTS_ON_DEVICE=ON`), push to `/data/local/tmp`, run with `MM_ONLY_PERF=1 MM_TEST_SF2=… MM_ASSETS_DIR=… nice -n 19`, then delete the files. The owner gets no measurement tasks and judges by ear.
- The owner said on 2026-11-11 "use wifi to build and test on the org": installing and launching Midi Master over Wi-Fi adb for a test is fine. Take screenshots only while Midi Master is in front (it is the owner's everyday phone).
- `versionCode` / `versionName` bumped in `app/build.gradle.kts` for every APK given to the owner.
- `docs/CHANGELOG.md` gets an entry. `README.md` and `CREDITS.md` are updated when behaviour or dependencies change.

## Do not
- Add paid or closed-source dependencies, or any network calls (the app is offline). Every dependency must be GPL-3.0-only-compatible: no GPL-2.0-only code.
- Bundle samples or banks without a licence row in `CREDITS.md`. Never bundle QY70 ROM data or Melodee's CZ-1 / Prophet presets.
- Rename the rhythm *Daloka · Sayra*.
