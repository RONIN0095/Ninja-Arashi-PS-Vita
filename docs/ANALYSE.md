# Ninja Arashi (1.9.3, Unity 6000.2.6f2, IL2CPP): APK-Analyse für den Vita-Loader

Ergänzt `NINJA_ARASHI_PORT_UEBERGABE.md`. Alles unten wurde direkt aus der APK
(`armeabi-v7a`, `assets/bin/Data`) gemessen.

## Kurzfassung: was sich gegenüber der Übergabe ändert

1. **Uniform Buffer sind fast kein Problem.** Von 633 GLES3-Shaderprogrammen
   nutzen nur VR/Stereo-Varianten und `Hidden/Internal-Loading` UBO-Blöcke.
   Alle normalen Shader (Sprites, UI, Partikel, TextMeshPro, Legacy, KriptoFX)
   verwenden **normale `uniform`-Variablen**. Damit reicht es,
   `GL_ACTIVE_UNIFORM_BLOCKS = 0` zu melden und die UBO-Funktionen als Stubs zu
   liefern. Eine UBO→Uniform-Umschreibung braucht es zunächst nicht.
2. **Die Shader sind sehr gleichförmig** (hlslcc-Ausgabe von Unity). Ein kleiner
   Vorübersetzer GLSL ES 3.00 → GLSL ES 1.00 in `glShaderSource`, vor vitaGL,
   reicht wahrscheinlich aus (Details unten).
3. **Kein Multithread-Rendering**: `boot.config` enthält
   `gfx-disable-mt-rendering=1`. Unity rendert also auf dem Hauptthread, was
   die GL-Sperre vereinfacht.
4. **Steuerung:** Das Spiel nutzt `CrossPlatformInputManager` (Standard Assets)
   mit `VirtualAxis`/`VirtualButton`, `SetAxis`, `SetButtonDown/Up`,
   `Horizontal`/`Vertical`. Über Hooks darauf lassen sich die Vita-Tasten
   sauber abbilden, ohne Touch-Emulation.
5. **Audio:** Unity versucht zuerst `libaaudio.so` (per `dlopen`/`dlsym`) und
   fällt auf `libOpenSLES.so` (`slCreateEngine`, `SL_IID_*`) zurück. Wenn
   `dlopen("libaaudio.so")` NULL liefert, läuft es über OpenSL ES, und
   `ng_sles.c` aus dem Ninjago-Port passt.

## Bibliotheken

| Modul | Größe im Speicher | Imports | Exporte | NEEDED |
|---|---|---|---|---|
| libmain.so | ~40 KB | 25 | `JNI_OnLoad` | liblog, libm, libdl, libc |
| libunity.so | ~15,7 MB (.bss 0,66 MB) | 350 | `JNI_OnLoad`, `JNI_OnUnload`, `UnityInitialize`, `UnitySendMessage`, `UnityShutdown`, Swappy | libmain, libandroid, liblog, libz, libEGL, libmediandk, libm, libdl, libc |
| libil2cpp.so | ~42 MB (.bss 0,5 MB) | 282 | 2420 (`il2cpp_*`-API) | liblog, libm, libdl, libc |

- Zusammen 431 verschiedene Imports. Kein `PT_TLS`, also keine echten
  Thread-Local-Segmente. `_ZTH15gDeferredAction` ist ein schwacher
  TLS-Wrapper-Import und kann auf NULL zeigen.
- Relocations nur `REL` (kein RELA); `.rel.dyn` in il2cpp ist 2,3 MB groß
  (~296.000 Einträge). Beim Laden vorher filtern und zählen, nicht pro Eintrag
  loggen.
- **libmain** macht praktisch nichts: Es holt sich die JavaVM, ruft
  `com/unity3d/player/NativeLoader.load(String)` auf und lädt per `dlopen`
  `libunity.so`. Der Loader kann libmain überspringen und `libunity.JNI_OnLoad`
  direkt aufrufen, oder libmain mitladen und `dlopen` auf die schon geladenen
  Module umleiten.
- **libil2cpp** wird von libunity per `dlopen` geladen und über `dlsym`
  (`il2cpp_init`, …) angesprochen. `dlsym` muss also Exporte der geladenen
  Module liefern.
- **Wichtig für C++-Exceptions:** il2cpp importiert `dl_unwind_find_exidx`
  und `dl_iterate_phdr`. IL2CPP wirft bei jeder Managed-Exception eine
  C++-Exception. Diese Funktionen müssen die `.ARM.exidx` des passenden Moduls
  liefern, sonst endet jede `try/catch` in C# mit `abort`.
