/*
 * model_animation_viewer.c — GUI model + animation viewer.
 *
 * File types: .glb, .gltf, .fbx, .usd/.usda/.usdc/.usdz, .obj.
 * Skinned glTF and FBX models expose their animation clips; the viewer
 * selects "idle" automatically when present, otherwise the first clip.
 */
#include <vlcrlib.h>
#include <vgltf.h>
#include <vloaders.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WIN_W 1440
#define WIN_H 860
#define SIDEBAR_W 350
#define MAX_ANIMS 64
#define PATH_CAP 1024

typedef struct { float yaw, pitch, dist; Vector3 target; } Orbit;

typedef struct {
    Model        staticModel;
    SkinnedModel skinned;
    Animator    *animator;
    bool         isSkinned;
    bool         loaded;
    Vector3      modelScale;

    Animation    anims[MAX_ANIMS];
    char         animNames[MAX_ANIMS][96];
    float        animDurations[MAX_ANIMS];
    int          animCount;
    int          animIndex;
    float        animTime;
    float        animSpeed;
    bool         playing;
    bool         loop;

    char         path[PATH_CAP];
    char         status[160];
} Viewer;

static char g_dialogPath[PATH_CAP];
static volatile int g_dialogReady = 0;

static void fileDialogCallback(void *user, const char *const *files, int filter) {
    (void)user; (void)filter;
    if (files && files[0]) {
        snprintf(g_dialogPath, sizeof(g_dialogPath), "%s", files[0]);
        g_dialogReady = 1;
    }
}

static Camera3D orbit_cam(const Orbit *o) {
    float cp = cosf(o->pitch);
    Camera3D c = {0};
    c.position = (Vector3){
        o->target.x + o->dist * cp * sinf(o->yaw),
        o->target.y + o->dist * sinf(o->pitch),
        o->target.z + o->dist * cp * cosf(o->yaw)
    };
    c.target = o->target;
    c.up = (Vector3){0, 1, 0};
    c.fovy = 45.0f;
    c.nearPlane = 0.05f;
    c.farPlane = 300.0f;
    return c;
}

static void viewer_unload(Viewer *v) {
    v->modelScale = (Vector3){1,1,1};
    if (v->animator) DestroyAnimator(v->animator);
    v->animator = NULL;
    if (v->skinned.id) UnloadSkinnedModel(v->skinned);
    if (v->staticModel.id) UnloadModel(v->staticModel);
    memset(&v->skinned, 0, sizeof(v->skinned));
    memset(&v->staticModel, 0, sizeof(v->staticModel));
    v->isSkinned = false;
    v->loaded = false;
    v->animCount = 0;
    v->animIndex = 0;
    v->animTime = 0.0f;
}

static void select_idle(Viewer *v) {
    v->animIndex = 0;
    for (int i = 0; i < v->animCount; ++i) {
        if (SDL_strcasecmp(v->animNames[i], "idle") == 0) { v->animIndex = i; break; }
        const char *p = strstr(v->animNames[i], "Idle");
        if (p) { v->animIndex = i; break; }
    }
}

