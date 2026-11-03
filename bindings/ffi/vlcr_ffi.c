/*
 * vlcr_ffi.c — implementation of the stable binding ABI.
 *
 * Everything here is deliberately thin: it converts handles to engine
 * structs, unpacks colors and forwards the call. The only state it keeps is
 * the resource registry (fonts / textures / sounds / music), because foreign
 * runtimes cannot hold a `Font` or a `Texture2D` by value.
 */
#define VLCR_FFI_BUILD_SHARED 1

#include "vlcr_ffi.h"

#include <vlcrlib.h>

#include <stdlib.h>
#include <string.h>

#ifndef VLCR_FFI_HAS_AUDIO
#  define VLCR_FFI_HAS_AUDIO 1
#endif

#define VLCR_FFI_VERSION "1.0.0"

#define VLCR_MAX_TEXTURES 256
#define VLCR_MAX_FONTS     64
#define VLCR_MAX_SOUNDS   128
#define VLCR_MAX_MUSIC     64

/* ------------------------------------------------------------------------- */
/* Resource registry                                                          */
/* ------------------------------------------------------------------------- */

typedef union vlcr_resource {
    Texture2D texture;
    Font      font;
    Sound     sound;
    Music     music;
} vlcr_resource;

typedef struct vlcr_slot {
    int           used;
    vlcr_resource value;
} vlcr_slot;

static vlcr_slot g_textures[VLCR_MAX_TEXTURES];
static vlcr_slot g_fonts[VLCR_MAX_FONTS];
static vlcr_slot g_sounds[VLCR_MAX_SOUNDS];
static vlcr_slot g_music[VLCR_MAX_MUSIC];

static int g_ready;
static int g_audio_ready;
static vlcr_handle g_default_font;

static vlcr_slot *slot_at(vlcr_slot *table, int capacity, vlcr_handle handle) {
    if (handle <= 0 || handle > capacity) return NULL;
    vlcr_slot *slot = &table[handle - 1];
    return slot->used ? slot : NULL;
}

static vlcr_handle slot_add(vlcr_slot *table, int capacity, const vlcr_resource *value) {
    for (int i = 0; i < capacity; ++i) {
        if (!table[i].used) {
            table[i].used  = 1;
            table[i].value = *value;
            return (vlcr_handle)(i + 1);
        }
    }
    return 0;
}

static void slot_remove(vlcr_slot *table, int capacity, vlcr_handle handle) {
    if (handle <= 0 || handle > capacity) return;
    memset(&table[handle - 1], 0, sizeof(vlcr_slot));
}

