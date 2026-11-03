/*
 * pbr_properties_gallery_example.c — glTF 2.0 PBR property laboratory.
 *
 * 16 cells, 3 variants each, covering:
 *   Base Color, Metallic, Roughness, Normal, Ambient Occlusion,
 *   Emissive + KHR_materials_emissive_strength, Alpha Coverage,
 *   Specular, Clearcoat, Sheen, Iridescence, Anisotropy,
 *   IOR, Transmission, Volume, Dispersion.
 *
 * Controls:
 *   LEFT / RIGHT  select a property
 *   SPACE         toggle camera orbit
 *   R             reset selection and camera
 *
 * PBR_GALLERY_TEST=1 runs five frames and exits (smoke test).
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PBR_PI       3.14159265358979323846f
#define PROP_COUNT   16
#define VARIANTS     3
#define CELL_STEP    4.4f
#define WIN_W        1280
#define WIN_H        720

typedef struct PropInfo {
    const char *name;
    const char *desc;
    const char *values[VARIANTS];
} PropInfo;

static const PropInfo PROPS[PROP_COUNT] = {
    { "BASE COLOR",
      "Linear baseColorFactor multiplied by the base color texture. Alpha also feeds alpha coverage.",
      { "red", "green", "blue" } },
    { "METALLIC",
      "Metallic workflow: 0 = dielectric, 1 = conductor. Metals tint reflections with base color and lose diffuse.",
      { "0.0", "0.5", "1.0" } },
    { "ROUGHNESS",
      "Perceptual roughness; shader converts it to alpha = roughness^2 for GGX. Low values give sharp reflections.",
      { "0.05", "0.35", "0.80" } },
    { "NORMAL",
      "Tangent-space normal map. normalScale 0 flattens it, 1 is authored strength, 2.5 exaggerates detail.",
      { "scale 0.0", "scale 1.0", "scale 2.5" } },
    { "AMBIENT OCCLUSION",
      "R-channel occlusion map multiplied into indirect diffuse and used by the specular occlusion term.",
      { "strength 0", "strength 0.5", "strength 1.0" } },
    { "EMISSIVE + STRENGTH",
      "KHR_materials_emissive_strength: HDR emission above 1 is preserved for bloom and tone mapping.",
      { "x0.5", "x2", "x8" } },
    { "ALPHA COVERAGE",
      "alphaMode MASK + alphaCutoff. The generated spoke texture makes cutoff changes visible. BLEND is supported too.",
      { "cutoff 0.25", "cutoff 0.50", "cutoff 0.75" } },
    { "SPECULAR",
      "KHR_materials_specular scales dielectric F0 (default 0.04). Factor 0 removes the dielectric reflection.",
      { "factor 0.0", "factor 0.5", "factor 1.0" } },
    { "CLEARCOAT",
      "KHR_materials_clearcoat: a second glossy dielectric layer over the base material, with its own roughness.",
      { "cc 0.0", "cc 0.6", "cc 1.0" } },
    { "SHEEN",
      "KHR_materials_sheen: fabric-like grazing response (Charlie + Ashikhmin), strongest at low sheen roughness.",
      { "rough 0.15", "rough 0.50", "rough 1.00" } },
    { "IRIDESCENCE",
      "KHR_materials_iridescence: thin-film interference tints F0. Thickness is in nanometres (150 / 350 / 700).",
      { "150 nm", "350 nm", "700 nm" } },
    { "ANISOTROPY",
      "KHR_materials_anisotropy: anisotropic GGX stretches the highlight along the tangent direction.",
      { "off", "0.7 @ 0deg", "1.0 @ 90deg" } },
    { "INDEX OF REFRACTION",
      "KHR_materials_ior changes dielectric F0 = ((ior-1)/(ior+1))^2. Air=1.0, glass~1.5, diamond~2.4.",
      { "ior 1.0", "ior 1.5", "ior 2.4" } },
    { "TRANSMISSION",
      "KHR_materials_transmission: thin-walled transmission. Drawn in the blended pass; uses IBL as background.",
      { "0.0", "0.5", "1.0" } },
    { "VOLUME",
      "KHR_materials_volume: thickness + attenuationColor/attenuationDistance approximate absorption inside glass.",
      { "thick 0.4", "thick 1.2", "thick 3.0" } },
    { "DISPERSION",
      "KHR_materials_dispersion is parsed and exported to the UBO. The raster shader is transmissive; spectral "
      "refraction needs the path-traced reference renderer.",
      { "0.0", "0.5", "3.0" } }
};

typedef struct Item {
    Model model;
    int   prop;
    int   variant;
} Item;

static Item      gItems[PROP_COUNT * VARIANTS];
static Texture2D gNormalTex;
static Texture2D gAOTex;
static Texture2D gAlphaTex;

/* ------------------------------------------------------------------ */
/*  Procedural data textures                                          */
/* ------------------------------------------------------------------ */

