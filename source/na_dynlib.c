/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  na_dynlib.c
 * @brief Imports of libunity.so/libil2cpp.so that the generic table in
 *        dynlib.c does not cover (or covers wrongly for Unity).
 *
 * Entries here take precedence over dynlib.c. docs/imports/ lists every
 * import of both libraries; tools/check_imports.py checks that each one is
 * in one of the two tables.
 */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/time.h>

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>

#include <so_util/so_util.h>

#include "na_modules.h"
#include "na_android.h"
#include "na_egl.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "reimpl/errno.h"
#include "reimpl/pthr.h"
#include "reimpl/sys.h"

/*
 * stdio: bionic's __sF[3] and the stdout/stderr variables (API 23+).
 * The libraries pass these FILE pointers to fprintf/fwrite/...; the wrappers
 * below turn them into newlib's streams.
 */
#define BIONIC_FILE_SIZE 84
static uint8_t sF_bionic[3][BIONIC_FILE_SIZE];
static FILE *stdin_bionic = (FILE *)sF_bionic[0];
static FILE *stdout_bionic = (FILE *)sF_bionic[1];
static FILE *stderr_bionic = (FILE *)sF_bionic[2];

static FILE *fix_file(FILE *f) {
    uintptr_t p = (uintptr_t)f, b = (uintptr_t)sF_bionic;
    if (p >= b && p < b + sizeof(sF_bionic)) {
        switch ((p - b) / BIONIC_FILE_SIZE) {
            case 0: return stdin;
            case 1: return stdout;
            default: return stderr;
        }
    }
    return f;
}

static int is_std_stream(FILE *f) {
    f = fix_file(f);
    return f == stdout || f == stderr;
}

// Output to stdout/stderr goes to the log
static int vfprintf_na(FILE *f, const char *fmt, va_list ap) {
    if (is_std_stream(f)) {
        char buf[1024];
        int r = vsnprintf(buf, sizeof(buf), fmt, ap);
        l_info("[stdio] %s", buf);
        return r;
    }
    return vfprintf(fix_file(f), fmt, ap);
}

static int fprintf_na(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf_na(f, fmt, ap);
    va_end(ap);
    return r;
}

static size_t fwrite_na(const void *p, size_t sz, size_t n, FILE *f) {
    if (is_std_stream(f)) {
        size_t len = sz * n;
        char buf[1024];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, p, len);
        buf[len] = 0;
        l_info("[stdio] %s", buf);
        return n;
    }
    return fwrite(p, sz, n, fix_file(f));
}

static int fputs_na(const char *s, FILE *f) {
    if (is_std_stream(f)) {
        l_info("[stdio] %s", s);
        return 1;
    }
    return fputs(s, fix_file(f));
}

static int fputc_na(int c, FILE *f) {
    if (is_std_stream(f)) return c;
    return fputc(c, fix_file(f));
}

static int fflush_na(FILE *f) { return f ? fflush(fix_file(f)) : 0; }
static int fileno_na(FILE *f) { return fileno(fix_file(f)); }
static int ferror_na(FILE *f) { return ferror(fix_file(f)); }
static int feof_na(FILE *f) { return feof(fix_file(f)); }
static void clearerr_na(FILE *f) { clearerr(fix_file(f)); }
static void setbuf_na(FILE *f, char *buf) { setbuf(fix_file(f), buf); }

static int puts_na(const char *s) {
    l_info("[stdio] %s", s);
    return 1;
}

static int printf_na(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf_na(stdout, fmt, ap);
    va_end(ap);
    return r;
}

static int vprintf_na(const char *fmt, va_list ap) {
    return vfprintf_na(stdout, fmt, ap);
}

/*
 * System properties
 */
typedef struct {
    const char *name;
    const char *value;
} prop_entry;