static void viewer_load(Viewer *v, const char *path) {
    viewer_unload(v);
    snprintf(v->path, sizeof(v->path), "%s", path);

    const char *ext = strrchr(path, '.');
    bool isFBX = ext && SDL_strcasecmp(ext, ".fbx") == 0;
    bool isUSD = ext && (SDL_strcasecmp(ext, ".usd") == 0 ||
                         SDL_strcasecmp(ext, ".usda") == 0 ||
                         SDL_strcasecmp(ext, ".usdc") == 0 ||
                         SDL_strcasecmp(ext, ".usdz") == 0);

    if (isFBX) {
        v->skinned = LoadFBXSkinned(path);
    } else if (!isUSD) {
        v->skinned = LoadSkinnedModel(path);
    }

    if (v->skinned.id) {
        v->isSkinned = true;
        Vector3 bmin = {0}, bmax = {0};
        if (GetSkinnedModelBounds(v->skinned, &bmin, &bmax)) {
            float h = bmax.y - bmin.y;
            if (h > 10.0f && h > 1e-4f) {
                float s = 2.0f / h;
                v->modelScale = (Vector3){s, s, s};
            }
        }
        int n = GetSkinnedModelAnimationCount(v->skinned);
        if (n > MAX_ANIMS) n = MAX_ANIMS;
        v->animCount = n;
        for (int i = 0; i < n; ++i) {
            v->anims[i] = GetSkinnedModelAnimation(v->skinned, i);
            v->animDurations[i] = GetSkinnedModelAnimationDuration(v->skinned, i);
            const char *name = GetSkinnedModelAnimationName(v->skinned, i);
            snprintf(v->animNames[i], sizeof(v->animNames[i]), "%s",
                     name && name[0] ? name : "clip");
        }
        v->animator = CreateAnimator(v->skinned);
        if (v->animator) {
            for (int i = 0; i < n; ++i)
                AnimatorAddState(v->animator, v->animNames[i],
                                 v->anims[i], true);
        }
        select_idle(v);
        if (v->animator && n > 0) {
            AnimatorPlay(v->animator, v->animNames[v->animIndex], 0.0f);
            fprintf(stderr, "[viewer] selected='%s' state='%s' prev='%s'\n",
                    v->animNames[v->animIndex], AnimatorGetState(v->animator),
                    AnimatorGetPreviousState(v->animator));
        }
        v->playing = n > 0;
        v->loaded = true;
        fprintf(stderr,
                "[viewer] skinned clips=%d bones=%d bounds=(%.2f,%.2f,%.2f)-"
                "(%.2f,%.2f,%.2f) scale=%.4f\n",
                n, GetSkinnedModelBoneCount(v->skinned),
                bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z,
                v->modelScale.x);
        snprintf(v->status, sizeof(v->status), "Skinned — %d clip(s)", n);
        return;
    }

    // Static fallback: FBX/USD handlers when available, glTF/OBJ otherwise.
    if (isFBX) v->staticModel = LoadFBX(path);
    else if (isUSD) v->staticModel = LoadUSD(path);
    else v->staticModel = LoadModelGLTFTiny(path);

    if (v->staticModel.id) {
        v->loaded = true;
        snprintf(v->status, sizeof(v->status), "Static — %d mesh(es)",
                 v->staticModel.meshCount);
    } else {
        snprintf(v->status, sizeof(v->status), "Load failed (backend missing or bad file)");
    }
}

static void viewer_tick(Viewer *v, float dt) {
    if (!v->isSkinned || v->animCount == 0) return;
    if (v->animator) {
        if (v->playing) AnimatorUpdate(v->animator, dt * v->animSpeed);
        v->animTime = AnimatorGetTime(v->animator);
        return;
    }
    if (!v->playing) return;
    float dur = v->animDurations[v->animIndex];
    if (dur < 0.001f) return;
    v->animTime += dt * v->animSpeed;
    if (v->loop) {
        while (v->animTime > dur) v->animTime -= dur;
        while (v->animTime < 0.0f) v->animTime += dur;
    } else if (v->animTime >= dur) {
        v->animTime = dur;
        v->playing = false;
    }
}

static void viewer_draw3d(const Viewer *v, Camera3D cam) {
    if (!v->loaded) return;
    BeginMode3D(cam);
    if (v->isSkinned && v->skinned.id) {
        if (v->animator)
            AnimatorDraw(v->animator, (Vector3){0,0,0}, (Vector3){0,0,0},
                         v->modelScale, WHITE);
        else {
            Animation anim = v->animCount > 0 ? v->anims[v->animIndex] : (Animation){0};
            DrawSkinnedModel(v->skinned, anim, v->animTime,
                             (Vector3){0,0,0}, (Vector3){0,0,0},
                             v->modelScale, WHITE);
        }
    } else if (v->staticModel.id) {
        DrawModel(v->staticModel, (Vector3){0,0,0}, (Vector3){0,0,0},
                  (Vector3){1,1,1}, WHITE);
    }
    EndMode3D();
}

