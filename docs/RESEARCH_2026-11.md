# Research notes, 2026-11-11 (latency, engines, the Firmware Hub)

Findings behind `docs/PLAN_MIDI_MASTER.md`, with sources. Four passes:

- a read-only code audit;
- low-latency Android best practice;
- open-source instrument engines;
- thefirmwarehub.com.

## A. Code audit: key-to-sound latency in the web app (0.5.0)

Measured with benchmark scripts (headless Chrome 157 + node).

1. **The note path is clean.** Notes start at `ctx.currentTime`: `synth.js:43`, `sf2.js:158`, `orgsampler.js:52`. `engine.js:144` passes `t` undefined. There is no await, timer or rAF.
2. **DynamicsCompressorNode limiter (`audio.js:9–15`): +6.0 ms on every note.** An impulse came out 288 frames late at 48 kHz, matching Chromium's `kPreDelay = 0.006f`.
3. **Slow attacks.** Time for a note to reach −6 dB:

   | Voice | Time to −6 dB |
   | --- | --- |
   | strings | 51 ms (full level at 120 ms) |
   | flute | 20 ms |
   | sax | 17 ms |
   | accordion, brass | 13 ms |
   | lead | 5 ms |
   | oud, piano, tambour, bass | 2 ms |

   Strings is the default lower and pad voice in every style.
4. **Arranger: `LOOKAHEAD = 0.2` (`sequencer.js:8`, `:54`).** Notes are transposed when they are scheduled, so 175–200 ms of accompaniment keeps the old chord after a change. Sustained pad and chord notes keep it until their next event. Sync-start adds 20 ms (`sequencer.js:66`).
5. **PCM is decoded on the first note** (`orgsampler.js:28–44`, `sf2.js:142–156`). On Motaso.Set (347 samples), on a Mac: median 0.13 ms, worst 1.9 ms. Estimated ×5–10 on a phone. About 6 MB is allocated, so expect GC churn.
6. **ORG kit samples start with silence.** "drums 1" and "ZOL" have a median of 17 ms of silence before reaching −40 dB relative to peak. Melodic instruments are mostly under 1 ms.
7. **Chrome's SilentSinkSuspender.** After 30 s of exact silence Chrome switches to a fake sink, and the real stream may close 5 s later. The first note after that restarts the stream: likely tens of ms, not measured. The fix is an inaudible ConstantSourceNode.
8. **JS cost per note is negligible.** noteOn + noteOff takes 31–66 µs on a Mac, about 0.2–1 ms on a phone. `createBiquadFilter` costs 12 µs. Main-thread jitter while a style plays is 1–5 ms (Worker tick every 25 ms, beat-light rAF, GC).
9. **The AudioContext settings are right.** `'interactive'` at the native rate is correct. A numeric `latencyHint` cannot go below the hardware buffer (`GetExactBufferSize`). AudioWorklet would not reduce output latency.
10. **The audit's verdict.**
    - Chrome's floor on a good phone, wired: about 20–35 ms.
    - The code adds about 8–11 ms on fast voices and 50–90 ms with strings or flute.
    - A phone without `FEATURE_AUDIO_LOW_LATENCY` gets a buffer of at least 2048 frames, about 43 ms.

