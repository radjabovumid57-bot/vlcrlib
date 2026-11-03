#ifndef VLCRLIB_VTEXT_H
#define VLCRLIB_VTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vgraphics.h"

typedef struct Font {
    Texture2D atlas;         /* RGBA atlas, A = coverage          */
    void     *glyphs;        /* stbtt_bakedchar* — opaque         */
    int       fontSize;      /* размер, под который испечён атлас */
    int       atlasW, atlasH;
    int       first, count;  /* диапазон глифов (ASCII 32..126)   */
    int       ok;            /* 1 — валидный, 0 — пустой          */
} Font;

Font    LoadFont(const char *path);
Font    LoadFontEx(const char *path, int fontSize, int first, int count);
void    UnloadFont(Font font);
Font    GetFontDefault(void);
/* Load a platform default font once so DrawText()/MeasureText() work in
   examples that never call LoadFontEx() explicitly. Safe to call repeatedly. */
void    vtext_EnsureDefaultFont(void);

void    DrawText(const char *text, int x, int y, int fontSize, Color color);
void    DrawTextEx(Font font, const char *text, Vector2 pos,
                   float fontSize, float spacing, Color tint);

Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing);
int     MeasureText(const char *text, int fontSize);

#ifdef __cplusplus
}
#endif
#endif