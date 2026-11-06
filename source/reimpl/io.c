/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"


#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

// First file accesses go to the log (always on), to see what the game loads
static int io_logged = 0;
#define l_io(...) do { if (io_logged < 400) { io_logged++; l_info(__VA_ARGS__); } } while (0)


#include "na_apk.h"
/*
 * Unity reads big assets either whole (name) or as name.split0, .split1, ...
 * and prefers the splits when they exist. Reading across the splits went
 * wrong here (it read split1 and then split3), which left the title picture,
 * the cutscenes and the level scenery mostly zero (black or flat colours).
 * When the joined file is there and as big as all splits together, the
 * splits are hidden, so Unity reads the one file.
 */
static const char *hide_split(const char *p, char *out, size_t n) {
    if (!p) return p;
    const char *s = strstr(p, ".split");
    if (!s) return p;
    const char *d = s + 6;
    if (!*d) return p;
    for (const char *q = d; *q; q++)
        if (*q < '0' || *q > '9') return p;
    char base[512];
    size_t bl = (size_t)(s - p);
    if (bl >= sizeof(base) - 16) return p;
    memcpy(base, p, bl);
    base[bl] = 0;
    struct stat st;
    if (stat(base, &st) != 0 || !S_ISREG(st.st_mode)) return p;
    // the joined file must hold all splits
    long long sum = 0;
    for (int i = 0; i < 512; i++) {
        char sp[528];
        struct stat ss;
        snprintf(sp, sizeof(sp), "%s.split%d", base, i);
        if (stat(sp, &ss) != 0) break;
        sum += ss.st_size;
    }
    if (sum != (long long)st.st_size) {
        static int warned = 0;
        if (warned++ < 8) l_warn("%s: %lld bytes, its splits %lld: splits kept", base, (long long)st.st_size, sum);
        return p;
    }
    snprintf(out, n, "%s.hidden", p);
    return out;
}

#define MAP_PATH(p) char mapped_##p[512], hidden_##p[540]; p = na_apk_map_path(p, mapped_##p, sizeof(mapped_##p)); \
    p = hide_split(p, hidden_##p, sizeof(hidden_##p))

FILE * fopen_soloader(const char * filename, const char * mode) {
    MAP_PATH(filename);
    // Three cores for applications at 444 MHz
    if (strcmp(filename, "/sys/devices/system/cpu/present") == 0 ||
        strcmp(filename, "/sys/devices/system/cpu/possible") == 0) {
        return fopen_soloader("app0:/cpu_present", mode);
    } else if (strstr(filename, "/sys/devices/system/cpu/cpu") == filename && strstr(filename, "cpuinfo_max_freq")) {
        return fopen_soloader("app0:/cpu_max_freq", mode);
    } else if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    if (ret) {
        if (strncmp(filename, "app0:/cpu", 9) != 0) l_io("fopen(%s, %s): %p", filename, mode, ret);
    } else {
        // Unity polls the /sys cpu files every frame: report missing ones once
        static int sys_misses = 0;
        if (strncmp(filename, "/sys/", 5) != 0 || sys_misses++ < 4)
            l_warn("fopen(%s, %s): %p", filename, mode, ret);
    }

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    MAP_PATH(path);
    if (strcmp(path, "/sys/devices/system/cpu/present") == 0 ||
        strcmp(path, "/sys/devices/system/cpu/possible") == 0) {
        return open_soloader("app0:/cpu_present", oflag);
    } else if (strstr(path, "/sys/devices/system/cpu/cpu") == path && strstr(path, "cpuinfo_max_freq")) {
        return open_soloader("app0:/cpu_max_freq", oflag);
    } else if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    } else if (strcmp(path, "/dev/urandom") == 0) {
        return open_soloader("app0:/urandom", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret >= 0)
        l_io("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    if (res != 0) l_warn("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    MAP_PATH(path);
    if (strcmp(path, "/system/lib/libOpenSLES.so") == 0) {
        l_debug("stat(%s): returning 0 in case this is a check for OpenSLES support", path);
        return 0;
    }

    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_io("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    const char *path = _pathname;
    MAP_PATH(path);
    DIR* ret = opendir(path);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}

int access_soloader(const char *path, int mode) {
    MAP_PATH(path);
    struct stat st;
    int r = stat(path, &st);
    l_io("access(%s): %i", path, r);
    return r;
}