static void sidebar(VGuiContext *gui, Font *font, Viewer *v, Orbit *orbit,
                    char *pathBuf, int pathCap) {
    DrawRectangle(0, 0, SIDEBAR_W, WIN_H, (Color){22, 24, 30, 235});
    DrawRectangleLines(0, 0, SIDEBAR_W, WIN_H, (Color){60, 65, 80, 255});

    VGuiBeginFrame(gui, *font, GetFrameTime());
    float cx = 12, cy = 12, fw = SIDEBAR_W - 24;

    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Model");
    cy += 28;
    VGuiInputText(gui, (Rectangle){cx, cy, fw - 92, 28}, pathBuf, pathCap);
    if (VGuiButton(gui, (Rectangle){cx + fw - 88, cy, 88, 28}, "Browse...")) {
        const SDL_DialogFileFilter filters[] = {
            { "3D models", "glb;gltf;fbx;obj;usd;usda;usdc;usdz" },
            { "All files", "*" }
        };
        SDL_ShowOpenFileDialog(fileDialogCallback, NULL,
                               (SDL_Window *)vcore_GetWindow(),
                               filters, 2, NULL, false);
    }
    cy += 34;
    if (VGuiButton(gui, (Rectangle){cx, cy, fw, 26}, "Open path")) {
        viewer_load(v, pathBuf);
        orbit->dist = v->isSkinned ? 4.0f : 5.0f;
    }
    cy += 34;

    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Animation");
    cy += 28;
    if (!v->loaded) {
        VGuiLabel(gui, (Vector2){cx, cy}, "No model",
                  (Color){150,155,170,255});
        cy += 24;
    } else if (!v->isSkinned) {
        VGuiLabel(gui, (Vector2){cx, cy}, "Static model — no skeleton",
                  (Color){150,155,170,255});
        cy += 24;
    } else if (v->animCount == 0) {
        VGuiLabel(gui, (Vector2){cx, cy}, "No animation clips",
                  (Color){150,155,170,255});
        cy += 24;
    }

    if (v->animCount > 0) {
        const char *labels[MAX_ANIMS];
        for (int i = 0; i < v->animCount; ++i) labels[i] = v->animNames[i];
        int listH = v->animCount * 24;
        if (listH > 144) listH = 144;
        int prev = v->animIndex;
        VGuiListBox(gui, (Rectangle){cx, cy, fw, (float)listH},
                    labels, v->animCount, &v->animIndex);
        if (v->animIndex != prev) {
            v->animTime = 0;
            v->playing = true;
            if (v->animator)
                AnimatorPlay(v->animator, v->animNames[v->animIndex], 0.25f);
        }
        cy += listH + 6;
    }

    {
        float dur = (v->animCount > 0) ? v->animDurations[v->animIndex] : 1.0f;
        if (dur < 0.001f) dur = 1.0f;
        float before = v->animTime;
        VGuiSlider(gui, (Rectangle){cx, cy, fw, 18}, &v->animTime, 0, dur);
        if (v->animator && v->animTime != before)
            AnimatorSetTime(v->animator, v->animTime);
        cy += 24;
        if (VGuiButton(gui, (Rectangle){cx, cy, (fw-8)/2, 26},
                       v->playing ? "Pause" : "Play")) v->playing = !v->playing;
        if (VGuiButton(gui, (Rectangle){cx + (fw+8)/2, cy, (fw-8)/2, 26}, "Rewind")) {
            v->animTime = 0;
            v->playing = true;
            if (v->animator) AnimatorSetTime(v->animator, 0.0f);
        }
        cy += 32;
        VGuiCheckBox(gui, (Rectangle){cx, cy, fw, 22}, &v->loop, "Loop");
        cy += 26;
        VGuiLabel(gui, (Vector2){cx, cy}, "Speed", (Color){200,205,215,255});
        cy += 18;
        VGuiSlider(gui, (Rectangle){cx, cy, fw, 18}, &v->animSpeed, 0.1f, 3.0f);
        cy += 26;
    }

    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Camera");
    cy += 28;
    VGuiSlider(gui, (Rectangle){cx, cy, fw, 18}, &orbit->dist, 0.5f, 50.0f);
    cy += 26;
    if (VGuiButton(gui, (Rectangle){cx, cy, fw, 26}, "Reset camera")) {
        orbit->yaw = -0.35f; orbit->pitch = 0.2f; orbit->dist = 5.0f;
        orbit->target = (Vector3){0,0,0};
    }
    cy += 34;

    VGuiPanel(gui, (Rectangle){cx, cy, fw, 22}, "Status");
    cy += 28;
    VGuiLabel(gui, (Vector2){cx, cy}, v->status, (Color){170,180,200,255});
    cy += 22;
    VGuiLabel(gui, (Vector2){cx, cy}, "Drag: orbit   Wheel: zoom", (Color){140,150,170,255});

    VGuiEndFrame(gui);
}

