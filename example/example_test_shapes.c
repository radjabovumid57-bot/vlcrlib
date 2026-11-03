/*
 * example_test_shapes.c — 2D/3D shape draw smoke
 */
#include <vlcrlib.h>
#include <stdio.h>

static int fails = 0;
static void expect(int c, const char *n) {
    if (c) fprintf(stderr, "[PASS] %s\n", n);
    else { fprintf(stderr, "[FAIL] %s\n", n); fails++; }
}

int main(void) {
    if (!InitWindow(640, 480, "shapes test")) return 1;

    for (int f = 0; f < 5; ++f) {
        BeginDrawing();
        ClearBackground((Color){40, 40, 50, 255});
        DrawRectangle(10, 10, 100, 60, (Color){200, 80, 80, 255});
        DrawCircle(200, 100, 40, (Color){80, 180, 80, 255});
        DrawLine(0, 0, 640, 480, WHITE);
        DrawText("shapes", 20, 200, 20, WHITE);
        EndDrawing();
    }
    expect(1, "2d shapes 5 frames");

    Camera3D cam = {0};
    cam.position = (Vector3){3, 3, 3};
    cam.target = (Vector3){0, 0, 0};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 45; cam.nearPlane = 0.1f; cam.farPlane = 50.f;

    BeginDrawing();
    ClearBackground((Color){30, 30, 40, 255});
    BeginMode3D(cam);
    DrawGrid(6, 1.0f);
    DrawCube((Vector3){0, 0.5f, 0}, (Vector3){1, 1, 1}, (Color){180, 120, 60, 255});
    EndMode3D();
    EndDrawing();
    expect(1, "3d grid+cube");

    CloseWindow();
    fprintf(stderr, "[shapes_test] fails=%d\n", fails);
    return fails ? 1 : 0;
}
