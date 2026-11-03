/* ktx_preview.c — inspect a cooked KTX2 BC7/BC5 texture in the 2D pipeline. */
#include <vlcrlib.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: ktx_preview <file.ktx2>\n");
        return 2;
    }
    if (!InitWindow(800, 600, "vlcrlib — KTX2 preview")) return 1;
    SetTargetFPS(60);

    Texture2D tex = LoadTextureKTX2(argv[1]);
    fprintf(stderr, "[ktx_preview] id=%u %dx%d %s\n",
            tex.id, tex.width, tex.height, argv[1]);

    int frames = 0;
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 30, 32, 38, 255 });
        if (tex.id)
            DrawTexture(tex, 0, 0, WHITE);
        else
            DrawText("KTX2 LOAD FAILED", 20, 20, 24, RED);
        EndDrawing();
        frames++;
        const char *shot = getenv("VLCR_SCREENSHOT");
        if (shot && frames == 5) vgraphics_CaptureScreenshot(shot);
    }
    if (tex.id) UnloadTexture(tex);
    CloseWindow();
    return 0;
}