static Texture2D UploadRGBA(unsigned char *pixels, int w, int h) {
    Image img;
    memset(&img, 0, sizeof(img));
    img.data = pixels;
    img.width = w;
    img.height = h;
    img.mipmaps = 1;
    img.format = 0;
    Texture2D tex = LoadTextureFromImage(img);
    free(pixels);
    return tex;
}

static float BumpHeight(float u, float v) {
    return 0.50f * sinf(u * 2.0f) * cosf(v * 2.0f)
         + 0.25f * sinf(u * 5.0f + 1.0f)
         + 0.20f * cosf(v * 7.0f);
}

static Texture2D MakeNormalTexture(int size) {
    unsigned char *pix = (unsigned char *)malloc((size_t)size * size * 4);
    if (!pix) return (Texture2D){0};
    const float e = 0.02f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = ((float)x + 0.5f) / (float)size * PBR_PI * 6.0f;
            float v = ((float)y + 0.5f) / (float)size * PBR_PI * 6.0f;
            float dx = (BumpHeight(u + e, v) - BumpHeight(u - e, v)) / (2.0f * e);
            float dy = (BumpHeight(u, v + e) - BumpHeight(u, v - e)) / (2.0f * e);
            float nx = -dx * 0.35f;
            float ny = -dy * 0.35f;
            float nz = 1.0f;
            float inv = 1.0f / sqrtf(nx*nx + ny*ny + nz*nz);
            nx *= inv; ny *= inv; nz *= inv;
            size_t i = ((size_t)y * size + x) * 4;
            pix[i + 0] = (unsigned char)((nx * 0.5f + 0.5f) * 255.0f);
            pix[i + 1] = (unsigned char)((ny * 0.5f + 0.5f) * 255.0f);
            pix[i + 2] = (unsigned char)((nz * 0.5f + 0.5f) * 255.0f);
            pix[i + 3] = 255;
        }
    }
    return UploadRGBA(pix, size, size);
}

static Texture2D MakeAOTexture(int size) {
    unsigned char *pix = (unsigned char *)malloc((size_t)size * size * 4);
    if (!pix) return (Texture2D){0};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = ((float)x + 0.5f) / (float)size;
            float v = ((float)y + 0.5f) / (float)size;
            float grid = fminf(fabsf(sinf(u * PBR_PI * 8.0f)),
                               fabsf(sinf(v * PBR_PI * 8.0f)));
            float ao = 0.30f + 0.70f * fminf(grid * 6.0f, 1.0f);
            unsigned char c = (unsigned char)(ao * 255.0f);
            size_t i = ((size_t)y * size + x) * 4;
            pix[i + 0] = c; pix[i + 1] = c; pix[i + 2] = c; pix[i + 3] = 255;
        }
    }
    return UploadRGBA(pix, size, size);
}

static Texture2D MakeAlphaTexture(int size) {
    unsigned char *pix = (unsigned char *)malloc((size_t)size * size * 4);
    if (!pix) return (Texture2D){0};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = ((float)x + 0.5f) / (float)size;
            float v = ((float)y + 0.5f) / (float)size;
            float ang = atan2f(v - 0.5f, u - 0.5f);
            float a = 0.5f + 0.5f * sinf(ang * 14.0f);
            a = a * 0.85f + 0.10f;
            size_t i = ((size_t)y * size + x) * 4;
            pix[i + 0] = 255; pix[i + 1] = 255; pix[i + 2] = 255;
            pix[i + 3] = (unsigned char)(a * 255.0f);
        }
    }
    return UploadRGBA(pix, size, size);
}

/* ------------------------------------------------------------------ */
/*  Materials                                                         */
/* ------------------------------------------------------------------ */