- Weitere Sonderfälle in den Imports: `__sF` (bionic-stdio-Struktur),
  `__system_property_get/find/read`, `getauxval`, `process_vm_readv`/`ptrace`
  (Crash-Handler, als Stub mit Fehler zurückgeben), `sigaltstack`/`sigaction`
  (Unity installiert Signal-Handler, ignorieren), `pthread_getattr_np` +
  `pthread_attr_getstack` (Boehm-GC von il2cpp braucht die **echten**
  Stackgrenzen jedes Threads), `sem_*`, `pthread_rwlock_*`, Locale-`*_l`-
  Funktionen, `getaddrinfo` & Sockets (Analytics, als Fehler zurückgeben).
- NDK-Funktionen: `ANativeWindow_*`, `ALooper_*`, `ASensor*`,
  `AChoreographer_*` direkt importiert. AAudio, AAsset, AMediaCodec usw. holt
  Unity per `dlsym`.

## OpenGL ES

- Unity prüft die Version selbst (`"[EGL] Checking ES 3.%d support..."`,
  `"OpenGL ES 3.0 is required..."`). vitaGL meldet heute
  `"OpenGL ES 2.0 VitaGL"` (`source/get_info.c:193`). Das muss im Loader oder
  per Patch auf `"OpenGL ES 3.0 VitaGL"` gesetzt werden, dazu
  `GL_MAJOR_VERSION=3`, `GL_MINOR_VERSION=0`, und `EGL_CONTEXT_CLIENT_VERSION 3`
  muss bei `eglCreateContext` angenommen werden.
- 283 `gl*`/`egl*`-Namen in libunity, davon 151 in vitaGL vorhanden
  (aktueller `master`, e119b3e) und **132 fehlen** (Liste unten).
- Unity nutzt nur `#version 300 es`-Shader (Plattform 9 = GLES3x in allen
  Shader-Blobs). Es gibt keine Vulkan-Shader, also ist GLES3 der einzige
  Grafikpfad.

### Fehlende Funktionen nach Priorität

**Echt umsetzen** (Unity nutzt sie auf ES 3.0 sicher):
`glTexStorage2D`, `glTexImage3D`/`glTexSubImage3D`/`glTexStorage3D` (zur Not
Fehler, da 2D-Spiel), `glCompressedTexSubImage2D`, `glDrawBuffers`,
`glClearBufferfv/fi/uiv`, `glInvalidateFramebuffer` (No-op),
`glRenderbufferStorageMultisample` (auf normales Renderbuffer abbilden),
`glFramebufferTextureLayer`, `glVertexAttribIPointer`, `glUniform1-4uiv`,
`glFenceSync`/`glClientWaitSync`/`glDeleteSync` (über `glFinish` oder
vitaGL-Fences), `glCopyBufferSubData`, `glGetInternalformativ`,
`glGetShaderPrecisionFormat`, `glGetTexLevelParameteriv/fv`,
`glGetTexParameteriv`, `glGetRenderbufferParameteriv`, `glGetUniformiv`,
`glDetachShader`, `glValidateProgram`, `glIsVertexArray`,
`glGetActiveUniformBlockiv/Name`, `glGetUniformIndices`,
`glGetActiveUniformsiv` (bei 0 Blöcken trivial), `glColorMaski`,
`glBlendEquationi`/`glBlendFuncSeparatei`/`glBlendEquationSeparatei` (auf
Index 0 abbilden), `glDrawElementsInstancedBaseVertex*`/`glDrawElementsBaseVertex*`
(Indizes verschieben oder Stub), EGL: `eglCreatePbufferSurface`,
`eglGetCurrentSurface`, `eglSurfaceAttrib`, `eglCreateSyncKHR`/
`eglClientWaitSyncKHR`/`eglDestroySyncKHR`.