int main(int argc, char **argv) {
    if (!InitWindow(WIN_W, WIN_H, "vlcrlib — Model & Animation Viewer")) return 1;
    SetTargetFPS(60);

    Viewer v = {0};
    v.animSpeed = 1.0f;
    v.loop = true;
    char pathBuf[PATH_CAP] = {0};

    if (argc > 1) {
        snprintf(pathBuf, sizeof(pathBuf), "%s", argv[1]);
        viewer_load(&v, pathBuf);
    }

    vgraphics_SetDirectionalLight(true,
        (Vector3){-0.35f,-0.70f,-0.60f}, (Color){255,250,240,255}, 3.0f);
    vgraphics_SetAmbientLight(true, (Color){90,100,120,255}, 0.15f);
    bool ibl = vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(ibl ? 1.2f : 0.0f);
    vgraphics_SetSkybox(0);
    SetSSAO(1, 0.5f, 0.6f);
    SetBloom(0.9f, 0.15f);
    SetGraphicsExposure(1.0f);

    Orbit orbit = {-0.35f, 0.18f, 5.0f, (Vector3){0,1,0}};
    VGuiContext gui = {0};
    vtext_EnsureDefaultFont();
    Font uiFont = GetFontDefault();
    Vector2 prev = {0};
    int dragging = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (g_dialogReady) {
            g_dialogReady = 0;
            snprintf(pathBuf, sizeof(pathBuf), "%s", g_dialogPath);
            viewer_load(&v, pathBuf);
            orbit.dist = v.isSkinned ? 4.0f : 5.0f;
        }

        Vector2 mouse = GetMousePosition();
        if (IsMouseButtonPressed(SDL_BUTTON_LEFT) && mouse.x > SIDEBAR_W) {
            dragging = 1; prev = mouse;
        }
        if (IsMouseButtonReleased(SDL_BUTTON_LEFT)) dragging = 0;
        if (dragging) {
            orbit.yaw -= (mouse.x - prev.x) * 0.01f;
            orbit.pitch += (mouse.y - prev.y) * 0.01f;
            if (orbit.pitch > 1.4f) orbit.pitch = 1.4f;
            if (orbit.pitch < -1.4f) orbit.pitch = -1.4f;
            prev = mouse;
        }
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            orbit.dist *= (1.0f - 0.1f * wheel);
            if (orbit.dist < 0.5f) orbit.dist = 0.5f;
            if (orbit.dist > 50.0f) orbit.dist = 50.0f;
        }
        if (IsKeyPressed(SDL_SCANCODE_SPACE)) v.playing = !v.playing;
        if (IsKeyPressed(SDL_SCANCODE_R)) {
            orbit = (Orbit){-0.35f, 0.18f, 5.0f, (Vector3){0,0,0}};
        }

        viewer_tick(&v, dt);

        BeginDrawing();
        ClearBackground((Color){110, 112, 118, 255});
        Camera3D cam = orbit_cam(&orbit);
        viewer_draw3d(&v, cam);

        DrawText("Model & Animation Viewer", SIDEBAR_W + 20, 16, 28, WHITE);
        DrawText(pathBuf, SIDEBAR_W + 20, 50, 14, (Color){220,225,235,255});
        FrameStats fs = {0};
        vgraphics_GetFrameStats(&fs);
        char stats[160];
        snprintf(stats, sizeof(stats),
                 "%d draws | %llu tris | cpu %.2f gpu %.2f ms | sh %.2f sc %.2f post %.2f",
                 fs.drawCalls, (unsigned long long)fs.triangles,
                 fs.cpuFrameMs, fs.gpuFrameMs, fs.shadowMs, fs.sceneMs, fs.postMs);
        DrawText(stats, SIDEBAR_W + 20, 72, 13, (Color){200,210,225,255});
        sidebar(&gui, &uiFont, &v, &orbit, pathBuf, PATH_CAP);
        EndDrawing();
    }

    viewer_unload(&v);
    if (uiFont.ok) UnloadFont(uiFont);
    CloseWindow();
    return 0;
}
