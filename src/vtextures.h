#ifndef VLCRLIB_VTEXTURES_H
#define VLCRLIB_VTEXTURES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vgraphics.h"

/* ---------- Image (CPU-side) ---------- */
Image LoadImage(const char *path);
Image LoadImageFromMemory(const char *fileType, const unsigned char *data, int size);
void  UnloadImage(Image image);

/* ---------- Texture (GPU-side) ---------- */
Texture2D LoadTexture(const char *path);
Texture2D LoadTextureFromImage(Image image);   /* реализовано в vgraphics.c */
void      UnloadTexture(Texture2D texture);    /* реализовано в vgraphics.c */

/* ---------- Drawing ---------- */
void DrawTexture(Texture2D texture, int x, int y, Color tint);
void DrawTextureV(Texture2D texture, Vector2 position, Color tint);
void DrawTextureRec(Texture2D texture, Rectangle source, Vector2 position, Color tint);
void DrawTextureEx(Texture2D texture, Vector2 position, float rotation, float scale, Color tint);
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                    Vector2 origin, float rotation, Color tint);

/* ---------- Image manipulation ---------- */
void ImageFlipVertical(Image *image);
void ImageFlipHorizontal(Image *image);
void ExportImage(Image image, const char *path);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VTEXTURES_H */