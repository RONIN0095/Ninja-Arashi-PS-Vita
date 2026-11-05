/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  na_gles3.c
 * @brief OpenGL ES 3.0 as Unity 6 sees it, on top of vitaGL (ES 2.0).
 *
 * Unity fetches every GL function with eglGetProcAddress()/dlsym(). The
 * lookup goes:
 *   1. the table below (ES 3.0 additions, version/limits queries, shader
 *      translation, functions that must look absent),
 *   2. vitaGL (vglGetProcAddress),
 *   3. NULL, with a log line.
 *
 * Functions vitaGL does not have are either implemented on top of ES 2.0
 * (glTexStorage2D, glClearBuffer*, glDrawBuffers, fences, ...) or are stubs
 * that log their first call and return 0. Every name Unity asks for is
 * logged once, so the log shows exactly what the game uses.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vitaGL.h>

#include "na_shaders.h"
#include "utils/logger.h"

#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_NUM_EXTENSIONS 0x821D
#endif
#define GL_MAX_3D_TEXTURE_SIZE_ 0x8073
#define GL_MAX_ARRAY_TEXTURE_LAYERS_ 0x88FF
#define GL_MAX_DRAW_BUFFERS_ 0x8824
#define GL_MAX_COLOR_ATTACHMENTS_ 0x8CDF
#define GL_MAX_SAMPLES_ 0x8D57
#define GL_MAX_UNIFORM_BUFFER_BINDINGS_ 0x8A2F
#define GL_MAX_UNIFORM_BLOCK_SIZE_ 0x8A30
#define GL_MAX_VERTEX_UNIFORM_BLOCKS_ 0x8A2B
#define GL_MAX_FRAGMENT_UNIFORM_BLOCKS_ 0x8A2D
#define GL_MAX_COMBINED_UNIFORM_BLOCKS_ 0x8A2E
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT_ 0x8A34
#define GL_MAX_VERTEX_OUTPUT_COMPONENTS_ 0x9122
#define GL_MAX_FRAGMENT_INPUT_COMPONENTS_ 0x9125
#define GL_MAX_VARYING_COMPONENTS_ 0x8B4B
#define GL_MAX_ELEMENT_INDEX_ 0x8D6B
#define GL_MAX_ELEMENTS_VERTICES_ 0x80E8
#define GL_MAX_ELEMENTS_INDICES_ 0x80E9
#define GL_MAX_PROGRAM_TEXEL_OFFSET_ 0x8905
#define GL_MIN_PROGRAM_TEXEL_OFFSET_ 0x8904
#define GL_NUM_PROGRAM_BINARY_FORMATS_ 0x87FE
#define GL_NUM_SHADER_BINARY_FORMATS_ 0x8DF9
#define GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS_ 0x8C8A
#define GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS_ 0x8C8B
#define GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS_ 0x8C80
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS_ 0x8B4A
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS_ 0x8B49
#define GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS_ 0x8A31
#define GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS_ 0x8A33
#define GL_MAX_SERVER_WAIT_TIMEOUT_ 0x9111
#define GL_MAX_TEXTURE_LOD_BIAS_ 0x84FD
#define GL_READ_BUFFER_ 0x0C02
#define GL_DRAW_BUFFER0_ 0x8825
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT_ 0x84FF
#define GL_ACTIVE_UNIFORM_BLOCKS_ 0x8A36
#define GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH_ 0x8A35
#define GL_PROGRAM_BINARY_LENGTH_ 0x8741
#define GL_UNIFORM_BLOCK_INDEX_ 0x8A3A
#define GL_NUM_SAMPLE_COUNTS_ 0x9380
#define GL_ALREADY_SIGNALED_ 0x911A
#define GL_SHADER_TYPE_ 0x8B4F
#define GL_COLOR_ 0x1800
#define GL_DEPTH_ 0x1801
#define GL_STENCIL_ 0x1802
#define GL_DEPTH_STENCIL_ 0x84F9
#define GL_TEXTURE_BINDING_2D_ 0x8069
#define GL_TEXTURE_BINDING_CUBE_MAP_ 0x8514
#ifndef GL_LOW_FLOAT
#define GL_LOW_FLOAT 0x8DF0
#define GL_MEDIUM_FLOAT 0x8DF1
#define GL_HIGH_FLOAT 0x8DF2
#endif
#ifndef GL_DEPTH_STENCIL_OES
#define GL_DEPTH_STENCIL_OES 0x84F9
#endif
#ifndef GL_UNSIGNED_INT_24_8_OES
#define GL_UNSIGNED_INT_24_8_OES 0x84FA
#endif

/*
 * Version and extensions
 */
static const char *extensions[] = {
    "GL_EXT_texture_compression_s3tc",
    "GL_EXT_texture_compression_dxt1",
    "GL_IMG_texture_compression_pvrtc",
    "GL_OES_compressed_ETC1_RGB8_texture",
    "GL_OES_depth24",
    "GL_OES_packed_depth_stencil",
    "GL_OES_rgb8_rgba8",
    "GL_OES_texture_npot",
    "GL_OES_element_index_uint",
    "GL_OES_vertex_array_object",
    "GL_EXT_texture_format_BGRA8888",
};
#define N_EXTENSIONS ((int)(sizeof(extensions) / sizeof(extensions[0])))
static char extension_string[1024];

static const GLubyte *glGetString_na(GLenum name) {
    switch (name) {
        case GL_VENDOR: return (const GLubyte *)"vitaGL";
        case GL_RENDERER: return (const GLubyte *)"vitaGL (PS Vita SGX543MP4+)";
        case GL_VERSION: return (const GLubyte *)"OpenGL ES 3.0 vitaGL";
        case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)"OpenGL ES GLSL ES 3.00";
        case GL_EXTENSIONS:
            if (!extension_string[0]) {
                for (int i = 0; i < N_EXTENSIONS; i++) {
                    strcat(extension_string, extensions[i]);
                    strcat(extension_string, " ");
                }
            }
            return (const GLubyte *)extension_string;
        default:
            return glGetString(name);
    }
}

static const GLubyte *glGetStringi_na(GLenum name, GLuint index) {
    if (name == GL_EXTENSIONS && index < (GLuint)N_EXTENSIONS)
        return (const GLubyte *)extensions[index];
    return NULL;
}