static void registry_clear(void) {
    memset(g_textures, 0, sizeof(g_textures));
    memset(g_fonts,    0, sizeof(g_fonts));
    memset(g_sounds,   0, sizeof(g_sounds));
    memset(g_music,    0, sizeof(g_music));
    g_default_font = 0;
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static Color color_from_u32(uint32_t packed) {
    Color c;
    c.r = (unsigned char)( packed        & 0xFFu);
    c.g = (unsigned char)((packed >>  8) & 0xFFu);
    c.b = (unsigned char)((packed >> 16) & 0xFFu);
    c.a = (unsigned char)((packed >> 24) & 0xFFu);
    return c;
}

static Vector2 vec2(float x, float y) {
    Vector2 v;
    v.x = x;
    v.y = y;
    return v;
}

static Vector3 vec3(float x, float y, float z) {
    Vector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static Font font_or_default(vlcr_handle handle) {
    vlcr_slot *slot;
    Font empty = {0};

    if (handle == 0) {
        vtext_EnsureDefaultFont();
        return GetFontDefault();
    }
    slot = slot_at(g_fonts, VLCR_MAX_FONTS, handle);
    return slot ? slot->value.font : empty;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

const char *vlcr_version(void) {
    return VLCR_FFI_VERSION;
}

int vlcr_init(int width, int height, const char *title) {
    if (g_ready) return 1;
    if (width <= 0 || height <= 0) return 0;

    if (!InitWindow(width, height, title ? title : "vlcrlib")) return 0;

    registry_clear();
    g_audio_ready = 0;
    g_ready       = 1;
    return 1;
}

void vlcr_shutdown(void) {
    if (!g_ready) return;

    for (int i = 0; i < VLCR_MAX_TEXTURES; ++i) {
        if (g_textures[i].used) UnloadTexture(g_textures[i].value.texture);
    }
    for (int i = 0; i < VLCR_MAX_FONTS; ++i) {
        if (g_fonts[i].used) UnloadFont(g_fonts[i].value.font);
    }
#if VLCR_FFI_HAS_AUDIO
    for (int i = 0; i < VLCR_MAX_SOUNDS; ++i) {
        if (g_sounds[i].used) UnloadSound(g_sounds[i].value.sound);
    }
    for (int i = 0; i < VLCR_MAX_MUSIC; ++i) {
        if (g_music[i].used) UnloadMusicStream(g_music[i].value.music);
    }
    if (g_audio_ready) CloseAudioDevice();
#endif

    registry_clear();
    g_audio_ready = 0;
    g_ready       = 0;

    CloseWindow();
}

int vlcr_is_ready(void) {
    return g_ready;
}

int vlcr_should_close(void) {
    return g_ready ? (WindowShouldClose() ? 1 : 0) : 1;
}

/* ------------------------------------------------------------------------- */
/* Frame                                                                      */
/* ------------------------------------------------------------------------- */

void   vlcr_begin_drawing(void) { if (g_ready) BeginDrawing(); }
void   vlcr_end_drawing(void)   { if (g_ready) EndDrawing(); }
void   vlcr_clear(uint32_t color) { if (g_ready) ClearBackground(color_from_u32(color)); }
void   vlcr_set_target_fps(int fps) { if (g_ready) SetTargetFPS(fps); }
int    vlcr_fps(void)           { return g_ready ? GetFPS() : 0; }
float  vlcr_frame_time(void)    { return g_ready ? GetFrameTime() : 0.0f; }
double vlcr_time(void)          { return g_ready ? GetTime() : 0.0; }

/* ------------------------------------------------------------------------- */
/* Window                                                                     */
/* ------------------------------------------------------------------------- */

void vlcr_window_size(int *width, int *height) {
    int w = 0, h = 0;
    if (g_ready) vcore_GetScreenSize(&w, &h);
    if (width)  *width  = w;
    if (height) *height = h;
}

void vlcr_set_window_title(const char *title) {
    if (g_ready && title) SetWindowTitle(title);
}

void vlcr_set_window_size(int width, int height) {
    if (g_ready) SetWindowSize(width, height);
}

void vlcr_set_window_position(int x, int y) {
    if (g_ready) SetWindowPosition(x, y);
}

void vlcr_toggle_fullscreen(void) {
    if (g_ready) ToggleFullscreen();
}

void vlcr_set_exit_key(int key) {
    if (g_ready) SetExitKey(key);
}

void *vlcr_native_window(void) {
    return g_ready ? GetWindowHandle() : NULL;
}

/* ------------------------------------------------------------------------- */
/* Input                                                                      */
/* ------------------------------------------------------------------------- */

int vlcr_key_down(int key)      { return g_ready ? (IsKeyDown(key) ? 1 : 0) : 0; }
int vlcr_key_pressed(int key)   { return g_ready ? (IsKeyPressed(key) ? 1 : 0) : 0; }
int vlcr_key_released(int key)  { return g_ready ? (IsKeyReleased(key) ? 1 : 0) : 0; }

int vlcr_mouse_down(int button)     { return g_ready ? (IsMouseButtonDown(button) ? 1 : 0) : 0; }
int vlcr_mouse_pressed(int button)  { return g_ready ? (IsMouseButtonPressed(button) ? 1 : 0) : 0; }
int vlcr_mouse_released(int button) { return g_ready ? (IsMouseButtonReleased(button) ? 1 : 0) : 0; }

void vlcr_mouse_position(float *x, float *y) {
    Vector2 p = vec2(0.0f, 0.0f);
    if (g_ready) p = GetMousePosition();
    if (x) *x = p.x;
    if (y) *y = p.y;
}

float vlcr_mouse_wheel(void) {
    return g_ready ? GetMouseWheelMove() : 0.0f;
}

int vlcr_gamepad_available(int index) {
    return g_ready ? (IsGamepadAvailable(index) ? 1 : 0) : 0;
}

int vlcr_gamepad_down(int index, int button) {
    return g_ready ? (IsGamepadButtonDown(index, button) ? 1 : 0) : 0;
}

float vlcr_gamepad_axis(int index, int axis) {
    return g_ready ? GetGamepadAxisMovement(index, axis) : 0.0f;
}

/* ------------------------------------------------------------------------- */
/* Camera modes                                                               */
/* ------------------------------------------------------------------------- */

void vlcr_begin_mode_2d(float offset_x, float offset_y,
                        float target_x, float target_y,
                        float rotation, float zoom) {
    Camera2D camera;
    if (!g_ready) return;

    camera.offset   = vec2(offset_x, offset_y);
    camera.target   = vec2(target_x, target_y);
    camera.rotation = rotation;
    camera.zoom     = zoom;
    BeginMode2D(camera);
}

void vlcr_end_mode_2d(void) {
    if (g_ready) EndMode2D();
}

void vlcr_begin_mode_3d(float position_x, float position_y, float position_z,
                        float target_x, float target_y, float target_z,
                        float up_x, float up_y, float up_z,
                        float fovy, float near_plane, float far_plane) {
    Camera3D camera;
    if (!g_ready) return;

    camera.position   = vec3(position_x, position_y, position_z);
    camera.target     = vec3(target_x, target_y, target_z);
    camera.up         = vec3(up_x, up_y, up_z);
    camera.fovy       = fovy;
    camera.nearPlane  = near_plane;
    camera.farPlane   = far_plane;
    BeginMode3D(camera);
}

void vlcr_end_mode_3d(void) {
    if (g_ready) EndMode3D();
}

/* ------------------------------------------------------------------------- */
/* 2D shapes                                                                  */
/* ------------------------------------------------------------------------- */

void vlcr_draw_pixel(int x, int y, uint32_t color) {
    if (g_ready) DrawPixel(x, y, color_from_u32(color));
}

void vlcr_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (g_ready) DrawLine(x0, y0, x1, y1, color_from_u32(color));
}

void vlcr_draw_line_ex(float x0, float y0, float x1, float y1,
                       float thickness, uint32_t color) {
    if (g_ready) DrawLineEx(vec2(x0, y0), vec2(x1, y1), thickness, color_from_u32(color));
}

void vlcr_draw_circle(int center_x, int center_y, float radius, uint32_t color) {
    if (g_ready) DrawCircle(center_x, center_y, radius, color_from_u32(color));
}

void vlcr_draw_circle_lines(int center_x, int center_y, float radius, uint32_t color) {
    if (g_ready) DrawCircleLines(center_x, center_y, radius, color_from_u32(color));
}

void vlcr_draw_rectangle(int x, int y, int width, int height, uint32_t color) {
    if (g_ready) DrawRectangle(x, y, width, height, color_from_u32(color));
}

void vlcr_draw_rectangle_lines(int x, int y, int width, int height, uint32_t color) {
    if (g_ready) DrawRectangleLines(x, y, width, height, color_from_u32(color));
}

void vlcr_draw_rectangle_pro(float x, float y, float width, float height,
                             float origin_x, float origin_y, float rotation,
                             uint32_t color) {
    Rectangle rec;
    if (!g_ready) return;

    rec.x      = x;
    rec.y      = y;
    rec.width  = width;
    rec.height = height;
    DrawRectanglePro(rec, vec2(origin_x, origin_y), rotation, color_from_u32(color));
}

void vlcr_draw_triangle(float x1, float y1, float x2, float y2,
                        float x3, float y3, uint32_t color) {
    if (g_ready) {
        DrawTriangle(vec2(x1, y1), vec2(x2, y2), vec2(x3, y3), color_from_u32(color));
    }
}

void vlcr_draw_poly(float center_x, float center_y, int sides, float radius,
                    float rotation, uint32_t color) {
    if (g_ready) {
        DrawPoly(vec2(center_x, center_y), sides, radius, rotation, color_from_u32(color));
    }
}

/* ------------------------------------------------------------------------- */
/* 3D primitives                                                              */
/* ------------------------------------------------------------------------- */

void vlcr_draw_cube(float x, float y, float z,
                    float width, float height, float depth,
                    uint32_t color) {
    if (g_ready) DrawCube(vec3(x, y, z), vec3(width, height, depth), color_from_u32(color));
}

void vlcr_draw_sphere(float x, float y, float z, float radius, uint32_t color) {
    if (g_ready) DrawSphere(vec3(x, y, z), radius, color_from_u32(color));
}

void vlcr_draw_line_3d(float x0, float y0, float z0,
                       float x1, float y1, float z1, uint32_t color) {
    if (g_ready) DrawLine3D(vec3(x0, y0, z0), vec3(x1, y1, z1), color_from_u32(color));
}

void vlcr_draw_grid(int slices, float spacing) {
    if (g_ready) DrawGrid(slices, spacing);
}

/* ------------------------------------------------------------------------- */
/* Text                                                                       */
/* ------------------------------------------------------------------------- */

vlcr_handle vlcr_font_default(void) {
    vlcr_resource value;
    Font font;

    if (g_default_font && slot_at(g_fonts, VLCR_MAX_FONTS, g_default_font)) {
        return g_default_font;
    }
    vtext_EnsureDefaultFont();
    font = GetFontDefault();
    if (!font.ok) return 0;

    memset(&value, 0, sizeof(value));
    value.font = font;
    g_default_font = slot_add(g_fonts, VLCR_MAX_FONTS, &value);
    return g_default_font;
}

vlcr_handle vlcr_font_load(const char *path, int size) {
    vlcr_resource value;
    Font font = LoadFontEx(path, size, 32, 96);

    if (!path || !font.ok) return 0;

    memset(&value, 0, sizeof(value));
    value.font = font;
    return slot_add(g_fonts, VLCR_MAX_FONTS, &value);
}

void vlcr_font_unload(vlcr_handle font) {
    vlcr_slot *slot = slot_at(g_fonts, VLCR_MAX_FONTS, font);
    if (!slot) return;

    UnloadFont(slot->value.font);
    if (font == g_default_font) g_default_font = 0;
    slot_remove(g_fonts, VLCR_MAX_FONTS, font);
}

void vlcr_draw_text(const char *text, int x, int y, int font_size, uint32_t color) {
    if (!g_ready || !text) return;
    vtext_EnsureDefaultFont();
    DrawText(text, x, y, font_size, color_from_u32(color));
}

void vlcr_draw_text_ex(vlcr_handle font, const char *text, float x, float y,
                       float font_size, float spacing, uint32_t color) {
    if (!g_ready || !text) return;
    DrawTextEx(font_or_default(font), text, vec2(x, y), font_size, spacing,
               color_from_u32(color));
}

int vlcr_measure_text(const char *text, int font_size) {
    if (!g_ready || !text) return 0;
    vtext_EnsureDefaultFont();
    return MeasureText(text, font_size);
}

void vlcr_measure_text_ex(vlcr_handle font, const char *text,
                          float font_size, float spacing,
                          float *width, float *height) {
    Vector2 size = vec2(0.0f, 0.0f);

    if (g_ready && text) {
        size = MeasureTextEx(font_or_default(font), text, font_size, spacing);
    }
    if (width)  *width  = size.x;
    if (height) *height = size.y;
}

/* ------------------------------------------------------------------------- */
/* Textures                                                                   */
/* ------------------------------------------------------------------------- */

vlcr_handle vlcr_texture_load(const char *path) {
    vlcr_resource value;
    Texture2D texture;

    if (!path) return 0;

    texture = LoadTexture(path);
    if (texture.id == 0) return 0;

    memset(&value, 0, sizeof(value));
    value.texture = texture;
    return slot_add(g_textures, VLCR_MAX_TEXTURES, &value);
}

void vlcr_texture_size(vlcr_handle texture, int *width, int *height) {
    vlcr_slot *slot = slot_at(g_textures, VLCR_MAX_TEXTURES, texture);
    if (width)  *width  = slot ? slot->value.texture.width  : 0;
    if (height) *height = slot ? slot->value.texture.height : 0;
}

void vlcr_texture_unload(vlcr_handle texture) {
    vlcr_slot *slot = slot_at(g_textures, VLCR_MAX_TEXTURES, texture);
    if (!slot) return;

    UnloadTexture(slot->value.texture);
    slot_remove(g_textures, VLCR_MAX_TEXTURES, texture);
}

void vlcr_draw_texture(vlcr_handle texture, int x, int y, uint32_t color) {
    vlcr_slot *slot = slot_at(g_textures, VLCR_MAX_TEXTURES, texture);
    if (g_ready && slot) DrawTexture(slot->value.texture, x, y, color_from_u32(color));
}

void vlcr_draw_texture_ex(vlcr_handle texture, float x, float y,
                          float rotation, float scale, uint32_t color) {
    vlcr_slot *slot = slot_at(g_textures, VLCR_MAX_TEXTURES, texture);
    if (g_ready && slot) {
        DrawTextureEx(slot->value.texture, vec2(x, y), rotation, scale, color_from_u32(color));
    }
}

void vlcr_draw_texture_region(vlcr_handle texture,
                              float source_x, float source_y,
                              float source_width, float source_height,
                              float x, float y, uint32_t color) {
    vlcr_slot *slot = slot_at(g_textures, VLCR_MAX_TEXTURES, texture);
    Rectangle source;

    if (!g_ready || !slot) return;

    source.x      = source_x;
    source.y      = source_y;
    source.width  = source_width;
    source.height = source_height;
    DrawTextureRec(slot->value.texture, source, vec2(x, y), color_from_u32(color));
}

/* ------------------------------------------------------------------------- */
/* Audio                                                                      */
/* ------------------------------------------------------------------------- */

#if VLCR_FFI_HAS_AUDIO

int vlcr_audio_init(void) {
    if (!g_ready) return 0;
    if (g_audio_ready) return 1;
    g_audio_ready = InitAudioDevice() ? 1 : 0;
    return g_audio_ready;
}

void vlcr_audio_close(void) {
    if (g_audio_ready) CloseAudioDevice();
    g_audio_ready = 0;
}

int vlcr_audio_available(void) {
    return g_audio_ready;
}

vlcr_handle vlcr_sound_load(const char *path) {
    vlcr_resource value;
    Sound sound;

    if (!path) return 0;
    sound = LoadSound(path);
    if (sound.id == 0) return 0;

    memset(&value, 0, sizeof(value));
    value.sound = sound;
    return slot_add(g_sounds, VLCR_MAX_SOUNDS, &value);
}

void vlcr_sound_unload(vlcr_handle sound) {
    vlcr_slot *slot = slot_at(g_sounds, VLCR_MAX_SOUNDS, sound);
    if (!slot) return;

    UnloadSound(slot->value.sound);
    slot_remove(g_sounds, VLCR_MAX_SOUNDS, sound);
}

void vlcr_sound_play(vlcr_handle sound) {
    vlcr_slot *slot = slot_at(g_sounds, VLCR_MAX_SOUNDS, sound);
    if (slot) PlaySound(slot->value.sound);
}

void vlcr_sound_stop(vlcr_handle sound) {
    vlcr_slot *slot = slot_at(g_sounds, VLCR_MAX_SOUNDS, sound);
    if (slot) StopSound(slot->value.sound);
}

void vlcr_sound_set_volume(vlcr_handle sound, float volume) {
    vlcr_slot *slot = slot_at(g_sounds, VLCR_MAX_SOUNDS, sound);
    if (slot) SetSoundVolume(slot->value.sound, volume);
}

int vlcr_sound_playing(vlcr_handle sound) {
    vlcr_slot *slot = slot_at(g_sounds, VLCR_MAX_SOUNDS, sound);
    return slot ? (IsSoundPlaying(slot->value.sound) ? 1 : 0) : 0;
}

vlcr_handle vlcr_music_load(const char *path) {
    vlcr_resource value;
    Music music;

    if (!path) return 0;
    music = LoadMusicStream(path);
    if (music.id == 0) return 0;

    memset(&value, 0, sizeof(value));
    value.music = music;
    return slot_add(g_music, VLCR_MAX_MUSIC, &value);
}

void vlcr_music_unload(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    if (!slot) return;

    UnloadMusicStream(slot->value.music);
    slot_remove(g_music, VLCR_MAX_MUSIC, music);
}

void vlcr_music_play(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    if (slot) PlayMusicStream(slot->value.music);
}

void vlcr_music_stop(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    if (slot) StopMusicStream(slot->value.music);
}

void vlcr_music_pause(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    if (slot) PauseMusicStream(slot->value.music);
}

void vlcr_music_resume(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    if (slot) ResumeMusicStream(slot->value.music);
}

void vlcr_music_update(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    if (slot) UpdateMusicStream(slot->value.music);
}

void vlcr_music_set_volume(vlcr_handle music, float volume) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    if (slot) SetMusicVolume(slot->value.music, volume);
}

int vlcr_music_playing(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    return slot ? (IsMusicStreamPlaying(slot->value.music) ? 1 : 0) : 0;
}

float vlcr_music_length(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    return slot ? GetMusicTimeLength(slot->value.music) : 0.0f;
}

float vlcr_music_played(vlcr_handle music) {
    vlcr_slot *slot = slot_at(g_music, VLCR_MAX_MUSIC, music);
    return slot ? GetMusicTimePlayed(slot->value.music) : 0.0f;
}

#else /* !VLCR_FFI_HAS_AUDIO — GPU-free build without an audio backend */

int  vlcr_audio_init(void)  { return 0; }
void vlcr_audio_close(void) { }
int  vlcr_audio_available(void) { return 0; }

vlcr_handle vlcr_sound_load(const char *path) { (void)path; return 0; }
void vlcr_sound_unload(vlcr_handle sound) { (void)sound; }
void vlcr_sound_play(vlcr_handle sound) { (void)sound; }
void vlcr_sound_stop(vlcr_handle sound) { (void)sound; }
void vlcr_sound_set_volume(vlcr_handle sound, float volume) { (void)sound; (void)volume; }
int  vlcr_sound_playing(vlcr_handle sound) { (void)sound; return 0; }

vlcr_handle vlcr_music_load(const char *path) { (void)path; return 0; }
void  vlcr_music_unload(vlcr_handle music) { (void)music; }
void  vlcr_music_play(vlcr_handle music) { (void)music; }
void  vlcr_music_stop(vlcr_handle music) { (void)music; }
void  vlcr_music_pause(vlcr_handle music) { (void)music; }
void  vlcr_music_resume(vlcr_handle music) { (void)music; }
void  vlcr_music_update(vlcr_handle music) { (void)music; }
void  vlcr_music_set_volume(vlcr_handle music, float volume) { (void)music; (void)volume; }
int   vlcr_music_playing(vlcr_handle music) { (void)music; return 0; }
float vlcr_music_length(vlcr_handle music) { (void)music; return 0.0f; }
float vlcr_music_played(vlcr_handle music) { (void)music; return 0.0f; }

#endif /* VLCR_FFI_HAS_AUDIO */

/* ------------------------------------------------------------------------- */
/* Files                                                                      */
/* ------------------------------------------------------------------------- */

int vlcr_file_exists(const char *path) {
    return (path && FileExists(path)) ? 1 : 0;
}

int vlcr_directory_exists(const char *path) {
    return (path && DirectoryExists(path)) ? 1 : 0;
}

unsigned int vlcr_file_size(const char *path) {
    return path ? GetFileSizeBytes(path) : 0u;
}
