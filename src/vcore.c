#include "vcore.h"
#include "vgraphics.h"
#include "vcpu.h"
#include "vplatform.h"
#include "vtext.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>

#if defined(VLCRLIB_GRAPHICS_CPU) && VLCRLIB_GRAPHICS_CPU
#define VLCRLIB_CPU_RENDERER 1
#else
#define VLCRLIB_CPU_RENDERER 0
#endif

typedef struct CoreState {
    SDL_Window *window;
    bool        shouldClose;
    bool        resized;
    int         width, height;
    double      targetFPS;
    double      prevTime, frameTime;

    bool keysDown[SDL_SCANCODE_COUNT];
    bool keysPressed[SDL_SCANCODE_COUNT];
    bool keysReleased[SDL_SCANCODE_COUNT];

    bool    mouseDown[8], mousePressed[8], mouseReleased[8];
    Vector2 mousePos;
    float   mouseWheel;

    int exitKey;
    char textInput[32];

    Camera2D currentCamera2D;
    Camera3D currentCamera3D;
    bool     inMode2D, inMode3D;

    bool          cpuMode;
    VCpuCanvas    cpuCanvas;
    SDL_Renderer *cpuRenderer;
    SDL_Texture  *cpuTexture;
    Color         cpuClear;
} CoreState;

static CoreState CORE = {0};
static bool graphicsReady = false;
static void vcore_ResizeCpuCanvas(void);
static void vcore_BuildCpuShadows(void);

#if VLCRLIB_USE_VULKAN || VLCRLIB_CPU_RENDERER
/* Built once per Model handle instead of rebuilding all CPU geometry on every
   DrawModel() call. Invalidated by UnloadModel() and material changes. */
static VCpuModel g_cpuModelCache[VLCRLIB_MAX_MODELS];
static bool      g_cpuModelValid[VLCRLIB_MAX_MODELS];

#define CPU_MAX_SHADOW_CASTERS 64
typedef struct CpuCaster {
    unsigned int modelId;
    Matrix       world;
} CpuCaster;
static CpuCaster g_cpuCasters[CPU_MAX_SHADOW_CASTERS];
static int       g_cpuCasterCount = 0;
#endif

extern void vshapes_FlushBatch(void);

void *GetWindowHandle(void) {
    return (void *)CORE.window;
}

