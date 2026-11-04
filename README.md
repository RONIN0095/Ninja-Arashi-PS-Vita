# Ninja Arashi – PS Vita Port (Loader)

Ein Loader, der die originalen Android-Bibliotheken von *Ninja Arashi*
(Unity 6, IL2CPP, armeabi-v7a) auf der PS Vita ausführt. Android, Java und
OpenGL ES 3.0 werden nachgebildet; das Spiel selbst ist nicht enthalten.

**Stand: in Arbeit, noch nicht auf echter Hardware getestet.** Siehe
[`docs/NOTES.md`](docs/NOTES.md) für Stand und Risiken und
[`docs/ANALYSE.md`](docs/ANALYSE.md) für die Analyse der APK.

## Voraussetzungen auf der Vita

- [kubridge](https://github.com/bythos14/kubridge) (in `ur0:tai/config.txt`)
- `libshacccg.suprx` (ShaRKBR33D)
- eine eigene APK von Ninja Arashi (analysiert: Version 1.9.3)

## Installation

1. VPK aus den GitHub-Actions-Artefakten installieren.
2. Aus der APK (ist ein ZIP) kopieren nach `ux0:data/ninjaarashi/`:
   - die APK selbst als `base.apk`
   - `lib/armeabi-v7a/libunity.so` → `lib/libunity.so`
   - `lib/armeabi-v7a/libil2cpp.so` → `lib/libil2cpp.so`
3. Starten. Das Log steht in `ux0:data/ninjaarashi/log.txt`.

## Bauen

Wie der Ninjago-Port: VitaSDK-softfp plus vdpm-Pakete, dann

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Prüfungen ohne Vita-Toolchain:

```sh
python3 tools/check_imports.py      # jeder Import der Bibliotheken hat einen Eintrag
cc -Isource tools/shader_test.c source/na_shaders.c -o shader_test && ./shader_test
```

## Aufbau

| Datei | Inhalt |
|---|---|
| `source/na_modules.c` | Laden von libunity/libil2cpp, `dlopen`/`dlsym`/`dladdr`, Unwind-Tabellen, `dl_iterate_phdr` |
| `source/na_dynlib.c` | Unity-spezifische Importe (stdio, Properties, `sysconf`, Threads, Signale, Locale) |
| `source/dynlib.c` | allgemeine Importtabelle aus dem soloader-boilerplate |
| `source/na_android.c` | ALooper, ANativeWindow, ASensor, AChoreographer |
| `source/na_egl.c` | EGL mit ES-3.0-Konfiguration |
| `source/na_gles3.c` | GLES-3.0-Schicht über vitaGL (Version, Limits, TexStorage, ClearBuffer, Stubs) |
| `source/na_shaders.c` | GLSL ES 3.00 → GLSL ES 1.00 für vitaGL |
| `source/na_java.c` | Java-Seite: Context, Build, Locale, DisplayMetrics, SharedPreferences |
| `source/na_unity.c` | `JNI_OnLoad`, `RegisterNatives`, Startsequenz, `nativeRender` |
| `source/na_sles.c` | OpenSL ES auf SceAudio (aus dem Ninjago-Port) |

Grundlage: [soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate)
(Andy Nguyen, Rinnegatamante, Volodymyr Atamanenko) und der
[Ninjago-Port](https://github.com/RONIN0095/Ninjago-Vita). Lizenz: MIT.
