# Credits and licenses

| File | Source | License |
| --- | --- | --- |
| Native engine (`engine/`), app (`app/`) | Written for this project | GPL-3.0-only |
| [Oboe](https://github.com/google/oboe) 1.9.3 (linked from Maven, `com.google.oboe:oboe`) | Google | Apache-2.0 |
| AndroidX, Jetpack Compose, Kotlin stdlib (linked from Maven) | Google, JetBrains | Apache-2.0 |
| Gradle wrapper (`gradlew`, `gradle/wrapper/`) | Gradle Inc. | Apache-2.0 |
| Reference web app (`legacy-web/`), built-in sounds (`legacy-web/js/synth.js`, ported to `engine/core/builtin.cpp`) | Synthesised in code, no samples | GPL-3.0-only (code) |
| `app/src/main/assets/FluidR3Mono_GM.sf3` (v2.312, 14.5 MB; from MuseScore 2.3.2 `share/sound/`) | Frank Wen (FluidR3 GM), mono version by Michael Cowgill; Temple Blocks by Ethan Winer; Drumline Percussion by Michael Schorsch | MIT (text in `app/src/main/assets/licenses/NOTICES.txt`) |
| Built-in styles (`legacy-web/js/styles.js`, exported to `app/src/main/assets/styles.json`) | Written for this project from published descriptions of each rhythm (sources listed in `docs/CONTENT.md`); no pattern was copied from a commercial style file | CC BY-SA 4.0 |
| `legacy-web/icons/*.png`, app icon | Drawn for this project | CC BY-SA 4.0 |

Every sample added to a bundled bank needs a row here with its origin and license (own recording, CC0, CC-BY, …).

The app shows `app/src/main/assets/licenses/NOTICES.txt` under Settings → About → Licences. It holds the credits above, the FluidR3Mono MIT notice, the Apache-2.0 licence (Oboe, AndroidX, Compose, Kotlin) and the GPL-3.0 text.

The SoundFont player (`engine/core/sf2*.cpp`) is written for this project from the SoundFont 2.04 specification; no FluidSynth code is used. Its attenuation scaling (×0.4) follows FluidSynth's documented behaviour, the player the bank was voiced for.