bool InitWindow(int width, int height, const char *title) {
    graphicsReady = false;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "[vlcrlib] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    CORE.cpuMode = (VRenderBackendGet() == VRENDER_BACKEND_CPU);
    /* Vulkan on desktop; WebGPU on web builds */
#if defined(__EMSCRIPTEN__) || (defined(VLCRLIB_USE_WEBGPU) && VLCRLIB_USE_WEBGPU)
    /* Canvas is driven by WebGPU; SDL window still hosts the canvas element */
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#else
    SDL_WindowFlags flags = CORE.cpuMode
        ? SDL_WINDOW_RESIZABLE
        : (SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
#endif
    CORE.window = SDL_CreateWindow(title, width, height, flags);
    if (!CORE.window) {
        fprintf(stderr, "[vlcrlib] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    CORE.width  = width;
    CORE.height = height;

    /* The window manager may not honour the requested size (DPI scaling,
       clamping to the desktop, ...). The swapchain is built from
       CORE.width/height, so storing the REQUEST here produced a swapchain
       that did not match the window (e.g. an 800x600 swapchain presented in
       a 640x480 window: stretched image and a wrong aspect ratio).
       Prefer the real pixel size of the drawable. */
    {
        int pw = 0, ph = 0;
        if (SDL_GetWindowSizeInPixels(CORE.window, &pw, &ph) && pw > 0 && ph > 0) {
            CORE.width = pw; CORE.height = ph;
        } else if (SDL_GetWindowSize(CORE.window, &pw, &ph) && pw > 0 && ph > 0) {
            CORE.width = pw; CORE.height = ph;
        }
        if (CORE.width != width || CORE.height != height)
            fprintf(stderr, "[vlcrlib] window opened as %dx%d (requested %dx%d)\n",
                    CORE.width, CORE.height, width, height);
    }

    CORE.targetFPS = 60.0;
    CORE.prevTime  = (double)SDL_GetTicks() / 1000.0;
    CORE.exitKey   = SDL_SCANCODE_ESCAPE;
    SDL_StartTextInput(CORE.window);

    if (CORE.cpuMode) {
        CORE.cpuRenderer = SDL_CreateRenderer(CORE.window, NULL);
        CORE.cpuTexture = CORE.cpuRenderer
            ? SDL_CreateTexture(CORE.cpuRenderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                CORE.width, CORE.height)
            : NULL;
        if (!CORE.cpuTexture ||
            !VCpuInit(&CORE.cpuCanvas, CORE.width, CORE.height)) {
            fprintf(stderr, "[vlcrlib] CPU backend init failed\n");
            if (CORE.cpuTexture) SDL_DestroyTexture(CORE.cpuTexture);
            if (CORE.cpuRenderer) SDL_DestroyRenderer(CORE.cpuRenderer);
            SDL_DestroyWindow(CORE.window);
            CORE.window = NULL;
            SDL_Quit();
            return false;
        }
        CORE.cpuClear = (Color){0, 0, 0, 255};
        graphicsReady = true;
        return true;
    }

    VkInitResult r = InitGraphicsDevice();
    if (r != VKINIT_OK) {
        fprintf(stderr, "[vlcrlib] InitGraphicsDevice failed: %d\n", (int)r);
        if (CORE.window) SDL_DestroyWindow(CORE.window);
        CORE.window = NULL;
        SDL_Quit();
        return false;
    }
    graphicsReady = true;
    return true;
}

bool vcore_IsCpuBackend(void) {
    return CORE.cpuMode;
}

#if VLCRLIB_USE_VULKAN || VLCRLIB_CPU_RENDERER
void vcore_InvalidateCpuModel(unsigned int modelId) {
    if (modelId == 0 || modelId >= VLCRLIB_MAX_MODELS) return;
    if (!g_cpuModelValid[modelId]) return;
    VCpuModelFree(&g_cpuModelCache[modelId]);
    g_cpuModelValid[modelId] = false;
}

void vcore_InvalidateCpuModels(void) {
    for (unsigned int i = 1; i < VLCRLIB_MAX_MODELS; ++i)
        vcore_InvalidateCpuModel(i);
}

static bool vcore_GetCpuModel(Model model, const VCpuModel **out) {
    if (!out || model.id == 0 || model.id >= VLCRLIB_MAX_MODELS) return false;
    if (g_cpuModelValid[model.id]) {
        *out = &g_cpuModelCache[model.id];
        return true;
    }
    VCpuModel built;
    if (!vgraphics_BuildCpuModel(model, &built)) return false;
    g_cpuModelCache[model.id] = built;
    g_cpuModelValid[model.id] = true;
    *out = &g_cpuModelCache[model.id];
    return true;
}

static void vcore_CpuShutdown(void) {
    vcore_InvalidateCpuModels();
    VCpuShutdown();
}

/* Build this frame's shadow map from the previous frame's caster list. The
   immediate-mode API has no scene graph, so a one-frame delay is unavoidable. */
static void vcore_BuildCpuShadows(void) {
    if (!VCpuShadowsEnabled()) {
        g_cpuCasterCount = 0;
        return;
    }
    VCpuShadowCaster list[CPU_MAX_SHADOW_CASTERS];
    int count = 0;
    for (int i = 0; i < g_cpuCasterCount; ++i) {
        unsigned int id = g_cpuCasters[i].modelId;
        if (id == 0 || id >= VLCRLIB_MAX_MODELS || !g_cpuModelValid[id])
            continue;
        list[count].model = &g_cpuModelCache[id];
        list[count].world = g_cpuCasters[i].world;
        count++;
    }
    VCpuShadowBuild(list, count);
    g_cpuCasterCount = 0;
}
#else
static void vcore_BuildCpuShadows(void) {}
#endif

void vcore_CpuDrawVertices(const Vertex2D *vertices, int count,
                           Texture2D texture) {
    if (!CORE.cpuMode || !vertices || count < 3) return;
    const uint32_t *pixels = NULL;
    int tw = 0, th = 0;
    VCpuTexture cpuTex;
    const VCpuTexture *useTex = NULL;
    if (texture.id && vgraphics_GetCpuTexturePixels(texture, &pixels, &tw, &th)) {
        cpuTex = (VCpuTexture){ tw, th, (uint32_t *)pixels };
        useTex = &cpuTex;
    }
    VCpuBatch batch;
    VCpuBatchBegin(&batch, &CORE.cpuCanvas, useTex);
    for (int i = 0; i + 2 < count; i += 3) {
        const Vertex2D *a = &vertices[i], *b = &vertices[i + 1],
                       *c = &vertices[i + 2];
        Vector2 pa = {a->x, a->y}, pb = {b->x, b->y}, pc = {c->x, c->y};
        Vector2 ua = {a->u, a->v}, ub = {b->u, b->v}, uc = {c->u, c->v};
        Color ca = {a->r, a->g, a->b, a->a};
        VCpuBatchPushTriangle(&batch, pa, pb, pc, ua, ub, uc, ca);
    }
    VCpuBatchFlush(&batch);
    VCpuBatchEnd(&batch);
}

void vcore_CpuDrawModel(Model model, Vector3 position, Vector3 rotation,
                        Vector3 scale, Color tint) {
#if VLCRLIB_USE_VULKAN || VLCRLIB_CPU_RENDERER
    if (!CORE.cpuMode || model.id == 0) return;
    const VCpuModel *cpu = NULL;
    if (!vcore_GetCpuModel(model, &cpu)) return;

    Camera3D cam = CORE.currentCamera3D;
    if (cam.fovy <= 0.0f) {
        cam.position = (Vector3){0, 0, 5};
        cam.target = (Vector3){0, 0, 0};
        cam.up = (Vector3){0, 1, 0};
        cam.fovy = 45.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 1000.0f;
    }
    float aspect = (CORE.height > 0)
        ? (float)CORE.width / (float)CORE.height : 1.0f;
    Matrix proj = MatrixPerspective(cam.fovy * VLCRLIB_DEG2RAD, aspect,
                                    cam.nearPlane, cam.farPlane);
    proj.m5 = -proj.m5; /* top-left CPU framebuffer */
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    /* Same convention as the GPU path: T * R * S. */
    Matrix world = MatrixMultiply(
        MatrixTranslate(position.x, position.y, position.z),
        MatrixMultiply(MatrixRotateXYZ(rotation),
                       MatrixScale(scale.x, scale.y, scale.z)));
    if (VCpuShadowsEnabled() && g_cpuCasterCount < CPU_MAX_SHADOW_CASTERS) {
        g_cpuCasters[g_cpuCasterCount].modelId = model.id;
        g_cpuCasters[g_cpuCasterCount].world = world;
        g_cpuCasterCount++;
    }
    Matrix mvp = MatrixMultiply(proj, MatrixMultiply(view, world));
    VCpuDrawModelShaded(&CORE.cpuCanvas, cpu, mvp, world, cam.position, tint);
#else
    (void)model; (void)position; (void)rotation; (void)scale; (void)tint;
#endif
}

void CloseWindow(void) {
#if VLCRLIB_USE_VULKAN || VLCRLIB_CPU_RENDERER
    vcore_CpuShutdown();
#endif
    if (CORE.cpuMode) {
        VCpuFree(&CORE.cpuCanvas);
        if (CORE.cpuTexture) SDL_DestroyTexture(CORE.cpuTexture);
        if (CORE.cpuRenderer) SDL_DestroyRenderer(CORE.cpuRenderer);
    } else if (graphicsReady) {
        CloseGraphicsDevice();
    }
    if (CORE.window) SDL_DestroyWindow(CORE.window);
    SDL_Quit();
    memset(&CORE, 0, sizeof(CORE));
    graphicsReady = false;
}

bool IsWindowResized(void) {
    bool resized = CORE.resized;
    CORE.resized = false;
    return resized;
}

void SetWindowTitle(const char *title) {
    if (CORE.window && title) SDL_SetWindowTitle(CORE.window, title);
}

void SetWindowPosition(int x, int y) {
    if (CORE.window) SDL_SetWindowPosition(CORE.window, x, y);
}

void SetWindowSize(int width, int height) {
    if (CORE.window) SDL_SetWindowSize(CORE.window, width, height);
}

/* Declared in vcore.h but never implemented; the bindings expose it, and so
   does every example that wants a borderless window. SDL keeps the state, so
   the toggle is simply "flip whatever the window currently is". */
void ToggleFullscreen(void) {
    if (!CORE.window) return;

    bool fullscreen = (SDL_GetWindowFlags(CORE.window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(CORE.window, fullscreen ? false : true);
}

void SetExitKey(int key) {
    CORE.exitKey = key;
}

bool WindowShouldClose(void) {
    if (!graphicsReady) return true;
    memset(CORE.keysPressed, 0, sizeof(CORE.keysPressed));
    memset(CORE.keysReleased, 0, sizeof(CORE.keysReleased));
    memset(CORE.mousePressed, 0, sizeof(CORE.mousePressed));
    memset(CORE.mouseReleased, 0, sizeof(CORE.mouseReleased));
    CORE.mouseWheel = 0.0f;
    CORE.textInput[0] = '\0';

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_EVENT_QUIT:
                CORE.shouldClose = true;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                CORE.resized = true;
                CORE.width   = ev.window.data1;
                CORE.height  = ev.window.data2;
                if (CORE.cpuMode) vcore_ResizeCpuCanvas();
                else vgraphics_Resize();
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                /* Authoritative drawable size: the window may have been
                   resized by DPI changes without a logical RESIZED event. */
                CORE.resized = true;
                CORE.width   = ev.window.data1;
                CORE.height  = ev.window.data2;
                if (CORE.cpuMode) vcore_ResizeCpuCanvas();
                else vgraphics_Resize();
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!ev.key.repeat) {
                    CORE.keysPressed[ev.key.scancode] = true;
                    CORE.keysDown[ev.key.scancode]    = true;
                    if (ev.key.scancode == CORE.exitKey) CORE.shouldClose = true;
                }
                break;
            case SDL_EVENT_KEY_UP:
                CORE.keysReleased[ev.key.scancode] = true;
                CORE.keysDown[ev.key.scancode]     = false;
                break;
            case SDL_EVENT_TEXT_INPUT:
                strncpy(CORE.textInput, ev.text.text, sizeof(CORE.textInput) - 1);
                CORE.textInput[sizeof(CORE.textInput) - 1] = '\0';
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button < 8) {
                    CORE.mousePressed[ev.button.button] = true;
                    CORE.mouseDown[ev.button.button]    = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button < 8) {
                    CORE.mouseReleased[ev.button.button] = true;
                    CORE.mouseDown[ev.button.button]     = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                CORE.mousePos.x = ev.motion.x;
                CORE.mousePos.y = ev.motion.y;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                CORE.mouseWheel = ev.wheel.y;
                break;
        }
    }
    return CORE.shouldClose;
}

void SetTargetFPS(int fps)  { CORE.targetFPS = (fps > 0) ? fps : 60.0; }
float GetFrameTime(void)    { return (float)CORE.frameTime; }
double GetTime(void)        { return (double)SDL_GetTicks() / 1000.0; }

bool IsKeyPressed(int k)  { return (k>0 && k<SDL_SCANCODE_COUNT) ? CORE.keysPressed[k]  : false; }
bool IsKeyDown(int k)     { return (k>0 && k<SDL_SCANCODE_COUNT) ? CORE.keysDown[k]     : false; }
bool IsKeyReleased(int k) { return (k>0 && k<SDL_SCANCODE_COUNT) ? CORE.keysReleased[k] : false; }
int  GetKeyPressed(void) {
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i)
        if (CORE.keysPressed[i]) return i;
    return 0;
}

int GetCharPressed(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0 || CORE.textInput[0] == '\0') return 0;
    strncpy(buffer, CORE.textInput, capacity - 1);
    buffer[capacity - 1] = '\0';
    CORE.textInput[0] = '\0';
    return 1;
}