static int es3_integer(GLenum pname, GLint *v) {
    switch (pname) {
        case GL_MAJOR_VERSION: *v = 3; return 1;
        case GL_MINOR_VERSION: *v = 0; return 1;
        case GL_NUM_EXTENSIONS: *v = N_EXTENSIONS; return 1;
        case GL_MAX_3D_TEXTURE_SIZE_: *v = 256; return 1;
        case GL_MAX_ARRAY_TEXTURE_LAYERS_: *v = 256; return 1;
        case GL_MAX_DRAW_BUFFERS_: *v = 1; return 1;
        case GL_MAX_COLOR_ATTACHMENTS_: *v = 1; return 1;
        case GL_MAX_SAMPLES_: *v = 1; return 1;
        case GL_MAX_UNIFORM_BUFFER_BINDINGS_: *v = 24; return 1;
        case GL_MAX_UNIFORM_BLOCK_SIZE_: *v = 16384; return 1;
        case GL_MAX_VERTEX_UNIFORM_BLOCKS_: *v = 12; return 1;
        case GL_MAX_FRAGMENT_UNIFORM_BLOCKS_: *v = 12; return 1;
        case GL_MAX_COMBINED_UNIFORM_BLOCKS_: *v = 24; return 1;
        case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT_: *v = 256; return 1;
        case GL_MAX_VERTEX_OUTPUT_COMPONENTS_: *v = 64; return 1;
        case GL_MAX_FRAGMENT_INPUT_COMPONENTS_: *v = 60; return 1;
        case GL_MAX_VARYING_COMPONENTS_: *v = 60; return 1;
        case GL_MAX_ELEMENT_INDEX_: *v = 0xFFFFFF; return 1;
        case GL_MAX_ELEMENTS_VERTICES_: *v = 65536; return 1;
        case GL_MAX_ELEMENTS_INDICES_: *v = 65536; return 1;
        case GL_MAX_PROGRAM_TEXEL_OFFSET_: *v = 7; return 1;
        case GL_MIN_PROGRAM_TEXEL_OFFSET_: *v = -8; return 1;
        case GL_NUM_PROGRAM_BINARY_FORMATS_: *v = 0; return 1;
        case GL_NUM_SHADER_BINARY_FORMATS_: *v = 0; return 1;
        case GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS_: *v = 64; return 1;
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS_: *v = 4; return 1;
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS_: *v = 4; return 1;
        case GL_MAX_VERTEX_UNIFORM_COMPONENTS_: *v = 1024; return 1;
        case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS_: *v = 896; return 1;
        case GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS_: *v = 1024; return 1;
        case GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS_: *v = 896; return 1;
        case GL_MAX_SERVER_WAIT_TIMEOUT_: *v = 0; return 1;
        case GL_READ_BUFFER_: *v = GL_BACK; return 1;
        case GL_DRAW_BUFFER0_: *v = GL_BACK; return 1;
        default: return 0;
    }
}

static void glGetIntegerv_na(GLenum pname, GLint *data) {
    if (!es3_integer(pname, data))
        glGetIntegerv(pname, data);
}

static void glGetInteger64v_na(GLenum pname, int64_t *data) {
    GLint v = 0;
    if (pname == GL_MAX_SERVER_WAIT_TIMEOUT_) {
        *data = 0;
        return;
    }
    glGetIntegerv_na(pname, &v);
    *data = v;
}

static void glGetFloatv_na(GLenum pname, GLfloat *data) {
    GLint v;
    if (pname == GL_MAX_TEXTURE_LOD_BIAS_) {
        *data = 15.0f;
        return;
    }
    if (es3_integer(pname, &v)) {
        *data = (GLfloat)v;
        return;
    }
    glGetFloatv(pname, data);
}

static void glGetBooleanv_na(GLenum pname, GLboolean *data) {
    GLint v;
    if (es3_integer(pname, &v)) {
        *data = v ? GL_TRUE : GL_FALSE;
        return;
    }
    glGetBooleanv(pname, data);
}

/*
 * Shaders: GLSL ES 3.00 -> 1.00 (na_shaders.c)
 */
#define MAX_SHADER_IDS 8192
static uint8_t shader_is_fragment[MAX_SHADER_IDS];

static GLuint glCreateShader_na(GLenum type) {
    GLuint s = glCreateShader(type);
    if (s < MAX_SHADER_IDS) shader_is_fragment[s] = (type == GL_FRAGMENT_SHADER);
    return s;
}

static int shaders_seen = 0;

static void glShaderSource_na(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) {
    // Join the parts
    size_t total = 0;
    for (int i = 0; i < count; i++)
        total += length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
    char *src = malloc(total + 1);
    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        size_t n = length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
        memcpy(src + pos, string[i], n);
        pos += n;
    }
    src[pos] = 0;

    int frag = shader < MAX_SHADER_IDS ? shader_is_fragment[shader] : 0;
    int changed = 0;
    char *t = na_shaders_translate(src, frag, &changed);
    if (shaders_seen++ < 4) {
        l_info("glShaderSource(%u, %s): %u bytes%s", shader, frag ? "fragment" : "vertex",
               (unsigned)pos, t ? ", translated from GLSL ES 3.00" : "");
    }
    const GLchar *s = t ? t : src;
    GLint len = (GLint)strlen(s);
    glShaderSource(shader, 1, &s, &len);
    free(t);
    free(src);
}

static void glGetProgramiv_na(GLuint program, GLenum pname, GLint *params) {
    switch (pname) {
        case GL_ACTIVE_UNIFORM_BLOCKS_:
        case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH_:
        case GL_PROGRAM_BINARY_LENGTH_:
            *params = 0;
            return;
        default:
            glGetProgramiv(program, pname, params);
    }
}

static void glGetActiveUniformBlockiv_na(GLuint p, GLuint i, GLenum pname, GLint *params) { *params = 0; }

static void glGetActiveUniformBlockName_na(GLuint p, GLuint i, GLsizei bufsize, GLsizei *length, GLchar *name) {
    if (length) *length = 0;
    if (name && bufsize > 0) name[0] = 0;
}

static void glGetUniformIndices_na(GLuint p, GLsizei count, const GLchar *const *names, GLuint *indices) {
    for (int i = 0; i < count; i++) indices[i] = 0xFFFFFFFF; // GL_INVALID_INDEX
}

static void glGetActiveUniformsiv_na(GLuint p, GLsizei count, const GLuint *indices, GLenum pname, GLint *params) {
    for (int i = 0; i < count; i++)
        params[i] = (pname == GL_UNIFORM_BLOCK_INDEX_) ? -1 : 0;
}

static void glGetShaderPrecisionFormat_na(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) {
    switch (precisiontype) {
        case GL_LOW_FLOAT: case GL_MEDIUM_FLOAT: case GL_HIGH_FLOAT:
            range[0] = 127; range[1] = 127; *precision = 23;
            break;
        default:
            range[0] = 31; range[1] = 30; *precision = 0;
            break;
    }
}

static void glDetachShader_na(GLuint p, GLuint s) {}
static void glValidateProgram_na(GLuint p) {}
static GLboolean glIsVertexArray_na(GLuint a) { return a != 0; }
static void glProgramParameteri_na(GLuint p, GLenum pname, GLint v) {}

/*
 * Buffer targets vitaGL doesn't have
 *
 * vitaGL knows ARRAY, ELEMENT_ARRAY and UNIFORM buffers. Unity creates and
 * fills its vertex and index buffers through GL_COPY_WRITE_BUFFER (so the
 * bound VAO is left alone): vitaGL rejected those calls, the buffers never
 * got memory, and draws read their indices from address 0 (GPU crash).
 * Bindings of the other targets are kept here; operations on them run with
 * the buffer briefly bound as GL_ARRAY_BUFFER (which no VAO captures until
 * glVertexAttribPointer).
 */
extern uint32_t vertex_array_unit; // vitaGL buffers.c

typedef struct { // vitaGL's vbo (shared.h): only the leading fields
    void *ptr;
    int32_t size;
} vgl_vbo;

#define N_EXTRA_TARGETS 10
static const GLenum extra_targets[N_EXTRA_TARGETS] = {
    0x8F36, 0x8F37,         // COPY_READ, COPY_WRITE
    0x88EB, 0x88EC,         // PIXEL_PACK, PIXEL_UNPACK
    0x8C8E, 0x8F3F, 0x90EE, // TRANSFORM_FEEDBACK, DRAW_INDIRECT, DISPATCH_INDIRECT
    0x90D2, 0x92C0, 0x8C2A, // SHADER_STORAGE, ATOMIC_COUNTER, TEXTURE_BUFFER
};
static GLuint extra_bound[N_EXTRA_TARGETS];

