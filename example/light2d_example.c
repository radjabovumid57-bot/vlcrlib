#include <vlcrlib.h>
#include "vlight.h"
#include "vinput.h"
#include <SDL3/SDL_scancode.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct Obstacle {
    Rectangle rect;
    bool      active;
} Obstacle;

#define MAX_OBSTACLES 16

/* ============================================================ */
/*  Ray vs Rect                                                  */
/* ============================================================ */
static bool ray_vs_rect(Vector2 origin, Vector2 dir,
                        Rectangle r, float *outT, Vector2 *outHit)
{
    float tmin = 0.0f, tmax = 1e9f;

    if (fabsf(dir.x) > 1e-8f) {
        float t1 = (r.x           - origin.x) / dir.x;
        float t2 = (r.x + r.width - origin.x) / dir.x;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
    } else if (origin.x < r.x || origin.x > r.x + r.width) {
        return false;
    }

    if (fabsf(dir.y) > 1e-8f) {
        float t1 = (r.y            - origin.y) / dir.y;
        float t2 = (r.y + r.height - origin.y) / dir.y;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
    } else if (origin.y < r.y || origin.y > r.y + r.height) {
        return false;
    }

    if (tmin > tmax || tmax < 0.0f) return false;

    *outT = tmin;
    outHit->x = origin.x + dir.x * tmin;
    outHit->y = origin.y + dir.y * tmin;
    return true;
}

/* ============================================================ */
/*  Световое пятно                                               */
/* ============================================================ */
static void DrawLight(VLight2D light, float pulse)
{
    Color core = VLightColorAt2D(light, light.position);
    float radius = light.range * pulse;

    DrawCircleV(light.position, radius,
                (Color){ core.r, core.g, core.b, 12 });
    DrawCircleV(light.position, radius * 0.65f,
                (Color){ core.r, core.g, core.b, 22 });
    DrawCircleV(light.position, radius * 0.28f,
                (Color){ core.r, core.g, core.b, 45 });
    DrawCircleV(light.position, 8.0f, WHITE);
    DrawCircleLines((int)light.position.x, (int)light.position.y,
                    radius, (Color){ core.r, core.g, core.b, 180 });
}

/* ============================================================ */
/*  Тени — клиньями                                              */
/* ============================================================ */
static void DrawShadowClip(VLight2D light, Obstacle *obs, int count, float range)
{
    for (int i = 0; i < count; ++i) {
        if (!obs[i].active) continue;

        Rectangle r = obs[i].rect;

        if (light.position.x > r.x && light.position.x < r.x + r.width &&
            light.position.y > r.y && light.position.y < r.y + r.height) {
            continue;
        }

        Vector2 corners[4] = {
            { r.x,             r.y              },
            { r.x + r.width,   r.y              },
            { r.x + r.width,   r.y + r.height   },
            { r.x,             r.y + r.height   },
        };

        for (int e = 0; e < 4; ++e) {
            Vector2 a = corners[e];
            Vector2 b = corners[(e + 1) % 4];

            Vector2 edge    = { b.x - a.x, b.y - a.y };
            Vector2 normal  = { edge.y, -edge.x };
            Vector2 toLight = { light.position.x - a.x,
                                light.position.y - a.y };

            if (normal.x * toLight.x + normal.y * toLight.y <= 0.0f) continue;

            Vector2 da = { a.x - light.position.x, a.y - light.position.y };
            Vector2 db = { b.x - light.position.x, b.y - light.position.y };
            float la = sqrtf(da.x * da.x + da.y * da.y);
            float lb = sqrtf(db.x * db.x + db.y * db.y);
            if (la < 0.001f || lb < 0.001f) continue;

            Vector2 aFar = { a.x + da.x / la * range * 2.0f,
                             a.y + da.y / la * range * 2.0f };
            Vector2 bFar = { b.x + db.x / lb * range * 2.0f,
                             b.y + db.y / lb * range * 2.0f };

            DrawTriangle(a, b, aFar, (Color){ 0, 0, 0, 220 });
            DrawTriangle(b, bFar, aFar, (Color){ 0, 0, 0, 220 });
        }
    }
}