bool IsMouseButtonPressed(int b)  { return (b>=0 && b<8) ? CORE.mousePressed[b]  : false; }
bool IsMouseButtonDown(int b)     { return (b>=0 && b<8) ? CORE.mouseDown[b]     : false; }
bool IsMouseButtonReleased(int b) { return (b>=0 && b<8) ? CORE.mouseReleased[b] : false; }
Vector2 GetMousePosition(void)    { return CORE.mousePos; }
float GetMouseWheelMove(void)     { return CORE.mouseWheel; }
void SetMousePosition(int x, int y) { if (CORE.window) SDL_WarpMouseInWindow(CORE.window, (float)x, (float)y); }


bool IsGamepadAvailable(int pad) {
    SDL_JoystickID id = (SDL_JoystickID)pad;
    return SDL_IsGamepad(id);
}
bool IsGamepadButtonPressed(int pad, int b) {
    SDL_Gamepad *g = SDL_GetGamepadFromID((SDL_JoystickID)pad);
    return g ? SDL_GetGamepadButton(g, (SDL_GamepadButton)b) : false;
}
bool IsGamepadButtonDown(int pad, int b) {
    SDL_Gamepad *g = SDL_GetGamepadFromID((SDL_JoystickID)pad);
    return g ? SDL_GetGamepadButton(g, (SDL_GamepadButton)b) : false;
}
float GetGamepadAxisMovement(int pad, int a) {
    SDL_Gamepad *g = SDL_GetGamepadFromID((SDL_JoystickID)pad);
    return g ? (float)SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a) : 0.0f;
}