static int extra_index(GLenum target) {
    for (int i = 0; i < N_EXTRA_TARGETS; i++)
        if (extra_targets[i] == target) return i;
    return -1;
}

#define PIXEL_UNPACK_IDX 3

static void glBindBuffer_na(GLenum target, GLuint buffer) {
    int i = extra_index(target);
    if (i >= 0) extra_bound[i] = buffer;
    else glBindBuffer(target, buffer);
}

static void glDeleteBuffers_na(GLsizei n, const GLuint *ids) {
    for (int k = 0; k < n; k++)
        for (int i = 0; i < N_EXTRA_TARGETS; i++)
            if (extra_bound[i] == ids[k]) extra_bound[i] = 0;
    glDeleteBuffers(n, ids);
}

// Runs op with target's buffer bound as GL_ARRAY_BUFFER if target is one of ours.
#define WITH_TARGET(target, op) do { \
        int i_ = extra_index(target); \
        if (i_ < 0) { op(target); break; } \
        uint32_t saved_ = vertex_array_unit; \
        vertex_array_unit = extra_bound[i_]; \
        op(GL_ARRAY_BUFFER); \
        vertex_array_unit = saved_; \
    } while (0)

static void glBufferData_na(GLenum target, GLsizei size, const void *data, GLenum usage) {
#define OP(t) glBufferData(t, size, data, usage)
    WITH_TARGET(target, OP);
#undef OP
}

static void glBufferSubData_na(GLenum target, GLintptr off, GLsizeiptr size, const void *data) {
#define OP(t) glBufferSubData(t, off, size, data)
    WITH_TARGET(target, OP);
#undef OP
}

static void *glMapBufferRange_na(GLenum target, GLintptr off, GLsizeiptr len, GLbitfield access) {
    void *r = NULL;
#define OP(t) r = glMapBufferRange(t, off, len, access)
    WITH_TARGET(target, OP);
#undef OP
    return r;
}

static GLboolean glUnmapBuffer_na(GLenum target) {
    GLboolean r = GL_FALSE;
#define OP(t) r = glUnmapBuffer(t)
    WITH_TARGET(target, OP);
#undef OP
    return r;
}

static void glFlushMappedBufferRange_na(GLenum target, GLintptr off, GLsizeiptr len) {
#define OP(t) glFlushMappedBufferRange(t, off, len)
    WITH_TARGET(target, OP);
#undef OP
}

static void glGetBufferParameteriv_na(GLenum target, GLenum pname, GLint *params) {
#define OP(t) glGetBufferParameteriv(t, pname, params)
    WITH_TARGET(target, OP);
#undef OP
}

static GLuint buffer_bound_to(GLenum target) {
    int i = extra_index(target);
    if (i >= 0) return extra_bound[i];
    if (target == GL_ARRAY_BUFFER) return vertex_array_unit;
    return 0;
}

static void glCopyBufferSubData_na(GLenum rt, GLenum wt, GLintptr roff, GLintptr woff, GLsizeiptr size) {
    vgl_vbo *src = (vgl_vbo *)buffer_bound_to(rt);
    GLuint dst = buffer_bound_to(wt);
    if (!src || !src->ptr || !dst || roff + size > src->size) {
        static int warned = 0;
        if (warned++ < 8) l_warn("glCopyBufferSubData(0x%x -> 0x%x, %d bytes): no source/destination", rt, wt, (int)size);
        return;
    }
    uint32_t saved = vertex_array_unit;
    vertex_array_unit = dst;
    glBufferSubData(GL_ARRAY_BUFFER, woff, size, (const uint8_t *)src->ptr + roff);
    vertex_array_unit = saved;
}

// Texture uploads read from the bound PIXEL_UNPACK buffer when there is one
static const void *unpack_ptr(const void *data) {
    vgl_vbo *b = (vgl_vbo *)extra_bound[PIXEL_UNPACK_IDX];
    if (!b) return data;
    return b->ptr ? (const uint8_t *)b->ptr + (uintptr_t)data : NULL;
}

/*
 * Textures: immutable storage on top of glTexImage2D
 */
typedef struct {
    GLuint tex;
    GLenum ifmt;
    GLsizei levels, w, h;
} tex_storage;

#define MAX_STORAGE 4096
static tex_storage storage[MAX_STORAGE];

static tex_storage *storage_for(GLuint tex, int create) {
    uint32_t h = (tex * 2654435761u) % MAX_STORAGE;
    for (int n = 0; n < MAX_STORAGE; n++) {
        tex_storage *s = &storage[(h + n) % MAX_STORAGE];
        if (s->tex == tex) return s;
        if (s->tex == 0) {
            if (!create) return NULL;
            s->tex = tex;
            return s;
        }
    }
    return NULL;
}

