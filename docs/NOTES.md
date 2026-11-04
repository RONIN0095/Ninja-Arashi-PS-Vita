# Stand des Loaders und offene Punkte

Der Loader ist noch **nicht auf einer Vita getestet**. Er baut per GitHub
Actions zu einer VPK. Alles unten beschreibt, was der Code tut und was beim
ersten Start zu erwarten ist.

## Was der Loader macht

1. `main.c`: Ordner anlegen, prüfen ob `base.apk` da ist.
2. `utils/init.c`: Takt hoch, kubridge prüfen, dann `na_modules_load()`.
3. `na_modules.c`: `libil2cpp.so` bei `0x98100000` und `libunity.so` bei
   `0x9AA00000` laden, relozieren und gegen die Importtabellen linken
   (`na_dynlib.c` vor `dynlib.c`). libmain.so wird nicht geladen, es ruft
   auf Android nur `NativeLoader.load()` auf.
   `dlopen("libil2cpp.so")` gibt das geladene Modul zurück und führt beim
   ersten Mal dessen Konstruktoren aus (wie bionic).
   `dl_unwind_find_exidx` und `dl_iterate_phdr` liefern echte Werte (für
   C++-Exceptions von IL2CPP bzw. die GC-Wurzeln).
4. vitaGL-Init (960×544, kein MSAA, ETC2→DXT5).
5. `na_unity.c`: Konstruktoren von libunity, `JNI_OnLoad`. Die per
   `RegisterNatives` gemeldeten Methoden werden mit Signatur ins Log
   geschrieben. Danach `initJni`, `nativeRecreateGfxState`,
   `nativeSendSurfaceChangedEvent`, `nativeUnityPlayerSetRunning`,
   `nativeResume`, `nativeFocusChanged`. Jede Methode wird nur aufgerufen,
   wenn ihre Signatur passt; sonst steht eine Zeile im Log.
6. Schleife: Choreographer-Callbacks, `nativeRender()`.

## Log lesen

`ux0:data/ninjaarashi/log.txt`. Wichtige Stellen:

- `[natives] ...`: Alle nativen UnityPlayer-Methoden mit Signatur. Wenn
  `[unity] X has signature ...: not called` erscheint, muss der Aufruf in
  `na_unity.c` an die Signatur angepasst werden.
- `[JAVA] default answer for Klasse.methode(sig)`: Java-Methoden, die noch
  nicht nachgebaut sind (in `na_java.c` ergänzen, wenn das Ergebnis zählt).
- `[GL] name: loader|vitaGL|MISSING|hidden`: jede GL-Funktion, die Unity
  holt. `MISSING` heißt: weder vitaGL noch Loader haben sie.
- `GL stub called: ...`: Unity ruft eine Funktion auf, die nur ein Stub ist.
- `Unresolved import: ...` (von so_util): Import ohne Tabelleneintrag.
  `tools/check_imports.py` prüft das schon beim Bauen.

## Offene Risiken (nach Gewicht)

1. **Garbage Collector und Signale.** IL2CPP nutzt Boehm GC. Für eine
   Sammlung hält er alle registrierten Threads per Signal an
   (`pthread_kill` + `sigsuspend`). Die Vita hat keine Signale; der Loader
   meldet `pthread_kill` als erfolgreich, aber der Thread hält nicht an.
   Mögliche Folge: Hänger (GC wartet auf Bestätigung) oder falsch
   freigegebene Objekte. Lösungsideen: `GC_stop_world`/`GC_start_world` in
   libil2cpp per Hook ersetzen (Threads über eigene Sperren an sicheren
   Punkten parken), oder die GC-Threads-Liste klein halten
   (`il2cpp_gc_disable` während des Ladens). Wird sich beim ersten Start
   zeigen.
2. **Shader.** Der Übersetzer (`na_shaders.c`) ist am PC gegen alle 1236
   Shader-Stufen des Spiels getestet (Textebene). Ob vitaGLs GLSL→CG-Schritt
   alles annimmt (Überladungen von `texture`, `int`-Varyings, `uint`), zeigt
   erst die Vita. Nicht abgedeckt: `texelFetch`, `textureSize`,
   `sampler2DShadow` (nur in Schatten-/Deferred-/VR-Shadern, die ein 2D-Spiel
   kaum nutzt).
3. **R8/Alpha8-Texturen.** ES3-Unity speichert Alpha8 als `GL_R8` und holt
   Alpha per Swizzle (`GL_TEXTURE_SWIZZLE_A`). vitaGL kennt keinen Swizzle;
   `GL_R8` wird hier zu `GL_LUMINANCE`. Betroffen wäre z. B. TextMeshPro-Text
   (SDF-Atlas). Lösung bei Bedarf: Swizzle-Parameter abfangen und das
   SceGxm-Texturformat überschreiben (`vglOverrideTexFormat`).
4. **Speicher.** Code+Daten der Bibliotheken ~58 MB, newlib-Heap 300 MB
   (`_newlib_heap_size_user`), erweiterter Modus `ATTRIBUTE2=12`. Ob das mit
   Unity-Heap und Texturen reicht, zeigt der erste Start.
5. **Ladezeit.** ETC2→DXT5 kostet pro 2048er-Atlas etwa 1–3 s CPU-Zeit.
   Später: Ergebnis cachen.
6. **Mehrere GL-Kontexte.** `gfx-disable-mt-rendering=1` heißt, Unity
   rendert auf einem Thread. Falls Unity trotzdem einen zweiten Kontext auf
   einem anderen Thread benutzt (Log: `eglMakeCurrent ... on thread`), muss
   die GL-Sperre aus dem Ninjago-Port (`ng_gllock.c`) übernommen werden.

## Noch nicht gemacht

- Eingabe: Plan ist, `CrossPlatformInputManager` (Assembly-CSharp-firstpass)
  per Hook zu bedienen. Dafür mit Cpp2IL (Metadaten v31) die Adressen von
  `SetAxis`, `SetButtonDown`, `SetButtonUp` bestimmen. Alternativ Touch über
  `nativeInjectEvent` mit nachgebauten `MotionEvent`-Objekten.
- Audio: `na_sles.c` (aus dem Ninjago-Port) ist eingebunden. Unity probiert
  zuerst AAudio (`dlopen("libaaudio.so")` liefert NULL) und dann OpenSL ES.
  Ob FMOD mit diesem OpenSL-Ersatz auskommt (Formate, Puffergrößen), ist
  ungetestet.
- LiveArea: nur Platzhalterbilder (`tools/make_livearea.py`).