static const prop_entry props[] = {
    { "ro.build.version.sdk", "28" },
    { "ro.build.version.release", "9" },
    { "ro.product.model", "PS Vita" },
    { "ro.product.manufacturer", "Sony" },
    { "ro.product.brand", "Sony" },
    { "ro.product.device", "vita" },
    { "ro.product.name", "vita" },
    { "ro.product.board", "vita" },
    { "ro.hardware", "vita" },
    { "ro.board.platform", "vita" },
    { "ro.product.cpu.abi", "armeabi-v7a" },
    { "ro.product.cpu.abilist", "armeabi-v7a,armeabi" },
    { "ro.product.cpu.abilist32", "armeabi-v7a,armeabi" },
    { "ro.product.cpu.abilist64", "" },
    { "ro.build.fingerprint", "Sony/vita/vita:9/PSVITA/1:user/release-keys" },
    { "ro.build.tags", "release-keys" },
    { "ro.build.type", "user" },
    { "ro.debuggable", "0" },
    { "ro.kernel.qemu", "0" },
    { "ro.opengles.version", "196608" }, // 3.0
    { "ro.sf.lcd_density", "220" },
    { "ro.config.low_ram", "true" },
    { "persist.sys.locale", "en-US" },
    { "ro.product.locale", "en-US" },
    { "debug.unity.no-strict-mode", "1" },
};

static const prop_entry *find_prop(const char *name) {
    for (unsigned i = 0; i < sizeof(props) / sizeof(props[0]); i++)
        if (strcmp(props[i].name, name) == 0) return &props[i];
    return NULL;
}

static int system_property_get(const char *name, char *value) {
    const prop_entry *p = find_prop(name);
    if (!p) {
        value[0] = 0;
        l_debug("__system_property_get(%s): (unset)", name);
        return 0;
    }
    strcpy(value, p->value);
    l_debug("__system_property_get(%s): %s", name, value);
    return strlen(value);
}

static const void *system_property_find(const char *name) {
    return find_prop(name);
}

static int system_property_read(const void *pi, char *name, char *value) {
    const prop_entry *p = pi;
    if (!p) return 0;
    if (name) strcpy(name, p->name);
    strcpy(value, p->value);
    return strlen(p->value);
}

/*
 * sysconf with bionic's constants
 */
#define BIONIC_SC_CLK_TCK 0x0006
#define BIONIC_SC_PAGESIZE 0x0027
#define BIONIC_SC_PAGE_SIZE 0x0028
#define BIONIC_SC_NPROCESSORS_CONF 0x0060
#define BIONIC_SC_NPROCESSORS_ONLN 0x0061
#define BIONIC_SC_PHYS_PAGES 0x0062
#define BIONIC_SC_AVPHYS_PAGES 0x0063

static long sysconf_na(int name) {
    switch (name) {
        case BIONIC_SC_CLK_TCK: return 100;
        case BIONIC_SC_PAGESIZE:
        case BIONIC_SC_PAGE_SIZE: return 4096;
        // Three cores are usable by applications
        case BIONIC_SC_NPROCESSORS_CONF:
        case BIONIC_SC_NPROCESSORS_ONLN: return 3;
        case BIONIC_SC_PHYS_PAGES: return (512 * 1024 * 1024) / 4096;
        case BIONIC_SC_AVPHYS_PAGES: return (256 * 1024 * 1024) / 4096;
        default:
            l_warn("sysconf(0x%x): unknown, returning -1", name);
            errno = EINVAL;
            return -1;
    }
}

/*
 * Files
 */
static long long lseek64_na(int fd, long long off, int whence) {
    return lseek(fd, (off_t)off, whence);
}

typedef struct {
    uint32_t f_type;
    uint32_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    int32_t f_fsid[2];
    uint32_t f_namelen;
    uint32_t f_frsize;
    uint32_t f_flags;
    uint32_t f_spare[4];
} statfs_bionic;