static GLuint bound_texture(GLenum target) {
    GLint t = 0;
    glGetIntegerv(target == GL_TEXTURE_CUBE_MAP || (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
                  ? GL_TEXTURE_BINDING_CUBE_MAP_ : GL_TEXTURE_BINDING_2D_, &t);
    return (GLuint)t;
}

// Sized internal format -> format/type for glTexImage2D. Returns 0 for compressed formats.
static int sized_to_unsized(GLenum ifmt, GLenum *format, GLenum *type) {
    *type = GL_UNSIGNED_BYTE;
    switch (ifmt) {
        case 0x8058: /* RGBA8 */ case 0x8C43: /* SRGB8_ALPHA8 */ case GL_RGBA:
        case 0x881A: /* RGBA16F */ case 0x8059: /* RGB10_A2 */
            *format = GL_RGBA; return 1;
        case 0x8C3A: /* R11F_G11F_B10F: HDR render targets; GXM has no 24-bit color surface */
            *format = GL_RGBA; return 1;
        case 0x8051: /* RGB8 */ case 0x8C41: /* SRGB8 */ case GL_RGB:
            *format = GL_RGB; return 1;
        case 0x8D62: /* RGB565 */
            *format = GL_RGB; *type = GL_UNSIGNED_SHORT_5_6_5; return 1;
        case 0x8056: /* RGBA4 */
            *format = GL_RGBA; *type = GL_UNSIGNED_SHORT_4_4_4_4; return 1;
        case 0x8057: /* RGB5_A1 */
            *format = GL_RGBA; *type = GL_UNSIGNED_SHORT_5_5_5_1; return 1;
        case 0x8229: /* R8 */ case 0x822D: /* R16F */
            *format = GL_LUMINANCE; return 1;
        case 0x822B: /* RG8 */
            *format = GL_LUMINANCE_ALPHA; return 1;
        case GL_ALPHA: *format = GL_ALPHA; return 1;
        case GL_LUMINANCE: *format = GL_LUMINANCE; return 1;
        case GL_LUMINANCE_ALPHA: *format = GL_LUMINANCE_ALPHA; return 1;
        case 0x81A5: /* DEPTH_COMPONENT16 */ case 0x81A6: /* DEPTH_COMPONENT24 */ case 0x8CAC: /* DEPTH_COMPONENT32F */
            *format = GL_DEPTH_COMPONENT; *type = GL_UNSIGNED_INT; return 1;
        case 0x88F0: /* DEPTH24_STENCIL8 */
            *format = GL_DEPTH_STENCIL_OES; *type = GL_UNSIGNED_INT_24_8_OES; return 1;
        default:
            return 0;
    }
}

static int tex_storage_logged = 0;
static void glTexStorage2D_na(GLenum target, GLsizei levels, GLenum ifmt, GLsizei w, GLsizei h) {
    GLuint tex = bound_texture(target);
    if (tex_storage_logged++ < 64)
        l_info("[GL] glTexStorage2D(tex %u, target 0x%x, %d levels, 0x%x, %dx%d)", tex, target, levels, ifmt, w, h);
    tex_storage *s = storage_for(tex, 1);
    if (s) {
        s->ifmt = ifmt;
        s->levels = levels;
        s->w = w;
        s->h = h;
    }
    GLenum format, type;
    if (!sized_to_unsized(ifmt, &format, &type)) {
        // Compressed: allocated on the first full-level glCompressedTexSubImage2D
        return;
    }
    int faces = target == GL_TEXTURE_CUBE_MAP ? 6 : 1;
    for (int f = 0; f < faces; f++) {
        GLenum t = faces == 6 ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + f : target;
        for (int l = 0; l < levels; l++) {
            GLsizei lw = w >> l, lh = h >> l;
            if (lw < 1) lw = 1;
            if (lh < 1) lh = 1;
            glTexImage2D(t, l, format, lw, lh, 0, format, type, NULL);
            break; // mipmaps are off (see "Mipmaps" below)
        }
    }
}

static void glCompressedTexSubImage2D_na(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                         GLenum format, GLsizei size, const void *data) {
    if (level > 0) return; // mipmaps are off
    data = unpack_ptr(data);
    if (!data) return;
    tex_storage *s = storage_for(bound_texture(target), 0);
    GLsizei lw = s ? s->w >> level : w, lh = s ? s->h >> level : h;
    if (lw < 1) lw = 1;
    if (lh < 1) lh = 1;
    if (x == 0 && y == 0 && w >= lw && h >= lh) {
        glCompressedTexImage2D(target, level, format, w, h, 0, size, data);
    } else {
        static int warned = 0;
        if (warned++ < 8)
            l_warn("glCompressedTexSubImage2D: partial update (%d,%d %dx%d of %dx%d) ignored", x, y, w, h, lw, lh);
    }
}

static void glGetTexLevelParameteriv_na(GLenum target, GLint level, GLenum pname, GLint *params) { *params = 0; }
static void glGetTexLevelParameterfv_na(GLenum target, GLint level, GLenum pname, GLfloat *params) { *params = 0; }
static void glGetTexParameteriv_na(GLenum target, GLenum pname, GLint *params) { *params = 0; }

static void glGetInternalformativ_na(GLenum target, GLenum ifmt, GLenum pname, GLsizei bufsize, GLint *params) {
    if (pname == GL_NUM_SAMPLE_COUNTS_ && bufsize > 0) params[0] = 0;
}

/*
 * Framebuffers
 */
static GLfloat clear_color[4] = { 0, 0, 0, 0 };
static GLfloat clear_depth = 1.0f;
static GLint clear_stencil = 0;

static void glClearColor_na(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    clear_color[0] = r; clear_color[1] = g; clear_color[2] = b; clear_color[3] = a;
    glClearColor(r, g, b, a);
}

static void glClearDepthf_na(GLfloat d) {
    clear_depth = d;
    glClearDepthf(d);
}

static void glClearStencil_na(GLint s) {
    clear_stencil = s;
    glClearStencil(s);
}

static void glClearBufferfv_na(GLenum buffer, GLint drawbuffer, const GLfloat *v) {
    if (buffer == GL_COLOR_) {
        glClearColor(v[0], v[1], v[2], v[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    } else if (buffer == GL_DEPTH_) {
        glClearDepthf(v[0]);
        glClear(GL_DEPTH_BUFFER_BIT);
        glClearDepthf(clear_depth);
    }
}

static void glClearBufferiv_na(GLenum buffer, GLint drawbuffer, const GLint *v) {
    if (buffer == GL_STENCIL_) {
        glClearStencil(v[0]);
        glClear(GL_STENCIL_BUFFER_BIT);
        glClearStencil(clear_stencil);
    }
}

static void glClearBufferuiv_na(GLenum buffer, GLint drawbuffer, const GLuint *v) {
    if (buffer == GL_COLOR_) {
        GLfloat f[4] = { v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f };
        glClearBufferfv_na(buffer, drawbuffer, f);
    }
}

static void glClearBufferfi_na(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
    glClearDepthf(depth);
    glClearStencil(stencil);
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glClearDepthf(clear_depth);
    glClearStencil(clear_stencil);
}

static void glDrawBuffers_na(GLsizei n, const GLenum *bufs) {}
static void glInvalidateFramebuffer_na(GLenum target, GLsizei n, const GLenum *attachments) {}

/*
 * Mipmaps
 *
 * vitaGL's mip chains for runtime-recompressed (ETC2 -> DXT5, padded to a
 * power of two) and immutable-storage textures don't match what it tells
 * the GPU when a sampler asks for mipmaps, and the GPU then reads past the
 * texture: a GPU crash on the first frame of the game scene. A 2D game
 * loses next to nothing without them, so only level 0 is kept and the
 * mipmapped minification filters become their base filter.
 */
static GLint base_filter(GLint f) {
    switch (f) {
        case 0x2700: case 0x2702: return GL_NEAREST; // NEAREST_MIPMAP_NEAREST/LINEAR
        case 0x2701: case 0x2703: return GL_LINEAR;  // LINEAR_MIPMAP_NEAREST/LINEAR
        default: return f;
    }
}

static void glTexParameteri_na(GLenum target, GLenum pname, GLint v) {
    if (pname == GL_TEXTURE_MIN_FILTER) v = base_filter(v);
    if (pname == 0x813C || pname == 0x813D || pname == 0x813A || pname == 0x813B) return; // BASE/MAX_LEVEL, MIN/MAX_LOD
    glTexParameteri(target, pname, v);
}

static void glTexParameterf_na(GLenum target, GLenum pname, GLfloat v) {
    if (pname == GL_TEXTURE_MIN_FILTER) {
        glTexParameteri_na(target, pname, (GLint)v);
        return;
    }
    if (pname == 0x813C || pname == 0x813D || pname == 0x813A || pname == 0x813B) return;
    glTexParameterf(target, pname, v);
}

static void glTexParameteriv_na(GLenum target, GLenum pname, const GLint *v) {
    glTexParameteri_na(target, pname, v[0]);
}

static void glSamplerParameteri_na(GLuint smp, GLenum pname, GLint v) {
    if (pname == GL_TEXTURE_MIN_FILTER) v = base_filter(v);
    if (pname == 0x813A || pname == 0x813B) return; // MIN/MAX_LOD
    glSamplerParameteri(smp, pname, v);
}

static void glSamplerParameterf_na(GLuint smp, GLenum pname, GLfloat v) {
    if (pname == GL_TEXTURE_MIN_FILTER) {
        glSamplerParameteri_na(smp, pname, (GLint)v);
        return;
    }
    if (pname == 0x813A || pname == 0x813B) return;
    glSamplerParameterf(smp, pname, v);
}

static void glTexImage2D_na(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum format,
                            GLenum type, const void *data) {
    if (level > 0) return;
    glTexImage2D(target, level, ifmt, w, h, border, format, type, unpack_ptr(data));
}

static void glTexSubImage2D_na(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                               GLenum type, const void *data) {
    if (level > 0) return;
    data = unpack_ptr(data);
    if (!data) return;
    glTexSubImage2D(target, level, x, y, w, h, format, type, data);
}

static void glCompressedTexImage2D_na(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border,
                                      GLsizei size, const void *data) {
    if (level > 0) return;
    data = unpack_ptr(data);
    if (!data) return;
    glCompressedTexImage2D(target, level, ifmt, w, h, border, size, data);
}

static void glGenerateMipmap_na(GLenum target) {}

/*
 * Render targets
 *
 * vitaGL only knows depth/stencil renderbuffers. Unity renders into color
 * renderbuffers (MSAA or not) and resolves them with glBlitFramebuffer, so
 * a color renderbuffer is backed by a texture and attached as one;
 * otherwise the framebuffer has no color surface and the GPU writes to
 * address 0.
 */
typedef struct {
    GLuint rb;
    GLuint tex;
    GLenum ifmt;
    GLsizei w, h;
} rb_info;

#define MAX_RBS 256
static rb_info rbs[MAX_RBS];
static GLuint bound_rb = 0;
static int gl_trace_left = 400; // FBO setup lines logged

#define TRACE(...) do { if (gl_trace_left > 0) { gl_trace_left--; l_info("[GL] " __VA_ARGS__); } } while (0)

static rb_info *rb_for(GLuint rb, int create) {
    if (!rb) return NULL;
    rb_info *free_slot = NULL;
    for (int i = 0; i < MAX_RBS; i++) {
        if (rbs[i].rb == rb) return &rbs[i];
        if (!rbs[i].rb && !free_slot) free_slot = &rbs[i];
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->rb = rb;
    return free_slot;
}

static int is_color_format(GLenum ifmt) {
    switch (ifmt) {
        case 0x81A5: case 0x81A6: case 0x81A7: case 0x8CAC: // DEPTH_COMPONENT16/24/32/32F
        case 0x88F0: case 0x8CAD: case 0x8D48: case GL_DEPTH_COMPONENT: // DEPTH24_STENCIL8, DEPTH32F_STENCIL8, STENCIL_INDEX8
        case 0x84F9: // DEPTH_STENCIL
            return 0;
        default:
            return 1;
    }
}

static void glBindRenderbuffer_na(GLenum target, GLuint rb) {
    bound_rb = rb;
    glBindRenderbuffer(target, rb);
}

static void glRenderbufferStorage_na(GLenum target, GLenum ifmt, GLsizei w, GLsizei h) {
    TRACE("glRenderbufferStorage(rb %u, 0x%x, %dx%d)", bound_rb, ifmt, w, h);
    rb_info *r = rb_for(bound_rb, 1);
    if (r) {
        r->ifmt = ifmt;
        r->w = w;
        r->h = h;
    }
    if (!is_color_format(ifmt) || !r) {
        glRenderbufferStorage(target, ifmt, w, h);
        return;
    }
    GLenum format, type;
    if (!sized_to_unsized(ifmt, &format, &type)) {
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    }
    GLint old = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_, &old);
    if (!r->tex) glGenTextures(1, &r->tex);
    glBindTexture(GL_TEXTURE_2D, r->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old);
}

static void glRenderbufferStorageMultisample_na(GLenum target, GLsizei samples, GLenum ifmt, GLsizei w, GLsizei h) {
    glRenderbufferStorage_na(target, ifmt, w, h);
}

static void glFramebufferRenderbuffer_na(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb) {
    rb_info *r = rb_for(rb, 0);
    TRACE("glFramebufferRenderbuffer(0x%x, rb %u%s)", attachment, rb, r && r->tex ? " as texture" : "");
    if (r && r->tex) {
        glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, r->tex, 0);
        return;
    }
    if (attachment == GL_COLOR_ATTACHMENT0 && rb == 0) {
        glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, 0, 0);
        return;
    }
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 16) return; // MRT: unsupported
    glFramebufferRenderbuffer(target, attachment, rbtarget, rb);
}

