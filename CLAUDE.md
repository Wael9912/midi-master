# Midi Master — Sudanese arranger keyboard (native Android app)

## Goal
Free, open-source (GPL-3.0-only), offline Android app. A USB-MIDI keyboard (first target: the owner's Youngmei YM-928 on a Nothing Phone (3a)) becomes a low-latency sound module and arranger with Sudanese and Western styles. Arabic (RTL) + English UI. Target: **≤ 15 ms from key to sound** on the phone speaker. Distributed as an APK on GitHub Releases only (no Play Store).

**Read `docs/PLAN_MIDI_MASTER.md` §0 first.** It holds the owner's decisions of 2026-11-11 and the milestones M1–M6. The latest `docs/CHANGELOG.md` entry says where work stopped.

## Layout
- `app/`: Kotlin + Jetpack Compose (no WebView). It handles UI, MIDI device discovery (`MidiConnector.kt`), file import, settings and setups. The JNI surface is `NativeEngine.kt`.
- `engine/core/`: pure C++17 with no Android headers. It holds the synth and the MIDI parser, and later chord detection, sequencer and style parsers. It builds and is tested on the desktop.
- `engine/android/`: Oboe output stream + AMidi input (`audio_engine.cpp`), and the JNI (`jni_bridge.cpp`).
- `legacy-web/`: the old browser app (0.5.1), **frozen**. It is the reference implementation, and its node tests and parsers are the oracle for the C++ ports. Do not develop it further. GitHub Pages is off.

## Real-time rules (do not break)
- MIDI from the keyboard is read with **AMidi inside the audio callback**. Key presses never pass through Kotlin/Java.
- In the audio callback, never: allocate memory, take a lock, do I/O, log, call JNI, or wait. The UI talks to the audio thread through `SpscQueue` (single producer = the main thread). Stats come back through atomics.
- The stream is opened LowLatency + Exclusive, float, at the device's native rate (never force 44.1 kHz), with `LatencyTuner` and the ADPF hint. It stays open while the app is in front and reopens on a route change (`onErrorAfterClose`).
- Key-part sounds: the first audible sample comes ≤ 1 ms after note-on (the test enforces this). No look-ahead limiter on the master: `Synth::softClip`.
- Heavy work (loading banks, building instruments) happens on a worker thread. It is swapped in with an atomic pointer and freed off the audio thread.

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
node legacy-web/tests/run.mjs                     # reference engine (453 tests)
./gradlew assembleDebug                           # APK (NDK 29.0.14206865, CMake 3.31.6)
$ANDROID_HOME/platform-tools/adb install -r app/build/outputs/apk/debug/app-debug.apk
```
The Mac has 8 GB of RAM, so the Gradle limits in `gradle.properties` are measured values: keep them. The phone is usually on Wi-Fi adb at `192.168.100.14:5555`. It is the owner's everyday phone, which also runs the Memo robot app: install over adb, but don't launch apps on it without asking.

## Definition of done for any task
- The desktop engine tests, the legacy node tests and `./gradlew assembleDebug` pass. CI (`.github/workflows/ci.yml`) runs all three.
- Anything on the audio path is checked on the phone by you, not the owner: the stream line in `adb logcat -s MidiMaster` and the app's *Audio path* card (low-latency · exclusive · MMAP, burst, buffer, glitches, CPU peak). The owner gets no measurement tasks and judges by ear.
- `versionCode` / `versionName` bumped in `app/build.gradle.kts` for every APK given to the owner.
- `docs/CHANGELOG.md` gets an entry. `README.md` and `CREDITS.md` are updated when behaviour or dependencies change.

## Do not
- Add paid or closed-source dependencies, or any network calls (the app is offline). Every dependency must be GPL-3.0-only-compatible: no GPL-2.0-only code.
- Bundle samples or banks without a licence row in `CREDITS.md`. Never bundle QY70 ROM data or Melodee's CZ-1 / Prophet presets.
- Rename the rhythm *Daloka · Sayra*.
