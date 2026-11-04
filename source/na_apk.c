/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  na_apk.c
 * @brief Reading single files out of base.apk (a ZIP archive).
 *
 * Unity reads its data from the APK itself; this is only for the few files
 * the Java side hands out (AssetManager.open() in na_java.c). The central
 * directory is read once and kept in memory.
 */

#include "na_apk.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "utils/logger.h"

#define APK_PATH DATA_PATH "base.apk"

typedef struct {
    char *name;
    uint32_t local_offset;
    uint32_t comp_size;
    uint32_t size;
    uint16_t method;
} apk_entry;

static apk_entry *entries = NULL;
static int n_entries = 0;
static int loaded = 0;

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int load_directory(void) {
    if (loaded) return n_entries > 0;
    loaded = 1;
    FILE *f = fopen(APK_PATH, "rb");
    if (!f) {
        l_error("[apk] cannot open %s", APK_PATH);
        return 0;
    }
    // End of central directory: in the last 64 KB + 22 bytes
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long tail = size < 65557 ? size : 65557;
    uint8_t *buf = malloc(tail);
    fseek(f, size - tail, SEEK_SET);
    fread(buf, 1, tail, f);
    long eocd = -1;
    for (long i = tail - 22; i >= 0; i--)
        if (rd32(buf + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) {
        l_error("[apk] no end of central directory");
        free(buf);
        fclose(f);
        return 0;
    }
    uint16_t count = rd16(buf + eocd + 10);
    uint32_t cd_size = rd32(buf + eocd + 12);
    uint32_t cd_off = rd32(buf + eocd + 16);
    free(buf);

    uint8_t *cd = malloc(cd_size);
    fseek(f, cd_off, SEEK_SET);
    if (fread(cd, 1, cd_size, f) != cd_size) {
        free(cd);
        fclose(f);
        return 0;
    }
    fclose(f);

    entries = calloc(count, sizeof(apk_entry));
    const uint8_t *p = cd;
    for (int i = 0; i < count && p + 46 <= cd + cd_size; i++) {
        if (rd32(p) != 0x02014b50) break;
        uint16_t name_len = rd16(p + 28), extra = rd16(p + 30), comment = rd16(p + 32);
        apk_entry *e = &entries[n_entries++];
        e->method = rd16(p + 10);
        e->comp_size = rd32(p + 20);
        e->size = rd32(p + 24);
        e->local_offset = rd32(p + 42);
        e->name = malloc(name_len + 1);
        memcpy(e->name, p + 46, name_len);
        e->name[name_len] = 0;
        p += 46 + name_len + extra + comment;
    }
    free(cd);
    l_info("[apk] %d entries in %s", n_entries, APK_PATH);
    return n_entries > 0;
}

static apk_entry *find(const char *name) {
    if (!load_directory()) return NULL;
    for (int i = 0; i < n_entries; i++)
        if (strcmp(entries[i].name, name) == 0) return &entries[i];
    return NULL;
}

int na_apk_exists(const char *name) {
    return find(name) != NULL;
}

void *na_apk_read(const char *name, size_t *size_out) {
    apk_entry *e = find(name);
    if (!e) return NULL;
    FILE *f = fopen(APK_PATH, "rb");
    if (!f) return NULL;
    uint8_t lh[30];
    fseek(f, e->local_offset, SEEK_SET);
    if (fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50) {
        fclose(f);
        return NULL;
    }
    fseek(f, e->local_offset + 30 + rd16(lh + 26) + rd16(lh + 28), SEEK_SET);
    uint8_t *comp = malloc(e->comp_size + 1);
    size_t got = fread(comp, 1, e->comp_size, f);
    fclose(f);
    if (got != e->comp_size) {
        free(comp);
        return NULL;
    }

    uint8_t *out;
    if (e->method == 0) {
        out = comp;
    } else if (e->method == 8) {
        out = malloc(e->size + 1);
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        inflateInit2(&zs, -MAX_WBITS);
        zs.next_in = comp;
        zs.avail_in = e->comp_size;
        zs.next_out = out;
        zs.avail_out = e->size;
        int r = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        free(comp);
        if (r != Z_STREAM_END) {
            free(out);
            l_error("[apk] inflate %s failed (%d)", name, r);
            return NULL;
        }
    } else {
        free(comp);
        l_error("[apk] %s: unsupported compression %d", name, e->method);
        return NULL;
    }
    out[e->size] = 0; // handy for text files
    if (size_out) *size_out = e->size;
    return out;
}