static void glDeleteRenderbuffers_na(GLsizei n, const GLuint *ids) {
    for (int i = 0; i < n; i++) {
        rb_info *r = rb_for(ids[i], 0);
        if (r) {
            if (r->tex) glDeleteTextures(1, &r->tex);
            memset(r, 0, sizeof(*r));
        }
        if (ids[i] == bound_rb) bound_rb = 0;
    }
    glDeleteRenderbuffers(n, ids);
}

static void glGetRenderbufferParameteriv_na(GLenum target, GLenum pname, GLint *params) {
    rb_info *r = rb_for(bound_rb, 0);
    switch (pname) {
        case 0x8D42: *params = r ? r->w : 0; break;       // WIDTH
        case 0x8D43: *params = r ? r->h : 0; break;       // HEIGHT
        case 0x8D44: *params = r ? r->ifmt : 0; break;    // INTERNAL_FORMAT
        case 0x8CAB: *params = 1; break;                  // SAMPLES
        default: *params = 0; break;
    }
}

static void glFramebufferTexture2D_na(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level) {
    TRACE("glFramebufferTexture2D(0x%x, 0x%x, tex %u, level %d)", attachment, textarget, tex, level);
    if (attachment > GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 16) return; // MRT: unsupported
    if (attachment != GL_COLOR_ATTACHMENT0) return; // depth textures: vitaGL keeps its own depth buffer
    if (textarget != GL_TEXTURE_2D) {
        static int warned = 0;
        if (warned++ < 8) l_warn("glFramebufferTexture2D: target 0x%x not supported", textarget);
        return;
    }
    glFramebufferTexture2D(target, attachment, textarget, tex, level);
}

static void glFramebufferTexture_na(GLenum target, GLenum attachment, GLuint tex, GLint level) {
    glFramebufferTexture2D_na(target, attachment, GL_TEXTURE_2D, tex, level);
}

static GLenum glCheckFramebufferStatus_na(GLenum target) {
    GLenum r = glCheckFramebufferStatus(target);
    if (r != GL_FRAMEBUFFER_COMPLETE) TRACE("glCheckFramebufferStatus(0x%x) = 0x%x", target, r);
    return r;
}

static void glBlitFramebuffer_na(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                 GLbitfield mask, GLenum filter) {
    static int logged_blits = 0;
    if (logged_blits++ < 8)
        l_info("[GL] glBlitFramebuffer(%d,%d-%d,%d -> %d,%d-%d,%d, mask 0x%x)", sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask);
    if (!(mask & GL_COLOR_BUFFER_BIT)) return; // depth/stencil blits: not supported by vitaGL
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT, filter);
}

static void glColorMaski_na(GLuint i, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (i == 0) glColorMask(r, g, b, a);
}

static void glBlendEquationi_na(GLuint i, GLenum mode) {
    if (i == 0) glBlendEquation(mode);
}

static void glBlendEquationSeparatei_na(GLuint i, GLenum rgb, GLenum alpha) {
    if (i == 0) glBlendEquationSeparate(rgb, alpha);
}

static void glBlendFuncSeparatei_na(GLuint i, GLenum sr, GLenum dr, GLenum sa, GLenum da) {
    if (i == 0) glBlendFuncSeparate(sr, dr, sa, da);
}

