/*
 * gltf_anim_viewer.c — glTF scene viewer with skinned animation and a GUI sidebar.
 *
 * Features:
 *   - Load any .glb / .gltf via an on-screen file path input + "Open" button
 *   - Static scenes  (LoadModelGLTFTiny)    rendered with full PBR + IBL
 *   - Skinned scenes (LoadSkinnedModel)     with animation playback
 *   - Orbital camera: LMB drag = orbit, wheel = zoom, R = reset
 *   - GUI sidebar: file picker, animation list, playback controls, render options
 *
 * Controls:
 *   LMB drag  — orbit camera
 *   Wheel     — zoom
 *   R         — reset camera
 *   Space     — play / pause animation
 *   Esc       — quit
 */

#include <vlcrlib.h>
#include <vgltf.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mouse.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* =========================================================== */
/*  Configuration                                               */
/* =========================================================== */
#define WIN_W       1440
#define WIN_H        860
#define SIDEBAR_W    340
#define MAX_ANIMS     64
#define PATH_CAP     512

/* =========================================================== */
/*  Orbital camera                                              */
/* =========================================================== */
typedef struct { float yaw, pitch, dist; Vector3 target; } Orbit;

static Camera3D orbit_to_cam(const Orbit *o) {
    float cp = cosf(o->pitch);
    Camera3D c = {0};
    c.position = (Vector3){
        o->target.x + o->dist * cp * sinf(o->yaw),
        o->target.y + o->dist * sinf(o->pitch),
        o->target.z + o->dist * cp * cosf(o->yaw)
    };
    c.target   = o->target;
    c.up       = (Vector3){0, 1, 0};
    c.fovy     = 45.0f;
    c.nearPlane = 0.05f;
    c.farPlane  = 200.0f;
    return c;
}

/* =========================================================== */
/*  Scene state                                                 */
/* =========================================================== */
typedef struct {
    /* loaded assets */
    Model        staticModel;
    SkinnedModel skinnedModel;
    bool         isSkinned;

    /* animations (skinned only) */
    Animation    anims[MAX_ANIMS];
    float        animDurations[MAX_ANIMS];
    int          animCount;
    int          animIndex;       /* currently selected */
    float        animTime;
    float        animSpeed;
    bool         playing;
    bool         loop;

    /* file path */
    char         loadedPath[PATH_CAP];
    bool         loadOk;
    char         statusMsg[128];  /* shown on the bottom status bar */
} Scene;

/* =========================================================== */
/*  Helper: load a file, try skinned first, fall back to static */
/* =========================================================== */
static void scene_unload(Scene *s) {
    if (s->skinnedModel.id) { UnloadSkinnedModel(s->skinnedModel); s->skinnedModel.id = 0; }
    if (s->staticModel.id)  { UnloadModel(s->staticModel);         s->staticModel.id  = 0; }
    s->animCount = 0;
    s->animIndex = 0;
    s->animTime  = 0.0f;
    s->isSkinned = false;
    s->loadOk    = false;
}

static void scene_load(Scene *s, const char *path) {
    scene_unload(s);
    snprintf(s->loadedPath, sizeof(s->loadedPath), "%s", path);

    /* Try skinned first (has bones / animations) */
    SkinnedModel sk = LoadSkinnedModel(path);
    if (sk.id) {
        s->skinnedModel = sk;
        s->isSkinned    = true;

        int n = GetSkinnedModelAnimationCount(sk);
        if (n > MAX_ANIMS) n = MAX_ANIMS;
        s->animCount = n;
        for (int i = 0; i < n; ++i) {
            s->anims[i]         = GetSkinnedModelAnimation(sk, i);
            s->animDurations[i] = GetSkinnedModelAnimationDuration(sk, i);
        }
        if (n > 0) s->animIndex = 0;
        s->playing = (n > 0);
        s->loadOk  = true;
        snprintf(s->statusMsg, sizeof(s->statusMsg),
                 "Skinned model — %d anim(s)", n);
        return;
    }

    /* Fall back to static PBR model */
    Model m = LoadModelGLTFTiny(path);
    if (m.id) {
        s->staticModel = m;
        s->isSkinned   = false;
        s->loadOk      = true;
        snprintf(s->statusMsg, sizeof(s->statusMsg),
                 "Static model — %d mesh(es), %d material(s)", m.meshCount, m.materialCount);
        return;
    }

    snprintf(s->statusMsg, sizeof(s->statusMsg), "Failed to load: %s", path);
}