**Stub reicht** (ES 3.1+/Erweiterungen, Unity fragt sie nur ab):
Debug/Labels (`glDebugMessage*`, `glObjectLabel*`, `glPush/PopDebugGroup*`,
`glPush/PopGroupMarkerEXT`, `glLabelObjectEXT`, `glGetObjectLabel*`), Compute
(`glDispatchCompute*`, `glMemoryBarrier`, `glBindImageTexture`),
`glGetProgramInterfaceiv`/`glGetProgramResource*`, Indirect-Draws,
Tessellation (`glPatchParameteri*`), Multiview (`*MultiviewOVR`), Queries
(`glGen/Delete/Begin/EndQueryEXT`, `glGetQueryiv`, `glGetQueryObjectui64v*`),
`glTexBuffer*`, `glTexStorage*Multisample`, `glTexImage2DMultisample`,
`glFramebufferTexture2DMultisample*`, `glResolveMultisampleFramebufferAPPLE`,
`glTexPageCommitment*`, `glTextureView*`, `glCopyImageSubData*`,
`glProgramParameteri`, `glProgramUniform*`, `glBlendBarrier*`,
`glFramebufferTexture3D`, `eglGet*Frame*ANDROID`.

Damit Unity diese Pfade gar nicht erst nimmt, sollte `GL_EXTENSIONS` nur das
Nötigste melden (z. B. `GL_EXT_shader_texture_lod` weglassen, kein
`GL_KHR_debug`, kein `GL_EXT_disjoint_timer_query`).

## Shader

- 75 Shader-Assets, 633 GLES3-Programme (Vertex + Fragment). Es ist die
  Built-in-Pipeline: `Sprites/Default`, `Sprites/Mask`, `UI/Default`,
  `UI/Default Font`, `TextMeshPro/Mobile/Distance Field`, `TextMeshPro/Sprite`,
  `Legacy Shaders/*`, `Mobile/Particles/*`, `Unlit/*`, KriptoFX-Effekte,
  `Effects/*` sowie Unitys `Hidden/*`-Shader (Blit, GUI, UIR, Deferred, …).
- Aufbau (Beispiel `Sprites/Default`):

  ```glsl
  #version 300 es
  #define UNITY_SUPPORTS_UNIFORM_LOCATION 1
  #define UNITY_LOCATION(x) layout(location = x)
  uniform vec4 hlslcc_mtx4x4unity_ObjectToWorld[4];
  in highp vec4 in_POSITION0;
  out mediump vec4 vs_COLOR0;
  ...
  UNITY_LOCATION(0) uniform mediump sampler2D _MainTex;
  layout(location = 0) out mediump vec4 SV_Target0;
  ... texture(_MainTex, vs_TEXCOORD0.xy) ...
  ```

- Benutzte Funktionen (Häufigkeit): `texture` 1479, `inversesqrt` 768,
  `clamp` 654, `textureLod` 154, `texelFetch` 43, `roundEven` 40, `lessThan`
  23, `dFdx/dFdy` 34, `textureSize` 12, `equal` 6. Dazu `sampler2DShadow` (48,
  nur Schatten/Deferred), `samplerCube` (57), `uvec`/`ivec` (wenige),
  `gl_FragDepth` (6), `gl_InstanceID` (5).
- Vorschlag für den Vorübersetzer (vor dem GLSL→CG-Schritt von vitaGL):
  - `#version 300 es` entfernen, `UNITY_SUPPORTS_UNIFORM_LOCATION` auf 0
    setzen (entfernt `layout(location=x)` an Samplern).
  - Vertex: `in` → `attribute`, `out` → `varying`. Fragment: `in` →
    `varying`, `layout(location=0) out … SV_Target0;` entfernen und
    `#define SV_Target0 gl_FragColor`.
  - `texture`, `textureLod`, `texelFetch`, `textureSize`, `roundEven` als
    **überladene CG-Hilfsfunktionen** im Header bereitstellen (`tex2D`,
    `texCUBE`, `tex2Dlod`, `tex2Dfetch`, `round`). CG erlaubt Überladung nach
    Sampler-Typ, damit muss man den Typ nicht selbst auflösen.
  - Matrizen kommen als `vec4 hlslcc_mtx4x4…[4]`-Arrays, die vitaGL schon
    kann. Mit `SAFE_UNIFORMS=1` bauen.
  - Fehlerhafte Programme protokollieren und im Shader-Cache-Paket merken
    (wie bei Ninjago).
- Die extrahierten Shader-Quelltexte lassen sich als Testkorpus für den
  Übersetzer nutzen (am PC gegen `vitashark`/einen CG-Parser laufen lassen,
  bevor es auf die Vita geht).

## Java/JNI