/*
 * Vertex input / uniforms
 */
static void glVertexAttribIPointer_na(GLuint index, GLint size, GLenum type, GLsizei stride, const void *ptr) {
    glVertexAttribPointer(index, size, type, GL_FALSE, stride, ptr);
}

#define UIV(n) \
    static void glUniform##n##uiv_na(GLint loc, GLsizei count, const GLuint *v) { \
        glUniform##n##iv(loc, count, (const GLint *)v); \
    }
UIV(1) UIV(2) UIV(3) UIV(4)

static void glGetUniformiv_na(GLuint p, GLint loc, GLint *params) { *params = 0; }

/*
 * Draw validation
 *
 * A draw that reads past a vertex or index buffer faults on the GPU ("GPU
 * crash", no CPU stack to look at). Every draw is checked against vitaGL's
 * own buffer state first: out-of-range draws are skipped and logged. The
 * first draws of the game scene are also logged one by one and followed by
 * glFinish, so a crash that gets past the checks still names its draw.
 */
typedef struct {
    uint16_t streamIndex, offset;
    uint8_t format, componentCount;
    uint16_t regIndex;
} vgl_attr;

typedef struct {
    uint16_t stride, indexSource;
} vgl_stream;

typedef struct { // vitaGL's vao (shared.h), VERTEX_ATTRIBS_NUM = 16
    uint32_t index_array_unit;
    uint8_t vertex_attrib_size[16];
    uint32_t vertex_attrib_offsets[16];
    uint32_t vertex_attrib_vbo[16];
    uint32_t vertex_attrib_state;
    uint32_t vertex_attrib_divisor;
    float *vertex_attrib_value[16];
    vgl_attr vertex_attrib_config[16];
    vgl_stream vertex_stream_config[16];
} vgl_vao;

extern vgl_vao *cur_vao; // vitaGL buffers.c
extern volatile int na_egl_swap_count; // na_egl.c

static GLuint cur_program = 0;
static int draw_no = 0, draws_traced = 0, bad_draws = 0;
#define TRACE_FROM_SWAP 3   // the game scene starts after the third swap
#define TRACED_DRAWS 150

static void glUseProgram_na(GLuint p) {
    cur_program = p;
    glUseProgram(p);
}

static uint32_t max_index(const void *p, GLsizei count, GLenum type) {
    uint32_t m = 0;
    if (type == GL_UNSIGNED_SHORT) {
        const uint16_t *s = p;
        for (GLsizei i = 0; i < count; i++) if (s[i] > m) m = s[i];
    } else if (type == GL_UNSIGNED_INT) {
        const uint32_t *s = p;
        for (GLsizei i = 0; i < count; i++) if (s[i] > m) m = s[i];
    } else {
        const uint8_t *s = p;
        for (GLsizei i = 0; i < count; i++) if (s[i] > m) m = s[i];
    }
    return m;
}

// Returns 1 if the draw stays inside its buffers; desc gets a short description.
static int check_draw(GLsizei count, GLenum type, const void *indices, GLint base, GLint first, int elements,
                      char *desc, size_t desc_size) {
    if (count <= 0) return 0;
    uint32_t last;
    if (elements) {
        int isz = type == GL_UNSIGNED_INT ? 4 : type == GL_UNSIGNED_SHORT ? 2 : 1;
        vgl_vbo *ib = (vgl_vbo *)cur_vao->index_array_unit;
        const void *src;
        if (ib) {
            uint32_t off = (uint32_t)(uintptr_t)indices;
            if (!ib->ptr || off + (uint32_t)count * isz > (uint32_t)ib->size) {
                if (bad_draws++ < 40)
                    l_warn("[GL] draw %d skipped (prog %u): %d indices at +%u past index buffer of %d bytes (ptr %p)",
                           draw_no, cur_program, count, off, ib->size, ib->ptr);
                return 0;
            }
            src = (const uint8_t *)ib->ptr + off;
        } else {
            src = indices;
            if ((uintptr_t)src < 0x81000000) {
                if (bad_draws++ < 40)
                    l_warn("[GL] draw %d skipped (prog %u): no index buffer, indices %p", draw_no, cur_program, indices);
                return 0;
            }
        }
        last = max_index(src, count, type) + base;
    } else {
        last = first + count - 1;
    }
    snprintf(desc, desc_size, "count %d, last vertex %u", count, last);

    uint32_t state = cur_vao->vertex_attrib_state;
    for (int i = 0; i < 16; i++) {
        if (!(state & (1u << i))) continue;
        uint32_t id = cur_vao->vertex_attrib_vbo[i];
        uint32_t off = cur_vao->vertex_attrib_offsets[i];
        uint32_t stride = cur_vao->vertex_stream_config[i].stride;
        uint32_t comps = cur_vao->vertex_attrib_config[i].componentCount;
        if (!id) {
            if (off < 0x81000000) {
                if (bad_draws++ < 40)
                    l_warn("[GL] draw %d skipped (prog %u): attrib %d enabled without buffer (pointer 0x%x)",
                           draw_no, cur_program, i, off);
                return 0;
            }
            continue; // client-side array
        }
        vgl_vbo *vb = (vgl_vbo *)id;
        uint32_t divided = cur_vao->vertex_attrib_divisor & (1u << i);
        uint32_t need = off + (divided ? 0 : last) * stride + comps;
        if (!vb->ptr || need > (uint32_t)vb->size) {
            if (bad_draws++ < 40)
                l_warn("[GL] draw %d skipped (prog %u): attrib %d needs %u bytes (offset %u, stride %u, last vertex %u), "
                       "buffer has %d (ptr %p)", draw_no, cur_program, i, need, off, stride, last, vb->size, vb->ptr);
            return 0;
        }
    }
    return 1;
}

static int trace_this_draw(void) {
    return na_egl_swap_count >= TRACE_FROM_SWAP && draws_traced < TRACED_DRAWS;
}

static void traced(const char *what, const char *desc) {
    draws_traced++;
    l_info("[GL] draw %d: %s prog %u, %s, vao attribs 0x%x", draw_no, what, cur_program, desc,
           cur_vao->vertex_attrib_state);
}

static void traced_done(void) {
    glFinish();
    l_info("[GL] draw %d ok", draw_no);
}

static void glDrawArrays_na(GLenum mode, GLint first, GLsizei count) {
    char desc[96];
    draw_no++;
    if (!check_draw(count, 0, NULL, 0, first, 0, desc, sizeof(desc))) return;
    int t = trace_this_draw();
    if (t) traced("DrawArrays", desc);
    glDrawArrays(mode, first, count);
    if (t) traced_done();
}

static void glDrawArraysInstanced_na(GLenum mode, GLint first, GLsizei count, GLsizei n) {
    char desc[96];
    draw_no++;
    if (!check_draw(count, 0, NULL, 0, first, 0, desc, sizeof(desc))) return;
    int t = trace_this_draw();
    if (t) traced("DrawArraysInstanced", desc);
    glDrawArraysInstanced(mode, first, count, n);
    if (t) traced_done();
}

static void glDrawElements_na(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    char desc[96];
    draw_no++;
    if (!check_draw(count, type, indices, 0, 0, 1, desc, sizeof(desc))) return;
    int t = trace_this_draw();
    if (t) traced("DrawElements", desc);
    glDrawElements(mode, count, type, indices);
    if (t) traced_done();
}

