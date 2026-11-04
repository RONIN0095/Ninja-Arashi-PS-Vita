/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  na_java.c
 * @brief Stand-ins for the Java side of a Unity 6 player.
 *
 * Every Java method libunity.so calls ends up in jo_java_call() (see
 * jni_objects.c). Known methods are answered here: the Context (package
 * name, APK path, directories), Build, Locale, DisplayMetrics,
 * SharedPreferences (PlayerPrefs) and a few services. Everything else is
 * logged once and answered with a neutral value: 0/false, "" for strings,
 * and a fresh object of the declared return class for objects, so that
 * call chains like getResources().getConfiguration().locale do not crash.
 */

#include "jni_objects.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <psp2/kernel/processmgr.h>

#include "na_android.h"
#include "utils/logger.h"

#define APK_PATH DATA_PATH "base.apk"

static JNIEnv *env(void) { return jo_env(); }

static jstring jstr(const char *s) {
    return (*env())->NewStringUTF(env(), s);
}

static int is(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static jobject file_object(const char *path) {
    jobject f = jo_new_object("java/io/File");
    jo_set_string(f, "path", path);
    return f;
}

/*
 * SharedPreferences (PlayerPrefs): one key=value text file per preferences name
 */
#define MAX_PREFS 512

typedef struct {
    char key[128];
    char type; // 's', 'i', 'l', 'f', 'b'
    char *value;
} pref;

static pref prefs[MAX_PREFS];
static int n_prefs = 0;
static int prefs_loaded = 0;

#define PREFS_FILE DATA_PATH "prefs.txt"

static void prefs_load(void) {
    if (prefs_loaded) return;
    prefs_loaded = 1;
    FILE *f = fopen(PREFS_FILE, "r");
    if (!f) return;
    char line[4096];
    while (fgets(line, sizeof(line), f) && n_prefs < MAX_PREFS) {
        line[strcspn(line, "\r\n")] = 0;
        // format: <type> <key>\t<value>
        if (strlen(line) < 3 || line[1] != ' ') continue;
        char *tab = strchr(line + 2, '\t');
        if (!tab) continue;
        *tab = 0;
        pref *p = &prefs[n_prefs++];
        p->type = line[0];
        strncpy(p->key, line + 2, sizeof(p->key) - 1);
        p->value = strdup(tab + 1);
    }
    fclose(f);
    l_info("[prefs] %d values loaded", n_prefs);
}

static void prefs_save(void) {
    FILE *f = fopen(PREFS_FILE ".tmp", "w");
    if (!f) return;
    for (int i = 0; i < n_prefs; i++)
        if (prefs[i].value)
            fprintf(f, "%c %s\t%s\n", prefs[i].type, prefs[i].key, prefs[i].value);
    fclose(f);
    remove(PREFS_FILE);
    rename(PREFS_FILE ".tmp", PREFS_FILE);
}

static pref *pref_find(const char *key) {
    prefs_load();
    for (int i = 0; i < n_prefs; i++)
        if (prefs[i].value && strcmp(prefs[i].key, key) == 0) return &prefs[i];
    return NULL;
}

static void pref_set(const char *key, char type, const char *value) {
    pref *p = pref_find(key);
    if (!p) {
        for (int i = 0; i < n_prefs; i++)
            if (!prefs[i].value) { p = &prefs[i]; break; }
        if (!p && n_prefs < MAX_PREFS) p = &prefs[n_prefs++];
        if (!p) return;
        strncpy(p->key, key, sizeof(p->key) - 1);
    } else {
        free(p->value);
    }
    p->type = type;
    p->value = strdup(value);
}

static void pref_remove(const char *key) {
    pref *p = pref_find(key);
    if (p) {
        free(p->value);
        p->value = NULL;
    }
}

static jvalue prefs_call(const char *name, const jvalue *args, int argc) {
    jvalue r;
    r.j = 0;
    char *key = argc >= 1 ? jo_jstring_to_utf8((jstring)args[0].l) : NULL;
    char buf[64];

    if (is(name, "getString")) {
        pref *p = key ? pref_find(key) : NULL;
        r.l = p ? jstr(p->value) : (argc >= 2 ? args[1].l : NULL);
    } else if (is(name, "getInt")) {
        pref *p = key ? pref_find(key) : NULL;
        r.i = p ? atoi(p->value) : args[1].i;
    } else if (is(name, "getLong")) {
        pref *p = key ? pref_find(key) : NULL;
        r.j = p ? atoll(p->value) : args[1].j;
    } else if (is(name, "getFloat")) {
        pref *p = key ? pref_find(key) : NULL;
        r.f = p ? (float)atof(p->value) : args[1].f;
    } else if (is(name, "getBoolean")) {
        pref *p = key ? pref_find(key) : NULL;
        r.z = p ? (jboolean)atoi(p->value) : args[1].z;
    } else if (is(name, "contains")) {
        r.z = key && pref_find(key) ? JNI_TRUE : JNI_FALSE;
    } else if (is(name, "edit")) {
        r.l = jo_new_object("android/content/SharedPreferences$Editor");
    } else if (is(name, "putString")) {
        char *v = jo_jstring_to_utf8((jstring)args[1].l);
        if (key) pref_set(key, 's', v ? v : "");
        free(v);
    } else if (is(name, "putInt")) {
        snprintf(buf, sizeof(buf), "%d", args[1].i);
        if (key) pref_set(key, 'i', buf);
    } else if (is(name, "putLong")) {
        snprintf(buf, sizeof(buf), "%lld", (long long)args[1].j);
        if (key) pref_set(key, 'l', buf);
    } else if (is(name, "putFloat")) {
        snprintf(buf, sizeof(buf), "%.9g", args[1].f);
        if (key) pref_set(key, 'f', buf);
    } else if (is(name, "putBoolean")) {
        if (key) pref_set(key, 'b', args[1].z ? "1" : "0");
    } else if (is(name, "remove")) {
        if (key) pref_remove(key);
    } else if (is(name, "clear")) {
        prefs_load();
        for (int i = 0; i < n_prefs; i++) {
            free(prefs[i].value);
            prefs[i].value = NULL;
        }
    } else if (is(name, "apply") || is(name, "commit")) {
        prefs_save();
        r.z = JNI_TRUE;
    }
    // put*/remove/clear return the editor itself
    if (strncmp(name, "put", 3) == 0 || is(name, "remove") || is(name, "clear"))
        r.l = jo_new_object("android/content/SharedPreferences$Editor");
    free(key);
    return r;
}

/*
 * Static fields
 */
jvalue jo_java_static_field(const char *cls, const char *name, const char *sig) {
    jvalue r;
    r.j = 0;
    if (is(cls, "android/os/Build$VERSION")) {
        if (is(name, "SDK_INT")) r.i = 28;
        else if (is(name, "RELEASE")) r.l = jstr("9");
        else if (is(name, "INCREMENTAL")) r.l = jstr("1");
        else if (is(name, "CODENAME")) r.l = jstr("REL");
        else if (is(name, "PREVIEW_SDK_INT")) r.i = 0;
    } else if (is(cls, "android/os/Build")) {
        if (is(name, "MODEL")) r.l = jstr("PS Vita");
        else if (is(name, "MANUFACTURER") || is(name, "BRAND")) r.l = jstr("Sony");
        else if (is(name, "DEVICE") || is(name, "PRODUCT") || is(name, "HARDWARE") || is(name, "BOARD")) r.l = jstr("vita");
        else if (is(name, "FINGERPRINT")) r.l = jstr("Sony/vita/vita:9/PSVITA/1:user/release-keys");
        else if (is(name, "TAGS")) r.l = jstr("release-keys");
        else if (is(name, "TYPE")) r.l = jstr("user");
        else if (is(name, "CPU_ABI")) r.l = jstr("armeabi-v7a");
        else if (sig[0] == 'L') r.l = jstr("");
    } else if (is(name, "SDK_INT")) {
        r.i = 28;
    } else if (sig[0] == 'L' && is(sig, "Ljava/lang/String;")) {
        // Constants like Context.WINDOW_SERVICE: the name in lower case is close enough
        char lower[64];
        size_t n = strlen(name);
        if (n >= sizeof(lower)) n = sizeof(lower) - 1;
        for (size_t i = 0; i < n; i++) lower[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i];
        lower[n] = 0;
        r.l = jstr(lower);
    } else if (sig[0] == 'L') {
        char cname[128];
        size_t n = strlen(sig);
        if (n > 2 && n - 2 < sizeof(cname)) {
            memcpy(cname, sig + 1, n - 2);
            cname[n - 2] = 0;
            r.l = jo_new_object(cname);
        }
    }
    l_debug("[JAVA] static %s.%s (%s)", cls, name, sig);
    return r;
}

/*
 * Methods
 */
static void log_once(const char *cls, const char *name, const char *sig) {
    static char seen[256][96];
    static int count = 0;
    char key[96];
    snprintf(key, sizeof(key), "%s.%s", cls, name);
    for (int i = 0; i < count; i++)
        if (strcmp(seen[i], key) == 0) return;
    if (count < 256) strcpy(seen[count++], key);
    l_info("[JAVA] default answer for %s.%s%s", cls, name, sig);
}

// Neutral value for the method's return type
static jvalue default_return(const char *sig) {
    jvalue r;
    r.j = 0;
    const char *ret = strchr(sig, ')');
    if (!ret) return r;
    ret++;
    if (is(ret, "Ljava/lang/String;")) {
        r.l = jstr("");
    } else if (is(ret, "Ljava/lang/Class;")) {
        // a jclass, not an object
        r.l = (*env())->FindClass(env(), "java/lang/Object");
    } else if (ret[0] == 'L') {
        char cname[128];
        size_t n = strlen(ret);
        if (n > 2 && n - 2 < sizeof(cname)) {
            memcpy(cname, ret + 1, n - 2);
            cname[n - 2] = 0;
            r.l = jo_new_object(cname);
        }
    }
    return r;
}

static const char *self_class(jobject self) {
    return self && jo_is_object(self) ? jo_class_name(self) : "";
}

jvalue jo_java_call(const char *cls, const char *name, const char *sig,
                    jobject self, const jvalue *args, int argc) {
    jvalue r;
    r.j = 0;
    const char *sc = self_class(self);

    // SharedPreferences and its Editor
    if (strstr(cls, "SharedPreferences") || strstr(sc, "SharedPreferences"))
        return prefs_call(name, args, argc);

    // Constructors
    if (is(name, "<init>")) {
        if (strstr(cls, "java/io/File") && argc >= 1) {
            // File(String) or File(File parent, String child)
            char *a0 = NULL, *a1 = NULL;
            if (strncmp(sig, "(Ljava/lang/String;", 19) == 0) a0 = jo_jstring_to_utf8((jstring)args[0].l);
            else if (strncmp(sig, "(Ljava/io/File;", 15) == 0 && args[0].l) a0 = jo_get_string(args[0].l, "path");
            if (argc >= 2) a1 = jo_jstring_to_utf8((jstring)args[1].l);
            char path[512];
            snprintf(path, sizeof(path), "%s%s%s", a0 ? a0 : "", a1 ? "/" : "", a1 ? a1 : "");
            jo_set_string(self, "path", path);
            free(a0);
            free(a1);
        }
        return r;
    }

    // Context / Activity
    if (is(name, "getPackageName")) {
        r.l = jstr(PACKAGE_NAME);
    } else if (is(name, "getApplicationContext") || is(name, "getBaseContext") || is(name, "getApplication")) {
        r.l = jo_new_object("android/app/Activity");
    } else if (is(name, "getApplicationInfo")) {
        jobject ai = jo_new_object("android/content/pm/ApplicationInfo");
        jo_set_string(ai, "sourceDir", APK_PATH);
        jo_set_string(ai, "publicSourceDir", APK_PATH);
        jo_set_string(ai, "nativeLibraryDir", DATA_PATH "lib");
        jo_set_string(ai, "dataDir", DATA_PATH);
        jo_set_string(ai, "packageName", PACKAGE_NAME);
        jo_set_int(ai, "targetSdkVersion", 34);
        jo_set_int(ai, "minSdkVersion", 23);
        jo_set_int(ai, "flags", 0);
        r.l = ai;
    } else if (is(name, "getPackageCodePath") || is(name, "getPackageResourcePath")) {
        r.l = jstr(APK_PATH);
    } else if (is(name, "getFilesDir") || is(name, "getNoBackupFilesDir")) {
        r.l = file_object(DATA_PATH "files");
    } else if (is(name, "getCacheDir") || is(name, "getCodeCacheDir") || is(name, "getExternalCacheDir")) {
        r.l = file_object(DATA_PATH "cache");
    } else if (is(name, "getExternalFilesDir") || is(name, "getObbDir") || is(name, "getExternalStorageDirectory")) {
        r.l = file_object(DATA_PATH "external");
    } else if (is(name, "getDataDir")) {
        r.l = file_object(DATA_PATH);
    } else if (is(name, "getExternalFilesDirs") || is(name, "getObbDirs")) {
        r.l = NULL;
    } else if (is(name, "getExternalStorageState")) {
        r.l = jstr("mounted");
    } else if (is(name, "getSharedPreferences") || is(name, "getDefaultSharedPreferences") || is(name, "getPreferences")) {
        r.l = jo_new_object("android/content/SharedPreferences");

    // java.io.File
    } else if (is(sc, "java/io/File") && (is(name, "getPath") || is(name, "getAbsolutePath") ||
                                          is(name, "getCanonicalPath") || is(name, "toString"))) {
        char *p = jo_get_string(self, "path");
        r.l = jstr(p ? p : "");
        free(p);
    } else if (is(sc, "java/io/File") && is(name, "length")) {
        char *p = jo_get_string(self, "path");
        struct stat st;
        r.j = (p && stat(p, &st) == 0) ? (jlong)st.st_size : 0;
        free(p);
    } else if (is(sc, "java/io/File") && is(name, "exists")) {
        char *p = jo_get_string(self, "path");
        struct stat st;
        r.z = (p && stat(p, &st) == 0) ? JNI_TRUE : JNI_FALSE;
        free(p);
    } else if (is(sc, "java/io/File") && (is(name, "isDirectory") || is(name, "mkdirs") || is(name, "mkdir") ||
                                          is(name, "canWrite") || is(name, "canRead"))) {
        r.z = JNI_TRUE;

    // PackageManager
    } else if (is(name, "getPackageManager")) {
        r.l = jo_new_object("android/content/pm/PackageManager");
    } else if (is(name, "getPackageInfo")) {
        jobject pi = jo_new_object("android/content/pm/PackageInfo");
        jo_set_string(pi, "packageName", PACKAGE_NAME);
        jo_set_string(pi, "versionName", "1.9.3");
        jo_set_int(pi, "versionCode", 193);
        jo_set_long(pi, "firstInstallTime", 1700000000000LL);
        jo_set_long(pi, "lastUpdateTime", 1700000000000LL);
        r.l = pi;
    } else if (is(name, "getInstallerPackageName")) {
        r.l = jstr("com.android.vending");

    // Locale
    } else if (is(name, "getLanguage")) {
        r.l = jstr("en");
    } else if (is(name, "getCountry")) {
        r.l = jstr("US");
    } else if (is(name, "toLanguageTag")) {
        r.l = jstr("en-US");
    } else if (is(sc, "java/util/Locale") && is(name, "toString")) {
        r.l = jstr("en_US");

    // Display
    } else if (is(name, "getMetrics") || is(name, "getRealMetrics") || is(name, "getDisplayMetrics")) {
        jobject dm = (is(name, "getDisplayMetrics") || argc < 1) ? jo_new_object("android/util/DisplayMetrics") : args[0].l;
        if (dm) {
            jo_set_int(dm, "widthPixels", SCREEN_W);
            jo_set_int(dm, "heightPixels", SCREEN_H);
            jo_set_float(dm, "density", 1.375f);
            jo_set_int(dm, "densityDpi", 220);
            jo_set_float(dm, "scaledDensity", 1.375f);
            jo_set_float(dm, "xdpi", 220.0f);
            jo_set_float(dm, "ydpi", 220.0f);
        }
        r.l = dm;
    } else if (is(name, "getRefreshRate")) {
        r.f = 60.0f;
    } else if (is(name, "getRotation") || is(name, "getOrientation")) {
        r.i = 0;
    } else if (is(name, "getWidth") && strstr(sc, "Display")) {
        r.i = SCREEN_W;
    } else if (is(name, "getHeight") && strstr(sc, "Display")) {
        r.i = SCREEN_H;

    // Device capabilities
    } else if (is(name, "hasSystemFeature")) {
        r.z = JNI_FALSE;
    } else if (is(name, "checkCallingOrSelfPermission") || is(name, "checkSelfPermission") ||
               is(name, "checkPermission")) {
        r.i = 0; // PERMISSION_GRANTED
    } else if (is(name, "isInMultiWindowMode") || is(name, "isInPictureInPictureMode")) {
        r.z = JNI_FALSE;
    } else if (is(name, "hasWindowFocus")) {
        r.z = JNI_TRUE;

    // Lifecycle
    } else if (is(name, "finish") || is(name, "exit") || is(name, "quit") || is(name, "killProcess")) {
        l_info("[JAVA] %s.%s: the game asked to quit", cls, name);
        sceKernelExitProcess(0);

    // java.lang
    } else if (is(name, "hashCode")) {
        r.i = (jint)(uintptr_t)self;
    } else if (is(name, "equals")) {
        r.z = (argc >= 1 && args[0].l == self) ? JNI_TRUE : JNI_FALSE;
    } else if (is(name, "getName") && strstr(cls, "Class")) {
        r.l = jstr(sc[0] ? sc : cls);
    } else if (is(name, "currentThread")) {
        r.l = jo_new_object("java/lang/Thread");
    } else if (is(name, "getClassLoader")) {
        r.l = jo_new_object("java/lang/ClassLoader");
    } else if ((is(name, "loadClass") || is(name, "forName")) && argc >= 1) {
        char *n = jo_jstring_to_utf8((jstring)args[0].l);
        if (n) {
            for (char *c = n; *c; c++)
                if (*c == '.') *c = '/';
            r.l = (*env())->FindClass(env(), n);
            l_info("[JAVA] loadClass(%s)", n);
            free(n);
        }
    } else {
        log_once(cls[0] ? cls : sc, name, sig);
        r = default_return(sig);
    }
    return r;
}
