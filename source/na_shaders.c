/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  na_shaders.c
 * @brief Unity's GLSL ES 3.00 shaders as GLSL ES 1.00 for vitaGL.
 *
 * Unity 6 only ships "#version 300 es" shaders, produced by hlslcc. Their
 * shape is very regular (see docs/ANALYSE.md), so a line-based rewrite is
 * enough to turn them into the GLSL ES 1.00 dialect vitaGL translates to CG:
 *
 *  - "#version 300 es" goes away.
 *  - HLSLCC_ENABLE_UNIFORM_BUFFERS and UNITY_SUPPORTS_UNIFORM_LOCATION are
 *    switched off: hlslcc wraps every uniform block in
 *    "#if HLSLCC_ENABLE_UNIFORM_BUFFERS", so the blocks become plain
 *    uniforms, and samplers lose their layout(location=...).
 *  - Remaining layout(...) qualifiers are removed.
 *  - Global "in"/"out" become attribute/varying (vertex) or varying
 *    (fragment); the first fragment output becomes gl_FragColor.
 *  - texture()/textureLod()/roundEven() are provided as overloads in a
 *    prelude at the top (CG allows overloading by sampler type).
 *
 * na_shaders_translate() is plain C without Vita dependencies, so it can be
 * tested on a PC against the shaders extracted from the game
 * (tools/shader_test.c).
 */

#include "na_shaders.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *buf;
    size_t len, cap;
} sbuf;

static void sb_put(sbuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

static void sb_puts(sbuf *b, const char *s) {
    sb_put(b, s, strlen(s));
}

static const char *PRELUDE_COMMON =
    "#define roundEven(x) floor((x) + 0.5)\n"
    "#define uint int\n"
    "#define uvec2 ivec2\n"
    "#define uvec3 ivec3\n"
    "#define uvec4 ivec4\n"
    "vec4 texture(sampler2D s, vec2 c) { return tex2D(s, c); }\n"
    "vec4 texture(sampler2D s, vec2 c, float b) { return tex2Dbias(s, vec4(c, 0.0, b)); }\n"
    "vec4 texture(samplerCube s, vec3 c) { return texCUBE(s, c); }\n"
    "vec4 texture(samplerCube s, vec3 c, float b) { return texCUBEbias(s, vec4(c, b)); }\n"
    "vec4 textureLod(sampler2D s, vec2 c, float l) { return tex2Dlod(s, vec4(c, 0.0, l)); }\n"
    "vec4 textureLod(samplerCube s, vec3 c, float l) { return texCUBElod(s, vec4(c, l)); }\n";

// Removes every "layout(...)" in a line (also inside #define bodies).
static void strip_layout(char *line) {
    char *p;
    while ((p = strstr(line, "layout")) != NULL) {
        char *q = p + 6;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '(') {
            // not a qualifier ("layout" inside an identifier): skip it
            line = p + 6;
            continue;
        }
        int depth = 0;
        do {
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            q++;
        } while (*q && depth > 0);
        while (*q == ' ' || *q == '\t') q++;
        memmove(p, q, strlen(q) + 1);
    }
}

static int starts_with_word(const char *s, const char *w) {
    size_t n = strlen(w);
    return strncmp(s, w, n) == 0 && (s[n] == ' ' || s[n] == '\t');
}

// Parses "out [precision] type name[...];" and returns the name (static buffer).
static const char *decl_name(const char *s) {
    static char name[128];
    const char *end = strchr(s, ';');
    if (!end) return NULL;
    const char *e = end;
    // skip array suffix
    const char *br = memchr(s, '[', end - s);
    if (br) e = br;
    while (e > s && isspace((unsigned char)e[-1])) e--;
    const char *b = e;
    while (b > s && (isalnum((unsigned char)b[-1]) || b[-1] == '_')) b--;
    size_t n = e - b;
    if (n == 0 || n >= sizeof(name)) return NULL;
    memcpy(name, b, n);
    name[n] = 0;
    return name;
}

char *na_shaders_translate(const char *src, int is_fragment, int *changed) {
    if (changed) *changed = 0;
    if (!src || !strstr(src, "#version 300 es"))
        return NULL;

    sbuf out = { 0 };
    int frag_outputs = 0;
    sb_puts(&out, PRELUDE_COMMON);

    const char *p = src;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char *line = malloc(n + 64);
        memcpy(line, p, n);
        line[n] = 0;
        p += n + (nl ? 1 : 0);

        char *t = line;
        while (*t == ' ' || *t == '\t') t++;

        if (strncmp(t, "#version", 8) == 0) {
            free(line);
            continue;
        }
        if (strncmp(t, "#extension", 10) == 0) {
            // GL_EXT_shader_texture_lod etc.: textureLod is in the prelude
            free(line);
            continue;
        }
        if (strstr(t, "#define HLSLCC_ENABLE_UNIFORM_BUFFERS 1")) {
            sb_puts(&out, "#define HLSLCC_ENABLE_UNIFORM_BUFFERS 0\n");
            free(line);
            continue;
        }
        if (strstr(t, "#define UNITY_SUPPORTS_UNIFORM_LOCATION 1")) {
            sb_puts(&out, "#define UNITY_SUPPORTS_UNIFORM_LOCATION 0\n");
            free(line);
            continue;
        }

        strip_layout(line);
        t = line;
        while (*t == ' ' || *t == '\t') t++;

        // Interpolation qualifiers have no GLSL ES 1.00 equivalent
        char *q = t;
        while (starts_with_word(q, "flat") || starts_with_word(q, "smooth") ||
               starts_with_word(q, "centroid") || starts_with_word(q, "noperspective")) {
            q = strchr(q, ' ');
            while (*q == ' ' || *q == '\t') q++;
        }

        if (starts_with_word(q, "in") && strchr(q, ';') && !strchr(q, '(')) {
            sb_puts(&out, is_fragment ? "varying" : "attribute");
            sb_puts(&out, q + 2);
            sb_puts(&out, "\n");
            if (changed) *changed = 1;
        } else if (starts_with_word(q, "out") && strchr(q, ';') && !strchr(q, '(')) {
            if (is_fragment) {
                const char *name = decl_name(q);
                if (name && frag_outputs == 0) {
                    sb_puts(&out, "#define ");
                    sb_puts(&out, name);
                    sb_puts(&out, " gl_FragColor\n");
                } else {
                    // extra render targets are written to a dummy variable
                    sb_puts(&out, q + 4);
                    sb_puts(&out, "\n");
                }
                frag_outputs++;
            } else {
                sb_puts(&out, "varying");
                sb_puts(&out, q + 3);
                sb_puts(&out, "\n");
            }
            if (changed) *changed = 1;
        } else {
            sb_puts(&out, line);
            sb_puts(&out, "\n");
        }
        free(line);
    }

    if (changed) *changed = 1;
    return out.buf;
}