static void glDrawElementsBaseVertex_na(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint base) {
    char desc[96];
    draw_no++;
    if (!check_draw(count, type, indices, base, 0, 1, desc, sizeof(desc))) return;
    int t = trace_this_draw();
    if (t) traced("DrawElementsBaseVertex", desc);
    if (base == 0)
        glDrawElements(mode, count, type, indices);
    else
        glDrawElementsBaseVertex(mode, count, type, indices, base);
    if (t) traced_done();
}

static void glDrawElementsInstanced_na(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei n) {
    char desc[96];
    draw_no++;
    if (!check_draw(count, type, indices, 0, 0, 1, desc, sizeof(desc))) return;
    int t = trace_this_draw();
    if (t) traced("DrawElementsInstanced", desc);
    glDrawElementsInstanced(mode, count, type, indices, n);
    if (t) traced_done();
}

static void glDrawElementsInstancedBaseVertex_na(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                                 GLsizei instances, GLint basevertex) {
    if (basevertex == 0)
        glDrawElementsInstanced_na(mode, count, type, indices, instances);
    else
        glDrawElementsBaseVertex_na(mode, count, type, indices, basevertex);
}

/*
 * Sync objects: vitaGL submits in order, so fences are always signaled.
 */
static void *glFenceSync_na(GLenum cond, GLbitfield flags) { return (void *)1; }
static GLenum glClientWaitSync_na(void *sync, GLbitfield flags, uint64_t timeout) { return GL_ALREADY_SIGNALED_; }
static void glWaitSync_na(void *sync, GLbitfield flags, uint64_t timeout) {}
static void glDeleteSync_na(void *sync) {}
static GLboolean glIsSync_na(void *sync) { return sync != NULL; }

/*
 * Stubs: log the first call, return 0
 */
#define STUB(name) \
    static int name##_stub(void) { \
        static int called = 0; \
        if (!called++) l_warn("GL stub called: " #name); \
        return 0; \
    }

STUB(glTexImage3D) STUB(glTexSubImage3D) STUB(glTexStorage3D) STUB(glCompressedTexImage3D)
STUB(glCompressedTexSubImage3D) STUB(glFramebufferTextureLayer) STUB(glFramebufferTexture3D)
STUB(glCopyImageSubData) STUB(glTexBuffer) STUB(glTexStorage2DMultisample)
STUB(glTexStorage3DMultisample) STUB(glTexImage2DMultisample) STUB(glFramebufferTexture2DMultisample)
STUB(glResolveMultisampleFramebuffer) STUB(glTexPageCommitment) STUB(glTextureView)
STUB(glGetTextureParameteriv) STUB(glDispatchCompute) STUB(glDispatchComputeIndirect)
STUB(glMemoryBarrier) STUB(glBindImageTexture) STUB(glDrawArraysIndirect) STUB(glDrawElementsIndirect)
STUB(glPatchParameteri) STUB(glGetProgramInterfaceiv) STUB(glGetProgramResourceIndex)
STUB(glGetProgramResourceName) STUB(glGetProgramResourceiv) STUB(glGenQueriesEXT) STUB(glDeleteQueriesEXT)
STUB(glBeginQueryEXT) STUB(glEndQueryEXT) STUB(glGetQueryiv) STUB(glGetQueryObjectui64v)
STUB(glProgramUniformExtra) STUB(glBlendBarrier) STUB(glMultiview) STUB(glDebug)

typedef struct {
    const char *name;
    void *func;
} gl_entry;

#define E(n) { #n, (void *)&n##_na }
#define S(n, s) { #n, (void *)&s##_stub }
#define HIDE(n) { #n, NULL }