static Material MakeMaterial(int prop, int variant) {
    Material m = LoadMaterialDefault();
    SetMaterialBaseColorFactor(&m, 0.8f, 0.8f, 0.8f, 1.0f);
    SetMaterialPBR(&m, 0.0f, 0.5f);

    switch (prop) {
    case 0: { /* Base Color */
        static const float c[3][3] = {
            { 0.80f, 0.12f, 0.10f },
            { 0.12f, 0.62f, 0.22f },
            { 0.10f, 0.30f, 0.85f }
        };
        SetMaterialBaseColorFactor(&m, c[variant][0], c[variant][1], c[variant][2], 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.38f);
        break;
    }
    case 1: /* Metallic */
        SetMaterialBaseColorFactor(&m, 0.95f, 0.64f, 0.32f, 1.0f);
        SetMaterialPBR(&m, (float)variant * 0.5f, 0.22f);
        break;
    case 2: /* Roughness */
        SetMaterialBaseColorFactor(&m, 0.90f, 0.90f, 0.95f, 1.0f);
        SetMaterialPBR(&m, 1.0f, variant == 0 ? 0.05f : (variant == 1 ? 0.35f : 0.80f));
        break;
    case 3: /* Normal */
        SetMaterialBaseColorFactor(&m, 0.55f, 0.62f, 0.72f, 1.0f);
        SetMaterialPBR(&m, 0.10f, 0.42f);
        SetMaterialTexture(&m, MATERIAL_MAP_NORMAL, gNormalTex);
        m.normalScale = variant == 0 ? 0.0f : (variant == 1 ? 1.0f : 2.5f);
        break;
    case 4: /* Ambient occlusion */
        SetMaterialBaseColorFactor(&m, 0.78f, 0.78f, 0.82f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.78f);
        SetMaterialTexture(&m, MATERIAL_MAP_AO, gAOTex);
        m.aoStrength = variant == 0 ? 0.002f : (variant == 1 ? 0.5f : 1.0f);
        break;
    case 5: /* Emissive + KHR strength */
        SetMaterialBaseColorFactor(&m, 0.03f, 0.03f, 0.035f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.45f);
        SetMaterialEmission(&m, (Color){ 255, 140, 45, 255 }, 1.0f);
        m.emissiveStrengthKhr = variant == 0 ? 0.5f : (variant == 1 ? 2.0f : 8.0f);
        break;
    case 6: /* Alpha coverage */
        SetMaterialBaseColorFactor(&m, 0.85f, 0.14f, 0.18f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.40f);
        SetMaterialTexture(&m, MATERIAL_MAP_ALBEDO, gAlphaTex);
        m.alphaMode = 1; /* MASK */
        m.alphaCutoff = variant == 0 ? 0.25f : (variant == 1 ? 0.50f : 0.75f);
        break;
    case 7: /* Specular */
        SetMaterialBaseColorFactor(&m, 0.04f, 0.12f, 0.30f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.14f);
        SetMaterialSpecular(&m, variant == 0 ? 0.0f : (variant == 1 ? 0.5f : 1.0f),
                            1.0f, 0.72f, 0.42f);
        break;
    case 8: /* Clearcoat */
        SetMaterialBaseColorFactor(&m, 0.28f, 0.035f, 0.06f, 1.0f);
        SetMaterialPBR(&m, 0.10f, 0.55f);
        m.clearcoatFactor = variant == 0 ? 0.0f : (variant == 1 ? 0.6f : 1.0f);
        m.clearcoatRoughness = 0.08f;
        break;
    case 9: /* Sheen */
        SetMaterialBaseColorFactor(&m, 0.07f, 0.05f, 0.09f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.75f);
        m.sheenColor[0] = 0.95f; m.sheenColor[1] = 0.42f; m.sheenColor[2] = 0.55f;
        m.sheenRoughness = variant == 0 ? 0.15f : (variant == 1 ? 0.50f : 1.0f);
        break;
    case 10: /* Iridescence */
        SetMaterialBaseColorFactor(&m, 0.18f, 0.18f, 0.24f, 1.0f);
        SetMaterialPBR(&m, 1.0f, 0.12f);
        m.iridescenceFactor = 1.0f;
        m.iridescenceIor = 1.3f;
        m.iridescenceThicknessMin = variant == 0 ? 150.0f : (variant == 1 ? 350.0f : 700.0f);
        m.iridescenceThicknessMax = m.iridescenceThicknessMin;
        break;
    case 11: /* Anisotropy */
        SetMaterialBaseColorFactor(&m, 0.88f, 0.88f, 0.92f, 1.0f);
        SetMaterialPBR(&m, 1.0f, 0.14f);
        m.anisotropyStrength = variant == 0 ? 0.0f : (variant == 1 ? 0.7f : 1.0f);
        m.anisotropyRotation = variant == 0 ? 0.0f : (variant == 1 ? 0.0f : PBR_PI * 0.5f);
        break;
    case 12: /* Index of refraction */
        SetMaterialBaseColorFactor(&m, 0.72f, 0.72f, 0.75f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.04f);
        SetMaterialIOR(&m, variant == 0 ? 1.0f : (variant == 1 ? 1.5f : 2.4f));
        break;
    case 13: /* Transmission */
        SetMaterialBaseColorFactor(&m, 1.0f, 1.0f, 1.0f, 1.0f);
        SetMaterialPBR(&m, 0.0f, variant == 0 ? 0.02f : 0.06f);
        m.alphaMode = 2; /* BLEND */
        m.transmissionFactor = variant == 0 ? 0.0f : (variant == 1 ? 0.5f : 1.0f);
        m.thicknessFactor = 0.0f;
        break;
    case 14: /* Volume */
        SetMaterialBaseColorFactor(&m, 1.0f, 1.0f, 1.0f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.15f);
        m.alphaMode = 2;
        m.transmissionFactor = 1.0f;
        m.thicknessFactor = variant == 0 ? 0.4f : (variant == 1 ? 1.2f : 3.0f);
        m.attenuationColor[0] = 1.00f;
        m.attenuationColor[1] = 0.25f;
        m.attenuationColor[2] = 0.05f;
        m.attenuationDistance = 0.5f;
        break;
    case 15: /* Dispersion */
        SetMaterialBaseColorFactor(&m, 1.0f, 1.0f, 1.0f, 1.0f);
        SetMaterialPBR(&m, 0.0f, 0.03f);
        m.alphaMode = 2;
        m.transmissionFactor = 1.0f;
        m.thicknessFactor = 0.5f;
        m.dispersion = variant == 0 ? 0.0f : (variant == 1 ? 0.5f : 3.0f);
        break;
    default:
        break;
    }
    return m;
}

