/* scene_editor_lite.c вЂ” minimal scene editor: node list, transforms, save/load. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include "vscene.h"
#include "vgui.h"
#include "vtext.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDITOR_UNDO 16

typedef struct Editor {
    Scene        scene;
    SceneRuntime runtime;
    char         scenePath[512];
    int          selected;
    float        yaw, pitch, dist;
    Vector2      prevMouse;
    int          hasPrevMouse;
    Scene       *undo;
    int          undoCount;
    Scene       *redo;
    int          redoCount;
} Editor;

static void editorReload(Editor *ed);

static void editorPushUndo(Editor *ed) {
    if (!ed->undo) return;
    if (ed->undoCount >= EDITOR_UNDO) {
        memmove(&ed->undo[0], &ed->undo[1],
                sizeof(Scene) * (EDITOR_UNDO - 1));
        ed->undoCount = EDITOR_UNDO - 1;
    }
    ed->undo[ed->undoCount++] = ed->scene;
    ed->redoCount = 0;
}

static void editorUndo(Editor *ed) {
    if (!ed->undo || ed->undoCount == 0) return;
    if (ed->redoCount < EDITOR_UNDO) ed->redo[ed->redoCount++] = ed->scene;
    ed->scene = ed->undo[--ed->undoCount];
    ed->selected = 0;
    editorReload(ed);
}

static void editorRedo(Editor *ed) {
    if (!ed->redo || ed->redoCount == 0) return;
    if (ed->undoCount < EDITOR_UNDO) ed->undo[ed->undoCount++] = ed->scene;
    ed->scene = ed->redo[--ed->redoCount];
    ed->selected = 0;
    editorReload(ed);
}

static void editorReload(Editor *ed) {
    SceneUnloadModels(&ed->runtime);
    SceneLoadModels(&ed->scene, ed->scenePath, &ed->runtime);
}

static void editorEnsureDefaults(Editor *ed) {
    if (!ed->undo) ed->undo = (Scene *)calloc(EDITOR_UNDO, sizeof(Scene));
    if (!ed->redo) ed->redo = (Scene *)calloc(EDITOR_UNDO, sizeof(Scene));
    SceneInit(&ed->scene);
    snprintf(ed->scenePath, sizeof(ed->scenePath), "editor_scene.json");
    if (!SceneLoad(&ed->scene, ed->scenePath) || ed->scene.count == 0) {
        SceneNode n;
        memset(&n, 0, sizeof(n));
        snprintf(n.model, sizeof(n.model), "DamagedHelmet.glb");
        n.position = (Vector3){0, 1.0f, 0};
        n.rotation = (Vector3){0, 0, 0};
        n.scale = (Vector3){1, 1, 1};
        n.tint = WHITE;
        n.visible = 1;
        n.parent = -1;
        SceneAddNode(&ed->scene, &n);
    }
    ed->selected = 0;
}

static void editorCamera(Editor *ed, Camera3D *cam) {
    Vector2 m = GetMousePosition();
    if (m.x > 340 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && ed->hasPrevMouse) {
        ed->yaw   -= (m.x - ed->prevMouse.x) * 0.005f;
        ed->pitch += (m.y - ed->prevMouse.y) * 0.005f;
        if (ed->pitch > 1.3f) ed->pitch = 1.3f;
        if (ed->pitch < 0.05f) ed->pitch = 0.05f;
    }
    ed->prevMouse = m;
    ed->hasPrevMouse = 1;
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && m.x > 340) {
        ed->dist *= wheel > 0 ? 0.9f : 1.1f;
        if (ed->dist < 2.0f) ed->dist = 2.0f;
        if (ed->dist > 80.0f) ed->dist = 80.0f;
    }
    Vector3 target = {0, 1.0f, 0};
    cam->target = target;
    cam->position = (Vector3){
        target.x + ed->dist * cosf(ed->pitch) * sinf(ed->yaw),
        target.y + ed->dist * sinf(ed->pitch),
        target.z + ed->dist * cosf(ed->pitch) * cosf(ed->yaw) };
    cam->up = (Vector3){0, 1, 0};
    cam->fovy = 45.0f;
    cam->nearPlane = 0.05f;
    cam->farPlane = 200.0f;
}

static void editorUI(Editor *ed, VGuiContext *gui) {
    const float x = 12, w = 310, row = 26, gap = 4;
    float y = 12;

    VGuiPanel(gui, (Rectangle){6, 6, 330, 708}, "Scene editor lite");
    y += 30;
    VGuiText(gui, (Rectangle){x, y, w, 20}, "Nodes", 16, WHITE); y += 22;

    char names[SCENE_MAX_NODES][SCENE_MODEL_PATH];
    const char *items[SCENE_MAX_NODES];
    for (int i = 0; i < ed->scene.count; ++i) {
        snprintf(names[i], sizeof(names[i]), "%d  %s", i, ed->scene.nodes[i].model);
        items[i] = names[i];
    }
    if (ed->scene.count > 0) {
        if (VGuiListBox(gui, (Rectangle){x, y, w, 110}, items,
                        ed->scene.count, &ed->selected))
            ed->selected = ed->selected < 0 ? 0 : ed->selected;
        y += 116;
    }

    if (VGuiButton(gui, (Rectangle){x, y, 96, row}, "Add")) {
        editorPushUndo(ed);
        SceneNode n;
        memset(&n, 0, sizeof(n));
        snprintf(n.model, sizeof(n.model), "DamagedHelmet.glb");
        n.scale = (Vector3){1,1,1};
        n.tint = WHITE;
        n.visible = 1;
        n.parent = -1;
        SceneAddNode(&ed->scene, &n);
        ed->selected = ed->scene.count - 1;
    }
    if (VGuiButton(gui, (Rectangle){x + 104, y, 96, row}, "Delete") &&
        ed->scene.count > 0) {
        editorPushUndo(ed);
        for (int i = ed->selected; i + 1 < ed->scene.count; ++i)
            ed->scene.nodes[i] = ed->scene.nodes[i + 1];
        ed->scene.count--;
        if (ed->selected >= ed->scene.count) ed->selected = ed->scene.count - 1;
    }
    if (VGuiButton(gui, (Rectangle){x + 208, y, 96, row}, "Reload"))
        editorReload(ed);
    y += row + 8;
    if (VGuiButton(gui, (Rectangle){x, y, 96, row}, "Undo")) editorUndo(ed);
    if (VGuiButton(gui, (Rectangle){x + 104, y, 96, row}, "Redo")) editorRedo(ed);
    y += row + 8;

    if (ed->scene.count > 0) {
        SceneNode *n = &ed->scene.nodes[ed->selected < 0 ? 0 : ed->selected];
        VGuiText(gui, (Rectangle){x, y, w, 20}, "Model path", 14, LIGHTGRAY); y += 20;
        VGuiInputText(gui, (Rectangle){x, y, w, row}, n->model,
                      SCENE_MODEL_PATH); y += row + 8;

        VGuiText(gui, (Rectangle){x, y, w, 20}, "Position", 14, LIGHTGRAY); y += 20;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->position.x, 0.05f,
                      -100, 100, "%.2f"); y += row + 4;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->position.y, 0.05f,
                      -100, 100, "%.2f"); y += row + 4;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->position.z, 0.05f,
                      -100, 100, "%.2f"); y += row + 8;

        VGuiText(gui, (Rectangle){x, y, w, 20}, "Rotation (radians)", 14, LIGHTGRAY); y += 20;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->rotation.x, 0.02f,
                      -6.28f, 6.28f, "%.2f"); y += row + 4;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->rotation.y, 0.02f,
                      -6.28f, 6.28f, "%.2f"); y += row + 4;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->rotation.z, 0.02f,
                      -6.28f, 6.28f, "%.2f"); y += row + 8;

        VGuiText(gui, (Rectangle){x, y, w, 20}, "Scale", 14, LIGHTGRAY); y += 20;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->scale.x, 0.02f,
                      0.01f, 20, "%.2f"); y += row + 4;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->scale.y, 0.02f,
                      0.01f, 20, "%.2f"); y += row + 4;
        VGuiDragFloat(gui, (Rectangle){x, y, w, row}, &n->scale.z, 0.02f,
                      0.01f, 20, "%.2f"); y += row + 8;

        bool visible = n->visible != 0;
        if (VGuiCheckBox(gui, (Rectangle){x, y, w, row}, &visible, "Visible"))
            n->visible = visible ? 1 : 0;
        y += row + 8;

        VGuiText(gui, (Rectangle){x, y, w, 20}, "Parent (-1 = root)", 14, LIGHTGRAY);
        y += 20;
        VGuiInputInt(gui, (Rectangle){x, y, w, row}, &n->parent, -1,
                     ed->scene.count - 1);
        y += row + 8;
    }

    VGuiText(gui, (Rectangle){x, y, w, 20}, "Scene file", 14, LIGHTGRAY); y += 20;
    VGuiInputText(gui, (Rectangle){x, y, w, row}, ed->scenePath,
                  sizeof(ed->scenePath)); y += row + 8;
    if (VGuiButton(gui, (Rectangle){x, y, 150, row}, "Save scene"))
        SceneSave(&ed->scene, ed->scenePath);
    if (VGuiButton(gui, (Rectangle){x + 158, y, 150, row}, "Load scene")) {
        Scene loaded;
        SceneInit(&loaded);
        if (SceneLoad(&loaded, ed->scenePath)) {
            editorPushUndo(ed);
            ed->scene = loaded;
            ed->selected = 0;
            editorReload(ed);
        }
    }
}

int main(int argc, char **argv) {
    if (!InitWindow(1280, 720, "vlcrlib вЂ” Scene editor lite")) return 1;
    SetTargetFPS(60);

    Editor ed;
    memset(&ed, 0, sizeof(ed));
    if (argc > 1) snprintf(ed.scenePath, sizeof(ed.scenePath), "%s", argv[1]);
    editorEnsureDefaults(&ed);
    if (argc > 1 && SceneLoad(&ed.scene, ed.scenePath) && ed.scene.count > 0)
        ed.selected = 0;
    editorReload(&ed);

    vtext_EnsureDefaultFont();
    Font font = GetFontDefault();
    VGuiContext gui = {0};
    gui.theme = VGuiDefaultTheme();

    vgraphics_SetDirectionalLight(true, (Vector3){-0.35f, -0.7f, -0.6f},
                                  (Color){255, 250, 240, 255}, 2.5f);
    vgraphics_SetAmbientLight(true, (Color){90, 100, 120, 255}, 0.25f);
    vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(1.1f);
    SetPostProcess(1);
    SetFXAA(1);

    ed.yaw = 0.5f; ed.pitch = 0.35f; ed.dist = 6.0f;
    Camera3D cam = {0};

    int frames = 0;
    while (!WindowShouldClose()) {
        if ((IsKeyDown(SDL_SCANCODE_LCTRL) || IsKeyDown(SDL_SCANCODE_RCTRL)) &&
            IsKeyPressed(SDL_SCANCODE_Z))
            editorUndo(&ed);
        if ((IsKeyDown(SDL_SCANCODE_LCTRL) || IsKeyDown(SDL_SCANCODE_RCTRL)) &&
            IsKeyPressed(SDL_SCANCODE_Y))
            editorRedo(&ed);
        editorCamera(&ed, &cam);
        BeginDrawing();
        ClearBackground((Color){35, 38, 44, 255});
        BeginMode3D(cam);
        DrawGrid(20, 1.0f);
        SceneDraw(&ed.scene, &ed.runtime);
        EndMode3D();

        VGuiBeginFrame(&gui, font, GetFrameTime());
        editorUI(&ed, &gui);
        VGuiEndFrame(&gui);
        DrawText("Scene editor lite — drag to orbit, wheel to zoom", 350, 690, 15,
                 LIGHTGRAY);
        EndDrawing();
        frames++;
        const char *shot = getenv("VLCR_SCREENSHOT");
        if (shot && frames == 40) vgraphics_CaptureScreenshot(shot);
    }

    SceneUnloadModels(&ed.runtime);
    free(ed.undo);
    free(ed.redo);
    CloseWindow();
    return 0;
}

