#include "vtext.h"
#include "vshapes.h"
#include "vfiles.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* A borrowed copy of the first successfully loaded font backs the simple
   DrawText()/MeasureText() entry points. Explicitly loaded Font values remain
   owned by the caller. */
static Font g_defaultFont = {0};

/* ------------------------------------------------------------------ */
/*  Загрузка шрифта                                                   */
/* ------------------------------------------------------------------ */

Font LoadFont(const char *path) {
    return LoadFontEx(path, 48, 32, 96);
}

Font LoadFontEx(const char *path, int fontSize, int first, int count) {
    Font font = {0};

    if (!path || first < 0 || first > 0x10ffff) return font;

    if (fontSize < 8)   fontSize = 8;
    if (fontSize > 128) fontSize = 128;
    if (count <= 0)     count = 96;
    if (count > 65536 || count > 0x110000 - first) return font;

    unsigned int sz = 0;
    unsigned char *ttf = LoadFileData(path, &sz);
    if (!ttf) {
        fprintf(stderr, "[vlcrlib] LoadFont: cannot open %s\n", path);
        return font;
    }
    if (sz == 0) {
        fprintf(stderr, "[vlcrlib] LoadFont: empty file %s\n", path);
        UnloadFileData(ttf);
        return font;
    }

    int AW = 1024, AH = 1024;
    unsigned char *bitmap = calloc((size_t)AW * AH, 1);
    stbtt_bakedchar *baked = malloc(sizeof(stbtt_bakedchar) * count);
    if (!bitmap || !baked) {
        fprintf(stderr, "[vlcrlib] LoadFont: out of memory (atlas)\n");
        UnloadFileData(ttf);
        free(bitmap);
        free(baked);
        return font;
    }

    int rc = stbtt_BakeFontBitmap(ttf, 0, (float)fontSize,
                                  bitmap, AW, AH,
                                  first, count, baked);
    UnloadFileData(ttf);

    if (rc < 0) {
        fprintf(stderr, "[vlcrlib] stbtt_BakeFontBitmap: %d glyphs missing (size=%d, atlas %dx%d)\n",
                -rc, fontSize, AW, AH);
        free(bitmap);
        free(baked);
        return font;
    }

    /* Проверяем, что все запрошенные глифы реально испеклись.
       stbtt возвращает положительный rc, даже если часть не влезла. */
    int bakedCount = 0;
    for (int i = 0; i < count; ++i) {
        if (baked[i].xadvance > 0.0f) bakedCount++;
    }
    if (bakedCount < count) {
        fprintf(stderr, "[vlcrlib] Warning: only %d/%d glyphs baked for %s (size=%d)\n",
                bakedCount, count, path, fontSize);
    }

    /* Конвертируем coverage-маску в RGBA, где A = coverage, RGB = белый.
       Текстура создаётся как UNORM — корректно для tint * atlas. */
    unsigned char *rgba = malloc((size_t)AW * AH * 4);
    if (!rgba) {
        free(bitmap);
        free(baked);
        return font;
    }
    for (int i = 0; i < AW * AH; ++i) {
        rgba[i*4 + 0] = 255;
        rgba[i*4 + 1] = 255;
        rgba[i*4 + 2] = 255;
        rgba[i*4 + 3] = bitmap[i];
    }
    free(bitmap);

    Image img = { .data = rgba, .width = AW, .height = AH,
                  .mipmaps = 1, .format = 0 };
    font.atlas = LoadTextureFromImage(img);
    free(rgba);

    font.glyphs   = baked;
    font.fontSize = fontSize;
    font.atlasW   = AW;
    font.atlasH   = AH;
    font.first    = first;
    font.count    = count;
    font.ok       = (font.atlas.id != 0) ? 1 : 0;
    if (font.ok && !g_defaultFont.ok) g_defaultFont = font;
    if (!font.ok) {
        free(font.glyphs);
        font.glyphs = NULL;
    }
    return font;
}

void UnloadFont(Font font) {
    if (g_defaultFont.ok && g_defaultFont.glyphs == font.glyphs)
        g_defaultFont = (Font){0};
    if (font.glyphs) free(font.glyphs);
    if (font.atlas.id) UnloadTexture(font.atlas);
}

void vtext_EnsureDefaultFont(void) {
    if (g_defaultFont.ok) return;

    const char *paths[8];
    int n = 0;
#ifdef _WIN32
    paths[n++] = "C:/Windows/Fonts/segoeui.ttf";
    paths[n++] = "C:/Windows/Fonts/arial.ttf";
    paths[n++] = "C:/Windows/Fonts/tahoma.ttf";
#elif defined(__ANDROID__)
    paths[n++] = "/system/fonts/Roboto-Regular.ttf";
    paths[n++] = "/system/fonts/DroidSans.ttf";
    paths[n++] = "assets/Roboto-Medium.ttf";
#elif defined(__EMSCRIPTEN__)
    paths[n++] = "assets/Roboto-Medium.ttf";
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    paths[n++] = "/System/Library/Fonts/Core/Helvetica.ttc";
    paths[n++] = "assets/Roboto-Medium.ttf";
#else
    paths[n++] = "/System/Library/Fonts/Supplemental/Arial.ttf";
    paths[n++] = "/System/Library/Fonts/Helvetica.ttc";
#endif
#else
    paths[n++] = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    paths[n++] = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf";
    paths[n++] = "/usr/share/fonts/truetype/freefont/FreeSans.ttf";
    paths[n++] = "assets/Roboto-Medium.ttf";
#endif
    paths[n] = NULL;

    for (int i = 0; i < n; ++i) {
        Font f = LoadFontEx(paths[i], 32, 32, 96);
        if (f.ok) return;
    }
    fprintf(stderr, "[vlcrlib] no default UI font found\n");
}

