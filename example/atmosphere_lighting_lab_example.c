/*
 * atmosphere_lighting_lab_example.c
 *
 * Interactive atmosphere + lighting laboratory:
 *   - day/night cycle, sun/moon elevation and azimuth
 *   - procedural sky: sun/moon disks, stars, clouds, horizon haze
 *   - weather: clear / cloudy / rain / snow / fog
 *   - point, spot, directional, area and ambient lights
 *   - mobility labels: movable / stationary / static / orbital
 *   - distance falloff, spot cone, area size, shadows and softness
 *   - analytic volumetric fog in-scattering (sun/moon haze)
 *
 * Controls:
 *   LEFT/RIGHT or 1..5  toggle point/spot/sun/area/ambient
 *   W                   cycle weather
 *   T                   advance time by 30 minutes
 *   N                   toggle automatic day/night cycle
 *   S                   toggle directional shadows
 *   [ / ]               shadow softness
 *   V                   toggle volumetric fog
 *
 * ATMOSPHERE_TEST=1 runs six frames and exits.
 * ATMOSPHERE_TIME=18.0 / ATMOSPHERE_WEATHER=0..4 select a start state.
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ATM_PI       3.14159265358979323846f
#define WIN_W        1280
#define WIN_H        720
#define RAIN_COUNT   210
#define SNOW_COUNT   150

typedef struct RainDrop {
    float x, y, len, speed;
} RainDrop;

typedef struct SnowFlake {
    float x, y, size, speed, drift;
} SnowFlake;

typedef struct WeatherPreset {
    const char *name;
    float cloud;
    float haze;
    float fog;
} WeatherPreset;

static const WeatherPreset WEATHER[] = {
    { "CLEAR",  0.22f, 0.03f, 0.000f },
    { "CLOUDY", 0.78f, 0.12f, 0.008f },
    { "RAIN",   0.92f, 0.38f, 0.020f },
    { "SNOW",   0.86f, 0.28f, 0.014f },
    { "FOG",    0.45f, 0.90f, 0.045f }
};
#define WEATHER_COUNT ((int)(sizeof(WEATHER) / sizeof(WEATHER[0])))

static RainDrop  gRain[RAIN_COUNT];
static SnowFlake gSnow[SNOW_COUNT];

static float clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}

static Color MixColor(Color a, Color b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255
    };
}

static Vector3 ComputeSunDirection(float hours) {
    /* 06:00 east horizon, 12:00 zenith, 18:00 west horizon. */
    float a = (hours - 6.0f) / 12.0f * ATM_PI;
    /* Tilt the orbit so the sunset (18:00) happens in front of the default
       camera, making the sun disk and warm horizon visible. */
    Vector3 d = { 0.35f * cosf(a) + 0.10f, sinf(a),
                  0.90f * cosf(a) + 0.12f };
    return Vector3Normalize(d);
}

static Color SunLightColor(float sunY) {
    Color warm = { 255, 112, 42, 255 };
    Color noon = { 255, 245, 228, 255 };
    return MixColor(warm, noon, clampf(sunY / 0.42f, 0.0f, 1.0f));
}

static Vector2 ProjectPoint(Vector3 p, Matrix vp, int w, int h) {
    float x = vp.m0*p.x + vp.m4*p.y + vp.m8*p.z + vp.m12;
    float y = vp.m1*p.x + vp.m5*p.y + vp.m9*p.z + vp.m13;
    float ww = vp.m3*p.x + vp.m7*p.y + vp.m11*p.z + vp.m15;
    if (ww <= 0.0001f) return (Vector2){ -10000.0f, -10000.0f };
    return (Vector2){ (x / ww * 0.5f + 0.5f) * (float)w,
                      (y / ww * 0.5f + 0.5f) * (float)h };
}

static Model CreateModel(Mesh mesh, Material material) {
    int index = 0;
    return vgraphics_CreateModelFromMeshes(&mesh, &index, 1, &material, 1);
}

static Material MakeMaterial(float r, float g, float b,
                             float metallic, float roughness) {
    Material m = LoadMaterialDefault();
    SetMaterialBaseColorFactor(&m, r, g, b, 1.0f);
    SetMaterialPBR(&m, metallic, roughness);
    return m;
}