static int statfs_na(const char *path, statfs_bionic *st) {
    memset(st, 0, sizeof(*st));
    st->f_type = 0xEF53;
    st->f_bsize = st->f_frsize = 4096;
    st->f_blocks = (4ULL << 30) / 4096;
    st->f_bfree = st->f_bavail = (2ULL << 30) / 4096;
    st->f_files = st->f_ffree = 65536;
    st->f_namelen = 255;
    return 0;
}

static int readlink_na(const char *path, char *buf, size_t sz) {
    l_debug("readlink(%s): not supported", path);
    errno = EINVAL;
    return -1;
}

static int fail_enosys(void) {
    errno = ENOSYS;
    return -1;
}

/*
 * Users
 */
typedef struct {
    char *pw_name;
    char *pw_passwd;
    uint32_t pw_uid;
    uint32_t pw_gid;
    char *pw_dir;
    char *pw_shell;
} passwd_bionic;

#define APP_UID 10123

static passwd_bionic the_user = {
    "u0_a123", "", APP_UID, APP_UID, DATA_PATH, "/system/bin/sh"
};

static passwd_bionic *getpwuid_na(uint32_t uid) { return &the_user; }

static int getpwuid_r_na(uint32_t uid, passwd_bionic *pw, char *buf, size_t len, passwd_bionic **res) {
    *pw = the_user;
    *res = pw;
    return 0;
}

static int get_uid(void) { return APP_UID; }
static int get_pid(void) { return 4242; }
static int get_tid(void) { return sceKernelGetThreadId(); }

/*
 * Threads
 */
// Stack bounds of the calling thread (the Boehm GC scans them)
static int pthread_getattr_np_na(pthread_t thread, pthread_attr_t_bionic *attr) {
    SceKernelThreadInfo info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) < 0)
        return ESRCH;
    if (thread != pthread_self())
        l_warn("pthread_getattr_np: other thread, returning the caller's stack");
    memset(attr, 0, sizeof(*attr));
    // magic/real_ptr are not used by the functions below
    attr->real_ptr = NULL;
    attr->magic = (int32_t)(uintptr_t)info.stack; // stack_base (bionic layout)
    attr->stack_size = info.stackSize;
    attr->guard_size = 4096;
    return 0;
}

static int pthread_attr_getstack_na(const pthread_attr_t_bionic *attr, void **addr, size_t *size) {
    *addr = (void *)(uintptr_t)attr->magic;
    *size = attr->stack_size;
    return 0;
}

static int pthread_condattr_setclock_na(pthread_condattr_t *attr, int clock) {
    return 0;
}

// Condition variables created with CLOCK_MONOTONIC get monotonic deadlines;
// newlib's pthread waits on realtime. Detect and convert.
static int pthread_cond_timedwait_na(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex,
                                     const struct timespec *abstime) {
    struct timespec rt;
    if (abstime && abstime->tv_sec < 946684800) { // before 2000: a monotonic deadline
        struct timespec now_mono, now_rt;
        clock_gettime_soloader(1 /* CLOCK_MONOTONIC */, &now_mono);
        clock_gettime_soloader(0 /* CLOCK_REALTIME */, &now_rt);
        int64_t delta = (int64_t)(abstime->tv_sec - now_mono.tv_sec) * 1000000000LL +
                        (abstime->tv_nsec - now_mono.tv_nsec);
        if (delta < 0) delta = 0;
        int64_t t = (int64_t)now_rt.tv_sec * 1000000000LL + now_rt.tv_nsec + delta;
        rt.tv_sec = t / 1000000000LL;
        rt.tv_nsec = t % 1000000000LL;
        abstime = &rt;
    }
    return pthread_cond_timedwait_soloader(cond, mutex, (struct timespec *)abstime);
}

// bionic pthread_rwlock_t is 40 bytes; the first word points to ours.
static SceKernelLwMutexWork rwlock_init_lock;
static int rwlock_init_lock_ready = 0;