static const gl_entry gl_table[] = {
    E(glGetString), E(glGetStringi), E(glGetIntegerv), E(glGetInteger64v), E(glGetFloatv), E(glGetBooleanv),
    E(glCreateShader), E(glShaderSource), E(glGetProgramiv),
    E(glGetActiveUniformBlockiv), E(glGetActiveUniformBlockName), E(glGetUniformIndices),
    E(glGetActiveUniformsiv), E(glGetShaderPrecisionFormat), E(glDetachShader), E(glValidateProgram),
    E(glIsVertexArray), E(glProgramParameteri),
    E(glTexStorage2D), { "glTexStorage2DEXT", (void *)&glTexStorage2D_na },
    E(glCompressedTexSubImage2D), E(glGetTexLevelParameteriv), E(glGetTexLevelParameterfv),
    E(glGetTexParameteriv), E(glGetInternalformativ),
    E(glClearColor), E(glClearDepthf), E(glClearStencil),
    E(glClearBufferfv), E(glClearBufferiv), E(glClearBufferuiv), E(glClearBufferfi),
    E(glDrawBuffers), E(glInvalidateFramebuffer), E(glRenderbufferStorageMultisample),
    { "glRenderbufferStorageMultisampleEXT", (void *)&glRenderbufferStorageMultisample_na },
    { "glRenderbufferStorageMultisampleIMG", (void *)&glRenderbufferStorageMultisample_na },
    { "glRenderbufferStorageMultisampleAPPLE", (void *)&glRenderbufferStorageMultisample_na },
    E(glGetRenderbufferParameteriv), E(glBindRenderbuffer), E(glRenderbufferStorage),
    E(glFramebufferRenderbuffer), E(glDeleteRenderbuffers), E(glFramebufferTexture2D), E(glFramebufferTexture),
    { "glFramebufferTextureEXT", (void *)&glFramebufferTexture_na }, { "glFramebufferTextureOES", (void *)&glFramebufferTexture_na },
    E(glCheckFramebufferStatus), E(glBlitFramebuffer),
    E(glTexParameteri), E(glTexParameterf), E(glTexParameteriv), E(glSamplerParameteri), E(glSamplerParameterf),
    E(glTexImage2D), E(glTexSubImage2D), E(glCompressedTexImage2D), E(glGenerateMipmap),
    { "glGenerateMipmapOES", (void *)&glGenerateMipmap_na }, { "glGenerateMipmapEXT", (void *)&glGenerateMipmap_na },
    E(glColorMaski), E(glBlendEquationi), E(glBlendEquationSeparatei),
    E(glBlendFuncSeparatei),
    E(glVertexAttribIPointer), E(glUniform1uiv), E(glUniform2uiv), E(glUniform3uiv), E(glUniform4uiv),
    E(glGetUniformiv), E(glDrawElementsInstancedBaseVertex),
    { "glDrawElementsInstancedBaseVertexEXT", (void *)&glDrawElementsInstancedBaseVertex_na },
    { "glDrawElementsInstancedBaseVertexOES", (void *)&glDrawElementsInstancedBaseVertex_na },
    E(glDrawArrays), E(glDrawArraysInstanced), E(glDrawElements), E(glDrawElementsBaseVertex),
    E(glDrawElementsInstanced), E(glUseProgram),
    E(glBindBuffer), E(glDeleteBuffers), E(glBufferData), E(glBufferSubData), E(glMapBufferRange),
    { "glMapBufferRangeEXT", (void *)&glMapBufferRange_na }, E(glUnmapBuffer),
    { "glUnmapBufferOES", (void *)&glUnmapBuffer_na }, E(glFlushMappedBufferRange),
    { "glFlushMappedBufferRangeEXT", (void *)&glFlushMappedBufferRange_na }, E(glGetBufferParameteriv),
    E(glCopyBufferSubData),
    { "glDrawElementsBaseVertexEXT", (void *)&glDrawElementsBaseVertex_na },
    { "glDrawElementsBaseVertexOES", (void *)&glDrawElementsBaseVertex_na },
    { "glDrawArraysInstancedEXT", (void *)&glDrawArraysInstanced_na },
    { "glDrawElementsInstancedEXT", (void *)&glDrawElementsInstanced_na },
    E(glFenceSync), E(glClientWaitSync), E(glWaitSync), E(glDeleteSync), E(glIsSync),

    // Program binaries lose vitaGL's attribute bindings: let Unity compile from source
    HIDE(glGetProgramBinary), HIDE(glProgramBinary), HIDE(glGetProgramBinaryOES), HIDE(glProgramBinaryOES),

    // Stubs
    S(glTexImage3D, glTexImage3D), S(glTexSubImage3D, glTexSubImage3D), S(glTexStorage3D, glTexStorage3D),
    S(glTexStorage3DEXT, glTexStorage3D), S(glCompressedTexImage3D, glCompressedTexImage3D),
    S(glCompressedTexSubImage3D, glCompressedTexSubImage3D),
    S(glFramebufferTextureLayer, glFramebufferTextureLayer), S(glFramebufferTexture3D, glFramebufferTexture3D),
    S(glCopyImageSubData, glCopyImageSubData), S(glCopyImageSubDataEXT, glCopyImageSubData),
    S(glCopyImageSubDataOES, glCopyImageSubData),
    S(glTexBuffer, glTexBuffer), S(glTexBufferEXT, glTexBuffer),
    S(glTexStorage2DMultisample, glTexStorage2DMultisample), S(glTexStorage3DMultisample, glTexStorage3DMultisample),
    S(glTexImage2DMultisample, glTexImage2DMultisample),
    S(glFramebufferTexture2DMultisampleEXT, glFramebufferTexture2DMultisample),
    S(glFramebufferTexture2DMultisampleIMG, glFramebufferTexture2DMultisample),
    S(glResolveMultisampleFramebufferAPPLE, glResolveMultisampleFramebuffer),
    S(glTexPageCommitmentARB, glTexPageCommitment), S(glTexPageCommitmentEXT, glTexPageCommitment),
    S(glTextureView, glTextureView), S(glTextureViewEXT, glTextureView), S(glTextureViewOES, glTextureView),
    S(glGetTextureParameteriv, glGetTextureParameteriv),
    S(glDispatchCompute, glDispatchCompute), S(glDispatchComputeIndirect, glDispatchComputeIndirect),
    S(glMemoryBarrier, glMemoryBarrier), S(glBindImageTexture, glBindImageTexture),
    S(glDrawArraysIndirect, glDrawArraysIndirect), S(glDrawElementsIndirect, glDrawElementsIndirect),
    S(glPatchParameteri, glPatchParameteri), S(glPatchParameteriEXT, glPatchParameteri),
    S(glPatchParameteriOES, glPatchParameteri),
    S(glGetProgramInterfaceiv, glGetProgramInterfaceiv), S(glGetProgramResourceIndex, glGetProgramResourceIndex),
    S(glGetProgramResourceName, glGetProgramResourceName), S(glGetProgramResourceiv, glGetProgramResourceiv),
    S(glGenQueriesEXT, glGenQueriesEXT), S(glDeleteQueriesEXT, glDeleteQueriesEXT),
    S(glBeginQueryEXT, glBeginQueryEXT), S(glEndQueryEXT, glEndQueryEXT), S(glGetQueryiv, glGetQueryiv),
    S(glGetQueryObjectui64vEXT, glGetQueryObjectui64v), S(glGetQueryObjectui64vNV, glGetQueryObjectui64v),
    S(glProgramUniform1uiv, glProgramUniformExtra), S(glProgramUniform2uiv, glProgramUniformExtra),
    S(glProgramUniform3uiv, glProgramUniformExtra), S(glProgramUniform4uiv, glProgramUniformExtra),
    S(glProgramUniformMatrix2x3fv, glProgramUniformExtra), S(glProgramUniformMatrix2x4fv, glProgramUniformExtra),
    S(glProgramUniformMatrix3x2fv, glProgramUniformExtra), S(glProgramUniformMatrix3x4fv, glProgramUniformExtra),
    S(glProgramUniformMatrix4x2fv, glProgramUniformExtra), S(glProgramUniformMatrix4x3fv, glProgramUniformExtra),
    S(glBlendBarrier, glBlendBarrier), S(glBlendBarrierKHR, glBlendBarrier), S(glBlendBarrierNV, glBlendBarrier),
    S(glFramebufferTextureMultiviewOVR, glMultiview), S(glFramebufferTextureMultisampleMultiviewOVR, glMultiview),
    S(glDebugMessageCallback, glDebug), S(glDebugMessageCallbackKHR, glDebug),
    S(glDebugMessageControl, glDebug), S(glDebugMessageControlKHR, glDebug),
    S(glDebugMessageInsert, glDebug), S(glDebugMessageInsertKHR, glDebug),
    S(glObjectLabel, glDebug), S(glObjectLabelKHR, glDebug), S(glLabelObjectEXT, glDebug),
    S(glGetObjectLabel, glDebug), S(glGetObjectLabelKHR, glDebug), S(glGetObjectLabelEXT, glDebug),
    S(glPushDebugGroup, glDebug), S(glPushDebugGroupKHR, glDebug),
    S(glPopDebugGroup, glDebug), S(glPopDebugGroupKHR, glDebug),
    S(glPushGroupMarkerEXT, glDebug), S(glPopGroupMarkerEXT, glDebug),
    S(eglCreateSyncKHR, glDebug), S(eglDestroySyncKHR, glDebug), S(eglClientWaitSyncKHR, glDebug),
    S(eglGetFrameTimestampSupportedANDROID, glDebug), S(eglGetFrameTimestampsANDROID, glDebug),
    S(eglGetNextFrameIdANDROID, glDebug),
};

#define N_GL_TABLE ((int)(sizeof(gl_table) / sizeof(gl_table[0])))

// Each name is logged once: the log then lists every GL function Unity uses.
#define MAX_LOGGED 512
static const char *logged[MAX_LOGGED];
static int n_logged = 0;

static void log_lookup(const char *name, const char *how) {
    for (int i = 0; i < n_logged; i++)
        if (strcmp(logged[i], name) == 0) return;
    if (n_logged < MAX_LOGGED) logged[n_logged++] = strdup(name);
    l_info("[GL] %s: %s", name, how);
}

void *na_gl_get_proc(const char *name) {
    if (!name || (strncmp(name, "gl", 2) != 0 && strncmp(name, "egl", 3) != 0))
        return NULL;
    for (int i = 0; i < N_GL_TABLE; i++) {
        if (strcmp(name, gl_table[i].name) == 0) {
            log_lookup(name, gl_table[i].func ? "loader" : "hidden");
            return gl_table[i].func;
        }
    }
    if (strncmp(name, "egl", 3) == 0) return NULL;
    void *f = vglGetProcAddress(name);
    log_lookup(name, f ? "vitaGL" : "MISSING");
    return f;
}

void na_gles3_init(void) {
    // ETC2 textures (most of the game) are decoded and re-compressed to DXT5
    // instead of being stored as RGBA8 (4x the memory).
    glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);
}