static void InitPrecipitation(void) {
    srand(24601);
    for (int i = 0; i < RAIN_COUNT; ++i) {
        gRain[i].x = (float)(rand() % WIN_W);
        gRain[i].y = (float)(rand() % WIN_H);
        gRain[i].len = 9.0f + (float)(rand() % 12);
        gRain[i].speed = 620.0f + (float)(rand() % 420);
    }
    for (int i = 0; i < SNOW_COUNT; ++i) {
        gSnow[i].x = (float)(rand() % WIN_W);
        gSnow[i].y = (float)(rand() % WIN_H);
        gSnow[i].size = 1.5f + (float)(rand() % 25) * 0.1f;
        gSnow[i].speed = 24.0f + (float)(rand() % 40);
        gSnow[i].drift = (float)(rand() % 100) * 0.01f;
    }
}

static void UpdatePrecipitation(int weather, float dt, float time) {
    if (weather == 2) { /* rain */
        for (int i = 0; i < RAIN_COUNT; ++i) {
            gRain[i].y += gRain[i].speed * dt;
            gRain[i].x -= gRain[i].speed * 0.20f * dt;
            if (gRain[i].y > WIN_H + gRain[i].len) {
                gRain[i].y = -gRain[i].len;
                gRain[i].x = (float)(rand() % WIN_W);
            }
            if (gRain[i].x < -20.0f) gRain[i].x += WIN_W + 40.0f;
        }
    } else if (weather == 3) { /* snow */
        for (int i = 0; i < SNOW_COUNT; ++i) {
            gSnow[i].y += gSnow[i].speed * dt;
            gSnow[i].x += sinf(time * 1.3f + gSnow[i].drift * 40.0f) * 12.0f * dt;
            if (gSnow[i].y > WIN_H + 4.0f) {
                gSnow[i].y = -4.0f;
                gSnow[i].x = (float)(rand() % WIN_W);
            }
            if (gSnow[i].x < -10.0f) gSnow[i].x += WIN_W + 20.0f;
            if (gSnow[i].x > WIN_W + 10.0f) gSnow[i].x -= WIN_W + 20.0f;
        }
    }
}

static void DrawPrecipitation(int weather) {
    if (weather == 2) {
        for (int i = 0; i < RAIN_COUNT; ++i) {
            DrawLineEx((Vector2){ gRain[i].x, gRain[i].y },
                       (Vector2){ gRain[i].x + 4.0f, gRain[i].y + gRain[i].len },
                       1.3f, (Color){ 170, 192, 225, 120 });
        }
    } else if (weather == 3) {
        for (int i = 0; i < SNOW_COUNT; ++i) {
            DrawCircleV((Vector2){ gSnow[i].x, gSnow[i].y }, gSnow[i].size,
                        (Color){ 238, 244, 255, 165 });
        }
    }
}