/* ============================================================ */
/*  Main                                                         */
/* ============================================================ */
int main(void)
{
    if (!InitWindow(1280, 720, "vlcrlib 2D lights + VInput")) return 1;
    SetTargetFPS(60);

    /* ---------- Input ---------- */
    VInputState input;
    VInputInit(&input);

    VInputBindKey(&input, "next_light",     SDL_SCANCODE_TAB);
    VInputBindKey(&input, "toggle_shadows", SDL_SCANCODE_F1);
    VInputBindKey(&input, "toggle_debug",   SDL_SCANCODE_F2);
    VInputBindKey(&input, "sprint",         SDL_SCANCODE_LSHIFT);
    VInputBindMouseButton(&input, "drag_light",  MOUSE_BUTTON_LEFT);
    VInputBindMouseButton(&input, "drag_pillar", MOUSE_BUTTON_RIGHT);

    VInputBindAxisKeys(&input, "move_x", SDL_SCANCODE_A, SDL_SCANCODE_D);
    VInputBindAxisKeys(&input, "move_y", SDL_SCANCODE_W, SDL_SCANCODE_S);

    /* ---------- GUI ---------- */
    VGuiContext gui;
    memset(&gui, 0, sizeof(gui));
    gui.theme = VGuiDefaultTheme();

    Font uiFont = {0};
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 24, 24, 96);
#endif

    bool showPoint   = true;
    bool showSpot    = true;
    bool showPulse   = true;
    bool showShadows = true;
    bool showDebug   = false;

    /* ---------- Свет ---------- */
    VLight2D point = VLight2DPoint((Vector2){ 300, 330 }, ORANGE, 1.0f, 220.0f);
    VLight2D spot  = VLight2DSpot((Vector2){ 650, 320 }, (Vector2){ 1, 0 },
                                  SKYBLUE, 1.0f, 280.0f,
                                  35.0f * VLCRLIB_DEG2RAD,
                                  85.0f * VLCRLIB_DEG2RAD);
    VLight2D pulse = VLight2DPoint((Vector2){ 1010, 390 }, GOLD, 1.0f, 200.0f);

    int activeLight = 0;
    VLight2D *lightPtrs[3] = { &point, &spot, &pulse };

    /* ---------- Стены ---------- */
    Obstacle obstacles[MAX_OBSTACLES];
    memset(obstacles, 0, sizeof(obstacles));
    int obsCount = 0;

    obstacles[obsCount++] = (Obstacle){ { 200, 100,  30, 500 }, true };
    obstacles[obsCount++] = (Obstacle){ { 200, 100, 880,  30 }, true };
    obstacles[obsCount++] = (Obstacle){ {1050, 100,  30, 220 }, true };
    obstacles[obsCount++] = (Obstacle){ {1050, 450,  30, 150 }, true };
    obstacles[obsCount++] = (Obstacle){ { 200, 570, 500,  30 }, true };
    obstacles[obsCount++] = (Obstacle){ { 840, 570, 240,  30 }, true };
    obstacles[obsCount++] = (Obstacle){ { 400, 220,  40, 260 }, true };
    obstacles[obsCount++] = (Obstacle){ { 560, 380, 120,  40 }, true };
    obstacles[obsCount++] = (Obstacle){ { 820, 240,  60,  60 }, true };
    obstacles[obsCount++] = (Obstacle){ { 700, 450,  80,  80 }, true };

    int movedObstacle = 6;

    /* ============================================================ */
    /*  MAIN LOOP                                                    */
    /* ============================================================ */
    while (!WindowShouldClose()) {
        float dt   = GetFrameTime();
        float time = (float)GetTime();

        /* ---- Update input ---- */
        VInputUpdate(&input, dt);

        /* ---- Actions ---- */
        if (VInputIsActionPressed(&input, "next_light")) {
            activeLight = (activeLight + 1) % 3;
        }
        if (VInputIsActionPressed(&input, "toggle_shadows")) {
            showShadows = !showShadows;
        }
        if (VInputIsActionPressed(&input, "toggle_debug")) {
            showDebug = !showDebug;
        }

        bool sprint = VInputIsActionDown(&input, "sprint");
        Vector2 mouse = VInputMousePosition(&input);

        /* ---- Move lights/pillar ---- */
        if (VInputIsActionDown(&input, "drag_light") && !sprint) {
            lightPtrs[activeLight]->position = mouse;
        }
        if (VInputIsActionDown(&input, "drag_pillar") && sprint) {
            obstacles[movedObstacle].rect.x =
                mouse.x - obstacles[movedObstacle].rect.width  * 0.5f;
            obstacles[movedObstacle].rect.y =
                mouse.y - obstacles[movedObstacle].rect.height * 0.5f;
        }

        /* ---- WASD move active light ---- */
        {
            float wx = VInputGetAxis(&input, "move_x");
            float wy = VInputGetAxis(&input, "move_y");
            if (fabsf(wx) > 0.01f || fabsf(wy) > 0.01f) {
                lightPtrs[activeLight]->position.x += wx * 300.0f * dt;
                lightPtrs[activeLight]->position.y += wy * 300.0f * dt;
            }
        }

        /* ---- Animation for non-active lights ---- */
        if (activeLight != 0) {
            point.position.x = 300.0f + cosf(time) * 90.0f;
            point.position.y = 330.0f + sinf(time * 1.4f) * 55.0f;
        }
        if (activeLight != 1) {
            spot.direction.x = cosf(time);
            spot.direction.y = sinf(time);
            spot.direction = Vector2Normalize(spot.direction);
        }
        if (activeLight != 2) {
            pulse.position.y = 390.0f + sinf(time * 3.0f) * 50.0f;
        }

        /* ============================================================ */
        /*  RENDER                                                       */
        /* ============================================================ */
        BeginDrawing();
        ClearBackground((Color){ 14, 18, 30, 255 });

        DrawRectangle(0, 0, 1280, 720, (Color){ 22, 28, 46, 255 });

        /* grid */
        for (int x = 0; x < 1280; x += 64) {
            DrawLine(x, 0, x, 720, (Color){ 42, 52, 72, 160 });
        }
        for (int y = 0; y < 720; y += 64) {
            DrawLine(0, y, 1280, y, (Color){ 42, 52, 72, 160 });
        }

        /* walls behind lights */
        for (int i = 0; i < obsCount; ++i) {
            if (!obstacles[i].active) continue;
            DrawRectangleRec(obstacles[i].rect, (Color){ 115, 125, 145, 255 });
        }

        /* lights */
        if (showPoint) DrawLight(point, 1.0f);
        if (showSpot)  DrawLight(spot,  1.0f);
        if (showPulse) DrawLight(pulse, 0.9f + 0.15f * sinf(time * 3.0f));

        /* shadows */
        if (showShadows) {
            if (showPoint) DrawShadowClip(point, obstacles, obsCount, point.range);
            if (showSpot)  DrawShadowClip(spot,  obstacles, obsCount, spot.range);
            if (showPulse) DrawShadowClip(pulse, obstacles, obsCount, pulse.range);
        }

        /* walls over shadows */
        for (int i = 0; i < obsCount; ++i) {
            if (!obstacles[i].active) continue;
            DrawRectangleRec(obstacles[i].rect, (Color){ 135, 145, 165, 255 });
            DrawRectangleLines((int)obstacles[i].rect.x,
                               (int)obstacles[i].rect.y,
                               (int)obstacles[i].rect.width,
                               (int)obstacles[i].rect.height,
                               (Color){ 25, 25, 35, 255 });
        }

        /* active ring */
        {
            VLight2D *lp = lightPtrs[activeLight];
            for (int a = 0; a < 32; ++a) {
                float a1 = (float)a / 32 * 2.0f * VLCRLIB_PI;
                float a2 = (float)(a + 1) / 32 * 2.0f * VLCRLIB_PI;
                Vector2 p1 = { lp->position.x + cosf(a1) * 16,
                               lp->position.y + sinf(a1) * 16 };
                Vector2 p2 = { lp->position.x + cosf(a2) * 16,
                               lp->position.y + sinf(a2) * 16 };
                DrawLineEx(p1, p2, 2.0f, (Color){ 255, 255, 255, 200 });
            }
        }

        /* debug rays */
        if (showDebug) {
            VLight2D *lp = lightPtrs[activeLight];
            for (int a = 0; a < 128; ++a) {
                float angle = (float)a / 128 * 2.0f * VLCRLIB_PI;
                Vector2 dir = { cosf(angle), sinf(angle) };
                float   bestT = lp->range;
                Vector2 bestHit = { lp->position.x + dir.x * bestT,
                                    lp->position.y + dir.y * bestT };

                for (int j = 0; j < obsCount; ++j) {
                    if (!obstacles[j].active) continue;
                    float t;
                    Vector2 h;
                    if (ray_vs_rect(lp->position, dir,
                                    obstacles[j].rect, &t, &h)) {
                        if (t < bestT) {
                            bestT = t;
                            bestHit = h;
                        }
                    }
                }
                DrawLineEx(lp->position, bestHit, 1.0f,
                           (Color){ 255, 220, 100, 60 });
            }
        }

        /* ============================================================ */
        /*  GUI                                                          */
        /* ============================================================ */
        VGuiBeginFrame(&gui, uiFont, dt);

        VGuiPanel(&gui, (Rectangle){ 950, 24, 290, 240 }, "2D Lights");
        VGuiCheckBox(&gui, (Rectangle){ 970,  68, 250, 30 },
                     &showPoint,   "Point light");
        VGuiCheckBox(&gui, (Rectangle){ 970, 104, 250, 30 },
                     &showSpot,    "Spot light");
        VGuiCheckBox(&gui, (Rectangle){ 970, 140, 250, 30 },
                     &showPulse,   "Pulsing point");
        VGuiCheckBox(&gui, (Rectangle){ 970, 176, 250, 30 },
                     &showShadows, "Shadows");
        VGuiCheckBox(&gui, (Rectangle){ 970, 212, 250, 30 },
                     &showDebug,   "Debug rays");

        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Active: %d / 3", activeLight + 1);
            VGuiLabel(&gui, (Vector2){ 970, 250 }, buf, gui.theme.accent);
        }

        VGuiEndFrame(&gui);

        /* ============================================================ */
        /*  HUD                                                          */
        /* ============================================================ */
        DrawText("2D LIGHTS + VInput", 28, 24, 30, WHITE);

        if (showPoint) DrawText("Point light",   220, 580, 18, ORANGE);
        if (showSpot)  DrawText("Spot light",    570, 580, 18, SKYBLUE);
        if (showPulse) DrawText("Pulsing point", 920, 580, 18, GOLD);

        DrawText("LMB drag light | RMB drag pillar | TAB next | F1 shadows | F2 debug | WASD move",
                 28, 680, 16, LIGHTGRAY);

        {
            char fps[32];
            snprintf(fps, sizeof(fps), "%d FPS", GetFPS());
            Vector2 ts = MeasureTextEx(uiFont, fps, 24.0f, 1.0f);
            DrawTextEx(uiFont, fps,
                       (Vector2){ 1280 - ts.x - 20, 24 },
                       24.0f, 1.0f, (Color){ 0, 255, 60, 255 });
        }

        EndDrawing();
    }
    /* --- end while --- */

    if (uiFont.ok) UnloadFont(uiFont);
    CloseWindow();
    return 0;
}
