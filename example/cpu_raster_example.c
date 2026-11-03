/* cpu_raster_example.c — CPU-backend MVP: software triangles + SDL present. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

static void cube(VCpuCanvas *c, Matrix mvp, Color col) {
    const float s = 0.7f;
    const Vector3 p[8] = {
        {-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s},
        {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}
    };
    const int f[6][4] = {
        {0,1,2,3}, {4,5,6,7}, {0,1,5,4},
        {2,3,7,6}, {1,2,6,5}, {3,0,4,7}
    };
    for (int i = 0; i < 6; ++i) {
        Vector3 a = p[f[i][0]], b = p[f[i][1]];
        Vector3 d = p[f[i][2]], e = p[f[i][3]];
        VCpuTriangle3D(c, a, b, d, col, mvp);
        VCpuTriangle3D(c, a, d, e, col, mvp);
    }
}

int main(void) {
    const int W = 800, H = 600;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("vlcrlib CPU rasterizer", W, H, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    SDL_Texture *tex = ren
        ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, W, H)
        : NULL;
    VCpuCanvas canvas;
    if (!win || !ren || !tex || !VCpuInit(&canvas, W, H)) return 1;

    uint64_t start = SDL_GetTicks();
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = 0;
            if (ev.type == SDL_EVENT_KEY_DOWN &&
                ev.key.key == SDLK_ESCAPE) running = 0;
        }
        float t = (SDL_GetTicks() - start) / 1000.0f;
        VCpuClear(&canvas, (Color){18, 20, 28, 255});
        VCpuClearDepth(&canvas);

        Matrix view = MatrixLookAt((Vector3){3.2f, 2.4f, 4.2f},
                                   (Vector3){0, 0, 0}, (Vector3){0, 1, 0});
        Matrix proj = MatrixPerspective(50.0f * VLCRLIB_DEG2RAD,
                                        (float)W / (float)H, 0.1f, 100.0f);
        /* CPU framebuffer is top-left; undo the Vulkan Y flip. */
        proj.m5 = -proj.m5;
        Matrix model = MatrixMultiply(MatrixRotateY(t * 0.8f),
                                      MatrixRotateX(t * 0.47f));
        Matrix mvp = MatrixMultiply(proj, MatrixMultiply(view, model));
        cube(&canvas, mvp, (Color){220, 120, 70, 255});

        SDL_UpdateTexture(tex, NULL, canvas.color, W * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    VCpuFree(&canvas);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