/* ------------------------------------------------------------------ */
/*  HUD helpers                                                       */
/* ------------------------------------------------------------------ */

static Vector2 ProjectPoint(Vector3 p, Matrix vp, int w, int h) {
    float x = vp.m0*p.x + vp.m4*p.y + vp.m8*p.z + vp.m12;
    float y = vp.m1*p.x + vp.m5*p.y + vp.m9*p.z + vp.m13;
    float z = vp.m2*p.x + vp.m6*p.y + vp.m10*p.z + vp.m14;
    float ww = vp.m3*p.x + vp.m7*p.y + vp.m11*p.z + vp.m15;
    (void)z;
    if (ww <= 0.0001f) return (Vector2){ -10000.0f, -10000.0f };
    float nx = x / ww;
    float ny = y / ww;
    /* MatrixPerspective() already negates Y for Vulkan's top-left origin. */
    return (Vector2){ (nx * 0.5f + 0.5f) * (float)w,
                      (ny * 0.5f + 0.5f) * (float)h };
}

static void DrawWrappedText(const char *text, int x, int y, int size,
                            int maxWidth, Color color) {
    char line[512] = {0};
    size_t len = 0;
    const char *p = text;
    while (*p) {
        const char *start = p;
        while (*p && *p != ' ') ++p;
        int wl = (int)(p - start);
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s%s%.*s",
                 line, len > 0 ? " " : "", wl, start);
        if (MeasureText(candidate, size) > maxWidth && len > 0) {
            DrawText(line, x, y, size, color);
            y += size + 4;
            snprintf(line, sizeof(line), "%.*s", wl, start);
            len = (size_t)wl;
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
            len = strlen(line);
        }
        if (*p == ' ') ++p;
    }
    if (len > 0) DrawText(line, x, y, size, color);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    if (!InitWindow(WIN_W, WIN_H, "vlcrlib - glTF 2.0 PBR Property Lab")) return 1;
    SetTargetFPS(60);

    /* DrawText()/MeasureText() use the default font, which only exists after
       the first LoadFontEx() call. The gallery is a text-heavy HUD. */
    Font uiFont = {0};
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
    if (!uiFont.ok) uiFont = LoadFontEx("C:/Windows/Fonts/arial.ttf", 32, 32, 96);