void BeginDrawing(void) {
    if (!graphicsReady) return;
    /* Load the UI fallback between frames, before command recording starts. */
    vtext_EnsureDefaultFont();
    if (CORE.cpuMode) {
        VCpuClear(&CORE.cpuCanvas, CORE.cpuClear);
        VCpuClearDepth(&CORE.cpuCanvas);
        VCpuResetScissor(&CORE.cpuCanvas);
        vcore_BuildCpuShadows();
        return;
    }
    BeginVulkanFrame();
    SetVulkanViewport(0, 0, CORE.width, CORE.height);
    vgraphics_Set2DViewportSize(CORE.width, CORE.height);
}

void EndDrawing(void) {
    if (!graphicsReady) return;
    vshapes_FlushBatch();
    if (CORE.cpuMode) {
        if (CORE.cpuTexture && CORE.cpuRenderer) {
            SDL_UpdateTexture(CORE.cpuTexture, NULL, CORE.cpuCanvas.color,
                              CORE.cpuCanvas.width * 4);
            SDL_RenderClear(CORE.cpuRenderer);
            SDL_RenderTexture(CORE.cpuRenderer, CORE.cpuTexture, NULL, NULL);
            SDL_RenderPresent(CORE.cpuRenderer);
        }
    } else {
        EndVulkanFrame();
    }

    double now = (double)SDL_GetTicks() / 1000.0;
    double wait = (1.0 / CORE.targetFPS) - (now - CORE.prevTime);
    if (wait > 0) SDL_Delay((Uint32)(wait * 1000.0));
    CORE.frameTime = now - CORE.prevTime;
    CORE.prevTime  = now;
}