- Native Methoden, die Unity in `UnityPlayer` registriert (Auszug):
  `nativeRender`, `nativeResume`, `nativePause`, `nativeRecreateGfxState`,
  `nativeSendSurfaceChangedEvent`, `nativeFocusChanged`, `nativeInjectEvent`,
  `nativeDone`, `nativeUnitySendMessage`, `nativeConfigurationChanged`,
  `nativeOrientationChanged`, `nativeSetLaunchURL`, `nativeProxyInvoke`,
  `nativeProxyFinalize`, `nativeMuteMasterAudio`, `nativeIsAutorotationOn`,
  `nativeGetNoWindowMode`, `nativeSoftInput*`, `nativeSetInput*`,
  `nativeUnityPlayerSetRunning`, `nativeFrameReady`, `nativeSurfaceTextureReady`,
  `nativeMemoryUsageChanged`, `nativeApplicationUnload`.
  Die Tabelle aus `RegisterNatives` im Loader abfangen und speichern; daraus
  ruft der Loader später `nativeRender` usw. auf.
- Unity spricht 158 Java-Klassen an, unter anderem
  `UnityPlayerForActivityOrService`, `ReflectionHelper` (Java-Proxies, also
  `nativeProxyInvoke`), `AudioVolumeHandler`, `OrientationLockListener`,
  `PlayAssetDeliveryUnityWrapper`, `Camera2Wrapper`, `GoogleARCoreApi`, dazu
  Android-Klassen wie `SharedPreferences`, `DisplayMetrics`, `Build`,
  `PackageManager`, `AssetManager`, `MediaCodec`, `Vibrator`. In FalsoJNI mit
  Logging für unbekannte Klassen und Methoden beginnen und schrittweise
  ausfüllen.
- Werbung/Store/Analytics laufen über Java (`Unity.LevelPlay` = ironSource,
  `Unity.Purchasing`, `Unity.Services.*`, Analytics). Unbekannte
  `AndroidJavaClass`-Aufrufe müssen sauber `null`/0 liefern, ohne
  Java-Exception, sonst wirft C#.

## IL2CPP-Metadaten

- `global-metadata.dat`: Magic `0xFAB11BAF`, **Version 31**. Das Original
  Il2CppDumper kann v31 nicht. **Cpp2IL** (aktueller Stand) oder ein
  Il2CppDumper-Fork mit v31-Unterstützung verwenden. Hauptassemblies:
  `Assembly-CSharp.dll`, `Assembly-CSharp-firstpass.dll` (dort liegt
  `CrossPlatformInputManager`).

## Daten und Speicher

- APK entpackt 419 MB, davon 260 MB `assets/bin/Data` (Szenen `level*.split*`,
  `sharedassets*`, `.resource` für Audio/Video, Addressables-Hashdateien). Unity
  liest die APK als ZIP, daher sollte die APK unverändert auf `ux0:` liegen
  (ohne arm64-Bibliotheken spart man ~70 MB).
- Speicherbedarf Code+Daten der Module: ~57 MB. Mit dem Unity-Heap ist der
  erweiterte Speichermodus nötig (`ATTRIBUTE2=12`, ~365 MB für die App),
  `newlib`-Heap groß wählen, Texturen über vitaGL im CDRAM/phycont.

## Texturen und Paket (nachträglich ermittelt)

- Paketname `com.blackpanther.ninjaarashi`, Activity
  `com.unity3d.player.UnityPlayerActivity` (klassische Activity, kein
  GameActivity).
- Texturformate (Unity `TextureFormat`, Anzahl, komprimierte Größe):

  | Format | Anzahl | Größe |
  |---|---|---|
  | 47 ETC2_RGBA8 | 141 | 122 MB |
  | 4 RGBA32 | 50 | 13 MB |
  | 13 RGBA4444 | 32 | 0,1 MB |
  | 34 ETC_RGB4 | 14 | 0,8 MB |
  | 31 PVRTC_RGBA2 | 4 | 2 MB |
  | 1 Alpha8 / 3 RGB24 / 12 DXT5 | 10 | 2 MB |

  Die großen Atlanten (2048×2048) sind ETC2. Die Vita-GPU kann ETC2 nicht
  direkt. vitaGL dekodiert ETC2 und komprimiert es mit
  `glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST)` wieder zu DXT5 (gleiche
  8 bit/Pixel). Ohne das wären es ~250 MB RGBA8. Kosten: längere Ladezeit.
  PVRTC und DXT5 kann die Vita direkt.
- Dazu 669 Sprites, 152 AudioClips, 35 Meshes, 8 Fonts.

## Nächste Schritte

Siehe `docs/NOTES.md` (Stand des Loaders, offene Risiken).