static pthread_rwlock_t *rwlock_get(void *lock) {
    pthread_rwlock_t **slot = (pthread_rwlock_t **)lock;
    if (*slot) return *slot;
    sceKernelLockLwMutex(&rwlock_init_lock, 1, NULL);
    if (!*slot) {
        pthread_rwlock_t *l = malloc(sizeof(pthread_rwlock_t));
        pthread_rwlock_init(l, NULL);
        *slot = l;
    }
    sceKernelUnlockLwMutex(&rwlock_init_lock, 1);
    return *slot;
}

static int rwlock_init_na(void *lock, const void *attr) {
    memset(lock, 0, 40);
    rwlock_get(lock);
    return 0;
}
static int rwlock_rdlock_na(void *lock) { return pthread_rwlock_rdlock(rwlock_get(lock)); }
static int rwlock_wrlock_na(void *lock) { return pthread_rwlock_wrlock(rwlock_get(lock)); }
static int rwlock_unlock_na(void *lock) { return pthread_rwlock_unlock(rwlock_get(lock)); }

static int pthread_kill_na(pthread_t t, int sig) {
    // The Boehm GC suspends threads with signals (SIGPWR/SIGXCPU). There
    // are no signals on the Vita; see docs/NOTES.md.
    l_warn("pthread_kill(%p, %d): signals are not supported", (void *)t, sig);
    return 0;
}

/*
 * Signals (accepted and ignored)
 */
typedef unsigned long sigset_bionic;

static int sigemptyset_na(sigset_bionic *s) { *s = 0; return 0; }
static int sigfillset_na(sigset_bionic *s) { *s = ~0UL; return 0; }
static int sigaddset_na(sigset_bionic *s, int n) { if (n > 0 && n <= 32) *s |= 1UL << (n - 1); return 0; }
static int sigdelset_na(sigset_bionic *s, int n) { if (n > 0 && n <= 32) *s &= ~(1UL << (n - 1)); return 0; }

static int sigaction_na(int sig, const void *act, void *old) {
    l_debug("sigaction(%d): ignored", sig);
    if (old) memset(old, 0, 16);
    return 0;
}

static void *signal_na(int sig, void *handler) {
    return NULL; // SIG_DFL
}

static int sigsuspend_na(const sigset_bionic *mask) {
    l_warn("sigsuspend: no signals, returning EINTR");
    errno = EINTR;
    return -1;
}

static int sigprocmask_na(int how, const sigset_bionic *set, sigset_bionic *old) {
    if (old) *old = 0;
    return 0;
}

/*
 * Misc
 */
static int posix_memalign_na(void **out, size_t align, size_t size) {
    if (align < sizeof(void *)) align = sizeof(void *);
    void *p = memalign(align, size);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}

static int fd_isset_chk(int fd, const void *set, size_t sz) {
    return (((const uint32_t *)set)[fd / 32] >> (fd % 32)) & 1;
}

static void fd_set_chk(int fd, void *set, size_t sz) {
    ((uint32_t *)set)[fd / 32] |= 1u << (fd % 32);
}

static size_t ctype_get_mb_cur_max(void) { return 1; }

static char *gnu_strerror_r(int err, char *buf, size_t len) {
    strncpy(buf, strerror(err), len);
    if (len) buf[len - 1] = 0;
    return buf;
}

static void android_set_abort_message_na(const char *msg) {
    l_fatal("Abort message: %s", msg ? msg : "(null)");
}

static void syslog_na(int prio, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    l_info("[syslog] %s", buf);
}

static int isatty_na(int fd) { return 0; }
static unsigned if_nametoindex_na(const char *n) { return 0; }
static uint32_t inet_addr_na(const char *s) { return 0xffffffff; }

typedef struct { int quot; int rem; } div_bionic;
static div_bionic div_na(int a, int b) {
    div_bionic r = { a / b, a % b };
    return r;
}

