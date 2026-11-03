/* ==================================================================
 *  vfastgltf.c — vlcrlib fast glTF 2.0 / GLB loader
 * ==================================================================
 *
 * A self-contained, zero-dependency glTF 2.0 reader. It parses GLB
 * containers and .gltf JSON directly (own minimal JSON reader) and
 * uploads meshes straight to the GPU through the internal
 * vgraphics_* builders.
 *
 * Design goals, in order:
 *   1. Never read out of bounds. Every accessor offset, stride, count,
 *      bufferView and index is validated against the real buffer length
 *      before use, so a malformed or hostile file fails cleanly instead
 *      of corrupting memory.
 *   2. Be fast. One JSON pass, no parsed-JSON DOM, no per-load stderr
 *      chatter, and one GPU upload per glTF mesh.
 *   3. Keep glTF instancing. A mesh referenced by N nodes is uploaded
 *      once and drawn N times.
 *
 * Supported: POSITION / NORMAL / TEXCOORD_0, all component types,
 * interleaved and strided accessors, 8/16/32-bit indices, non-indexed
 * primitives, node hierarchies with TRS or matrix transforms, the
 * metallic-roughness material set (base colour, metallic-roughness,
 * normal, occlusion, emissive maps), and embedded / external /
 * data-URI buffers and images.
 *
 * Not supported (tolerated and skipped): skins, animations, morph
 * targets, sparse accessors, KHR_draco_mesh_compression, vertex
 * colours, TEXCOORD_1 and above, non-triangle primitive modes.
 */

#include "vfastgltf.h"
#include "vgraphics.h"
#include "vtextures.h"
#include "vfiles.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================== */
/*  Utilities                                                          */
/* ================================================================== */

#define FG_MAX_DEPTH  256          /* scene-graph recursion limit */
#define FG_JSON_DEPTH 64           /* JSON nesting limit          */
#define FG_MAX_VERTS  100000000    /* sanity cap per primitive    */

static float clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static unsigned char linear_to_srgb8(float v) {
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f)   return 255;
    float s = (v <= 0.0031308f) ? v * 12.92f
                                : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    return (unsigned char)(s * 255.0f + 0.5f);
}

static int fg_component_size(int type) {
    switch (type) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: return 0;
    }
}

static const char *const FG_TYPE_NAMES[7] = {
    "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"
};

static int fg_type_count(int typeIndex) {
    switch (typeIndex) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 3;
        case 3: return 4;
        case 4: return 4;
        case 5: return 9;
        case 6: return 16;
        default: return 0;
    }
}

/* Match a JVal string against a NUL-terminated literal. */
static bool str_eq(const char *text, const char *s, int len, const char *lit) {
    size_t n = strlen(lit);
    return (size_t)len == n && memcmp(s, lit, n) == 0;
}

/* ================================================================== */
/*  Minimal JSON reader                                                */
/*                                                                     */
/*  JVal is a small value handle. Strings stay in the source buffer    */
/*  (zero-copy) and are decoded on demand. Containers store raw byte   */
/*  spans, so a container's members can be re-walked cheaply.          */
/* ================================================================== */