void ClearBackground(Color c) {
    if (CORE.cpuMode) {
        CORE.cpuClear = c;
        VCpuClear(&CORE.cpuCanvas, c);
        VCpuClearDepth(&CORE.cpuCanvas);
        return;
    }
    SetVulkanClearColor(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

static void vcore_ResizeCpuCanvas(void) {
    if (!CORE.cpuMode || CORE.width <= 0 || CORE.height <= 0) return;
    if (CORE.cpuCanvas.width == CORE.width &&
        CORE.cpuCanvas.height == CORE.height) return;
    VCpuCanvas next;
    if (!VCpuInit(&next, CORE.width, CORE.height)) return;
    VCpuClear(&next, CORE.cpuClear);
    VCpuClearDepth(&next);
    VCpuFree(&CORE.cpuCanvas);
    CORE.cpuCanvas = next;
    if (CORE.cpuTexture) SDL_DestroyTexture(CORE.cpuTexture);
    CORE.cpuTexture = SDL_CreateTexture(CORE.cpuRenderer,
                                        SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        CORE.width, CORE.height);
}

void BeginMode2D(Camera2D camera) {
    vshapes_FlushBatch();
    CORE.currentCamera2D = camera;
    CORE.inMode2D = true;
    CORE.inMode3D = false;

    float w = (float)CORE.width;
    float h = (float)CORE.height;

    vgraphics_Set2DViewportSize(CORE.width, CORE.height);

    float zoom = (camera.zoom > 0.0001f) ? camera.zoom : 1.0f;
    Matrix proj = MatrixOrtho(0, w, h, 0, -1, 1);
    Matrix matTarget = MatrixTranslate(-camera.target.x, -camera.target.y, 0.0f);
    Matrix matRotation = MatrixRotateZ(camera.rotation);
    Matrix matScale = MatrixScale(zoom, zoom, 1.0f);
    Matrix matOffset = MatrixTranslate(camera.offset.x, camera.offset.y, 0.0f);
    Matrix view = MatrixMultiply(MatrixMultiply(MatrixMultiply(matTarget, matRotation), matScale), matOffset);

    SetVulkanViewMatrix(view);
    SetVulkanProjectionMatrix(proj);
}

void EndMode2D(void) {
    vshapes_FlushBatch();
    CORE.inMode2D = false;
}

void BeginMode3D(Camera3D camera) {
    vshapes_FlushBatch();
    CORE.currentCamera3D = camera;
    CORE.inMode3D = true;
    CORE.inMode2D = false;
    if (CORE.cpuMode) return;

    /* A minimized window reports 0 height; guarding here keeps the aspect
       finite instead of collapsing the X axis of the projection. */
    int rw = 0, rh = 0;
    vgraphics_GetRenderTargetSize(&rw, &rh);
    float fw = (rw > 0) ? (float)rw : ((CORE.width  > 0) ? (float)CORE.width  : 1.0f);
    float fh = (rh > 0) ? (float)rh : ((CORE.height > 0) ? (float)CORE.height : 1.0f);
    float aspect = fw / fh;
    float zn = camera.nearPlane > 0.0f ? camera.nearPlane : 0.1f;
    float zf = camera.farPlane  > zn    ? camera.farPlane  : 1000.0f;
    float fovy = camera.fovy > 0.0f ? camera.fovy : 45.0f;

    Matrix proj = MatrixPerspective(fovy * VLCRLIB_DEG2RAD, aspect, zn, zf);
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);

    SetVulkanViewMatrix(view);
    SetVulkanProjectionMatrix(proj);
    vgraphics_Set3DMatrices(view, proj);
    vgraphics_SetCameraPos(camera.position);

    /* HDR environment background (no-op unless IBL is loaded and the
       skybox is enabled). Drawn first so depth-tested geometry covers it
       and transparent objects blend over it. */
    vgraphics_DrawSkybox();
}

void EndMode3D(void) {
    extern void vgraphics_FlushTransparent(void);
    if (!CORE.cpuMode) vgraphics_FlushTransparent();
    vshapes_FlushBatch();
    CORE.inMode3D = false;
}

void UpdateCamera2D(Camera2D *camera, int mode) { (void)camera; (void)mode; }
void UpdateCamera3D(Camera3D *camera, int mode) { (void)camera; (void)mode; }
void UpdateCamera(void *camera, int mode)       { (void)camera; (void)mode; }

void *vcore_GetWindow(void) { return CORE.window; }
void vcore_GetScreenSize(int *w, int *h) {
    if (w) *w = CORE.width;
    if (h) *h = CORE.height;
}
/* ============================================================ */
/*  Кэш нажатий для vinput                                       */
/* ============================================================ */
bool vcore_KeyPressed(int scancode) {
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
    return CORE.keysPressed[scancode];
}

bool vcore_KeyReleased(int scancode) {
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
    return CORE.keysReleased[scancode];
}

bool vcore_MouseButtonPressed(int button) {
    if (button < 0 || button >= 8) return false;
    return CORE.mousePressed[button];
}

bool vcore_MouseButtonReleased(int button) {
    if (button < 0 || button >= 8) return false;
    return CORE.mouseReleased[button];
}

/* ============================================================ */
/*  FPS                                                          */
/* ============================================================ */
int GetFPS(void) {
    if (CORE.frameTime <= 0.0) return 0;
    return (int)(1.0 / CORE.frameTime);
}