// Locale variants: the libraries only ever pass the C/UTF-8 locale
static int iswalpha_l_na(wint_t c, void *l) { return iswalpha(c); }
static int iswblank_l_na(wint_t c, void *l) { return iswblank(c); }
static int iswcntrl_l_na(wint_t c, void *l) { return iswcntrl(c); }
static int iswdigit_l_na(wint_t c, void *l) { return iswdigit(c); }
static int iswlower_l_na(wint_t c, void *l) { return iswlower(c); }
static int iswprint_l_na(wint_t c, void *l) { return iswprint(c); }
static int iswpunct_l_na(wint_t c, void *l) { return iswpunct(c); }
static int iswspace_l_na(wint_t c, void *l) { return iswspace(c); }
static int iswupper_l_na(wint_t c, void *l) { return iswupper(c); }
static int iswxdigit_l_na(wint_t c, void *l) { return iswxdigit(c); }
static wint_t towlower_l_na(wint_t c, void *l) { return towlower(c); }
static wint_t towupper_l_na(wint_t c, void *l) { return towupper(c); }
static int strcoll_l_na(const char *a, const char *b, void *l) { return strcmp(a, b); }
static int wcscoll_l_na(const wchar_t *a, const wchar_t *b, void *l) { return wcscmp(a, b); }
static size_t strxfrm_l_na(char *d, const char *s, size_t n, void *l) { return strxfrm(d, s, n); }
static size_t wcsxfrm_l_na(wchar_t *d, const wchar_t *s, size_t n, void *l) { return wcsxfrm(d, s, n); }
static size_t strftime_l_na(char *s, size_t n, const char *f, const struct tm *t, void *l) { return strftime(s, n, f, t); }
static double strtold_l_na(const char *s, char **e, void *l) { return strtod(s, e); }
static long long strtoll_l_na(const char *s, char **e, int b, void *l) { return strtoll(s, e, b); }
static unsigned long long strtoull_l_na(const char *s, char **e, int b, void *l) { return strtoull(s, e, b); }
static double wcstold_na(const wchar_t *s, wchar_t **e) { return wcstod(s, e); }

static int unsetenv_na(const char *name) { return 0; }

// fnmatch with '*', '?' and '[...]'; flags are ignored (newlib on the Vita has none)
static int fnmatch_na(const char *p, const char *s, int flags) {
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 0;
            for (; *s; s++)
                if (fnmatch_na(p, s, flags) == 0) return 0;
            return 1;
        }
        if (!*s) return 1;
        if (*p == '[') {
            const char *q = p + 1;
            int neg = (*q == '!' || *q == '^');
            if (neg) q++;
            int ok = 0;
            for (; *q && *q != ']'; q++) {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*s >= q[0] && *s <= q[2]) ok = 1;
                    q += 2;
                } else if (*q == *s) {
                    ok = 1;
                }
            }
            if (ok == neg || !*q) return 1;
            p = q;
            continue;
        }
        if (*p != '?' && *p != *s) return 1;
    }
    return *s ? 1 : 0;
}