Sources:
- Chromium [dynamics_compressor.cc](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/platform/audio/dynamics_compressor.cc)
- Chromium [audio_manager_android.cc](https://raw.githubusercontent.com/chromium/chromium/main/media/audio/android/audio_manager_android.cc)
- Chromium [audio_latency.cc](https://raw.githubusercontent.com/chromium/chromium/main/media/base/audio_latency.cc)
- Chromium [aaudio_stream_wrapper.cc](https://raw.githubusercontent.com/chromium/chromium/main/media/audio/android/aaudio_stream_wrapper.cc)
- Chromium [silent_sink_suspender.cc](https://chromium.googlesource.com/chromium/src/+/master/media/base/silent_sink_suspender.cc)

## B. Low-latency Android: best practice

Stage-by-stage latency budget:

| Stage | Native Oboe (best practice) | Chrome PWA |
| --- | --- | --- |
| Key scan → USB packet (YM-928) | unknown, ~1–5 ms (measure) | same |
| USB host + MidiService → app | < 1 ms | same, plus a hand-off to the page and the main thread (unbounded when the page is busy) |
| Wait for next render | 0–1 burst (AMidi polled in the callback) | 128-frame quantum + FIFO (up to 512 frames) |
| Stream buffer | 2 bursts (~8 ms at 192 frames / 48 kHz) | 2–3 bursts |
| Mixer | exclusive MMAP skips it (Google: 20 ms round trip exclusive vs 26 ms shared) | always shared |
| HAL / DSP / speaker | a few ms; speaker protection adds a little | same |
| Bluetooth A2DP | +100–300 ms | same |
| **Total, wired / phone speaker** | **~10–20 ms** | ~25–45 ms on a modern phone (estimate) |

- **Device feature flags:**
  - `android.hardware.audio.low_latency`: ≤ 45 ms continuous output.
  - `android.hardware.audio.pro`: ≤ 20 ms round trip, ≤ 80 ms tap-to-tone.
  - Check with `adb shell pm list features | grep audio`. In `dumpsys media.audio_flinger`, an "F" marks a fast track.
- **Chrome:**
  - AAudio LOW_LATENCY, shared only.
  - `baseLatency` covers only Chrome's internal buffering.
  - Web MIDI runs on the main thread. Worker support is only at Intent to Prototype (Aug 2026).
  - A TWA runs the same engine, so it can't match native.
- **Bluetooth:**
  - SBC / AAC / LDAC: 100–250 ms or more.
  - aptX Low Latency: ~40 ms, but retired.
  - LE Audio (LC3): the makers claim 20–60 ms end to end.
  - An app can only set `Usage::Game`, which helps on LE Audio and little on A2DP.
- **This phone:**
  - The Nothing Phone (3a) has no 3.5 mm jack and a USB-C 2.0 port with OTG.
  - Android supports USB Audio Class 1 output and routes to it automatically.
  - A hub with keyboard + DAC is very likely to work, but untested.
- **The YM-928:** 61 touch-response keys, MIDI out, line out, earphone and audio input. No MIDI in is listed and there is no maker spec sheet; test `access.outputs`.
- **Native rules:**
  - no allocation, lock, I/O or JNI in the callback;
  - pre-allocate voices and preload samples;
  - don't set a sample rate (natively, 44.1 kHz on AAudio measured 160 ms);
  - no audio-effect session (it blocks MMAP);
  - keep the stream warm;
  - ADPF `setPerformanceHintEnabled(true)`;
  - start at 2 bursts with LatencyTuner;
  - re-open exclusive streams on route change.
- **Hybrid (web UI + native engine):**
  - It works with Capacitor or a plain WebView, not a TWA.
  - Never send notes over the JS bridge.
  - Turn the web audio and MIDI off in native mode, because WebView grants MIDI permission automatically and notes would double.
- **Measuring:**
  - OboeTester *Tap to Tone* with USB-MIDI input, which removes the touchscreen's 15–30 ms. Run it on each output.
  - A second phone recording at 48 kHz, 10 notes, measured in Audacity (1 ms per 34 cm of mic distance).

Sources:
- Android: [audio latency](https://developer.android.com/ndk/guides/audio/audio-latency), [Oboe low-latency guide](https://developer.android.com/games/sdk/oboe/low-latency-audio), [NDK MIDI / AMidi](https://developer.android.com/ndk/guides/audio/midi), [AAudio](https://source.android.com/docs/core/audio/aaudio), [USB audio](https://source.android.com/docs/core/audio/usb), [Android 14 CDD](https://source.android.com/docs/compatibility/14/android-14-cdd)
- Oboe: [OboeTester usage](https://github.com/google/oboe/blob/main/apps/OboeTester/docs/Usage.md), [Tap to Tone](https://github.com/google/oboe/wiki/OboeTester_TapToTone), [ADPF for audio](https://github.com/google/oboe/wiki/Using-ADPF-for-High%E2%80%90Quality-Audio-Performance), [LatencyTuner](https://google.github.io/oboe/classoboe_1_1_latency_tuner.html), [samples](https://github.com/google/oboe/tree/main/samples)
- Samples: [NDK native-midi](https://github.com/android/ndk-samples/tree/main/native-midi), [android-midisuite](https://github.com/philburk/android-midisuite)
- Chromium: [Chrome Android audio latency bug](https://issues.chromium.org/issues/40103372), [Web MIDI in workers](https://chromestatus.com/feature/5383093186854912)
- Engines and apps: [FluidSynth's Oboe driver](https://github.com/FluidSynth/fluidsynth/blob/master/src/drivers/fluid_oboe.cpp), [einhander/piano](https://github.com/einhander/piano) (GPL-3.0-only, Kotlin + C++ + Oboe + FluidSynth, built for USB keyboards), [LivePerformer](https://github.com/ChrisTorng/LivePerformer) (MIT)
- Hardware: [Nothing Phone (3a) specs](https://m.gsmarena.com/_phone_(3a)-13672.php), [YM-928 listing](https://heartbeatmusicstore.com/products/Yongmei-YM-928-MIDI-Professional-Electronic-Keyboard-Black-p471705692)

## C. Open-source instrument engines

| Engine | Type | Licence | Android-ready? | CPU | Notes |
| --- | --- | --- | --- | --- | --- |
| [FluidSynth](https://github.com/FluidSynth/fluidsynth) | SF2/SF3, GS/XG | LGPL-2.1+ | yes (has an Oboe driver; [android docs](https://github.com/FluidSynth/fluidsynth/tree/master/doc/android)) | low–med | MIDI Tuning Standard octave/key tuning, polyphony setting |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | SF2 | MIT | trivial | low | set max voices or it allocates; SF3 decoded fully to RAM |
| [sfizz](https://github.com/sfztools/sfizz) | SFZ v2 | BSD-2 | yes (AAP port) | med | Scala tuning, disk streaming |
| [Sonivox EAS](https://github.com/pedrolcl/sonivox) | GM wavetable | Apache-2.0 | yes (AOSP) | very low | tiny fallback |
| [msfa / Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed) | DX7 FM | Apache-2.0 / GPL-3 | yes (msfa was Google's Android synth) | very low | no JUCE, `.syx` banks, Scala |
| [Plaits](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/open_source/) | 24-model macro oscillator | MIT | portable | very low | one mono voice (wrap N), fixed buffer, float pitch |
| [Surge XT](https://github.com/surge-synthesizer/surge) | hybrid | GPL-3 | AAP port | high | patch load locks / starts a thread in `process()` |
| [setBfree](https://github.com/pantherb/setBfree) | tonewheel organ | GPL-2+ | builds on aarch64 | low–med | best open Hammond-style organ |
| [ymfm](https://github.com/aaronsgiles/ymfm) | OPL/OPN chips | BSD-3 | portable | very low | + [libADLMIDI](https://github.com/Wohlstand/libADLMIDI) GM banks |
| [Faust](https://github.com/grame-cncm/faust) | DSP language → C++ / WASM | LGPL + exception | `faust2api -android` | depends | one DSP for web and native; physical models |

- **Too heavy on a phone, or tied to JUCE:** Vital, Odin 2, OB-Xd, Helm.
- **Hosts and arrangers to learn from:**
  - [AAP](https://github.com/atsushieno/aap-core) (MIT): Android plugin host with Dexed, Surge XT, sfizz ports.
  - [GrooveForge](https://github.com/yakari/grooveforge) (MIT, F-Droid): FluidSynth + AAudio, a rack of slots, 92 microtonal scales applied through a tuning table.
  - [JJazzLab](https://github.com/jjazzboss/JJazzLab) (LGPL-2.1): its YamJJazz reads SFF1/SFF2.
  - [MMA](https://github.com/infojunkie/mma) (GPL-2): text grooves.
  - [spessasynth_core](https://github.com/spessasus/spessasynth_core) (Apache-2.0): a JS/WASM SF2/SF3 player that could replace the web app's player.
- **GM banks:**
  - [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS): 31 MB, permissive.
  - FluidR3Mono_GM.sf3: 14 MB, MIT.
  - MuseScore_General.sf3: 36 MB, MIT.
  - TimGM6mb: 6 MB, GPL-2 (check "only").
  - Not bundleable: Arachno (non-commercial), Timbres of Heaven (licence unclear).
- **Arabic and Sudanese samples:**
  - [VCSL](https://github.com/sgossner/VCSL) (CC0): darbuka, frame drums.
  - The "Persian instruments" SF2 on [Musical Artifacts](https://musical-artifacts.com/artifacts?license=falv13) (Free Art License 1.3): oud, kanun, nay. Check the sample origin.
  - Otherwise: record our own, or use Faust physical models.

## D. thefirmwarehub.com (M-VAVE FM-1 firmwares)

- **The hub:**
  - Run by Sven Trogus ("zvenson"): [catalog.json](https://thefirmwarehub.com/catalog.json) (191 firmwares, list CC-BY-4.0), [engines.json](https://thefirmwarehub.com/engines.json).
  - The site's generator scripts are not public.
  - 50 firmwares are built by the signed workflow [zvenson/fm1-build](https://github.com/zvenson/fm1-build) (MIT); each card links to its commit and build log.
  - Firmware installs from the browser over Web MIDI SysEx.
  - VIBE FM rebuilds firmwares as one `.wasm` (web player and VST3/AU/CLAP); its SDK is not public yet.
- **The hardware:** JieLi AC791N, dual pi32v2 at 360 MHz, 44.1 kHz, 256-frame interrupts, ~96 KB RAM + 336 KB pool. Mostly fixed-point C.

| Project | Author | Repo | Licence | Engines |
| --- | --- | --- | --- | --- |
| Polly | Charles Vestal | [fm1-polly](https://github.com/charlesvestal/fm1-polly) | GPL-3.0-only (EAS part Apache-2.0) | GM via Sonivox EAS + an 8-part arranger (QY70 model) |
| Felucca | Leo Kuroshita | [hugelton/Felucca](https://github.com/hugelton/Felucca) | GPL-3.0-only | 15 engines: VA, FM6 (msfa), PD, chip, sample, voice, organ, grain, physical, noise, slice, drums |
| Felucca-Engines | Leo Kuroshita | [hugelton/Felucca-Engines](https://github.com/hugelton/Felucca-Engines) | **MIT** | 6 engines + the `engine_t` interface |
| Felucca [Salt] + Studio | Chance Roth | [ChanceTheMaker/Felucca](https://github.com/ChanceTheMaker/Felucca), [Studio](https://chancethemaker.github.io/Felucca/webapp/editor/) | GPL-3.0-only | 9 engines as WASM in a browser Studio (67-line C adapter) |
| CTL-1 | Charles Vestal | [fm1-ctl](https://github.com/charlesvestal/fm1-ctl) | MPL-2.0 | controller + GM fallback |
| X0X | Charles Vestal | [fm1-x0x](https://github.com/charlesvestal/fm1-x0x) | GPL-3.0-only | 909, 808, two 303s (Open303), breakbeats |
| sloopDX | zvenson | [dxsloop](https://github.com/zvenson/dxsloop) | GPL-3.0-only | DX7 in integer C, `.syx` |
| SLOOP | isod89 | [sloop-fm1](https://github.com/isod89/sloop-fm1) | GPL-3.0 | 12 engines (Felucca-derived) |
| Melodee | keremimo | [melodee](https://github.com/keremimo/melodee) | GPL-3.0-only | Prophet-style, FM6, CZ-1, SID, drums; 70 scales incl. maqam |
| FM-106 | PatttF | [fm-106](https://github.com/PatttF/fm-106) | GPL-3.0 | Juno-106 (KR-106 port) |
| DUELL | OSCILL/R | [DUELL](https://github.com/oscillr/DUELL) | GPL-3.0 | 10 engines, morphing |
| fm1_open | M-VAVE | [fm1_open](https://github.com/m-vave/fm1_open) | GPL-3.0 + SDK exception | device engine closed; web C engines open (DX7 Apache-2.0, Open303 / DaisySP / PSG MIT) |

- **Felucca's model:** one `engine_t` table entry per engine, with 8 parameter descriptions, presets, per-voice `note_on` / `render`, and per-part `block` / `post`. 4 parts each pick any engine, sharing an 8-voice pool (`voice_alloc` with retrigger and stealing). Every fork keeps this model.
- **Polly's arranger:**
  - 8 tracks: D1, D2, PC, BA, C1–C4.
  - Sections: Intro, Main A/B, Fill AB/BA, Ending, switched on the next bar.
  - Patterns over Cmaj7 at 480 PPQ.
  - 5 transposition rules in `app/dsp/xpose.c`.
  - `chords.c` matches 26 chord types (inversions, slash chords).
  - Text style files; all 16 MIDI channels play, so it doubles as a GM module.
- **Porting:**
  - Already compiled to WASM: Salt with Zig, Polly / X0X with Emscripten via [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0).
  - It would compile with the NDK as well.
  - Caveats: 44.1 kHz tables, global static state (one instance per library), shared engine file names across forks.
- **Licensing:**
  - GPL-3.0-only code makes the whole app GPL-3.0-only.
  - Keep the headers, ship the licence + NOTICE files, credit the authors.
  - Polly's grooves need CC-BY-4.0 attribution (Groove MIDI Dataset).
  - No QY70 ROM data, no Melodee CZ-1 / Prophet factory presets.