/* =========================================================== */
/*  Advance animation clock                                     */
/* =========================================================== */
static void scene_tick(Scene *s, float dt) {
    if (!s->isSkinned || !s->playing || s->animCount == 0) return;

    float dur = s->animDurations[s->animIndex];
    if (dur < 0.001f) return;

    s->animTime += dt * s->animSpeed;
    if (s->loop) {
        while (s->animTime > dur) s->animTime -= dur;
        while (s->animTime < 0.0f) s->animTime += dur;
    } else {
        if (s->animTime >= dur) { s->animTime = dur; s->playing = false; }
        if (s->animTime <  0.0f) { s->animTime = 0.0f; }
    }
}

/* =========================================================== */
/*  Draw scene (3-D portion)                                    */
/* =========================================================== */
static void scene_draw(const Scene *s, Camera3D cam) {
    if (!s->loadOk) return;

    BeginMode3D(cam);

    if (s->isSkinned && s->skinnedModel.id && s->animCount > 0 &&
        s->anims[s->animIndex].id)
    {
        DrawSkinnedModel(s->skinnedModel,
                         s->anims[s->animIndex],
                         s->animTime,
                         (Vector3){0, 0, 0},
                         (Vector3){0, 0, 0},
                         (Vector3){1, 1, 1},
                         WHITE);
    } else if (s->isSkinned && s->skinnedModel.id) {
        /* skinned but no anim — draw bind pose */
        DrawSkinnedModel(s->skinnedModel,
                         (Animation){0}, 0.0f,
                         (Vector3){0, 0, 0},
                         (Vector3){0, 0, 0},
                         (Vector3){1, 1, 1},
                         WHITE);
    } else if (s->staticModel.id) {
        DrawModel(s->staticModel,
                  (Vector3){0, 0, 0},
                  (Vector3){0, 0, 0},
                  (Vector3){1, 1, 1},
                  WHITE);
    }

    EndMode3D();
}