int main(void) {
    if (!InitWindow(WIN_W, WIN_H, "vlcrlib - Atmosphere & Lighting Lab")) return 1;
    SetTargetFPS(60);

    Font uiFont = {0};
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
    if (!uiFont.ok) uiFont = LoadFontEx("C:/Windows/Fonts/arial.ttf", 32, 32, 96);
#else
    uiFont = LoadFontEx("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 32, 32, 96);
#endif

    InitPrecipitation();

    /* IBL provides subtle reflections; the atmosphere shader owns the sky. */
    bool ibl = vgraphics_LoadIBL(NULL);
    if (ibl) vgraphics_SetIBLIntensity(0.10f);
    vgraphics_SetSkybox(1);

    /* Create the directional shadow resources before any material exists:
       toggling them mid-frame would update descriptor sets that the previous
       frame may still be using. */
    vgraphics_SetShadows(true, 2048, 0.002f, 0.75f);
    vgraphics_SetSpotShadows(false, 1024, 0.003f, 0.8f);
    vgraphics_SetPointShadows(false, 512, 0.004f, 0.8f);
    SetSSAO(1, 0.55f, 0.55f);
    SetBloom(0.95f, 0.20f);
    SetGraphicsExposure(0.9f);

    /* Scene geometry. */
    Mesh plane = GenMeshPlane(50.0f, 50.0f, 1, 1);
    Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    Mesh sphere = GenMeshSphere(1.0f, 24, 32);

    Material groundMat = MakeMaterial(0.32f, 0.33f, 0.36f, 0.0f, 0.92f);
    Material pedestalMat = MakeMaterial(0.16f, 0.17f, 0.19f, 0.1f, 0.65f);
    Material obeliskMat = MakeMaterial(0.10f, 0.11f, 0.13f, 0.2f, 0.55f);
    Material pointMat = MakeMaterial(0.88f, 0.42f, 0.12f, 0.85f, 0.24f);
    Material spotMat = MakeMaterial(0.16f, 0.36f, 0.88f, 0.75f, 0.20f);
    Material sunMat = MakeMaterial(0.82f, 0.80f, 0.75f, 0.0f, 0.52f);
    Material areaMat = MakeMaterial(0.95f, 0.72f, 0.26f, 0.90f, 0.30f);
    Material ambientMat = MakeMaterial(0.52f, 0.10f, 0.10f, 0.0f, 0.80f);

    Model ground = CreateModel(plane, groundMat);
    Model pedestal = CreateModel(cube, pedestalMat);
    Model obelisk = CreateModel(cube, obeliskMat);
    Model pointSphere = CreateModel(sphere, pointMat);
    Model spotSphere = CreateModel(sphere, spotMat);
    Model sunSphere = CreateModel(sphere, sunMat);
    Model areaSphere = CreateModel(sphere, areaMat);
    Model ambientSphere = CreateModel(sphere, ambientMat);

    float timeOfDay = 15.5f;
    bool autoTime = true;
    int weather = 0;
    bool lightPoint = true;
    bool lightSpot = true;
    bool lightSun = true;
    bool lightArea = true;
    bool lightAmbient = true;
    bool shadows = true;
    bool shadowsPrev = true;
    bool volumetric = true;
    float shadowSoftness = 1.5f;
    int frame = 0;

    {
        const char *t = getenv("ATMOSPHERE_TIME");
        if (t) timeOfDay = (float)atof(t);
        const char *w = getenv("ATMOSPHERE_WEATHER");
        if (w) {
            weather = atoi(w);
            if (weather < 0 || weather >= WEATHER_COUNT) weather = 0;
        }
    }
    int testFrames = getenv("ATMOSPHERE_TEST") ? 6 : -1;
    {
        const char *dbg = getenv("ATMOSPHERE_DEBUG");
        if (dbg) SetGraphicsDebugView(atoi(dbg));
    }

    Camera3D cam;
    memset(&cam, 0, sizeof(cam));
    cam.position = (Vector3){ 0.0f, 6.5f, 18.0f };
    cam.target = (Vector3){ 0.0f, 2.0f, 0.0f };
    cam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy = 50.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 120.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        float time = (float)GetTime();

        if (autoTime) {
            timeOfDay += dt * 0.08f; /* full cycle ~5 minutes */
            if (timeOfDay >= 24.0f) timeOfDay -= 24.0f;
        }
        if (IsKeyPressed(SDL_SCANCODE_T)) {
            timeOfDay += 0.5f;
            if (timeOfDay >= 24.0f) timeOfDay -= 24.0f;
        }
        if (IsKeyPressed(SDL_SCANCODE_N)) autoTime = !autoTime;
        if (IsKeyPressed(SDL_SCANCODE_W)) weather = (weather + 1) % WEATHER_COUNT;
        if (IsKeyPressed(SDL_SCANCODE_S)) shadows = !shadows;
        if (IsKeyPressed(SDL_SCANCODE_V)) volumetric = !volumetric;
        if (IsKeyPressed(SDL_SCANCODE_LEFTBRACKET))
            shadowSoftness = clampf(shadowSoftness - 0.25f, 1.0f, 2.0f);
        if (IsKeyPressed(SDL_SCANCODE_RIGHTBRACKET))
            shadowSoftness = clampf(shadowSoftness + 0.25f, 1.0f, 2.0f);
        if (IsKeyPressed(SDL_SCANCODE_1)) lightPoint = !lightPoint;
        if (IsKeyPressed(SDL_SCANCODE_2)) lightSpot = !lightSpot;
        if (IsKeyPressed(SDL_SCANCODE_3)) lightSun = !lightSun;
        if (IsKeyPressed(SDL_SCANCODE_4)) lightArea = !lightArea;
        if (IsKeyPressed(SDL_SCANCODE_5)) lightAmbient = !lightAmbient;

        Vector3 sun = ComputeSunDirection(timeOfDay);
        Vector3 moon = Vector3Normalize((Vector3){ -sun.x, -sun.y, -sun.z });
        float sunY = sun.y;
        float day = clampf((sunY + 0.12f) / 0.24f, 0.0f, 1.0f);
        float sunset = expf(-fabsf(sunY) * 9.0f) * clampf((sunY + 0.22f) / 0.30f, 0.0f, 1.0f);

        /* Celestial light: sun by day, moon by night. */
        bool sunUp = sunY > -0.04f;
        Vector3 celestialDir = sunUp ? sun : moon;
        float celestialI = sunUp ? (5.2f * sqrtf(clampf(sunY, 0.0f, 1.0f)))
                                 : (0.55f * clampf(-sunY, 0.0f, 1.0f));
        Color celestialColor = sunUp ? SunLightColor(sunY)
                                     : (Color){ 160, 185, 230, 255 };
        vgraphics_SetDirectionalLight(lightSun,
            Vector3Scale(celestialDir, -1.0f), celestialColor, celestialI);

        Color ambientDay = { 92, 112, 142, 255 };
        Color ambientNight = { 24, 30, 48, 255 };
        Color ambientSet = { 120, 76, 55, 255 };
        Color ambient = MixColor(ambientNight, ambientDay, day);
        ambient = MixColor(ambient, ambientSet, sunset * 0.55f);
        vgraphics_SetAmbientLight(lightAmbient, ambient,
                                  0.025f + 0.040f * day);

        const WeatherPreset *wp = &WEATHER[weather];
        float cloud = wp->cloud;
        float haze = wp->haze;
        float fogDensity = wp->fog + (volumetric ? 0.006f : 0.0f);

        Color fogNight = { 18, 22, 34, 255 };
        Color fogDay = { 142, 172, 204, 255 };
        Color fogSunset = { 214, 120, 78, 255 };
        Color fogColor = MixColor(fogNight, fogDay, day);
        fogColor = MixColor(fogColor, fogSunset, sunset * 0.70f);
        vgraphics_SetFog(fogDensity > 0.00005f, fogColor, 9.0f, 60.0f, fogDensity);

        /* Point light: movable, orbited around its station. */
        float pointAngle = time * 0.7f;
        Vector3 pointPos = {
            -8.0f + cosf(pointAngle) * 1.0f,
            3.1f,
            sinf(pointAngle) * 1.0f
        };
        vgraphics_SetPointLight(lightPoint, pointPos,
                                (Color){ 255, 178, 96, 255 }, 7.0f, 9.0f);

        /* Spot: stationary position, animated intensity. */
        Vector3 spotPos = { -4.0f, 7.0f, 1.2f };
        Vector3 spotDir = Vector3Normalize(Vector3Subtract((Vector3){ -4.0f, 0.0f, 0.0f }, spotPos));
        float spotPulse = 42.0f + 10.0f * sinf(time * 2.0f);
        vgraphics_SetSpotLight(lightSpot, spotPos, spotDir,
                               (Color){ 150, 190, 255, 255 }, spotPulse,
                               14.0f, 0.30f, 0.56f);

        /* Area light: fixed rectangular panel. */
        vgraphics_SetAreaLight(lightArea, (Vector3){ 4.0f, 5.3f, 0.0f },
                               (Vector3){ 0.0f, -1.0f, 0.0f },
                               3.5f, 2.0f, (Color){ 255, 214, 150, 255 }, 8.0f);

        AtmosphereParams atmosphere;
        memset(&atmosphere, 0, sizeof(atmosphere));
        atmosphere.enabled = 1;
        atmosphere.timeOfDay = timeOfDay;
        atmosphere.cloudCoverage = cloud;
        atmosphere.haze = haze;
        atmosphere.starIntensity = 0.85f;
        atmosphere.sunDirection = sun;
        atmosphere.sunIntensity = 4.0f;
        atmosphere.moonDirection = moon;
        atmosphere.moonIntensity = 0.7f;
        atmosphere.sunAngularRadius = 0.009f;
        atmosphere.moonAngularRadius = 0.011f;
        vgraphics_SetAtmosphere(&atmosphere);

        if (shadows != shadowsPrev) {
            vgraphics_SetShadows(shadows, 2048, 0.002f, 0.75f);
            shadowsPrev = shadows;
        }
        vgraphics_SetShadowSoftness(shadowSoftness);
        vgraphics_ComputeDirectionalShadowMatrix(24.0f, 0.5f, 70.0f);

        UpdatePrecipitation(weather, dt, time);

        Matrix proj = MatrixPerspective(cam.fovy * (ATM_PI / 180.0f),
                                        (float)WIN_W / (float)WIN_H,
                                        cam.nearPlane, cam.farPlane);
        Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
        Matrix vp = MatrixMultiply(proj, view);

        BeginDrawing();
        ClearBackground((Color){ 10, 12, 18, 255 });

        BeginMode3D(cam);
        DrawModel(ground, (Vector3){ 0, 0, 0 }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1, 1, 1 }, WHITE);
        /* Pedestals and spheres: five light stations. */
        DrawModel(pedestal, (Vector3){ -8, 0.25f, 0 }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 3.9f, 0.5f, 1.8f }, WHITE);
        DrawModel(pedestal, (Vector3){ -4, 0.25f, 0 }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1.8f, 0.5f, 1.8f }, WHITE);
        DrawModel(pedestal, (Vector3){  0, 0.25f, 0 }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1.8f, 0.5f, 1.8f }, WHITE);
        DrawModel(pedestal, (Vector3){  4, 0.25f, 0 }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 3.9f, 0.5f, 1.8f }, WHITE);
        DrawModel(pedestal, (Vector3){  8, 0.25f, 0 }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1.8f, 0.5f, 1.8f }, WHITE);

        /* Point station: three probes at increasing distance show falloff. */
        DrawModel(pointSphere, (Vector3){ -9.4f, 1.10f, 0.0f }, (Vector3){ 0, time * 0.3f, 0 },
                  (Vector3){ 0.46f, 0.46f, 0.46f }, WHITE);
        DrawModel(pointSphere, (Vector3){ -8.0f, 1.10f, 0.0f }, (Vector3){ 0, time * 0.3f, 0 },
                  (Vector3){ 0.46f, 0.46f, 0.46f }, WHITE);
        DrawModel(pointSphere, (Vector3){ -6.6f, 1.10f, 0.0f }, (Vector3){ 0, time * 0.3f, 0 },
                  (Vector3){ 0.46f, 0.46f, 0.46f }, WHITE);

        DrawModel(spotSphere, (Vector3){ -4.0f, 1.35f, 0.0f }, (Vector3){ 0, time * 0.2f, 0 },
                  (Vector3){ 0.78f, 0.78f, 0.78f }, WHITE);
        DrawModel(sunSphere, (Vector3){ 0.0f, 1.35f, 0.0f }, (Vector3){ 0, time * 0.1f, 0 },
                  (Vector3){ 0.78f, 0.78f, 0.78f }, WHITE);
        DrawModel(areaSphere, (Vector3){ 3.2f, 1.15f, 0.0f }, (Vector3){ 0, time * 0.15f, 0 },
                  (Vector3){ 0.60f, 0.60f, 0.60f }, WHITE);
        DrawModel(areaSphere, (Vector3){ 4.8f, 1.15f, 0.0f }, (Vector3){ 0, time * 0.15f, 0 },
                  (Vector3){ 0.60f, 0.60f, 0.60f }, WHITE);
        DrawModel(ambientSphere, (Vector3){ 8.0f, 1.35f, 0.0f }, (Vector3){ 0, time * 0.1f, 0 },
                  (Vector3){ 0.78f, 0.78f, 0.78f }, WHITE);

        DrawModel(obelisk, (Vector3){ 0.0f, 2.0f, -5.0f }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1.3f, 4.0f, 1.3f }, WHITE);

        if (lightPoint) DrawSphere(pointPos, 0.12f, (Color){ 255, 210, 150, 255 });
        if (lightSpot)  DrawSphere(spotPos, 0.14f, (Color){ 170, 205, 255, 255 });
        if (lightArea)  DrawSphere((Vector3){ 4.0f, 5.3f, 0.0f }, 0.14f, (Color){ 255, 225, 170, 255 });
        EndMode3D();

        DrawPrecipitation(weather);

        /* Station labels. */
        {
            struct { Vector3 pos; const char *name; const char *mobility; Color color; } labels[5] = {
                { { -8.0f, 2.55f, 0.0f }, "POINT",   "movable / falloff", (Color){255,190,120,255} },
                { { -4.0f, 2.75f, 0.0f }, "SPOT",    "stationary / cone", (Color){150,195,255,255} },
                { {  0.0f, 2.75f, 0.0f }, "SUN/MOON","orbital / parallel", (Color){255,235,170,255} },
                { {  4.0f, 2.55f, 0.0f }, "AREA",    "static / soft",     (Color){255,215,140,255} },
                { {  8.0f, 2.75f, 0.0f }, "AMBIENT", "static / fill",     (Color){190,200,215,255} }
            };
            for (int i = 0; i < 5; ++i) {
                Vector2 s = ProjectPoint(labels[i].pos, vp, WIN_W, WIN_H);
                if (s.x < 0 || s.x > WIN_W || s.y < 0 || s.y > WIN_H) continue;
                int w = MeasureText(labels[i].name, 15);
                DrawRectangle((int)s.x - w / 2 - 5, (int)s.y - 13, w + 10, 19,
                              (Color){ 0, 0, 0, 150 });
                DrawText(labels[i].name, (int)s.x - w / 2, (int)s.y - 11, 15,
                         labels[i].color);
                int w2 = MeasureText(labels[i].mobility, 12);
                DrawRectangle((int)s.x - w2 / 2 - 4, (int)s.y + 7, w2 + 8, 15,
                              (Color){ 0, 0, 0, 120 });
                DrawText(labels[i].mobility, (int)s.x - w2 / 2, (int)s.y + 8, 12,
                         (Color){ 190, 200, 215, 255 });
            }
        }

        /* HUD */
        int hh = (int)timeOfDay;
        int mm = (int)((timeOfDay - (float)hh) * 60.0f);
        if (hh >= 24) hh = 0;
        float elevation = asinf(clampf(sunY, -1.0f, 1.0f)) * 180.0f / ATM_PI;
        float azimuth = atan2f(sun.x, sun.z) * 180.0f / ATM_PI;
        if (azimuth < 0.0f) azimuth += 360.0f;
        float sunMoonAngle = acosf(clampf(Vector3Dot(sun, moon), -1.0f, 1.0f))
                           * 180.0f / ATM_PI;

        DrawRectangle(0, 0, 620, 112, (Color){ 8, 10, 16, 220 });
        DrawText("ATMOSPHERE & LIGHTING LAB", 18, 14, 24, WHITE);
        char line[256];
        snprintf(line, sizeof(line), "%02d:%02d   SUN elev %.1f deg   azim %.0f deg   sun-moon %.0f deg",
                 hh, mm, elevation, azimuth, sunMoonAngle);
        DrawText(line, 18, 48, 15, (Color){ 190, 205, 225, 255 });
        snprintf(line, sizeof(line), "WEATHER: %s   clouds %d%%   haze %d%%   %s",
                 wp->name, (int)(cloud * 100.0f), (int)(haze * 100.0f),
                 autoTime ? "AUTO TIME" : "MANUAL TIME");
        DrawText(line, 18, 72, 15, (Color){ 170, 185, 205, 255 });

        DrawRectangle(0, WIN_H - 132, WIN_W, 132, (Color){ 8, 10, 16, 228 });
        DrawText("LIGHTS", 18, WIN_H - 120, 16, (Color){ 255, 220, 120, 255 });
        snprintf(line, sizeof(line),
                 "[1] POINT %s (movable)   [2] SPOT %s (stationary)   [3] SUN/MOON %s (orbital)   [4] AREA %s (static)   [5] AMBIENT %s",
                 lightPoint ? "ON" : "OFF", lightSpot ? "ON" : "OFF",
                 lightSun ? "ON" : "OFF", lightArea ? "ON" : "OFF",
                 lightAmbient ? "ON" : "OFF");
        DrawText(line, 18, WIN_H - 98, 15, (Color){ 210, 220, 235, 255 });

        snprintf(line, sizeof(line),
                 "SHADOWS [S] %s 2048   soft %.2f  [ ]      VOLUMETRIC FOG [V] %s      [W] weather   [T] +30m   [N] auto time",
                 shadows ? "ON" : "OFF", shadowSoftness, volumetric ? "ON" : "OFF");
        DrawText(line, 18, WIN_H - 72, 15, (Color){ 190, 205, 225, 255 });
        DrawText("Falloff: point 9 m (inverse-square window)   spot 14 m / 17-32 deg   area 3.5 x 2.0 m   sun: no distance falloff",
                 18, WIN_H - 42, 14, (Color){ 155, 170, 190, 255 });

        EndDrawing();

        ++frame;
        if (testFrames > 0 && frame >= testFrames) break;
    }

    UnloadModel(ground);
    UnloadModel(pedestal);
    UnloadModel(obelisk);
    UnloadModel(pointSphere);
    UnloadModel(spotSphere);
    UnloadModel(sunSphere);
    UnloadModel(areaSphere);
    UnloadModel(ambientSphere);
    if (uiFont.ok) UnloadFont(uiFont);
    vgraphics_UnloadIBL();
    CloseWindow();
    fprintf(stderr, "[atmosphere] ok (%s, %.1f h)\n", WEATHER[weather].name, timeOfDay);
    return 0;
}
