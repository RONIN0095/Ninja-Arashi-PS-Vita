/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  main.c
 * @brief Ninja Arashi (Unity 6, IL2CPP) loader for the PS Vita.
 *
 * Data layout on the Vita (DATA_PATH = ux0:data/ninjaarashi/):
 *   base.apk               the game's APK (Unity reads its assets from it)
 *   lib/libunity.so        lib/armeabi-v7a/libunity.so from the APK
 *   lib/libil2cpp.so       lib/armeabi-v7a/libil2cpp.so from the APK
 *   log.txt                written by the loader
 */

#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/touch.h>

#include <vitaGL.h>
#include <falso_jni/FalsoJNI.h>

#include "jni_objects.h"
#include "na_egl.h"
#include "na_unity.h"
#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/init.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "reimpl/controls.h"

// Unity's heap, il2cpp's GC heap and the libraries' data all come from newlib
int _newlib_heap_size_user = 300 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

// The main thread runs Unity's player loop: give it a big stack
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

int main() {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(DATA_PATH, 0777);
    sceIoMkdir(DATA_PATH "files", 0777);
    sceIoMkdir(DATA_PATH "cache", 0777);
    sceIoMkdir(DATA_PATH "external", 0777);
    sceIoMkdir("ux0:data/shader_cache", 0777);
    l_info("Ninja Arashi loader starting.");

    if (!file_exists(DATA_PATH "base.apk"))
        fatal_error("Missing game file:\n%sbase.apk\n\nCopy the game's APK there (renamed to base.apk).", DATA_PATH);

    soloader_init_all();
    jni_objects_install();
    l_info("Loader initialized.");

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    gl_init();
    l_info("vitaGL initialized.");
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    vglSwapBuffers(GL_FALSE);

    na_unity_start();

    int frame = 0;
    while (1) {
        int swaps_before = na_egl_swap_count;
        int r = na_unity_frame();
        if (frame < 5 || frame % 600 == 0)
            l_info("Frame %d (nativeRender %d, swaps %d)", frame, r, na_egl_swap_count);
        // Unity skips the swap while it has nothing to show: keep the display alive
        if (na_egl_swap_count == swaps_before)
            sceKernelDelayThread(1000);
        frame++;
    }

    sceKernelExitDeleteThread(0);
}

// Input is not wired up yet (see docs/NOTES.md); the boilerplate thread is unused.
void controls_handler_key(int32_t keycode, ControlsAction action) {}
void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {}
void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {}