/* =========================================================== */
/*  GUI sidebar                                                 */
/* =========================================================== */
static void draw_sidebar(VGuiContext *gui, Font *font, Scene *s,
                         char *pathBuf, int pathCap,
                         float *exposure, bool *iblOn,
                         bool *bloomOn, bool *ssaoOn,
                         bool *showGrid,
                         Orbit *orbit)
{
    float sw = (float)SIDEBAR_W;
    float sh = (float)WIN_H;

    /* ---- background panel ---- */
    DrawRectangle(0, 0, SIDEBAR_W, WIN_H, (Color){22, 24, 30, 230});
    DrawRectangleLines(0, 0, SIDEBAR_W, WIN_H, (Color){60, 65, 80, 255});

    float cx = 10.0f;
    float cy = 10.0f;
    float fw  = sw - cx * 2.0f;  /* full widget width */

    VGuiBeginFrame(gui, *font, GetFrameTime());

    /* -------- File section -------- */
    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "File");
    cy += 26.0f;

    /* Path input */
    VGuiInputText(gui, (Rectangle){cx, cy, fw - 64.0f, 28}, pathBuf, pathCap);

    /* Open button */
    bool doOpen = VGuiButton(gui, (Rectangle){cx + fw - 60.0f, cy, 60.0f, 28}, "Open");
    cy += 34.0f;

    if (doOpen && pathBuf[0]) {
        scene_load(s, pathBuf);
        /* reset camera to a sensible default when a new file is opened */
        orbit->yaw   = -0.35f;
        orbit->pitch =  0.20f;
        orbit->dist  =  5.0f;
        orbit->target = (Vector3){0, 0, 0};
    }

    /* Quick-open buttons for built-in assets */
    VGuiLabel(gui, (Vector2){cx, cy}, "Quick open:", (Color){160, 165, 180, 255});
    cy += 20.0f;

    if (VGuiButton(gui, (Rectangle){cx, cy, fw, 24}, "DamagedHelmet.glb")) {
        snprintf(pathBuf, pathCap, "DamagedHelmet.glb");
        scene_load(s, pathBuf);
        orbit->yaw = -0.35f; orbit->pitch = 0.20f;
        orbit->dist = 5.0f; orbit->target = (Vector3){0, 0, 0};
    }
    cy += 28.0f;

    if (VGuiButton(gui, (Rectangle){cx, cy, fw, 24}, "skinned_quad.glb")) {
        snprintf(pathBuf, pathCap, "skinned_quad.glb");
        scene_load(s, pathBuf);
        orbit->yaw = -0.35f; orbit->pitch = 0.20f;
        orbit->dist = 3.0f; orbit->target = (Vector3){0.5f, 1.0f, 0};
    }
    cy += 34.0f;

    /* -------- Animation section (skinned only) -------- */
    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Animation");
    cy += 26.0f;

    if (!s->isSkinned || s->animCount == 0) {
        VGuiLabel(gui, (Vector2){cx, cy},
                  s->isSkinned ? "No animations in file." : "Static model — no skeleton.",
                  (Color){140, 145, 160, 255});
        cy += 22.0f;
    } else {
        /* Animation list */
        char animLabels[MAX_ANIMS][32];
        const char *animLabelPtrs[MAX_ANIMS];
        for (int i = 0; i < s->animCount; ++i) {
            snprintf(animLabels[i], sizeof(animLabels[i]), "[%d]  %.2fs", i, s->animDurations[i]);
            animLabelPtrs[i] = animLabels[i];
        }
        int listH = s->animCount * 24;
        if (listH < 24)  listH = 24;
        if (listH > 120) listH = 120;

        int prevIdx = s->animIndex;
        VGuiListBox(gui, (Rectangle){cx, cy, fw, (float)listH},
                    animLabelPtrs, s->animCount, &s->animIndex);
        if (s->animIndex != prevIdx) {
            s->animTime = 0.0f;
            s->playing  = true;
        }
        cy += (float)listH + 6.0f;

        /* Progress bar / scrubber */
        float dur = s->animDurations[s->animIndex];
        float prog = (dur > 0.001f) ? (s->animTime / dur) : 0.0f;
        VGuiProgressBar(gui, (Rectangle){cx, cy, fw, 12}, prog, 0.0f, 1.0f);
        cy += 18.0f;

        /* Scrub by dragging the slider */
        if (dur > 0.001f) {
            bool changed = VGuiSlider(gui, (Rectangle){cx, cy, fw, 18}, &s->animTime, 0.0f, dur);
            if (changed) s->playing = false; /* manual scrub pauses */
        }
        cy += 24.0f;

        /* Play/Pause / Stop / Rewind */
        float bw = (fw - 8.0f) / 3.0f;
        if (VGuiButton(gui, (Rectangle){cx,            cy, bw, 26}, s->playing ? "Pause" : "Play"))
            s->playing = !s->playing;
        if (VGuiButton(gui, (Rectangle){cx + bw + 4.0f,  cy, bw, 26}, "Rewind"))
            { s->animTime = 0.0f; s->playing = true; }
        if (VGuiButton(gui, (Rectangle){cx + (bw+4.0f)*2, cy, bw, 26}, "Stop"))
            { s->animTime = 0.0f; s->playing = false; }
        cy += 32.0f;

        /* Speed + Loop */
        VGuiLabel(gui, (Vector2){cx, cy}, "Speed:", (Color){200, 205, 215, 255});
        cy += 18.0f;
        VGuiSlider(gui, (Rectangle){cx, cy, fw, 18}, &s->animSpeed, 0.1f, 3.0f);
        cy += 24.0f;
        VGuiCheckBox(gui, (Rectangle){cx, cy, fw, 22}, &s->loop, "Loop");
        cy += 30.0f;
    }

    /* -------- Render options -------- */
    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Render");
    cy += 26.0f;

    VGuiLabel(gui, (Vector2){cx, cy}, "Exposure:", (Color){200, 205, 215, 255});
    cy += 18.0f;
    if (VGuiSlider(gui, (Rectangle){cx, cy, fw, 18}, exposure, 0.1f, 4.0f))
        SetGraphicsExposure(*exposure);
    cy += 24.0f;

    if (VGuiCheckBox(gui, (Rectangle){cx, cy, fw, 22}, iblOn, "IBL lighting"))
        vgraphics_SetIBLIntensity(*iblOn ? 1.2f : 0.0f);
    cy += 28.0f;

    if (VGuiCheckBox(gui, (Rectangle){cx, cy, fw, 22}, bloomOn, "Bloom")) {
        SetBloom(*bloomOn ? 0.9f : 0.0f, 0.15f);
    }
    cy += 28.0f;

    if (VGuiCheckBox(gui, (Rectangle){cx, cy, fw, 22}, ssaoOn, "SSAO")) {
        SetSSAO(*ssaoOn ? 1 : 0, 0.5f, 0.6f);
    }
    cy += 28.0f;

    VGuiCheckBox(gui, (Rectangle){cx, cy, fw, 22}, showGrid, "Show grid");
    cy += 34.0f;

    /* -------- Camera -------- */
    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Camera");
    cy += 26.0f;

    VGuiLabel(gui, (Vector2){cx, cy}, "Zoom:", (Color){200, 205, 215, 255});
    cy += 18.0f;
    VGuiSlider(gui, (Rectangle){cx, cy, fw, 18}, &orbit->dist, 0.5f, 50.0f);
    cy += 24.0f;

    if (VGuiButton(gui, (Rectangle){cx, cy, fw, 26}, "Reset Camera")) {
        orbit->yaw   = -0.35f;
        orbit->pitch =  0.20f;
        orbit->dist  =  5.0f;
        orbit->target = (Vector3){0, 0, 0};
    }
    cy += 34.0f;

    /* -------- Help -------- */
    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Controls");
    cy += 26.0f;
    VGuiLabel(gui, (Vector2){cx, cy},     "LMB drag — orbit", (Color){160, 165, 180, 255}); cy += 18.0f;
    VGuiLabel(gui, (Vector2){cx, cy},     "Wheel    — zoom",  (Color){160, 165, 180, 255}); cy += 18.0f;
    VGuiLabel(gui, (Vector2){cx, cy},     "R        — reset", (Color){160, 165, 180, 255}); cy += 18.0f;
    VGuiLabel(gui, (Vector2){cx, cy},     "Space    — play/pause", (Color){160, 165, 180, 255}); cy += 18.0f;
    VGuiLabel(gui, (Vector2){cx, cy},     "Esc      — quit",  (Color){160, 165, 180, 255});

    VGuiEndFrame(gui);
    (void)sh;
}

