# Ninja Arashi – PS Vita Port: Übergabe

Stand der Analyse für eine neue Session. Ziel: Loader-Port (wie GTA SA, Doodle
Jump, LEGO Ninjago Tournament): die originalen Android-ARM-Bibliotheken laufen
unverändert, ein Vita-Programm spielt Android/Java/OpenGL ES drumherum vor.

## Die APK

- Analysiert wurde Version 1.9.3 (~95 MB). Die APK ist nicht Teil dieses
  Repos: Wer den Port nutzt, nimmt die eigene APK (Play Store).
- Engine: **Unity 6000.2.6f2 (Unity 6.2), IL2CPP**
- 32-Bit-Bibliotheken vorhanden (`lib/armeabi-v7a/`):
  - `libmain.so` (8 KB, 25 Imports)
  - `libunity.so` (15,6 MB, 350 Imports; NEEDED: libmain, libandroid, liblog,
    libz, libEGL, libmediandk, libm, libdl, libc)
  - `libil2cpp.so` (41 MB, 282 Imports)
  - `libapplovin-native-crash-reporter.so` (Werbung, nicht laden)
- Alle Daten in der APK (`assets/bin/Data/...`), keine OBB.
  `global-metadata.dat` liegt unter `assets/bin/Data/Managed/Metadata/`.

## Das große Problem: OpenGL ES 3.0

- Unity 6 hat den GLES2-Renderer entfernt. Ohne ES 3.0 startet es nicht.
- Unity lädt GL-Funktionen per `eglGetProcAddress`/`dlsym` (keine direkten
  `gl*`-Imports). Im Binary stehen **257** GL-Funktionsnamen, davon fehlen in
  vitaGL (`lookup.c`) **~130**. Wichtige fehlende:
  - Uniform Buffer: `glGetActiveUniformBlockiv`, `glGetActiveUniformBlockName`,
    `glGetUniformIndices`, `glGetActiveUniformsiv` (Unity übergibt Konstanten
    über UBOs)
  - Texturen: `glTexStorage2D/3D`, `glTexImage3D`, `glTexSubImage3D`,
    `glCompressedTexSubImage2D/3D`, `glGetTexLevelParameteriv`
  - Framebuffer: `glDrawBuffers`, `glClearBufferfv/fi/uiv`,
    `glInvalidateFramebuffer`, `glRenderbufferStorageMultisample`,
    `glFramebufferTextureLayer`
  - Sonstiges: `glVertexAttribIPointer`, `glUniform*uiv`, `glFenceSync`,
    `glClientWaitSync`, `glDeleteSync`, `glCopyBufferSubData`,
    `glGetInternalformativ`, `glGetShaderPrecisionFormat`, `glDetachShader`,
    `glValidateProgram`, `glIsVertexArray`
  - Optional/abschaltbar: Debug-Labels, Compute, Multiview, Tessellation,
    Indirect-Draws, Queries
- Shader sind **GLSL ES 3.00** (`#version 300 es`) mit `in/out`, `texture()`,
  `layout(std140) uniform`-Blöcken. Der GLSL→CG-Übersetzer von vitaGL ist auf
  GLSL ES 1.0 ausgelegt. Uniform-Blöcke müssen auf normale Uniforms
  abgebildet werden (Unity-Shader per Regex/Parser umschreiben ist eine Option).
- Der Treiber muss "OpenGL ES 3.0" als Version melden, die fehlenden
  Funktionen zumindest als Stubs liefern, und die von Unity genutzten
  richtig umsetzen.

## Weitere Punkte

- **Mehrere Module**: libmain → libunity → libil2cpp (dlopen). so_util kann
  mehrere Module (`so_module` hat `next`); `dlopen`/`dlsym` müssen auf die
  geladenen Module zeigen.
- **Unity-Start (wie pg3d)**: `JNI_OnLoad` → `UnityPlayer`-Natives
  (`initJni`, `nativeFile` mit APK-Pfad, `nativeRecreateGfxState`,
  `nativeResume`, `nativeRender`-Schleife). Unity liest APK direkt als ZIP.
- **Java-Seite**: FalsoJNI mit dynamisch registrierten Klassen, PlayerPrefs →
  SharedPreferences, Display/PackageInfo-Stubs, NDK: ANativeWindow, ALooper,
  ASensor, AInputEvent, AAssetManager.
- **Speicher**: Vita ~350 MB für Spiele. libil2cpp + libunity ~60 MB Code
  plus Unity-Heap. Volles User-RW + phycont nutzen.
- **Audio**: Unity 6 nutzt eigenes Audio (FMOD intern) über AAudio/OpenSL ES.
  Bei Ninjago lief ein eigener OpenSL-ES-Ersatz (`ng_sles.c`), wiederverwendbar.
- **Steuerung**: IL2CPP-Dumper (`global-metadata.dat` + `libil2cpp.so`)
  liefert Methoden-Adressen; Eingabe über Touch-Events oder Hooks auf die
  Spiel-Skripte (Joystick-Klasse).

## Erfahrungen aus dem Ninjago-Port (übertragbar)

- Spiele laden Texturen in Hintergrund-Threads mit eigenen EGL-Kontexten:
  alle GL-Aufrufe über eine Sperre leiten (Ninjago: `ng_gllock.c` +
  generierte Thunks), Grafikzustand pro Thread sichern/wiederherstellen.
- vitaGL `SAFE_UNIFORMS=1`, wenn das Spiel Uniform-Locations mit Offsets nutzt.
- Shader-Cache als eine Paketdatei (vitaGL-Patch) statt tausender Einzeldateien;
  fehlgeschlagene Shader merken.
- Keine MSAA, wenn das Spiel in eigene FBOs rendert.
- Bionic-Mutexattr ist ein int; pthread-Objekte per Hash-Tabelle verwalten.
- Watchdog + Log-Datei + Core-Dump-Auswertung (vita-parse-core) sparen viel Zeit.
- Quellcode Ninjago-Port: https://github.com/RONIN0095/Ninjago-Vita

## Plan

1. Loader-Projekt aus dem soloader-boilerplate, drei Module laden, Imports
   auflösen, `JNI_OnLoad` aufrufen.
2. Unity-Start bis zur Grafik-Initialisierung; GL-Version 3.0 melden; loggen,
   welche GL-Funktionen Unity tatsächlich holt und aufruft.
3. Fehlende GLES3-Funktionen in vitaGL ergänzen (zuerst die wirklich genutzten),
   GLSL-ES-3.00-Shader von Unity übersetzen (UBOs → Uniforms).
4. Eingabe, Audio, Speicher, LiveArea.

Ehrliche Einschätzung: machbar, aber Wochenarbeit; das größte Risiko sind
GLES3/GLSL 3.00 in vitaGL und der Speicher.