so_default_dynlib na_dynlib[] = {
    // stdio
    { "__sF", (uintptr_t)&sF_bionic },
    { "stdin", (uintptr_t)&stdin_bionic },
    { "stdout", (uintptr_t)&stdout_bionic },
    { "stderr", (uintptr_t)&stderr_bionic },
    { "fprintf", (uintptr_t)&fprintf_na },
    { "vfprintf", (uintptr_t)&vfprintf_na },
    { "printf", (uintptr_t)&printf_na },
    { "vprintf", (uintptr_t)&vprintf_na },
    { "fwrite", (uintptr_t)&fwrite_na },
    { "fputs", (uintptr_t)&fputs_na },
    { "fputc", (uintptr_t)&fputc_na },
    { "puts", (uintptr_t)&puts_na },
    { "fflush", (uintptr_t)&fflush_na },
    { "fileno", (uintptr_t)&fileno_na },
    { "ferror", (uintptr_t)&ferror_na },
    { "feof", (uintptr_t)&feof_na },
    { "clearerr", (uintptr_t)&clearerr_na },
    { "setbuf", (uintptr_t)&setbuf_na },

    // libdl
    { "dlopen", (uintptr_t)&na_dlopen },
    { "dlsym", (uintptr_t)&na_dlsym },
    { "dlclose", (uintptr_t)&na_dlclose },
    { "dlerror", (uintptr_t)&na_dlerror },
    { "dladdr", (uintptr_t)&na_dladdr },
    { "dl_unwind_find_exidx", (uintptr_t)&na_dl_unwind_find_exidx },
    { "dl_iterate_phdr", (uintptr_t)&na_dl_iterate_phdr },

    // bionic internals
    { "__system_property_get", (uintptr_t)&system_property_get },
    { "__system_property_find", (uintptr_t)&system_property_find },
    { "__system_property_read", (uintptr_t)&system_property_read },
    { "__FD_ISSET_chk", (uintptr_t)&fd_isset_chk },
    { "__FD_SET_chk", (uintptr_t)&fd_set_chk },
    { "__ctype_get_mb_cur_max", (uintptr_t)&ctype_get_mb_cur_max },
    { "__gnu_strerror_r", (uintptr_t)&gnu_strerror_r },
    { "__register_atfork", (uintptr_t)&ret0 },
    { "__end__", 0 },
    { "_ZTH15gDeferredAction", 0 },
    { "android_set_abort_message", (uintptr_t)&android_set_abort_message_na },
    { "sysconf", (uintptr_t)&sysconf_na },

    // files
    { "lseek64", (uintptr_t)&lseek64_na },
    { "statfs", (uintptr_t)&statfs_na },
    { "readlink", (uintptr_t)&readlink_na },
    { "link", (uintptr_t)&fail_enosys },
    { "symlink", (uintptr_t)&fail_enosys },
    { "fchmod", (uintptr_t)&ret0 },
    { "flock", (uintptr_t)&ret0 },
    { "futimens", (uintptr_t)&ret0 },
    { "utimes", (uintptr_t)&ret0 },
    { "sendfile", (uintptr_t)&fail_enosys },
    { "dup2", (uintptr_t)&fail_enosys },
    { "isatty", (uintptr_t)&isatty_na },
    { "fnmatch", (uintptr_t)&fnmatch_na },

    // users / processes
    { "getuid", (uintptr_t)&get_uid },
    { "geteuid", (uintptr_t)&get_uid },
    { "getegid", (uintptr_t)&get_uid },
    { "getpid", (uintptr_t)&get_pid },
    { "gettid", (uintptr_t)&get_tid },
    { "getpwuid", (uintptr_t)&getpwuid_na },
    { "getpwuid_r", (uintptr_t)&getpwuid_r_na },
    { "getpriority", (uintptr_t)&ret0 },
    { "setpriority", (uintptr_t)&ret0 },
    { "process_vm_readv", (uintptr_t)&fail_enosys },
    { "ptrace", (uintptr_t)&fail_enosys },
    { "unsetenv", (uintptr_t)&unsetenv_na },

    // threads
    { "pthread_getattr_np", (uintptr_t)&pthread_getattr_np_na },
    { "pthread_attr_getstack", (uintptr_t)&pthread_attr_getstack_na },
    { "pthread_condattr_init", (uintptr_t)&pthread_condattr_init_soloader },
    { "pthread_condattr_destroy", (uintptr_t)&pthread_condattr_destroy_soloader },
    { "pthread_condattr_setclock", (uintptr_t)&pthread_condattr_setclock_na },
    { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_na },
    { "pthread_rwlock_init", (uintptr_t)&rwlock_init_na },
    { "pthread_rwlock_rdlock", (uintptr_t)&rwlock_rdlock_na },
    { "pthread_rwlock_wrlock", (uintptr_t)&rwlock_wrlock_na },
    { "pthread_rwlock_unlock", (uintptr_t)&rwlock_unlock_na },
    { "pthread_kill", (uintptr_t)&pthread_kill_na },

    // signals
    { "sigaction", (uintptr_t)&sigaction_na },
    { "signal", (uintptr_t)&signal_na },
    { "sigemptyset", (uintptr_t)&sigemptyset_na },
    { "sigfillset", (uintptr_t)&sigfillset_na },
    { "sigaddset", (uintptr_t)&sigaddset_na },
    { "sigdelset", (uintptr_t)&sigdelset_na },
    { "sigsuspend", (uintptr_t)&sigsuspend_na },
    { "sigprocmask", (uintptr_t)&sigprocmask_na },
    { "pthread_sigmask", (uintptr_t)&sigprocmask_na },
    { "sigaltstack", (uintptr_t)&ret0 },
    { "sigsetjmp", (uintptr_t)&setjmp },
    { "siglongjmp", (uintptr_t)&longjmp },

    // memory
    { "posix_memalign", (uintptr_t)&posix_memalign_na },
    { "madvise", (uintptr_t)&ret0 },
    { "mprotect", (uintptr_t)&ret0 },

    // math
    { "cbrtf", (uintptr_t)&cbrtf },
    { "hypot", (uintptr_t)&hypot },
    { "hypotf", (uintptr_t)&hypotf },
    { "log2f", (uintptr_t)&log2f },
    { "logb", (uintptr_t)&logb },
    { "modff", (uintptr_t)&modff },
    { "tanhf", (uintptr_t)&tanhf },
    { "div", (uintptr_t)&div_na },

    // locale variants
    { "iswalpha_l", (uintptr_t)&iswalpha_l_na },
    { "iswblank_l", (uintptr_t)&iswblank_l_na },
    { "iswcntrl_l", (uintptr_t)&iswcntrl_l_na },
    { "iswdigit_l", (uintptr_t)&iswdigit_l_na },
    { "iswlower_l", (uintptr_t)&iswlower_l_na },
    { "iswprint_l", (uintptr_t)&iswprint_l_na },
    { "iswpunct_l", (uintptr_t)&iswpunct_l_na },
    { "iswspace_l", (uintptr_t)&iswspace_l_na },
    { "iswupper_l", (uintptr_t)&iswupper_l_na },
    { "iswxdigit_l", (uintptr_t)&iswxdigit_l_na },
    { "towlower_l", (uintptr_t)&towlower_l_na },
    { "towupper_l", (uintptr_t)&towupper_l_na },
    { "strcoll_l", (uintptr_t)&strcoll_l_na },
    { "wcscoll_l", (uintptr_t)&wcscoll_l_na },
    { "strxfrm_l", (uintptr_t)&strxfrm_l_na },
    { "wcsxfrm_l", (uintptr_t)&wcsxfrm_l_na },
    { "strftime_l", (uintptr_t)&strftime_l_na },
    { "strtold_l", (uintptr_t)&strtold_l_na },
    { "strtoll_l", (uintptr_t)&strtoll_l_na },
    { "strtoull_l", (uintptr_t)&strtoull_l_na },
    { "wcstold", (uintptr_t)&wcstold_na },

    // logging / network
    { "openlog", (uintptr_t)&ret0 },
    { "closelog", (uintptr_t)&ret0 },
    { "syslog", (uintptr_t)&syslog_na },
    { "getnameinfo", (uintptr_t)&fail_enosys },
    { "if_nametoindex", (uintptr_t)&if_nametoindex_na },
    { "inet_addr", (uintptr_t)&inet_addr_na },

    // EGL additions
    { "eglSurfaceAttrib", (uintptr_t)&na_eglSurfaceAttrib },

    // NDK (na_android.c)
    NA_ANDROID_DYNLIB_ENTRIES
};

const int na_dynlib_count = sizeof(na_dynlib) / sizeof(na_dynlib[0]);

void na_dynlib_init(void) {
    sceKernelCreateLwMutex(&rwlock_init_lock, "rwlock_init", 0, 0, NULL);
    rwlock_init_lock_ready = 1;
}