/* =========================================================== */
/*  Entry point                                                 */
/* =========================================================== */
int main(int argc, char **argv) {
    if (!InitWindow(WIN_W, WIN_H, "vlcrlib — glTF Animated Scene Viewer")) return 1;
    SetTargetFPS(60);

    /* ---- font ---- */
    Font font = {0};
#ifdef _WIN32
    font = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 28, 32, 96);
#endif
    if (!font.ok) {
        vtext_EnsureDefaultFont();
        font = GetFontDefault();
    }

    /* ---- VGui context ---- */
    VGuiContext gui = {0};
    gui.theme = VGuiDefaultTheme();

    /* ---- scene ---- */
    Scene scene;
    memset(&scene, 0, sizeof(scene));
    scene.animSpeed = 1.0f;
    scene.loop      = true;
    scene.playing   = false;
    snprintf(scene.statusMsg, sizeof(scene.statusMsg), "No model loaded.");

    /* ---- lighting ---- */
    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.35f, -0.70f, -0.60f },
        (Color){ 255, 250, 240, 255 }, 3.0f);
    vgraphics_SetAmbientLight(true, (Color){ 90, 100, 120, 255 }, 0.15f);
    bool iblLoaded = vgraphics_LoadIBL(NULL);

    /* ---- render toggles ---- */
    float exposure = 1.0f;
    bool  iblOn    = iblLoaded;
    bool  bloomOn  = true;
    bool  ssaoOn   = true;
    bool  showGrid = true;

    vgraphics_SetIBLIntensity(iblOn ? 1.2f : 0.0f);
    vgraphics_SetSkybox(0);
    SetSSAO(ssaoOn ? 1 : 0, 0.5f, 0.6f);
    SetBloom(bloomOn ? 0.9f : 0.0f, 0.15f);
    SetGraphicsExposure(exposure);

    /* ---- path buffer for GUI ---- */
    char pathBuf[PATH_CAP] = {0};

    /* ---- load from command line ---- */
    if (argc > 1 && argv[1][0]) {
        snprintf(pathBuf, sizeof(pathBuf), "%s", argv[1]);
        scene_load(&scene, pathBuf);
    } else {
        /* Try default asset */
        FILE *f = fopen("DamagedHelmet.glb", "rb");
        if (f) { fclose(f); snprintf(pathBuf, sizeof(pathBuf), "DamagedHelmet.glb"); }
        else {
            f = fopen("skinned_quad.glb", "rb");
            if (f) { fclose(f); snprintf(pathBuf, sizeof(pathBuf), "skinned_quad.glb"); }
        }
        if (pathBuf[0]) scene_load(&scene, pathBuf);
    }

    /* ---- camera ---- */
    Orbit orbit = { -0.35f, 0.20f, 5.0f, (Vector3){0, 0, 0} };
    Vector2 prevMouse = {0};
    bool    dragging  = false;

    /* ================================================================ */
    while (!WindowShouldClose()) {

        /* ---- input ---- */
        Vector2 mouse = GetMousePosition();
        float   wheel = GetMouseWheelMove();
        float   dt    = GetFrameTime();

        /* orbit controls only when mouse is outside the sidebar */
        bool inSidebar = (mouse.x < (float)SIDEBAR_W);

        if (!inSidebar) {
            if (IsMouseButtonPressed(SDL_BUTTON_LEFT))  { dragging = true;  prevMouse = mouse; }
            if (IsMouseButtonReleased(SDL_BUTTON_LEFT))   dragging = false;
        }
        if (dragging) {
            orbit.yaw   -= (mouse.x - prevMouse.x) * 0.01f;
            orbit.pitch += (mouse.y - prevMouse.y) * 0.01f;
            if (orbit.pitch >  1.55f) orbit.pitch =  1.55f;
            if (orbit.pitch < -1.55f) orbit.pitch = -1.55f;
            prevMouse = mouse;
        }
        if (!inSidebar && wheel != 0.0f) {
            orbit.dist *= (1.0f - 0.12f * wheel);
            if (orbit.dist < 0.3f)  orbit.dist = 0.3f;
            if (orbit.dist > 100.0f) orbit.dist = 100.0f;
        }

        if (IsKeyPressed(SDL_SCANCODE_R))
            orbit = (Orbit){ -0.35f, 0.20f, 5.0f, (Vector3){0, 0, 0} };

        if (IsKeyPressed(SDL_SCANCODE_SPACE))
            scene.playing = !scene.playing;

        /* ---- tick animation ---- */
        scene_tick(&scene, dt);

        /* ---- draw 3D ---- */
        BeginDrawing();
        ClearBackground((Color){ 34, 36, 44, 255 });

        /* viewport is to the right of the sidebar */
        Camera3D cam = orbit_to_cam(&orbit);
        scene_draw(&scene, cam);

        if (showGrid) {
            BeginMode3D(cam);
            DrawGrid(14, 1.0f);
            EndMode3D();
        }

        /* ---- overlay info ---- */
        {
            int viewX = SIDEBAR_W + 12;
            if (scene.loadOk) {
                DrawText(scene.loadedPath, viewX, 12, 16, (Color){220, 225, 235, 220});
                DrawText(scene.statusMsg,  viewX, 32, 14, (Color){160, 175, 190, 200});
                if (scene.isSkinned && scene.animCount > 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Anim %d/%d  t=%.2fs/%.2fs  %s",
                             scene.animIndex + 1, scene.animCount,
                             scene.animTime, scene.animDurations[scene.animIndex],
                             scene.playing ? "▶" : "⏸");
                    DrawText(buf, viewX, 52, 14, (Color){140, 210, 140, 220});
                }
            } else {
                DrawText("No model loaded — use the panel to open a glTF file",
                         viewX, 12, 18, (Color){255, 180, 80, 220});
                DrawText(scene.statusMsg, viewX, 38, 14, (Color){220, 100, 100, 200});
            }
        }

        /* ---- GUI sidebar ---- */
        draw_sidebar(&gui, &font, &scene, pathBuf, PATH_CAP,
                     &exposure, &iblOn, &bloomOn, &ssaoOn, &showGrid, &orbit);

        /* ---- status bar ---- */
        DrawRectangle(0, WIN_H - 24, WIN_W, 24, (Color){16, 18, 24, 240});
        DrawText(scene.statusMsg, SIDEBAR_W + 10, WIN_H - 19, 14, (Color){180, 185, 200, 220});
        {
            char fps[32];
            snprintf(fps, sizeof(fps), "FPS: %d", (int)(1.0f / dt));
            DrawText(fps, WIN_W - 80, WIN_H - 19, 14, (Color){130, 200, 130, 220});
        }

        EndDrawing();
    }

    /* ---- cleanup ---- */
    scene_unload(&scene);
    CloseWindow();
    return 0;
}
