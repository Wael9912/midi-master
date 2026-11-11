# Midi Master (ميدي ماستر)

A free, open-source Android app that turns a USB-MIDI keyboard into a low-latency **sound module and arranger** with Sudanese and Western rhythms. Its first target is the Youngmei YM-928 on a phone, over a USB OTG cable. It works offline, in Arabic and English. Formerly *Daloka*, a browser app; that version is frozen in [`legacy-web/`](legacy-web/README.md).

**Status: 0.6.0, milestone M1 (first sound).** The app plays a test electric piano from the keyboard through a native C++ engine. Oboe/AAudio handles the sound, and AMidi reads the keyboard inside the audio callback. A diagnostics card shows the phone's audio path: rate, burst, buffer, exclusive / MMAP, latency estimate, MIDI hand-off, glitches and CPU. The arranger, the sound banks and the main screen come back in M2–M4 (`docs/PLAN_MIDI_MASTER.md` §0).

## Install
APK builds will be on GitHub Releases. Until then, build it yourself (below), or download the `midi-master-debug-apk` artifact from the latest CI run. Plug the keyboard in with a USB OTG cable and open the app: it connects to the first MIDI keyboard it finds. No USB permission dialog is needed.

## Build
Needs JDK 17, the Android SDK (platform 36), NDK 29.0.14206865 and CMake 3.31.6.
```
./gradlew assembleDebug                                  # app/build/outputs/apk/debug/app-debug.apk
cmake -S engine -B build/engine && cmake --build build/engine && ctest --test-dir build/engine   # engine tests on the desktop
node legacy-web/tests/run.mjs                            # reference web engine tests
```

## Layout
- `app/`: Kotlin + Jetpack Compose.
- `engine/core/`: portable C++17 (synth, MIDI parser), tested on the desktop.
- `engine/android/`: Oboe + AMidi + JNI.
- `legacy-web/`: the old browser app, the reference for the ports.
- `docs/`: plan, research, changelog, content sources.

## License
Code: **GPL-3.0-only**. Styles and sounds: CC BY-SA 4.0. Third-party parts and their licences: `CREDITS.md`.