#else
    uiFont = LoadFontEx("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 32, 32, 96);
#endif
    if (!uiFont.ok) fprintf(stderr, "[pbr_gallery] warning: no UI font found\n");

    /* Procedural data maps. */
    gNormalTex = MakeNormalTexture(256);
    gAOTex     = MakeAOTexture(256);
    gAlphaTex  = MakeAlphaTexture(256);

    /* Environment first: materials created afterwards bind valid IBL maps. */
    bool ibl = vgraphics_LoadIBL(NULL);
    if (ibl) {
        vgraphics_SetIBLIntensity(0.25f);
        vgraphics_SetSkybox(1);
    } else {
        vgraphics_SetIBLIntensity(0.0f);
        vgraphics_SetSkybox(0);
    }

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.45f, -1.0f, -0.35f },
        (Color){ 255, 246, 228, 255 }, 1.0f);
    vgraphics_SetAmbientLight(true, (Color){ 80, 95, 120, 255 }, 0.06f);
    /* A point light close to the camera gives a compact highlight that makes
       roughness / metallic / anisotropy readable; IBL alone is too broad. */
    vgraphics_SetPointLight(true, (Vector3){ 8.0f, 4.5f, 10.0f },
                            (Color){ 255, 244, 228, 255 }, 20.0f, 60.0f);
    vgraphics_SetSpotLight(false, (Vector3){ 0, 0, 0 }, (Vector3){ 0, -1, 0 },
                           WHITE, 0.0f, 1.0f, 0.4f, 0.6f);
    vgraphics_SetAreaLight(false, (Vector3){ 0, 5, 0 }, (Vector3){ 0, -1, 0 },
                           2.0f, 2.0f, WHITE, 0.0f);
    /* Shadows are intentionally off: the first shadow pass would leave the
       depth image in UNDEFINED layout for the first material draw. */
    vgraphics_SetShadows(false, 1024, 0.002f, 0.55f);
    vgraphics_ComputeDirectionalShadowMatrix(18.0f, 0.5f, 50.0f);
    SetSSAO(1, 0.5f, 0.6f);
    SetBloom(0.9f, 0.20f);
    SetGraphicsExposure(1.0f);

    Mesh sphere = GenMeshSphere(1.0f, 20, 28);
    for (int p = 0; p < PROP_COUNT; ++p) {
        for (int v = 0; v < VARIANTS; ++v) {
            int idx = p * VARIANTS + v;
            Material mat = MakeMaterial(p, v);
            int matIndex = 0;
            gItems[idx].model = vgraphics_CreateModelFromMeshes(
                &sphere, &matIndex, 1, &mat, 1);
            gItems[idx].prop = p;
            gItems[idx].variant = v;
        }
    }

    Camera3D cam;
    memset(&cam, 0, sizeof(cam));
    cam.target = (Vector3){ 1.5f, 1.2f, 0.0f };
    cam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy = 45.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;

    int selected = 0;
    {
        const char *start = getenv("PBR_GALLERY_START");
        if (start) {
            int v = atoi(start);
            if (v >= 0 && v < PROP_COUNT) selected = v;
        }
    }
    bool orbit = true;
    float angle = 0.65f;
    float time = 0.0f;
    int testFrames = getenv("PBR_GALLERY_TEST") ? 6 : -1;
    int frame = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        time += dt;

        if (IsKeyPressed(SDL_SCANCODE_LEFT))  selected = (selected + PROP_COUNT - 1) % PROP_COUNT;
        if (IsKeyPressed(SDL_SCANCODE_RIGHT)) selected = (selected + 1) % PROP_COUNT;
        if (IsKeyPressed(SDL_SCANCODE_SPACE)) orbit = !orbit;
        if (IsKeyPressed(SDL_SCANCODE_R)) { selected = 0; angle = 0.65f; orbit = true; }
        if (orbit) angle += dt * 0.06f;

        /* Anisotropy is easiest to read without a bright isotropic IBL
           highlight: lower the environment contribution for that page. */
        vgraphics_SetIBLIntensity(selected == 11 ? 0.04f : 0.55f);

        cam.position = (Vector3){ 1.5f + sinf(angle) * 10.5f, 3.6f,
                                  cosf(angle) * 10.5f };

        Matrix proj = MatrixPerspective(cam.fovy * (PBR_PI / 180.0f),
                                        (float)WIN_W / (float)WIN_H,
                                        cam.nearPlane, cam.farPlane);
        Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
        Matrix vp = MatrixMultiply(proj, view);

        BeginDrawing();
        ClearBackground((Color){ 14, 16, 22, 255 });

        BeginMode3D(cam);
        DrawGrid(40, 1.0f);
        for (int v = 0; v < VARIANTS; ++v) {
            int idx = selected * VARIANTS + v;
            Vector3 pos = { 1.5f + ((float)v - 1.0f) * 2.6f, 1.05f, 0.0f };
            Vector3 rot = { 0.0f, time * 0.28f + (float)selected * 0.4f, 0.0f };
            DrawModel(gItems[idx].model, pos, rot, (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
        }
        EndMode3D();

        /* Variant labels above the three spheres. */
        static const char *LEVEL[VARIANTS] = { "low", "mid", "high" };
        for (int v = 0; v < VARIANTS; ++v) {
            Vector3 world = { 1.5f + ((float)v - 1.0f) * 2.6f, 2.4f, 0.0f };
            Vector2 s = ProjectPoint(world, vp, WIN_W, WIN_H);
            if (s.x < 0.0f || s.x > (float)WIN_W || s.y < 0.0f || s.y > (float)WIN_H)
                continue;
            char label[128];
            snprintf(label, sizeof(label), "%s  %s", LEVEL[v],
                     PROPS[selected].values[v]);
            int tw = MeasureText(label, 15);
            DrawRectangle((int)s.x - tw / 2 - 6, (int)s.y - 15, tw + 12, 22,
                          (Color){ 0, 0, 0, 150 });
            DrawText(label, (int)s.x - tw / 2, (int)s.y - 12, 15,
                     (Color){ 230, 235, 245, 255 });
        }

        /* Left property list. */
        DrawRectangle(0, 0, 300, WIN_H, (Color){ 8, 10, 16, 242 });
        DrawText("PBR MATERIAL LAB", 18, 16, 22, WHITE);
        DrawText("16 glTF 2.0 properties", 18, 44, 13,
                 (Color){ 150, 162, 180, 255 });

        for (int i = 0; i < PROP_COUNT; ++i) {
            int y = 78 + i * 35;
            if (i == selected) {
                DrawRectangle(8, y - 5, 284, 30, (Color){ 255, 215, 80, 35 });
                DrawRectangle(8, y - 5, 4, 30, YELLOW);
            }
            char number[8];
            snprintf(number, sizeof(number), "%02d", i + 1);
            DrawText(number, 18, y, 13, (Color){ 135, 148, 165, 255 });
            DrawText(PROPS[i].name, 50, y, 15,
                     i == selected ? (Color){ 255, 215, 80, 255 }
                                   : (Color){ 205, 214, 228, 255 });
        }
        DrawText("LEFT / RIGHT  select", 18, 650, 13,
                 (Color){ 150, 162, 180, 255 });
        DrawText("SPACE  orbit    R  reset", 18, 672, 13,
                 (Color){ 150, 162, 180, 255 });

        /* Description panel under the 3D view. */
        DrawRectangle(300, 585, WIN_W - 300, 135, (Color){ 8, 10, 16, 235 });
        DrawText(PROPS[selected].name, 320, 597, 24,
                 (Color){ 255, 215, 80, 255 });
        DrawWrappedText(PROPS[selected].desc, 320, 632,
                        16, WIN_W - 340, (Color){ 220, 226, 236, 255 });

        char values[512];
        snprintf(values, sizeof(values), "low: %s      mid: %s      high: %s",
                 PROPS[selected].values[0], PROPS[selected].values[1],
                 PROPS[selected].values[2]);
        DrawText(values, 320, 694, 15, (Color){ 170, 182, 198, 255 });

        EndDrawing();

        ++frame;
        if (testFrames > 0 && frame >= testFrames) break;
    }

    for (int i = 0; i < PROP_COUNT * VARIANTS; ++i)
        UnloadModel(gItems[i].model);
    UnloadTexture(gNormalTex);
    UnloadTexture(gAOTex);
    UnloadTexture(gAlphaTex);
    if (uiFont.ok) UnloadFont(uiFont);
    vgraphics_UnloadIBL();
    CloseWindow();
    fprintf(stderr, "[pbr_gallery] ok (%d properties, %d variants)\n",
            PROP_COUNT, VARIANTS);
    return 0;
}
