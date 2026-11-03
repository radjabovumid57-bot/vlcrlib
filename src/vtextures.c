#include "vtextures.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Image LoadImage(const char *path) {
    Image img = {0};
    int w = 0, h = 0, ch = 0;
    unsigned char *data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) {
        fprintf(stderr, "[vlcrlib] LoadImage failed: %s\n", path);
        return img;
    }
    img.data    = data;
    img.width   = w;
    img.height  = h;
    img.mipmaps = 1;
    img.format  = 0;   /* RGBA8 */
    return img;
}

Image LoadImageFromMemory(const char *fileType, const unsigned char *data, int size) {
    (void)fileType;
    Image img = {0};
    int w = 0, h = 0, ch = 0;
    unsigned char *pix = stbi_load_from_memory(data, size, &w, &h, &ch, 4);
    if (!pix) return img;
    img.data    = pix;
    img.width   = w;
    img.height  = h;
    img.mipmaps = 1;
    img.format  = 0;
    return img;
}

void UnloadImage(Image image) {
    if (image.data) stbi_image_free(image.data);
}

Texture2D LoadTexture(const char *path) {
    if (path) {
        const char *ext = strrchr(path, '.');
        if (ext) {
            char e[8] = {0};
            for (int i = 0; i < 7 && ext[i]; ++i)
                e[i] = (char)((ext[i] >= 'A' && ext[i] <= 'Z') ? ext[i] + 32 : ext[i]);
            if (strcmp(e, ".ktx2") == 0)
                return LoadTextureKTX2(path);
        }
    }
    Image img = LoadImage(path);
    if (!img.data) return (Texture2D){0};
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

/* ---------- Drawing ---------- */
void DrawTexture(Texture2D texture, int x, int y, Color tint) {
    DrawTextureRec(texture,
                   (Rectangle){ 0, 0, (float)texture.width, (float)texture.height },
                   (Vector2){ (float)x, (float)y }, tint);
}

void DrawTextureV(Texture2D texture, Vector2 position, Color tint) {
    DrawTextureRec(texture,
                   (Rectangle){ 0, 0, (float)texture.width, (float)texture.height },
                   position, tint);
}

void DrawTextureRec(Texture2D texture, Rectangle source, Vector2 position, Color tint) {
    if (texture.id == 0 || texture.width == 0 || texture.height == 0) return;

    float u0 = source.x / texture.width;
    float v0 = source.y / texture.height;
    float u1 = (source.x + source.width)  / texture.width;
    float v1 = (source.y + source.height) / texture.height;

    Vector2 p0 = position;
    Vector2 p1 = { position.x + source.width, position.y };
    Vector2 p2 = { position.x, position.y + source.height };
    Vector2 p3 = { position.x + source.width, position.y + source.height };

    Vertex2D v[6];
    v[0] = (Vertex2D){ p0.x, p0.y, u0, v0, tint.r, tint.g, tint.b, tint.a };
    v[1] = (Vertex2D){ p2.x, p2.y, u0, v1, tint.r, tint.g, tint.b, tint.a };
    v[2] = (Vertex2D){ p1.x, p1.y, u1, v0, tint.r, tint.g, tint.b, tint.a };
    v[3] = (Vertex2D){ p1.x, p1.y, u1, v0, tint.r, tint.g, tint.b, tint.a };
    v[4] = (Vertex2D){ p2.x, p2.y, u0, v1, tint.r, tint.g, tint.b, tint.a };
    v[5] = (Vertex2D){ p3.x, p3.y, u1, v1, tint.r, tint.g, tint.b, tint.a };

    vgraphics_DrawVertexBuffer(v, 6, texture);
}

void DrawTextureEx(Texture2D texture, Vector2 position, float rotation, float scale, Color tint) {
    /* Рисуем 4 угла, повёрнутые относительно position */
    float w = texture.width  * scale;
    float h = texture.height * scale;
    float c = cosf(rotation), s = sinf(rotation);

    Vector2 pts[4] = {
        { 0, 0 }, { w, 0 }, { w, h }, { 0, h }
    };
    for (int i = 0; i < 4; ++i) {
        float dx = pts[i].x, dy = pts[i].y;
        pts[i].x = position.x + dx * c - dy * s;
        pts[i].y = position.y + dx * s + dy * c;
    }

    Vertex2D v[6];
    v[0] = (Vertex2D){ pts[0].x, pts[0].y, 0, 0, tint.r, tint.g, tint.b, tint.a };
    v[1] = (Vertex2D){ pts[3].x, pts[3].y, 0, 1, tint.r, tint.g, tint.b, tint.a };
    v[2] = (Vertex2D){ pts[1].x, pts[1].y, 1, 0, tint.r, tint.g, tint.b, tint.a };
    v[3] = (Vertex2D){ pts[1].x, pts[1].y, 1, 0, tint.r, tint.g, tint.b, tint.a };
    v[4] = (Vertex2D){ pts[3].x, pts[3].y, 0, 1, tint.r, tint.g, tint.b, tint.a };
    v[5] = (Vertex2D){ pts[2].x, pts[2].y, 1, 1, tint.r, tint.g, tint.b, tint.a };

    vgraphics_DrawVertexBuffer(v, 6, texture);
}

void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                    Vector2 origin, float rotation, Color tint)
{
    (void)origin; (void)rotation;
    /* упрощённая версия — просто рисуем в dest */
    if (texture.id == 0) return;
    float w = dest.width  > 0 ? dest.width  : source.width;
    float h = dest.height > 0 ? dest.height : source.height;
    DrawTextureRec(texture, source, (Vector2){ dest.x, dest.y }, tint);
    (void)w; (void)h;
}

/* ---------- Image manipulation ---------- */
void ImageFlipVertical(Image *image) {
    if (!image || !image->data) return;
    int bpp = 4;
    int rowSize = image->width * bpp;
    unsigned char *top    = (unsigned char*)image->data;
    unsigned char *bottom = (unsigned char*)image->data + (image->height - 1) * rowSize;
    unsigned char *tmp    = malloc(rowSize);

    for (int y = 0; y < image->height / 2; ++y) {
        memcpy(tmp,    top,    rowSize);
        memcpy(top,    bottom, rowSize);
        memcpy(bottom, tmp,    rowSize);
        top    += rowSize;
        bottom -= rowSize;
    }
    free(tmp);
}

void ImageFlipHorizontal(Image *image) {
    if (!image || !image->data) return;
    int bpp = 4;
    for (int y = 0; y < image->height; ++y) {
        unsigned char *row = (unsigned char*)image->data + y * image->width * bpp;
        for (int x = 0; x < image->width / 2; ++x) {
            for (int c = 0; c < bpp; ++c) {
                unsigned char t = row[x*bpp + c];
                row[x*bpp + c] = row[(image->width - 1 - x)*bpp + c];
                row[(image->width - 1 - x)*bpp + c] = t;
            }
        }
    }
}

void ExportImage(Image image, const char *path) {
    if (!image.data) return;
    stbi_write_png(path, image.width, image.height, 4,
                   image.data, image.width * 4);
}