Font GetFontDefault(void) {
    return g_defaultFont;
}

/* Decode one UTF-8 scalar, consuming one byte for malformed sequences so a
   corrupt string cannot stall text drawing. */
static unsigned int nextCodepoint(const char **cursor) {
    const unsigned char *s = (const unsigned char *)*cursor;
    unsigned int cp;
    int extra;
    if (s[0] < 0x80) { *cursor += 1; return s[0]; }
    if ((s[0] & 0xe0) == 0xc0) { cp = s[0] & 0x1f; extra = 1; if (cp < 2) { *cursor += 1; return 0xfffd; } }
    else if ((s[0] & 0xf0) == 0xe0) { cp = s[0] & 0x0f; extra = 2; }
    else if ((s[0] & 0xf8) == 0xf0) { cp = s[0] & 0x07; extra = 3; if (cp > 4) { *cursor += 1; return 0xfffd; } }
    else { *cursor += 1; return 0xfffd; }

    for (int i = 1; i <= extra; ++i) {
        if (s[i] == 0 || (s[i] & 0xc0) != 0x80) { *cursor += 1; return 0xfffd; }
        cp = (cp << 6) | (s[i] & 0x3f);
    }
    if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        *cursor += 1;
        return 0xfffd;
    }
    *cursor += extra + 1;
    return cp;
}

/* ------------------------------------------------------------------ */
/*  Отрисовка текста — pos = TOP-LEFT (не baseline)                   */
/* ------------------------------------------------------------------ */

void DrawTextEx(Font font, const char *text, Vector2 pos,
                float fontSize, float spacing, Color tint)
{
    if (!font.ok || !text || !*text) return;
    size_t n = strlen(text);
    if (n == 0) return;

    Vertex2D *verts = malloc(sizeof(Vertex2D) * 6 * n);
    int vcount = 0;

    float scale = fontSize / (float)font.fontSize;
    stbtt_bakedchar *bc = (stbtt_bakedchar *)font.glyphs;

    /* сдвигаем базовую линию вниз, чтобы pos.y означал TOP */
    float maxAboveBaseline = 0.0f;
    for (int i = 0; i < font.count; ++i) {
        float asc = -(float)bc[i].yoff;
        if (asc > maxAboveBaseline) maxAboveBaseline = asc;
    }
    float baseYOffset = maxAboveBaseline * scale;

    float cursor = pos.x;
    float baseY  = pos.y + baseYOffset;

    float invAW = 1.0f / (float)font.atlasW;
    float invAH = 1.0f / (float)font.atlasH;

    for (const char *p = text; *p;) {
        unsigned int c = nextCodepoint(&p);
        if (c == '\n') {
            cursor = pos.x;
            baseY += font.fontSize * scale * 1.2f;
            continue;
        }
        if (c < (unsigned int)font.first || c >= (unsigned int)font.first + (unsigned int)font.count) continue;

        stbtt_bakedchar *b = &bc[c - font.first];

        float x0 = cursor + b->xoff * scale;
        float y0 = baseY  + b->yoff * scale;
        float x1 = x0 + (b->x1 - b->x0) * scale;
        float y1 = y0 + (b->y1 - b->y0) * scale;

        float u0 = b->x0 * invAW, v0 = b->y0 * invAH;
        float u1 = b->x1 * invAW, v1 = b->y1 * invAH;

        unsigned char R = tint.r, G = tint.g, B = tint.b, A = tint.a;

        verts[vcount++] = (Vertex2D){ x0, y0, u0, v0, R, G, B, A };
        verts[vcount++] = (Vertex2D){ x1, y0, u1, v0, R, G, B, A };
        verts[vcount++] = (Vertex2D){ x1, y1, u1, v1, R, G, B, A };
        verts[vcount++] = (Vertex2D){ x0, y0, u0, v0, R, G, B, A };
        verts[vcount++] = (Vertex2D){ x1, y1, u1, v1, R, G, B, A };
        verts[vcount++] = (Vertex2D){ x0, y1, u0, v1, R, G, B, A };

        cursor += b->xadvance * scale + spacing;
    }

    vshapes_FlushBatch();
    if (vcount > 0) vgraphics_DrawVertexBuffer(verts, vcount, font.atlas);
    free(verts);
}

void DrawText(const char *text, int x, int y, int fontSize, Color color) {
    Font f = GetFontDefault();
    if (!f.ok) return;
    DrawTextEx(f, text, (Vector2){ (float)x, (float)y },
               (float)fontSize, 1.0f, color);
}

/* ------------------------------------------------------------------ */
/*  Измерение                                                         */
/* ------------------------------------------------------------------ */

Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing) {
    Vector2 size = {0};
    if (!font.ok || !text) return size;

    float scale  = fontSize / (float)font.fontSize;
    float cursor = 0.0f;
    float maxH   = font.fontSize * scale;

    stbtt_bakedchar *bc = (stbtt_bakedchar *)font.glyphs;

    for (const char *p = text; *p;) {
        unsigned int c = nextCodepoint(&p);
        if (c == '\n') {
            if (cursor > size.x) size.x = cursor;
            cursor = 0;
            maxH += font.fontSize * scale * 1.2f;
            continue;
        }
        if (c < (unsigned int)font.first || c >= (unsigned int)font.first + (unsigned int)font.count) continue;
        stbtt_bakedchar *b = &bc[c - font.first];
        cursor += b->xadvance * scale + spacing;
    }
    if (cursor > size.x) size.x = cursor;
    size.y = maxH;
    return size;
}

int MeasureText(const char *text, int fontSize) {
    Font f = GetFontDefault();
    if (!f.ok) return 0;
    return (int)MeasureTextEx(f, text, (float)fontSize, 1.0f).x;
}
