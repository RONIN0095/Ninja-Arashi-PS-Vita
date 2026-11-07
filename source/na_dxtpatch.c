/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/*
 * The game's big textures are ETC2 (RGBA8), which the Vita's GPU cannot
 * read. Each load used to turn them into DXT5 on the CPU, or read the DXT5
 * copy from ux0:data/ninjaarashi/texcache: a second file per texture next to
 * the ETC2 data Unity had just read, most of a level's loading time.
 *
 * ETC2 RGBA8 and DXT5 both take 16 bytes per 4x4 block, in the same order.
 * So the ETC2 data in the extracted asset files is replaced, once, by its
 * DXT5 conversion, and the texture's format field (m_TextureFormat) is set
 * from ETC2_RGBA8 (47) to DXT5 (12). Unity then uploads DXT5 itself, with
 * nothing left to convert or read twice.
 *
 * app0:dxt_index.txt lists the textures (tools/dxtpatch/make_index.py makes
 * it from the APK): file, offset and size of the image data, offset of the
 * format field, width, height, mip count and the CRC32 of the ETC2 data.
 * Offsets are in the whole asset file; big files are split in the APK into
 * 1 MB parts (name.split0, .split1, ...), which Unity reads one by one.
 * Data whose CRC does not match is left alone. When the work is done,
 * DATA_PATH "dxt_done.txt" is written; extracting an asset file again (see
 * na_apk.c) removes it, so the new copy is patched on the next start.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include "na_apk.h"
#include "na_dxtpatch.h"
#include "utils/logger.h"

#define INDEX_PATH "app0:dxt_index.txt"
#define DONE_PATH DATA_PATH "dxt_done.txt"
#define DONE_VERSION "dxt-1"
#define SPLIT_SIZE (1024 * 1024)

int na_etc2_to_dxt5(const void *etc, uint8_t *dxt, int w, int h); // na_gles3.c

typedef struct {
    char name[96];
    uint32_t img, size, fmt;
    int w, h, mips;
    uint32_t crc;
} tex_entry;

// The extracted file holding bytes [off, off + n) of an asset file, and the
// offset in it. Big files are split into 1 MB parts.
static int part_path(const char *name, uint32_t off, char *out, size_t outlen, uint32_t *part_off, uint32_t *part_left) {
    char entry[160];
    snprintf(entry, sizeof(entry), "assets/bin/Data/%s", name);
    char apk[256], buf[256];
    if (na_apk_exists(entry)) {
        snprintf(apk, sizeof(apk), DATA_PATH "base.apk/%s", entry);
        *part_off = off;
        *part_left = 0xffffffff;
    } else {
        snprintf(apk, sizeof(apk), DATA_PATH "base.apk/%s.split%u", entry, (unsigned)(off / SPLIT_SIZE));
        *part_off = off % SPLIT_SIZE;
        *part_left = SPLIT_SIZE - *part_off;
    }
    // extracts (and checks) the part if needed
    const char *p = na_apk_map_path(apk, buf, sizeof(buf));
    if (!p) return 0;
    snprintf(out, outlen, "%s", p);
    return 1;
}

// Reads or writes n bytes at offset off of the asset file, across parts
static int io(const char *name, uint32_t off, void *data, uint32_t n, int write) {
    uint8_t *d = data;
    while (n) {
        char path[256];
        uint32_t po, left;
        if (!part_path(name, off, path, sizeof(path), &po, &left)) return 0;
        uint32_t k = n < left ? n : left;
        FILE *f = fopen(path, write ? "r+b" : "rb");
        if (!f) return 0;
        int ok = fseek(f, po, SEEK_SET) == 0 && (write ? fwrite(d, 1, k, f) : fread(d, 1, k, f)) == k;
        fclose(f);
        if (!ok) return 0;
        d += k;
        off += k;
        n -= k;
    }
    return 1;
}

// DXT5 of the ETC2 data: from the texture cache when it is there, else converted
static int convert(const tex_entry *e, const uint8_t *etc, uint8_t *dxt) {
    if (e->mips == 1 && e->size == (uint32_t)(e->w * e->h)) {
        char zpath[160];
        snprintf(zpath, sizeof(zpath), DATA_PATH "texcache/%08x_%dx%d.dxt5z", (unsigned)e->crc, e->w, e->h);
        FILE *f = fopen(zpath, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long zn = ftell(f);
            fseek(f, 0, SEEK_SET);
            uint8_t *z = zn > 0 ? malloc(zn) : NULL;
            int ok = 0;
            if (z && fread(z, 1, zn, f) == (size_t)zn) {
                uLongf out = e->size;
                ok = uncompress(dxt, &out, z, zn) == Z_OK && out == e->size;
            }
            free(z);
            fclose(f);
            if (ok) return 1;
        }
    }
    // level by level (a level smaller than 4x4 is still one block)
    uint32_t off = 0;
    int w = e->w, h = e->h;
    for (int l = 0; l < e->mips && off < e->size; l++) {
        int bw = (w + 3) / 4, bh = (h + 3) / 4;
        uint32_t n = (uint32_t)bw * bh * 16;
        if (off + n > e->size) return 0;
        if (!na_etc2_to_dxt5(etc + off, dxt + off, bw * 4, bh * 4)) return 0;
        off += n;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return off == e->size;
}

// Progress bar on the black screen (the first run takes a few minutes)
static void progress(int done, int total) {
    int x0 = 180, y0 = 262, wbar = 600, hbar = 20;
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 960, 544);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(x0 - 2, y0 - 2, wbar + 4, hbar + 4);
    glClearColor(0.35f, 0.35f, 0.35f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(x0, y0, wbar, hbar);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int wdone = total ? wbar * done / total : 0;
    if (wdone > 0) {
        glScissor(x0, y0, wdone, hbar);
        glClearColor(0.85f, 0.75f, 0.35f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
    vglSwapBuffers(GL_FALSE);
}

void na_dxt_patch(void) {
    FILE *f = fopen(DONE_PATH, "r");
    if (f) {
        char v[32] = "";
        fgets(v, sizeof(v), f);
        fclose(f);
        if (strncmp(v, DONE_VERSION, strlen(DONE_VERSION)) == 0) return;
    }
    f = fopen(INDEX_PATH, "r");
    if (!f) {
        l_warn("[dxt] %s missing", INDEX_PATH);
        return;
    }
    tex_entry *list = NULL;
    int n = 0, cap = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        tex_entry e;
        unsigned img, size, fmt, crc;
        if (sscanf(line, "%95s %u %u %u %d %d %d %x", e.name, &img, &size, &fmt, &e.w, &e.h, &e.mips, &crc) != 8) continue;
        e.img = img;
        e.size = size;
        e.fmt = fmt;
        e.crc = crc;
        if (n == cap) {
            cap = cap ? cap * 2 : 256;
            tex_entry *nl = realloc(list, cap * sizeof(*list));
            if (!nl) break;
            list = nl;
        }
        list[n++] = e;
    }
    fclose(f);
    l_info("[dxt] %d textures to turn into DXT5", n);
    uint64_t t0 = sceKernelGetProcessTimeWide();
    int done = 0, skipped = 0, failed = 0;
    for (int i = 0; i < n; i++) {
        tex_entry *e = &list[i];
        progress(i, n);
        int32_t fmt = 0;
        if (!io(e->name, e->fmt, &fmt, 4, 0)) {
            failed++;
            continue;
        }
        if (fmt == 12) { // done in an earlier run
            skipped++;
            continue;
        }
        if (fmt != 47) {
            l_warn("[dxt] %s +%u: format %d, not ETC2_RGBA8", e->name, (unsigned)e->fmt, (int)fmt);
            failed++;
            continue;
        }
        uint8_t *etc = malloc(e->size), *dxt = malloc(e->size);
        int ok = etc && dxt && io(e->name, e->img, etc, e->size, 0);
        if (ok && (uint32_t)crc32(crc32(0L, Z_NULL, 0), etc, e->size) != e->crc) {
            l_warn("[dxt] %s +%u: data differs from the APK's, left as ETC2", e->name, (unsigned)e->img);
            ok = 0;
        } else if (ok) {
            int32_t dxt5 = 12;
            ok = convert(e, etc, dxt) && io(e->name, e->img, dxt, e->size, 1) && io(e->name, e->fmt, &dxt5, 4, 1);
        }
        free(etc);
        free(dxt);
        if (ok) done++;
        else failed++;
    }
    progress(n, n);
    free(list);
    l_info("[dxt] %d textures turned into DXT5, %d done before, %d not, in %u s", done, skipped, failed,
           (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000000));
    if (failed == 0) {
        f = fopen(DONE_PATH, "w");
        if (f) {
            fputs(DONE_VERSION "\n", f);
            fclose(f);
        }
    }
}

void na_dxt_patch_invalidate(void) {
    remove(DONE_PATH);
}