typedef enum JType { J_NULL = 0, J_TRUE, J_FALSE, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal {
    JType  type;
    int    start;    /* string: content offset; container: '{'/'[' offset */
    int    len;      /* string: content length;  container: span length  */
    bool   escaped;
    double num;
} JVal;

typedef struct JDoc {
    const char *text;
    int         len;
    int         pos;
    int         depth;
    bool        error;
} JDoc;

static JVal jv_null(void) { JVal v; memset(&v, 0, sizeof(v)); return v; }
static JVal jv_bool(bool b) { JVal v = jv_null(); v.type = b ? J_TRUE : J_FALSE; return v; }

static void jfail(JDoc *d) { d->error = true; }

static void jws(JDoc *d) {
    while (d->pos < d->len) {
        char c = d->text[d->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') d->pos++;
        else break;
    }
}

static bool jpeek(JDoc *d, char c) {
    jws(d);
    return d->pos < d->len && d->text[d->pos] == c;
}

static bool jexpect(JDoc *d, char c) {
    jws(d);
    if (d->pos >= d->len || d->text[d->pos] != c) { jfail(d); return false; }
    d->pos++;
    return true;
}

static int jutf8(unsigned int cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static bool jhex4(const char *p, int avail, unsigned int *out) {
    if (avail < 4) return false;
    unsigned int cp = 0;
    for (int i = 0; i < 4; ++i) {
        char c = p[i];
        unsigned int d;
        if (c >= '0' && c <= '9')      d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else return false;
        cp = (cp << 4) | d;
    }
    *out = cp;
    return true;
}

static JVal jstring(JDoc *d) {
    JVal v = jv_null();
    jws(d);
    if (d->pos >= d->len || d->text[d->pos] != '"') { jfail(d); return v; }

    d->pos++;
    v.type    = J_STR;
    v.start   = d->pos;
    v.escaped = false;

    while (d->pos < d->len) {
        unsigned char c = (unsigned char)d->text[d->pos];
        if (c == '"') break;
        if (c == '\\') {
            v.escaped = true;
            d->pos++;
            if (d->pos >= d->len) { jfail(d); return jv_null(); }
            if (d->text[d->pos] == 'u') {
                d->pos += 5;
                if (d->pos > d->len) { jfail(d); return jv_null(); }
                continue;
            }
            d->pos++;
            continue;
        }
        if (c < 0x20) { jfail(d); return jv_null(); }
        d->pos++;
    }
    if (d->pos >= d->len) { jfail(d); return jv_null(); }

    v.len = d->pos - v.start;
    d->pos++;
    return v;
}

static char *jdecode(const JDoc *d, const JVal *v) {
    if (!v || v->type != J_STR || v->len < 0) return NULL;

    char *out = (char *)malloc((size_t)v->len + 1);
    if (!out) return NULL;

    if (!v->escaped) {
        memcpy(out, d->text + v->start, (size_t)v->len);
        out[v->len] = '\0';
        return out;
    }

    const char *p = d->text + v->start;
    const char *end = p + v->len;
    char *o = out;

    while (p < end) {
        if (*p != '\\') { *o++ = *p++; continue; }
        p++;
        if (p >= end) break;
        char e = *p++;
        switch (e) {
            case '"':  *o++ = '"';  break;
            case '\\': *o++ = '\\'; break;
            case '/':  *o++ = '/';  break;
            case 'b':  *o++ = '\b'; break;
            case 'f':  *o++ = '\f'; break;
            case 'n':  *o++ = '\n'; break;
            case 'r':  *o++ = '\r'; break;
            case 't':  *o++ = '\t'; break;
            case 'u': {
                unsigned int cp = 0;
                if (!jhex4(p, (int)(end - p), &cp)) { free(out); return NULL; }
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && (end - p) >= 6 &&
                    p[0] == '\\' && p[1] == 'u')
                {
                    unsigned int lo = 0;
                    if (jhex4(p + 2, (int)(end - p) - 2, &lo) &&
                        lo >= 0xDC00 && lo <= 0xDFFF)
                    {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        p += 6;
                    }
                }
                char buf[4];
                int n = jutf8(cp, buf);
                for (int i = 0; i < n; ++i) *o++ = buf[i];
                break;
            }
            default: free(out); return NULL;
        }
    }
    *o = '\0';
    return out;
}

static JVal jvalue(JDoc *d);

/* Consume one value, discarding its contents but validating its syntax. */
static bool jskip(JDoc *d) {
    if (++d->depth > FG_JSON_DEPTH) { jfail(d); d->depth--; return false; }
    jws(d);
    if (d->pos >= d->len) { jfail(d); d->depth--; return false; }

    char c = d->text[d->pos];
    bool ok = true;

    if (c == '{' || c == '[') {
        char close = (c == '{') ? '}' : ']';
        d->pos++;
        jws(d);
        if (jpeek(d, close)) { d->pos++; d->depth--; return true; }
        for (;;) {
            if (c == '{') {
                JVal k = jstring(d);
                if (d->error) { ok = false; break; }
                (void)k;
                if (!jexpect(d, ':')) { ok = false; break; }
            }
            if (!jskip(d)) { ok = false; break; }
            jws(d);
            if (jpeek(d, ',')) { d->pos++; continue; }
            if (jpeek(d, close)) { d->pos++; break; }
            jfail(d); ok = false; break;
        }
    } else if (c == '"') {
        JVal s = jstring(d);
        ok = !d->error && s.type == J_STR;
    } else if (c == 't') {
        ok = (d->len - d->pos >= 4) && memcmp(d->text + d->pos, "true", 4) == 0;
        if (ok) d->pos += 4; else jfail(d);
    } else if (c == 'f') {
        ok = (d->len - d->pos >= 5) && memcmp(d->text + d->pos, "false", 5) == 0;
        if (ok) d->pos += 5; else jfail(d);
    } else if (c == 'n') {
        ok = (d->len - d->pos >= 4) && memcmp(d->text + d->pos, "null", 4) == 0;
        if (ok) d->pos += 4; else jfail(d);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        if (c == '-') d->pos++;
        while (d->pos < d->len) {
            char n = d->text[d->pos];
            if ((n >= '0' && n <= '9') || n == '.' || n == 'e' || n == 'E' ||
                n == '+' || n == '-')
                d->pos++;
            else break;
        }
    } else {
        jfail(d); ok = false;
    }

    d->depth--;
    return ok;
}

/* Scan an object, storing its span in `out`. */
static bool jscan_object(JDoc *d, JVal *out) {
    if (++d->depth > FG_JSON_DEPTH) { jfail(d); d->depth--; return false; }
    jws(d);
    if (d->pos >= d->len || d->text[d->pos] != '{') { jfail(d); d->depth--; return false; }

    jws(d);
    int start = d->pos;
    d->pos++;

    jws(d);
    if (jpeek(d, '}')) {
        d->pos++;
        out->type  = J_OBJ;
        out->start = start;
        out->len   = d->pos - start;
        d->depth--;
        return true;
    }
    for (;;) {
        JVal k = jstring(d);
        if (d->error) { d->depth--; return false; }
        (void)k;
        if (!jexpect(d, ':')) { d->depth--; return false; }
        if (!jskip(d)) { d->depth--; return false; }
        jws(d);
        if (jpeek(d, ',')) { d->pos++; continue; }
        if (jpeek(d, '}')) { d->pos++; break; }
        jfail(d); d->depth--; return false;
    }
    out->type  = J_OBJ;
    out->start = start;
    out->len   = d->pos - start;
    d->depth--;
    return true;
}

/* Scan an array, storing its span in `out`. */
static bool jscan_array(JDoc *d, JVal *out) {
    if (++d->depth > FG_JSON_DEPTH) { jfail(d); d->depth--; return false; }
    jws(d);
    if (d->pos >= d->len || d->text[d->pos] != '[') { jfail(d); d->depth--; return false; }

    jws(d);
    int start = d->pos;
    d->pos++;

    jws(d);
    if (jpeek(d, ']')) {
        d->pos++;
        out->type  = J_ARR;
        out->start = start;
        out->len   = d->pos - start;
        d->depth--;
        return true;
    }
    for (;;) {
        if (!jskip(d)) { d->depth--; return false; }
        jws(d);
        if (jpeek(d, ',')) { d->pos++; continue; }
        if (jpeek(d, ']')) { d->pos++; break; }
        jfail(d); d->depth--; return false;
    }
    out->type  = J_ARR;
    out->start = start;
    out->len   = d->pos - start;
    d->depth--;
    return true;
}

static JVal jvalue(JDoc *d) {
    JVal v = jv_null();
    jws(d);
    if (d->pos >= d->len) { jfail(d); return v; }

    char c = d->text[d->pos];

    if (c == '{') { if (!jscan_object(d, &v)) return jv_null(); return v; }
    if (c == '[') { if (!jscan_array(d, &v))  return jv_null(); return v; }
    if (c == '"') return jstring(d);

    if (c == 't') {
        if (d->len - d->pos >= 4 && memcmp(d->text + d->pos, "true", 4) == 0) {
            d->pos += 4; return jv_bool(true);
        }
        jfail(d); return jv_null();
    }
    if (c == 'f') {
        if (d->len - d->pos >= 5 && memcmp(d->text + d->pos, "false", 5) == 0) {
            d->pos += 5; return jv_bool(false);
        }
        jfail(d); return jv_null();
    }
    if (c == 'n') {
        if (d->len - d->pos >= 4 && memcmp(d->text + d->pos, "null", 4) == 0) {
            d->pos += 4; return jv_null();
        }
        jfail(d); return jv_null();
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        int start = d->pos;
        if (c == '-') d->pos++;
        while (d->pos < d->len) {
            char n = d->text[d->pos];
            if ((n >= '0' && n <= '9') || n == '.' || n == 'e' || n == 'E' ||
                n == '+' || n == '-')
                d->pos++;
            else break;
        }
        v.type  = J_NUM;
        v.start = start;
        v.len   = d->pos - start;
        char tmp[64];
        int n = v.len < 63 ? v.len : 63;
        memcpy(tmp, d->text + start, (size_t)n);
        tmp[n] = '\0';
        v.num = strtod(tmp, NULL);
        return v;
    }

    jfail(d);
    return jv_null();
}

/* ---- field access ------------------------------------------------- */

/* Read `key` out of object `v` into `*out`.
 *
 * `out` is ALWAYS written (J_NULL when the key is absent or malformed), so
 * callers never hold a stale value. Returning through a pointer rather than
 * by value is deliberate: MSVC rejects a struct-valued function call used as
 * a pointer argument, and it also avoids aliasing surprises when several
 * fields are read in one expression. */
static void jget(JDoc *d, const JVal *v, const char *key, JVal *out) {
    *out = jv_null();
    if (!v || v->type != J_OBJ || d->error) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;

    d->pos   = v->start;
    d->depth = 0;
    d->error = false;

    size_t keyLen = strlen(key);

    if (jexpect(d, '{')) {
        jws(d);
        if (!jpeek(d, '}')) {
            for (;;) {
                JVal k = jstring(d);
                if (d->error) break;
                bool match = ((size_t)k.len == keyLen) &&
                             memcmp(d->text + k.start, key, keyLen) == 0;
                if (!jexpect(d, ':')) break;
                if (match) {
                    *out = jvalue(d);
                    break;
                }
                if (!jskip(d)) break;
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }

    d->pos   = savePos;
    d->depth = saveDepth;
    d->error = saveErr;
}

/* Convenience: read a field and immediately reduce it to a scalar.
 * `JVal` is small (24 bytes) and passed by value, which is both cheap and
 * the form MSVC accepts in argument position. */
static int jint_of(JDoc *d, const JVal *obj, const char *key, int def) {
    JVal v;
    jget(d, obj, key, &v);
    return v.type == J_NUM ? (int)v.num : def;
}

static float jflt_of(JDoc *d, const JVal *obj, const char *key, float def) {
    JVal v;
    jget(d, obj, key, &v);
    return v.type == J_NUM ? (float)v.num : def;
}

static bool jbool_of(JDoc *d, const JVal *obj, const char *key, bool def) {
    JVal v;
    jget(d, obj, key, &v);
    if (v.type == J_TRUE)  return true;
    if (v.type == J_FALSE) return false;
    return def;
}

static bool jhas(JDoc *d, const JVal *obj, const char *key, int wantType) {
    JVal v;
    jget(d, obj, key, &v);
    return v.type == (JType)wantType;
}

/* Iterate the elements of an array container. */
typedef void (*JEachFn)(JDoc *, const JVal *, int, void *);

static void jforeach(JDoc *d, const JVal *arr, JEachFn fn, void *ud) {
    if (!arr || arr->type != J_ARR || d->error) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;

    d->pos   = arr->start;
    d->depth = 0;
    d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            int i = 0;
            for (;;) {
                JVal e = jvalue(d);
                if (d->error) break;
                if (fn) fn(d, &e, i, ud);
                i++;
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                if (jpeek(d, ']')) break;
                break;
            }
        }
    }

    d->pos   = savePos;
    d->depth = saveDepth;
    d->error = saveErr;
}

/* Read up to `want` numbers out of an array into `out`. */
static void jvec(JDoc *d, JVal v, float *out, int want) {
    if (v.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;

    d->pos   = v.start;
    d->depth = 0;
    d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            int i = 0;
            for (;;) {
                JVal e = jvalue(d);
                if (d->error) break;
                if (i < want) out[i] = (e.type == J_NUM) ? (float)e.num : 0.0f;
                i++;
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }

    d->pos   = savePos;
    d->depth = saveDepth;
    d->error = saveErr;
}

/* ================================================================== */
/*  glTF data model                                                    */
/* ================================================================== */

typedef struct FgBufferView {
    int    buffer;
    size_t offset;
    size_t length;
    size_t stride;
} FgBufferView;

typedef struct FgAccessor {
    int    bufferView;
    size_t offset;
    int    componentType;
    bool   normalized;
    int    count;
    int    numComponents;
    int    componentSize;
} FgAccessor;

typedef struct FgPrimitive {
    int  posAcc, norAcc, uvAcc, tanAcc, idxAcc;   /* -1 = absent */
    bool hasPos, hasNor, hasUV, hasTan, hasIdx;
    bool triangles;
    int  material;
    int  gltfMesh;                         /* owning mesh index */
} FgPrimitive;

typedef struct FgMesh {
    int firstPrim;
    int primCount;
} FgMesh;

typedef struct FgNode {
    int   mesh;              /* -1 = none */
    int   firstChild;
    int   childCount;
    bool  hasMatrix;
    float matrix[16];
    bool  hasTRS;
    float translation[3];
    float rotation[4];
    float scale[3];
} FgNode;

typedef struct FgTextureRef { int index; int texCoord; } FgTextureRef;

typedef struct FgMaterial {
    float       baseColor[4];
    float       metallic;
    float       roughness;
    float       emissive[3];
    float       normalScale;
    float       occlusionStrength;
    FgTextureRef baseColorTex;
    FgTextureRef metalRoughTex;
    FgTextureRef normalTex;
    FgTextureRef occlusionTex;
    FgTextureRef emissiveTex;
} FgMaterial;

typedef struct FgImage {
    char *uri;          /* may be NULL when embedded */
    int   bufferView;
    int   mimeType;     /* 0 unknown, 1 png, 2 jpeg */
} FgImage;

typedef struct FgTexture { int source; } FgTexture;

typedef struct FgBuffer {
    size_t         declaredLength;
    char          *uri;        /* NULL => GLB binary chunk */
    unsigned char *data;       /* resolved payload          */
    size_t         dataLen;
    bool           owned;
} FgBuffer;

typedef struct FgDoc {
    FgBuffer     *buffers;      int bufferCount;
    FgBufferView *views;        int viewCount;
    FgAccessor   *accessors;    int accessorCount;
    FgPrimitive  *primitives;   int primitiveCount;
    FgMesh       *meshes;       int meshCount;
    FgNode       *nodes;        int nodeCount;
    int          *children;     int childCount;
    FgMaterial   *materials;    int materialCount;
    FgImage      *images;       int imageCount;
    FgTexture    *textures;     int textureCount;
    int          *roots;        int rootCount;
} FgDoc;

/* ================================================================== */
/*  Buffer / image payload resolution                                  */
/* ================================================================== */

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static unsigned char *b64_decode(const char *src, size_t len, size_t *outLen) {
    *outLen = 0;
    if (len == 0) return NULL;

    unsigned char *out = (unsigned char *)malloc(len / 4 * 3 + 4);
    if (!out) return NULL;

    size_t o = 0;
    unsigned int acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = src[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = b64_val(c);
        if (v < 0) { free(out); return NULL; }
        acc = (acc << 6) | (unsigned int)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    *outLen = o;
    return out;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *percent_decode(const char *src) {
    size_t len = strlen(src);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; ++i) {
        if (src[i] == '%' && i + 2 < len) {
            int hi = hex_val(src[i + 1]);
            int lo = hex_val(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[o++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[o++] = src[i];
    }
    out[o] = '\0';
    return out;
}

/* Directory of `path`, always ending in '/' (or "" when there is none). */
static char *path_dirname(const char *path) {
    const char *slash  = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *last   = slash;
    if (bslash && (!last || bslash > last)) last = bslash;

    if (!last) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t n = (size_t)(last - path) + 1;
    char *dir = (char *)malloc(n + 1);
    if (!dir) return NULL;
    memcpy(dir, path, n);
    dir[n] = '\0';
    return dir;
}

static bool read_whole_file(const char *path, unsigned char **out, size_t *outLen) {
    *out = NULL;
    *outLen = 0;

    unsigned int size = 0;
    unsigned char *data = LoadFileData(path, &size);
    if (!data) return false;
    *out = data;
    *outLen = (size_t)size;
    return true;
}

/* Decode a data: URI body. Returns an owned buffer or NULL. */
static unsigned char *decode_data_uri(const char *uri, size_t *outLen) {
    *outLen = 0;
    if (strncmp(uri, "data:", 5) != 0) return NULL;
    const char *comma = strchr(uri, ',');
    if (!comma) return NULL;

    /* ";base64" must appear in the metadata section, before the comma. */
    const char *payload = comma + 1;
    const char *marker = strstr(uri, ";base64");
    if (marker && marker < comma) {
        return b64_decode(payload, strlen(payload), outLen);
    }
    char *dec = percent_decode(payload);
    if (!dec) return NULL;
    *outLen = strlen(dec);
    return (unsigned char *)dec;
}

/* Resolve a URI to file bytes (relative to `dir`). Caller frees. */
static unsigned char *read_uri(const char *uri, const char *dir, size_t *outLen) {
    *outLen = 0;
    if (!uri) return NULL;

    if (strncmp(uri, "data:", 5) == 0) return decode_data_uri(uri, outLen);

    char *rel = percent_decode(uri);
    if (!rel) return NULL;

    size_t need = strlen(dir) + strlen(rel) + 1;
    char *full = (char *)malloc(need);
    unsigned char *data = NULL;
    size_t len = 0;
    if (full) {
        snprintf(full, need, "%s%s", dir, rel);
        if (!read_whole_file(full, &data, &len)) data = NULL;
        free(full);
    }
    free(rel);
    *outLen = len;
    return data;
}

/* ================================================================== */
/*  Accessor reads — every path is bounds-checked                      */
/* ================================================================== */

typedef struct FgScene {
    const FgDoc *doc;
    const FgBuffer *buffers;
} FgScene;

/* Fetch the address of element `index` of `a`, requiring `need` bytes
 * to be readable there. */
static bool acc_elem(const FgScene *s, const FgAccessor *a, size_t index,
                     const unsigned char **out, size_t need)
{
    if (!a || a->bufferView < 0 || a->componentSize <= 0 || a->numComponents <= 0)
        return false;
    if (a->bufferView >= s->doc->viewCount) return false;
    if (index >= (size_t)a->count) return false;

    const FgBufferView *bv = &s->doc->views[a->bufferView];
    if (bv->buffer < 0 || bv->buffer >= s->doc->bufferCount) return false;

    const FgBuffer *buf = &s->buffers[bv->buffer];
    if (!buf->data) return false;

    size_t elemSize = (size_t)a->componentSize * (size_t)a->numComponents;
    size_t stride   = bv->stride ? bv->stride : elemSize;
    if (stride < elemSize) return false;      /* would alias components */

    if (stride != 0 && index > (SIZE_MAX - a->offset) / stride) return false;
    size_t rel = a->offset + stride * index;
    if (bv->offset > SIZE_MAX - rel) return false;
    size_t abs = bv->offset + rel;

    if (abs > buf->dataLen) return false;
    if (need > buf->dataLen - abs) return false;

    /* The element must also live inside the bufferView's own extent. */
    if (bv->length < elemSize) return false;
    if (rel > bv->length - elemSize) return false;

    *out = buf->data + abs;
    return true;
}

static float read_scalar(const unsigned char *p, int componentType, bool normalized) {
    switch (componentType) {
        case 5126: { float v;    memcpy(&v, p, 4); return v; }
        case 5121: return normalized ? ((float)*p / 255.0f) : (float)*p;
        case 5120: { signed char v; memcpy(&v, p, 1);
                     return normalized ? ((float)v / 127.0f) : (float)v; }
        case 5123: { uint16_t v; memcpy(&v, p, 2);
                     return normalized ? ((float)v / 65535.0f) : (float)v; }
        case 5122: { int16_t v;  memcpy(&v, p, 2);
                     return normalized ? ((float)v / 32767.0f) : (float)v; }
        case 5125: { uint32_t v; memcpy(&v, p, 4); return (float)v; }
        default:   return 0.0f;
    }
}

/* Read up to `want` floats; missing components become 0. */
static bool acc_read(const FgScene *s, const FgAccessor *a, size_t index,
                     float *out, int want)
{
    if (!a || a->componentSize <= 0 || a->numComponents <= 0) return false;

    size_t elemSize = (size_t)a->componentSize * (size_t)a->numComponents;
    const unsigned char *p = NULL;
    if (!acc_elem(s, a, index, &p, elemSize)) return false;

    int n = a->numComponents < want ? a->numComponents : want;
    for (int i = 0; i < n; ++i) {
        if (a->componentType == 5126) {
            float v;
            memcpy(&v, p + (size_t)i * 4u, 4);
            out[i] = v;
        } else {
            out[i] = read_scalar(p + (size_t)i * (size_t)a->componentSize,
                                 a->componentType, a->normalized);
        }
    }
    for (int i = n; i < want; ++i) out[i] = 0.0f;
    return true;
}

static bool acc_read_index(const FgScene *s, const FgAccessor *a, size_t index,
                           uint32_t *out)
{
    *out = 0;
    if (!a || a->componentSize <= 0) return false;

    const unsigned char *p = NULL;
    if (!acc_elem(s, a, index, &p, (size_t)a->componentSize)) return false;

    switch (a->componentType) {
        case 5121: *out = (uint32_t)(*p); return true;
        case 5123: { uint16_t v; memcpy(&v, p, 2); *out = (uint32_t)v; return true; }
        case 5125: { uint32_t v; memcpy(&v, p, 4); *out = v; return true; }
        default: return false;
    }
}

/* ================================================================== */
/*  Matrix helpers (Matrix is column-major, mN in declaration order)   */
/* ================================================================== */

typedef struct M4 { float v[16]; } M4;

#define M4_AT(m, r, c) ((m).v[(c) * 4 + (r)])

static M4 m4_identity(void) {
    M4 m; memset(&m, 0, sizeof(m));
    m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1.0f;
    return m;
}

static M4 m4_from_array(const float *a) {
    M4 m;
    for (int i = 0; i < 16; ++i) m.v[i] = a[i];
    return m;
}

static M4 m4_trs(const float *t, const float *q, const float *s) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    M4 m;
    m.v[0]  = (1.0f - (yy + zz)) * s[0];
    m.v[1]  = (xy + wz) * s[0];
    m.v[2]  = (xz - wy) * s[0];
    m.v[3]  = 0.0f;
    m.v[4]  = (xy - wz) * s[1];
    m.v[5]  = (1.0f - (xx + zz)) * s[1];
    m.v[6]  = (yz + wx) * s[1];
    m.v[7]  = 0.0f;
    m.v[8]  = (xz + wy) * s[2];
    m.v[9]  = (yz - wx) * s[2];
    m.v[10] = (1.0f - (xx + yy)) * s[2];
    m.v[11] = 0.0f;
    m.v[12] = t[0];
    m.v[13] = t[1];
    m.v[14] = t[2];
    m.v[15] = 1.0f;
    return m;
}

/* out = a * b */
static M4 m4_mul(M4 a, M4 b) {
    M4 o;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k)
                acc += M4_AT(a, r, k) * M4_AT(b, k, c);
            o.v[c * 4 + r] = acc;
        }
    }
    return o;
}

static void m4_scale_positions(M4 *m, float s) {
    m->v[12] *= s;
    m->v[13] *= s;
    m->v[14] *= s;
}

static M4 node_local(const FgNode *n) {
    if (n->hasMatrix) return m4_from_array(n->matrix);
    if (n->hasTRS)    return m4_trs(n->translation, n->rotation, n->scale);
    return m4_identity();
}

/* ================================================================== */
/*  JSON -> FgDoc                                                      */
/* ================================================================== */

typedef struct DocCtx { JDoc *d; FgDoc *doc; } DocCtx;

static void parse_buffers(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "buffers", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgBuffer *b = (FgBuffer *)realloc(doc->buffers,
                        (size_t)(doc->bufferCount + 1) * sizeof(FgBuffer));
                    if (!b) break;
                    doc->buffers = b;
                    memset(&doc->buffers[doc->bufferCount], 0, sizeof(FgBuffer));
                    FgBuffer *nb = &doc->buffers[doc->bufferCount];

                    nb->declaredLength =
                        (size_t)jflt_of(d, &v, "byteLength", 0.0f);
                    JVal uri;
                    jget(d, &v, "uri", &uri);
                    if (uri.type == J_STR) nb->uri = jdecode(d, &uri);
                    doc->bufferCount++;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

static void parse_buffer_views(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "bufferViews", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgBufferView *bv = (FgBufferView *)realloc(doc->views,
                        (size_t)(doc->viewCount + 1) * sizeof(FgBufferView));
                    if (!bv) break;
                    doc->views = bv;
                    FgBufferView *nv = &doc->views[doc->viewCount];
                    memset(nv, 0, sizeof(*nv));
                    nv->buffer = jint_of(d, &v, "buffer", -1);
                    nv->offset = (size_t)jflt_of(d, &v, "byteOffset", 0.0f);
                    nv->length = (size_t)jflt_of(d, &v, "byteLength", 0.0f);
                    nv->stride = (size_t)jflt_of(d, &v, "byteStride", 0.0f);
                    doc->viewCount++;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

static void parse_accessors(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "accessors", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgAccessor *a = (FgAccessor *)realloc(doc->accessors,
                        (size_t)(doc->accessorCount + 1) * sizeof(FgAccessor));
                    if (!a) break;
                    doc->accessors = a;
                    FgAccessor *na = &doc->accessors[doc->accessorCount];
                    memset(na, 0, sizeof(*na));
                    na->bufferView    = jint_of(d, &v, "bufferView", -1);
                    na->offset        = (size_t)jflt_of(d, &v, "byteOffset", 0.0f);
                    na->componentType = jint_of(d, &v, "componentType", 0);
                    na->normalized    = jbool_of(d, &v, "normalized", false);
                    na->count         = jint_of(d, &v, "count", 0);
                    na->componentSize = fg_component_size(na->componentType);

                    JVal t;
                    jget(d, &v, "type", &t);
                    na->numComponents = 0;
                    if (t.type == J_STR) {
                        for (int k = 0; k < 7; ++k) {
                            if (str_eq(d->text, d->text + t.start, t.len, FG_TYPE_NAMES[k])) {
                                na->numComponents = fg_type_count(k);
                                break;
                            }
                        }
                    }
                    doc->accessorCount++;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

/* --- meshes and primitives --- */

typedef struct PrimCtx { JDoc *d; FgDoc *doc; int meshIndex; } PrimCtx;

static void prim_each(JDoc *d, const JVal *v, int i, void *ud) {
    PrimCtx *c = (PrimCtx *)ud;
    (void)i;
    if (v->type != J_OBJ) return;

    FgPrimitive p;
    memset(&p, 0, sizeof(p));
    p.posAcc = p.norAcc = p.uvAcc = p.tanAcc = p.idxAcc = -1;
    p.hasPos = p.hasNor = p.hasUV = p.hasTan = p.hasIdx = false;
    p.material = -1;
    p.gltfMesh = c->meshIndex;
    p.triangles = (jint_of(d, v, "mode", 4) == 4);

    JVal attrs;
    jget(d, v, "attributes", &attrs);
    if (attrs.type == J_OBJ) {
        JVal pv;
        jget(d, &attrs, "POSITION", &pv);
        if (pv.type == J_NUM) { p.posAcc = (int)pv.num; p.hasPos = true; }
        JVal nv;
        jget(d, &attrs, "NORMAL", &nv);
        if (nv.type == J_NUM) { p.norAcc = (int)nv.num; p.hasNor = true; }
        JVal tv;
        jget(d, &attrs, "TEXCOORD_0", &tv);
        if (tv.type == J_NUM) { p.uvAcc = (int)tv.num; p.hasUV = true; }
        JVal tanv;
        jget(d, &attrs, "TANGENT", &tanv);
        if (tanv.type == J_NUM) { p.tanAcc = (int)tanv.num; p.hasTan = true; }
    }
    JVal iv;
    jget(d, v, "indices", &iv);
    if (iv.type == J_NUM) { p.idxAcc = (int)iv.num; p.hasIdx = true; }
    p.material = jint_of(d, v, "material", -1);

    FgPrimitive *np = (FgPrimitive *)realloc(c->doc->primitives,
        (size_t)(c->doc->primitiveCount + 1) * sizeof(FgPrimitive));
    if (!np) return;
    c->doc->primitives = np;
    c->doc->primitives[c->doc->primitiveCount++] = p;
}

static void parse_meshes(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "meshes", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            int meshIndex = 0;
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgMesh *m = (FgMesh *)realloc(doc->meshes,
                        (size_t)(doc->meshCount + 1) * sizeof(FgMesh));
                    if (!m) break;
                    doc->meshes = m;
                    FgMesh *nm = &doc->meshes[doc->meshCount];
                    nm->firstPrim = doc->primitiveCount;
                    nm->primCount = 0;
                    doc->meshCount++;

                    int added = 0;
                    PrimCtx pc;
                    pc.d = d; pc.doc = doc; pc.meshIndex = meshIndex;
                    JVal prims;
                    jget(d, &v, "primitives", &prims);
                    if (prims.type == J_ARR) {
                        int before = doc->primitiveCount;
                        jforeach(d, &prims, prim_each, &pc);
                        added = doc->primitiveCount - before;
                    }
                    nm->primCount = added;
                }
                meshIndex++;
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

/* --- nodes --- */

static void parse_nodes(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "nodes", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgNode *n = (FgNode *)realloc(doc->nodes,
                        (size_t)(doc->nodeCount + 1) * sizeof(FgNode));
                    if (!n) break;
                    doc->nodes = n;
                    FgNode *nn = &doc->nodes[doc->nodeCount];
                    memset(nn, 0, sizeof(*nn));
                    nn->mesh = jint_of(d, &v, "mesh", -1);
                    nn->rotation[3] = 1.0f;
                    nn->scale[0] = nn->scale[1] = nn->scale[2] = 1.0f;

                    /* A matrix must have exactly 16 numbers to be trusted. */
                    JVal mtx;
                    jget(d, &v, "matrix", &mtx);
                    if (mtx.type == J_ARR) {
                        int cnt = 0;
                        int sp = d->pos, sd = d->depth; bool se = d->error;
                        d->pos = mtx.start; d->depth = 0; d->error = false;
                        if (jexpect(d, '[')) {
                            jws(d);
                            if (!jpeek(d, ']')) {
                                for (;;) {
                                    JVal e = jvalue(d);
                                    if (d->error) break;
                                    cnt++;
                                    jws(d);
                                    if (jpeek(d, ',')) { d->pos++; continue; }
                                    break;
                                }
                            }
                        }
                        d->pos = sp; d->depth = sd; d->error = se;

                        if (cnt == 16) {
                            jvec(d, mtx, nn->matrix, 16);
                            nn->hasMatrix = true;
                        }
                    }
                    if (!nn->hasMatrix) {
                        JVal tr;
                        jget(d, &v, "translation", &tr);
                        if (tr.type == J_ARR) {
                            jvec(d, tr, nn->translation, 3);
                            nn->hasTRS = true;
                        }
                        JVal ro;
                        jget(d, &v, "rotation", &ro);
                        if (ro.type == J_ARR) {
                            jvec(d, ro, nn->rotation, 4);
                            nn->hasTRS = true;
                        }
                        JVal sc;
                        jget(d, &v, "scale", &sc);
                        if (sc.type == J_ARR) {
                            jvec(d, sc, nn->scale, 3);
                            nn->hasTRS = true;
                        }
                    }

                    nn->firstChild = doc->childCount;
                    nn->childCount = 0;
                    JVal kids;
                    jget(d, &v, "children", &kids);
                    if (kids.type == J_ARR) {
                        int sp = d->pos, sd = d->depth; bool se = d->error;
                        d->pos = kids.start; d->depth = 0; d->error = false;
                        if (jexpect(d, '[')) {
                            jws(d);
                            if (!jpeek(d, ']')) {
                                for (;;) {
                                    JVal e = jvalue(d);
                                    if (d->error) break;
                                    if (e.type == J_NUM) {
                                        int *nc = (int *)realloc(doc->children,
                                            (size_t)(doc->childCount + 1) * sizeof(int));
                                        if (!nc) break;
                                        doc->children = nc;
                                        doc->children[doc->childCount++] = (int)e.num;
                                    }
                                    jws(d);
                                    if (jpeek(d, ',')) { d->pos++; continue; }
                                    break;
                                }
                            }
                        }
                        d->pos = sp; d->depth = sd; d->error = se;
                        nn->childCount = doc->childCount - nn->firstChild;
                    }
                    doc->nodeCount++;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

/* --- materials --- */

static FgTextureRef read_texref(JDoc *d, const JVal *parent, const char *key) {
    FgTextureRef r;
    r.index = -1;
    r.texCoord = 0;
    if (!parent || parent->type != J_OBJ) return r;
    JVal t;
    jget(d, parent, key, &t);
    if (t.type != J_OBJ) return r;
    r.index = jint_of(d, &t, "index", -1);
    r.texCoord = jint_of(d, &t, "texCoord", 0);
    return r;
}

/* textureInfo that lives directly on the material (no extra nesting). */
static FgTextureRef read_texinfo(JDoc *d, const JVal *mat, const char *key,
                                 float *scale, float *strength) {
    FgTextureRef r;
    r.index = -1;
    r.texCoord = 0;
    JVal t;
    jget(d, mat, key, &t);
    if (t.type != J_OBJ) return r;
    r.index = jint_of(d, &t, "index", -1);
    r.texCoord = jint_of(d, &t, "texCoord", 0);
    if (scale)    *scale    = jflt_of(d, &t, "scale", 1.0f);
    if (strength) *strength = jflt_of(d, &t, "strength", 1.0f);
    return r;
}

static void parse_materials(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "materials", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgMaterial *m = (FgMaterial *)realloc(doc->materials,
                        (size_t)(doc->materialCount + 1) * sizeof(FgMaterial));
                    if (!m) break;
                    doc->materials = m;
                    FgMaterial *nm = &doc->materials[doc->materialCount];
                    memset(nm, 0, sizeof(*nm));
                    nm->baseColor[0] = nm->baseColor[1] = nm->baseColor[2] = 1.0f;
                    nm->baseColor[3] = 1.0f;
                    nm->metallic = 1.0f;
                    nm->roughness = 1.0f;
                    nm->emissive[0] = nm->emissive[1] = nm->emissive[2] = 0.0f;
                    nm->normalScale = 1.0f;
                    nm->occlusionStrength = 1.0f;
                    nm->baseColorTex.index    = -1;
                    nm->metalRoughTex.index   = -1;
                    nm->normalTex.index       = -1;
                    nm->occlusionTex.index    = -1;
                    nm->emissiveTex.index     = -1;

                    JVal em;
                    jget(d, &v, "emissiveFactor", &em);
                    jvec(d, em, nm->emissive, 3);

                    JVal pbr;
                    jget(d, &v, "pbrMetallicRoughness", &pbr);
                    if (pbr.type == J_OBJ) {
                        JVal bc;
                        jget(d, &pbr, "baseColorFactor", &bc);
                        jvec(d, bc, nm->baseColor, 4);
                        nm->metallic  = jflt_of(d, &pbr, "metallicFactor", 1.0f);
                        nm->roughness = jflt_of(d, &pbr, "roughnessFactor", 1.0f);
                        nm->baseColorTex  = read_texref(d, &pbr, "baseColorTexture");
                        nm->metalRoughTex = read_texref(d, &pbr, "metallicRoughnessTexture");
                    }
                    nm->normalTex    = read_texinfo(d, &v, "normalTexture", &nm->normalScale, NULL);
                    nm->occlusionTex = read_texinfo(d, &v, "occlusionTexture", NULL, &nm->occlusionStrength);
                    nm->emissiveTex  = read_texinfo(d, &v, "emissiveTexture", NULL, NULL);

                    doc->materialCount++;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

/* --- images and textures --- */

static void parse_images(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "images", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgImage *im = (FgImage *)realloc(doc->images,
                        (size_t)(doc->imageCount + 1) * sizeof(FgImage));
                    if (!im) break;
                    doc->images = im;
                    FgImage *ni = &doc->images[doc->imageCount];
                    memset(ni, 0, sizeof(*ni));
                    ni->bufferView = jint_of(d, &v, "bufferView", -1);
                    JVal uri;
                    jget(d, &v, "uri", &uri);
                    if (uri.type == J_STR) ni->uri = jdecode(d, &uri);
                    JVal mt;
                    jget(d, &v, "mimeType", &mt);
                    if (mt.type == J_STR) {
                        if (str_eq(d->text, d->text + mt.start, mt.len, "image/png"))  ni->mimeType = 1;
                        else if (str_eq(d->text, d->text + mt.start, mt.len, "image/jpeg")) ni->mimeType = 2;
                    }
                    doc->imageCount++;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

static void parse_textures(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal arr;
    jget(d, root, "textures", &arr);
    if (arr.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = arr.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_OBJ) {
                    FgTexture *t = (FgTexture *)realloc(doc->textures,
                        (size_t)(doc->textureCount + 1) * sizeof(FgTexture));
                    if (!t) break;
                    doc->textures = t;
                    doc->textures[doc->textureCount].source =
                        jint_of(d, &v, "source", -1);
                    doc->textureCount++;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

/* --- scene roots --- */

static void parse_roots(JDoc *d, const JVal *root, FgDoc *doc) {
    JVal scenes;
    jget(d, root, "scenes", &scenes);
    if (scenes.type != J_ARR) return;

    int want = jint_of(d, root, "scene", -1);
    JVal chosen = jv_null();

    /* Pick scenes[want], else scenes[0]. */
    {
        int savePos = d->pos, saveDepth = d->depth;
        bool saveErr = d->error;
        d->pos = scenes.start; d->depth = 0; d->error = false;

        if (jexpect(d, '[')) {
            jws(d);
            if (!jpeek(d, ']')) {
                int idx = 0;
                for (;;) {
                    JVal v = jvalue(d);
                    if (d->error) break;
                    if (v.type == J_OBJ) {
                        if (idx == want) { chosen = v; break; }
                        if (want < 0 && idx == 0) chosen = v;
                    }
                    idx++;
                    jws(d);
                    if (jpeek(d, ',')) { d->pos++; continue; }
                    break;
                }
            }
        }
        d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
    }

    if (chosen.type != J_OBJ) return;

    JVal nodes;
    jget(d, &chosen, "nodes", &nodes);
    if (nodes.type != J_ARR) return;

    int savePos = d->pos, saveDepth = d->depth;
    bool saveErr = d->error;
    d->pos = nodes.start; d->depth = 0; d->error = false;

    if (jexpect(d, '[')) {
        jws(d);
        if (!jpeek(d, ']')) {
            for (;;) {
                JVal v = jvalue(d);
                if (d->error) break;
                if (v.type == J_NUM) {
                    int *r = (int *)realloc(doc->roots,
                        (size_t)(doc->rootCount + 1) * sizeof(int));
                    if (!r) break;
                    doc->roots = r;
                    doc->roots[doc->rootCount++] = (int)v.num;
                }
                jws(d);
                if (jpeek(d, ',')) { d->pos++; continue; }
                break;
            }
        }
    }
    d->pos = savePos; d->depth = saveDepth; d->error = saveErr;
}

/* ================================================================== */
/*  Mesh construction                                                  */
/* ================================================================== */

static Mesh build_mesh(const FgScene *s, const FgPrimitive *p) {
    Mesh none;
    memset(&none, 0, sizeof(none));

    if (!p->hasPos || !p->triangles) return none;
    if (p->posAcc < 0 || p->posAcc >= s->doc->accessorCount) return none;

    const FgAccessor *pa = &s->doc->accessors[p->posAcc];
    if (pa->count <= 0 || pa->count > FG_MAX_VERTS) return none;
    if (pa->numComponents < 3 || pa->componentSize <= 0) return none;

    int vertexCount = pa->count;
    Vertex3D *verts = (Vertex3D *)calloc((size_t)vertexCount, sizeof(Vertex3D));
    if (!verts) return none;

    /* POSITION */
    for (int i = 0; i < vertexCount; ++i) {
        float v[3];
        if (!acc_read(s, pa, (size_t)i, v, 3)) { free(verts); return none; }
        verts[i].x = v[0];
        verts[i].y = v[1];
        verts[i].z = v[2];
    }

    /* NORMAL */
    bool haveNormal = false;
    if (p->hasNor && p->norAcc >= 0 && p->norAcc < s->doc->accessorCount) {
        const FgAccessor *na = &s->doc->accessors[p->norAcc];
        if (na->count == pa->count && na->numComponents >= 3 && na->componentSize > 0) {
            haveNormal = true;
            for (int i = 0; i < vertexCount; ++i) {
                float v[3];
                if (!acc_read(s, na, (size_t)i, v, 3)) { free(verts); return none; }
                verts[i].nx = v[0];
                verts[i].ny = v[1];
                verts[i].nz = v[2];
            }
        }
    }
    if (!haveNormal) {
        for (int i = 0; i < vertexCount; ++i) {
            verts[i].nx = 0.0f;
            verts[i].ny = 1.0f;
            verts[i].nz = 0.0f;
        }
    }

    /* TEXCOORD_0 */
    if (p->hasUV && p->uvAcc >= 0 && p->uvAcc < s->doc->accessorCount) {
        const FgAccessor *ua = &s->doc->accessors[p->uvAcc];
        if (ua->count == pa->count && ua->numComponents >= 2 && ua->componentSize > 0) {
            for (int i = 0; i < vertexCount; ++i) {
                float v[2];
                if (!acc_read(s, ua, (size_t)i, v, 2)) { free(verts); return none; }
                verts[i].u = v[0];
                verts[i].v = v[1];
            }
        }
    }

    /* TANGENT */
    bool haveTangent = false;
    if (p->hasTan && p->tanAcc >= 0 && p->tanAcc < s->doc->accessorCount) {
        const FgAccessor *ta = &s->doc->accessors[p->tanAcc];
        if (ta->count == pa->count && ta->numComponents >= 4 && ta->componentSize > 0) {
            haveTangent = true;
            for (int i = 0; i < vertexCount; ++i) {
                float v[4];
                if (!acc_read(s, ta, (size_t)i, v, 4)) { free(verts); return none; }
                verts[i].tx = v[0];
                verts[i].ty = v[1];
                verts[i].tz = v[2];
                verts[i].tw = v[3];
            }
        }
    }

    /* Indices */
    size_t indexCount = 0;
    uint32_t *indices = NULL;

    if (p->hasIdx) {
        if (p->idxAcc < 0 || p->idxAcc >= s->doc->accessorCount) { free(verts); return none; }
        const FgAccessor *ia = &s->doc->accessors[p->idxAcc];
        if (ia->count <= 0) { free(verts); return none; }
        indexCount = (size_t)ia->count;
        indices = (uint32_t *)malloc(indexCount * sizeof(uint32_t));
        if (!indices) { free(verts); return none; }

        for (size_t i = 0; i < indexCount; ++i) {
            uint32_t v = 0;
            if (!acc_read_index(s, ia, i, &v) || v >= (uint32_t)vertexCount) {
                free(verts); free(indices); return none;
            }
            indices[i] = v;
        }
    } else {
        indexCount = (size_t)vertexCount;
        indices = (uint32_t *)malloc(indexCount * sizeof(uint32_t));
        if (!indices) { free(verts); return none; }
        for (int i = 0; i < vertexCount; ++i) indices[i] = (uint32_t)i;
    }

    /* Generate tangents if missing and UVs are present */
    if (!haveTangent && p->hasUV && indexCount >= 3) {
        float *tan1 = (float *)calloc((size_t)vertexCount * 3, sizeof(float));
        if (tan1) {
            for (size_t t = 0; t + 2 < indexCount; t += 3) {
                uint32_t i0 = indices[t], i1 = indices[t+1], i2 = indices[t+2];
                if (i0 >= (uint32_t)vertexCount || i1 >= (uint32_t)vertexCount || i2 >= (uint32_t)vertexCount)
                    continue;
                Vertex3D *v0 = &verts[i0], *v1 = &verts[i1], *v2 = &verts[i2];
                float x1 = v1->x - v0->x, x2 = v2->x - v0->x;
                float y1 = v1->y - v0->y, y2 = v2->y - v0->y;
                float z1 = v1->z - v0->z, z2 = v2->z - v0->z;
                float s1 = v1->u - v0->u, s2 = v2->u - v0->u;
                float t1 = v1->v - v0->v, t2 = v2->v - v0->v;
                float r = (s1 * t2 - s2 * t1);
                if (fabsf(r) < 1e-8f) continue;
                float invR = 1.0f / r;
                float sdirX = (t2 * x1 - t1 * x2) * invR;
                float sdirY = (t2 * y1 - t1 * y2) * invR;
                float sdirZ = (t2 * z1 - t1 * z2) * invR;
                tan1[i0*3+0] += sdirX; tan1[i0*3+1] += sdirY; tan1[i0*3+2] += sdirZ;
                tan1[i1*3+0] += sdirX; tan1[i1*3+1] += sdirY; tan1[i1*3+2] += sdirZ;
                tan1[i2*3+0] += sdirX; tan1[i2*3+1] += sdirY; tan1[i2*3+2] += sdirZ;
            }
            for (int i = 0; i < vertexCount; ++i) {
                float nx = verts[i].nx, ny = verts[i].ny, nz = verts[i].nz;
                float tx = tan1[i*3+0], ty = tan1[i*3+1], tz = tan1[i*3+2];
                float dot = nx * tx + ny * ty + nz * tz;
                tx -= nx * dot; ty -= ny * dot; tz -= nz * dot;
                float len = sqrtf(tx * tx + ty * ty + tz * tz);
                if (len > 1e-6f) {
                    verts[i].tx = tx / len; verts[i].ty = ty / len; verts[i].tz = tz / len;
                } else {
                    verts[i].tx = 1.0f; verts[i].ty = 0.0f; verts[i].tz = 0.0f;
                }
                verts[i].tw = 1.0f;
            }
            free(tan1);
        }
    }

    Mesh mesh = vgraphics_RegisterMesh(verts, vertexCount, indices, (int)indexCount);
    free(verts);
    free(indices);
    return mesh;
}

/* ================================================================== */
/*  Scene traversal                                                    */
/* ================================================================== */

typedef struct FgDraw {
    Mesh mesh;
    int  material;   /* doc material index, -1 = none */
    M4   world;
} FgDraw;

typedef struct Walk {
    const FgScene *s;
    FgDraw        *draws;
    int            drawCount;
    int            drawCap;
    Mesh          *meshCache;   /* per glTF mesh */
    bool          *meshBuilt;
    uint8_t       *visited;     /* per glTF node */
    float          scale;
} Walk;

static void walk(Walk *w, int nodeIndex, M4 parent, int depth) {
    const FgDoc *doc = w->s->doc;
    if (nodeIndex < 0 || nodeIndex >= doc->nodeCount) return;
    if (depth > FG_MAX_DEPTH) return;
    if (w->visited[nodeIndex]) return;       /* cycle guard */
    w->visited[nodeIndex] = 1;

    const FgNode *n = &doc->nodes[nodeIndex];
    M4 world = m4_mul(parent, node_local(n));

    if (n->mesh >= 0 && n->mesh < doc->meshCount) {
        const FgMesh *m = &doc->meshes[n->mesh];

        /* Upload each glTF mesh once, then instance it. */
        if (!w->meshBuilt[n->mesh]) {
            w->meshBuilt[n->mesh] = true;
            w->meshCache[n->mesh] = (Mesh){0};
            if (m->primCount > 0) {
                int first = m->firstPrim;
                if (first >= 0 && first < doc->primitiveCount)
                    w->meshCache[n->mesh] = build_mesh(w->s, &doc->primitives[first]);
            }
        }
        Mesh mesh = w->meshCache[n->mesh];
        if (!mesh.id) return;

        for (int pi = 0; pi < m->primCount; ++pi) {
            int primIndex = m->firstPrim + pi;
            if (primIndex < 0 || primIndex >= doc->primitiveCount) continue;

            if (w->drawCount >= w->drawCap) {
                int next = w->drawCap ? w->drawCap * 2 : 16;
                FgDraw *p = (FgDraw *)realloc(w->draws, (size_t)next * sizeof(FgDraw));
                if (!p) return;
                w->draws = p;
                w->drawCap = next;
            }
            FgDraw *d = &w->draws[w->drawCount++];
            d->mesh = mesh;
            d->material = doc->primitives[primIndex].material;
            d->world = world;
            if (w->scale != 1.0f) m4_scale_positions(&d->world, w->scale);
        }
    }

    for (int i = 0; i < n->childCount; ++i) {
        int ci = n->firstChild + i;
        if (ci < 0 || ci >= doc->childCount) continue;
        walk(w, doc->children[ci], world, depth + 1);
    }
}

/* ================================================================== */
/*  Texture cache                                                      */
/* ================================================================== */

typedef struct TexCache {
    int       *gltfIndex;
    Texture2D *tex;
    int        count;
} TexCache;

static Texture2D texcache_find(const TexCache *c, int gltfIndex) {
    for (int i = 0; i < c->count; ++i)
        if (c->gltfIndex[i] == gltfIndex) return c->tex[i];
    Texture2D none;
    memset(&none, 0, sizeof(none));
    return none;
}

static void texcache_add(TexCache *c, int gltfIndex, Texture2D t) {
    int *gi = (int *)realloc(c->gltfIndex, (size_t)(c->count + 1) * sizeof(int));
    if (!gi) return;
    c->gltfIndex = gi;

    Texture2D *tt = (Texture2D *)realloc(c->tex, (size_t)(c->count + 1) * sizeof(Texture2D));
    if (!tt) return;
    c->tex = tt;

    c->gltfIndex[c->count] = gltfIndex;
    c->tex[c->count] = t;
    c->count++;
}

/* Fetch the raw bytes of an image (owned or pointing into a buffer). */
static bool image_bytes(const FgScene *s, const FgImage *im, const char *dir,
                        const unsigned char **out, size_t *outLen,
                        unsigned char **owned)
{
    *out = NULL;
    *outLen = 0;
    *owned = NULL;

    if (im->bufferView >= 0) {
        if (im->bufferView >= s->doc->viewCount) return false;
        const FgBufferView *bv = &s->doc->views[im->bufferView];
        if (bv->buffer < 0 || bv->buffer >= s->doc->bufferCount) return false;
        const FgBuffer *b = &s->buffers[bv->buffer];
        if (!b->data) return false;
        if (bv->offset > b->dataLen) return false;
        if (bv->length > b->dataLen - bv->offset) return false;
        *out = b->data + bv->offset;
        *outLen = bv->length;
        return true;
    }

    if (!im->uri) return false;

    if (strncmp(im->uri, "data:", 5) == 0) {
        unsigned char *data = decode_data_uri(im->uri, outLen);
        if (!data) return false;
        *owned = data;
        *out = data;
        return true;
    }

    unsigned char *data = read_uri(im->uri, dir, outLen);
    if (!data) return false;
    *owned = data;
    *out = data;
    return true;
}

static Texture2D texcache_get(const FgScene *s, const FgDoc *doc, const char *dir,
                              int gltfTexIndex, bool srgb, TexCache *cache)
{
    Texture2D none;
    memset(&none, 0, sizeof(none));

    if (gltfTexIndex < 0 || gltfTexIndex >= doc->textureCount) return none;

    Texture2D cached = texcache_find(cache, gltfTexIndex);
    if (cached.id) return cached;

    int imgIndex = doc->textures[gltfTexIndex].source;
    if (imgIndex < 0 || imgIndex >= doc->imageCount) {
        texcache_add(cache, gltfTexIndex, none);
        return none;
    }

    const unsigned char *bytes = NULL;
    size_t size = 0;
    unsigned char *owned = NULL;
    if (!image_bytes(s, &doc->images[imgIndex], dir, &bytes, &size, &owned)) {
        texcache_add(cache, gltfTexIndex, none);
        return none;
    }

    Texture2D result;
    memset(&result, 0, sizeof(result));

    if (size > 8 && size <= (size_t)INT32_MAX) {
        Image img = LoadImageFromMemory(
            doc->images[imgIndex].mimeType == 2 ? ".jpg" : ".png",
            bytes, (int)size);
        if (img.data) {
            result = srgb ? LoadTextureFromImageSRGB(img) : LoadTextureFromImage(img);
            UnloadImage(img);
        }
    }

    free(owned);
    texcache_add(cache, gltfTexIndex, result);
    return result;
}

/* ================================================================== */
/*  Public entry points                                                */
/* ================================================================== */

static Model fastload(const char *path, const FastLoadGltfOptions *opt,
                      FastLoadGltfInfo *info)
{
    Model empty;
    memset(&empty, 0, sizeof(empty));

    FastLoadGltfOptions options;
    if (opt) options = *opt;
    else {
        memset(&options, 0, sizeof(options));
        options.scale = 1.0f;
        options.flipV = false;
        options.srgbTextures = true;
        options.verbose = false;
    }
    if (!(options.scale > 0.0f)) options.scale = 1.0f;

    if (info) {
        memset(info, 0, sizeof(*info));
        info->ok = false;
        info->sizes = options;
    }
    if (!path) return empty;

    unsigned char *file = NULL;
    size_t fileLen = 0;
    if (!read_whole_file(path, &file, &fileLen)) {
        if (options.verbose) fprintf(stderr, "[FastLoadGltf] cannot open %s\n", path);
        return empty;
    }

    /* ---- GLB container ---- */
    const char *jsonText = NULL;
    size_t jsonLen = 0;
    const unsigned char *glbBin = NULL;
    size_t glbBinLen = 0;
    bool isGlb = false;

    if (fileLen >= 12 && file[0] == 'g' && file[1] == 'l' &&
        file[2] == 'T' && file[3] == 'F')
    {
        uint32_t version = 0, total = 0;
        memcpy(&version, file + 4, 4);
        memcpy(&total, file + 8, 4);

        if (version != 2 || (size_t)total > fileLen) {
            if (options.verbose)
                fprintf(stderr, "[FastLoadGltf] %s: bad GLB header\n", path);
            free(file);
            return empty;
        }

        size_t pos = 12;
        while (pos + 8 <= fileLen) {
            uint32_t chunkLen = 0, chunkType = 0;
            memcpy(&chunkLen, file + pos, 4);
            memcpy(&chunkType, file + pos + 4, 4);
            pos += 8;
            if ((size_t)chunkLen > fileLen - pos) break;

            if (chunkType == 0x4E4F534A) {          /* "JSON" */
                jsonText = (const char *)(file + pos);
                jsonLen  = chunkLen;
            } else if (chunkType == 0x004E4942) {   /* "BIN"  */
                glbBin    = file + pos;
                glbBinLen = chunkLen;
            }
            pos += chunkLen;
            if (chunkLen & 3u) pos += 4u - (chunkLen & 3u);
        }
        isGlb = true;

        if (!jsonText) {
            if (options.verbose)
                fprintf(stderr, "[FastLoadGltf] %s: GLB has no JSON chunk\n", path);
            free(file);
            return empty;
        }
    } else {
        jsonText = (const char *)file;
        jsonLen  = fileLen;
    }

    /* ---- JSON ---- */
    JDoc jd;
    memset(&jd, 0, sizeof(jd));
    jd.text = jsonText;
    jd.len  = (jsonLen > (size_t)INT32_MAX) ? INT32_MAX : (int)jsonLen;

    JVal root = jvalue(&jd);
    if (jd.error || root.type != J_OBJ) {
        if (options.verbose)
            fprintf(stderr, "[FastLoadGltf] %s: invalid glTF JSON\n", path);
        free(file);
        return empty;
    }

    FgDoc doc;
    memset(&doc, 0, sizeof(doc));

    parse_buffers(&jd, &root, &doc);
    parse_buffer_views(&jd, &root, &doc);
    parse_accessors(&jd, &root, &doc);
    parse_meshes(&jd, &root, &doc);
    parse_nodes(&jd, &root, &doc);
    parse_materials(&jd, &root, &doc);
    parse_images(&jd, &root, &doc);
    parse_textures(&jd, &root, &doc);
    parse_roots(&jd, &root, &doc);

    /* ---- resolve buffers ---- */
    char *dir = path_dirname(path);
    FgBuffer *resolved = (FgBuffer *)calloc((size_t)(doc.bufferCount ? doc.bufferCount : 1),
                                            sizeof(FgBuffer));
    FgScene scene;
    scene.doc = &doc;
    scene.buffers = resolved;

    if (resolved && dir) {
        for (int i = 0; i < doc.bufferCount; ++i) {
            const FgBuffer *src = &doc.buffers[i];
            FgBuffer *dst = &resolved[i];
            dst->declaredLength = src->declaredLength;

            if (!src->uri) {
                /* The GLB BIN chunk backs the first buffer. */
                if (i == 0 && isGlb && glbBin) {
                    dst->data    = (unsigned char *)glbBin;
                    dst->dataLen = glbBinLen;
                    dst->owned   = false;
                }
                continue;
            }
            size_t len = 0;
            unsigned char *data = read_uri(src->uri, dir, &len);
            if (data) {
                dst->data    = data;
                dst->dataLen = len;
                dst->owned   = true;
            }
        }
    }

    /* ---- traverse the scene ---- */
    Walk w;
    memset(&w, 0, sizeof(w));
    w.s = &scene;
    w.scale = options.scale;

    if (resolved && dir && doc.nodeCount > 0 && doc.rootCount > 0) {
        w.meshCache = (Mesh *)calloc((size_t)doc.meshCount, sizeof(Mesh));
        w.meshBuilt = (bool *)calloc((size_t)doc.meshCount, sizeof(bool));
        w.visited   = (uint8_t *)calloc((size_t)doc.nodeCount, 1);
    }

    if (w.meshCache && w.meshBuilt && w.visited) {
        for (int i = 0; i < doc.rootCount; ++i)
            walk(&w, doc.roots[i], m4_identity(), 0);
    }

    /* ---- build materials for the materials actually referenced ---- */
    Material *modelMats   = NULL;
    int      *drawMat     = NULL;
    int       modelMatCount = 0;
    int      *usedList    = NULL;
    int       usedCount   = 0;
    TexCache  cache;
    memset(&cache, 0, sizeof(cache));

    if (w.drawCount > 0) {
        usedList = (int *)malloc((size_t)w.drawCount * sizeof(int));
        if (usedList) {
            for (int i = 0; i < w.drawCount; ++i) {
                int dm = w.draws[i].material;
                if (dm < 0 || dm >= doc.materialCount) continue;
                bool seen = false;
                for (int k = 0; k < usedCount; ++k)
                    if (usedList[k] == dm) { seen = true; break; }
                if (!seen) usedList[usedCount++] = dm;
            }
        }
        if (usedCount == 0) {
            usedList[0] = -1;
            usedCount = 1;
        }
        modelMatCount = usedCount;

        modelMats = (Material *)calloc((size_t)modelMatCount, sizeof(Material));
        drawMat   = (int *)malloc((size_t)w.drawCount * sizeof(int));

        if (modelMats && drawMat) {
            for (int s = 0; s < modelMatCount; ++s) {
                int dm = usedList[s];
                Material mat = LoadMaterialDefault();

                if (dm >= 0 && dm < doc.materialCount) {
                    const FgMaterial *fm = &doc.materials[dm];

                    mat.baseColor.r = linear_to_srgb8(clamp01f(fm->baseColor[0]));
                    mat.baseColor.g = linear_to_srgb8(clamp01f(fm->baseColor[1]));
                    mat.baseColor.b = linear_to_srgb8(clamp01f(fm->baseColor[2]));
                    mat.baseColor.a = (unsigned char)(clamp01f(fm->baseColor[3]) * 255.0f + 0.5f);

                    mat.metallic  = clamp01f(fm->metallic);
                    mat.roughness = clamp01f(fm->roughness);
                    mat.normalScale = fm->normalScale;
                    mat.aoStrength  = clamp01f(fm->occlusionStrength);

                    mat.emissionColor.r = linear_to_srgb8(clamp01f(fm->emissive[0]));
                    mat.emissionColor.g = linear_to_srgb8(clamp01f(fm->emissive[1]));
                    mat.emissionColor.b = linear_to_srgb8(clamp01f(fm->emissive[2]));
                    mat.emissionColor.a = 255;

                    float emax = fm->emissive[0];
                    if (fm->emissive[1] > emax) emax = fm->emissive[1];
                    if (fm->emissive[2] > emax) emax = fm->emissive[2];
                    mat.emissionStrength = (emax > 0.0f) ? 1.0f : 0.0f;

                    if (fm->baseColorTex.index >= 0) {
                        Texture2D t = texcache_get(&scene, &doc, dir,
                                                   fm->baseColorTex.index,
                                                   options.srgbTextures, &cache);
                        if (t.id) SetMaterialTexture(&mat, MATERIAL_MAP_ALBEDO, t);
                    }
                    if (fm->metalRoughTex.index >= 0) {
                        Texture2D t = texcache_get(&scene, &doc, dir,
                                                   fm->metalRoughTex.index,
                                                   false, &cache);
                        if (t.id) SetMaterialTexture(&mat, MATERIAL_MAP_METALLICROUGHNESS, t);
                    }
                    if (fm->normalTex.index >= 0) {
                        Texture2D t = texcache_get(&scene, &doc, dir,
                                                   fm->normalTex.index,
                                                   false, &cache);
                        if (t.id) SetMaterialTexture(&mat, MATERIAL_MAP_NORMAL, t);
                    }
                    if (fm->occlusionTex.index >= 0) {
                        Texture2D t = texcache_get(&scene, &doc, dir,
                                                   fm->occlusionTex.index,
                                                   false, &cache);
                        if (t.id) SetMaterialTexture(&mat, MATERIAL_MAP_AO, t);
                    }
                    if (fm->emissiveTex.index >= 0) {
                        Texture2D t = texcache_get(&scene, &doc, dir,
                                                   fm->emissiveTex.index,
                                                   options.srgbTextures, &cache);
                        if (t.id) SetMaterialTexture(&mat, MATERIAL_MAP_EMISSION, t);
                    }
                }
                modelMats[s] = mat;
            }

            for (int i = 0; i < w.drawCount; ++i) {
                int dm = w.draws[i].material;
                int slot = 0;
                for (int k = 0; k < usedCount; ++k) {
                    if (usedList[k] == dm) { slot = k; break; }
                }
                drawMat[i] = slot;
            }
        }
    }

    /* ---- hand the draw list to vgraphics ---- */
    Model model;
    memset(&model, 0, sizeof(model));

    if (w.drawCount > 0 && modelMats && drawMat) {
        Mesh   *meshes  = (Mesh *)malloc((size_t)w.drawCount * sizeof(Mesh));
        int    *matIdx  = (int *)malloc((size_t)w.drawCount * sizeof(int));
        Matrix *xforms  = (Matrix *)malloc((size_t)w.drawCount * sizeof(Matrix));

        if (meshes && matIdx && xforms) {
            for (int i = 0; i < w.drawCount; ++i) {
                meshes[i] = w.draws[i].mesh;
                matIdx[i] = drawMat[i];
                memcpy(&xforms[i], &w.draws[i].world, sizeof(Matrix));
            }
            if (options.verbose) {
                fprintf(stderr, "[FastLoadGltf] creating model: %d draw slot(s), "
                                "%d material(s), mesh ids:", w.drawCount, modelMatCount);
                for (int i = 0; i < w.drawCount && i < 8; ++i)
                    fprintf(stderr, " %u", meshes[i].id);
                fprintf(stderr, "\n");
                fflush(stderr);
            }
            model = vgraphics_CreateModelFromMeshesEx(meshes, matIdx, xforms,
                                                      w.drawCount,
                                                      modelMats, modelMatCount);
            if (options.verbose) {
                fprintf(stderr, "[FastLoadGltf] CreateModelFromMeshesEx -> id=%u\n",
                        model.id);
                fflush(stderr);
            }
        } else if (options.verbose) {
            fprintf(stderr, "[FastLoadGltf] allocation for the draw list failed\n");
            fflush(stderr);
        }
        free(meshes);
        free(matIdx);
        free(xforms);
    }

    /* ---- transfer texture ownership, or roll back ---- */
    if (model.id) {
        int tracked = 0;
        for (int i = 0; i < cache.count; ++i) {
            Texture2D t = cache.tex[i];
            if (!t.id) continue;
            bool dup = false;
            for (int k = 0; k < i; ++k)
                if (cache.tex[k].id == t.id) { dup = true; break; }
            if (dup) continue;
            if (vgraphics_TrackModelTexture(model, t)) tracked++;
            else UnloadTexture(t);
        }
        if (info) {
            info->ok            = true;
            info->drawCount     = w.drawCount;
            info->materialCount = modelMatCount;
            info->textureCount  = tracked;
            info->nodeCount     = doc.nodeCount;
            info->meshCount     = doc.meshCount;
        }
    } else {
        for (int i = 0; i < cache.count; ++i) {
            Texture2D t = cache.tex[i];
            if (!t.id) continue;
            bool dup = false;
            for (int k = 0; k < i; ++k)
                if (cache.tex[k].id == t.id) { dup = true; break; }
            if (!dup) UnloadTexture(t);
        }
        if (options.verbose && w.drawCount > 0)
            fprintf(stderr, "[FastLoadGltf] %s: could not create model\n", path);
    }

    /* ---- teardown ---- */
    if (options.verbose) {
        fprintf(stderr, "[FastLoadGltf] done: model.id=%u, meshes=%d, materials=%d, "
                        "textures=%d, nodes=%d, drawSlots=%d\n",
                model.id, doc.meshCount, modelMatCount,
                cache.count, doc.nodeCount, w.drawCount);
        fflush(stderr);
    }
    free(cache.gltfIndex);
    free(cache.tex);
    free(w.draws);
    free(w.meshCache);
    free(w.meshBuilt);
    free(w.visited);
    free(modelMats);
    free(drawMat);
    free(usedList);

    if (resolved) {
        for (int i = 0; i < doc.bufferCount; ++i)
            if (resolved[i].owned) free(resolved[i].data);
        free(resolved);
    }
    for (int i = 0; i < doc.bufferCount; ++i) free(doc.buffers[i].uri);
    for (int i = 0; i < doc.imageCount; ++i)  free(doc.images[i].uri);
    free(doc.buffers);
    free(doc.views);
    free(doc.accessors);
    free(doc.primitives);
    free(doc.meshes);
    free(doc.nodes);
    free(doc.children);
    free(doc.materials);
    free(doc.images);
    free(doc.textures);
    free(doc.roots);
    free(dir);
    free(file);

    return model;
}

Model FastLoadGltf(const char *path) {
    return fastload(path, NULL, NULL);
}

Model FastLoadGltfEx(const char *path, const FastLoadGltfOptions *options,
                     FastLoadGltfInfo *info)
{
    return fastload(path, options, info);
}
