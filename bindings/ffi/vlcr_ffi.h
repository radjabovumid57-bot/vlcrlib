/*
 * vlcr_ffi.h — stable C ABI for vlcrlib language bindings
 * ---------------------------------------------------------------------------
 * vlcrlib's public headers are C, but they lean on structs passed by value,
 * `static inline` math helpers and, on Windows, a static library that exports
 * nothing. That is fine from C and C++, and awkward from anything that talks
 * to the library through a foreign-function interface (Python ctypes, C#
 * P/Invoke, Java JNI, ...).
 *
 * This header is the contract those bindings use:
 *
 *   - every function lives behind `vlcr_` and is exported from a shared
 *     library (vlcr_ffi.dll / libvlcr_ffi.so / libvlcr_ffi.dylib);
 *   - only scalars cross the boundary: int, float, double, UTF-8 strings,
 *     opaque handles and out-parameters. No structs by value, no callbacks.
 *   - colors are packed 32-bit values, see VLCR_RGBA();
 *   - resources (fonts, textures, sounds, music) are referred to by small
 *     integer handles, 1..N; 0 means "invalid". They are released with the
 *     matching *_unload() call (or by vlcr_shutdown()).
 *
 * Threading: the library is not thread-safe. Call these functions from the
 * thread that owns the window (normally the one that called vlcr_init()).
 *
 * The header is self-contained: it does not include any vlcrlib header, so
 * bindings can vendor it alone.
 */
#ifndef VLCR_FFI_H
#define VLCR_FFI_H

#include <stdint.h>

#if defined(_WIN32) && defined(VLCR_FFI_BUILD_SHARED)
#  define VLCR_FFI_API __declspec(dllexport)
#elif defined(_WIN32) && defined(VLCR_FFI_USE_SHARED)
#  define VLCR_FFI_API __declspec(dllimport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define VLCR_FFI_API __attribute__((visibility("default")))
#else
#  define VLCR_FFI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Pack a color for every function that takes one:
 *   VLCR_RGBA(255, 128, 0, 255)  ->  opaque orange
 * Component order is R in the low byte, A in the high byte. */
#define VLCR_RGBA(r, g, b, a) \
    ((uint32_t)(((uint32_t)(r) & 0xFFu)        | (((uint32_t)(g) & 0xFFu) << 8) | \
                (((uint32_t)(b) & 0xFFu) << 16) | (((uint32_t)(a) & 0xFFu) << 24)))

/* Integer handles returned by this API. 0 is never a valid handle. */
typedef int32_t vlcr_handle;

/* Mouse button ids, matching the engine. */
#define VLCR_MOUSE_LEFT   1
#define VLCR_MOUSE_MIDDLE 2
#define VLCR_MOUSE_RIGHT  3

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

/* Version of the binding ABI (not of the engine). */
VLCR_FFI_API const char *vlcr_version(void);

/* Create the window and initialise the renderer. Returns 1 on success. */
VLCR_FFI_API int vlcr_init(int width, int height, const char *title);

/* Unload every resource still registered, then close the window. */
VLCR_FFI_API void vlcr_shutdown(void);

/* 1 once vlcr_init() succeeded and until vlcr_shutdown(). */
VLCR_FFI_API int vlcr_is_ready(void);

/* 1 when the user asked to close the window (or SetExitKey was pressed). */
VLCR_FFI_API int vlcr_should_close(void);

/* ------------------------------------------------------------------------- */
/* Frame                                                                      */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API void   vlcr_begin_drawing(void);
VLCR_FFI_API void   vlcr_end_drawing(void);
VLCR_FFI_API void   vlcr_clear(uint32_t color);
VLCR_FFI_API void   vlcr_set_target_fps(int fps);
VLCR_FFI_API int    vlcr_fps(void);
VLCR_FFI_API float  vlcr_frame_time(void);
VLCR_FFI_API double vlcr_time(void);

/* ------------------------------------------------------------------------- */
/* Window                                                                     */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API void  vlcr_window_size(int *width, int *height);
VLCR_FFI_API void  vlcr_set_window_title(const char *title);
VLCR_FFI_API void  vlcr_set_window_size(int width, int height);
VLCR_FFI_API void  vlcr_set_window_position(int x, int y);
VLCR_FFI_API void  vlcr_toggle_fullscreen(void);
VLCR_FFI_API void  vlcr_set_exit_key(int key);
/* Native window handle (HWND on Windows, Window/X11 on Linux, ...). */
VLCR_FFI_API void *vlcr_native_window(void);

/* ------------------------------------------------------------------------- */
/* Input                                                                      */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API int   vlcr_key_down(int key);
VLCR_FFI_API int   vlcr_key_pressed(int key);
VLCR_FFI_API int   vlcr_key_released(int key);
VLCR_FFI_API int   vlcr_mouse_down(int button);
VLCR_FFI_API int   vlcr_mouse_pressed(int button);
VLCR_FFI_API int   vlcr_mouse_released(int button);
VLCR_FFI_API void  vlcr_mouse_position(float *x, float *y);
VLCR_FFI_API float vlcr_mouse_wheel(void);

VLCR_FFI_API int   vlcr_gamepad_available(int index);
VLCR_FFI_API int   vlcr_gamepad_down(int index, int button);
VLCR_FFI_API float vlcr_gamepad_axis(int index, int axis);

/* ------------------------------------------------------------------------- */
/* Camera modes                                                               */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API void vlcr_begin_mode_2d(float offset_x, float offset_y,
                                    float target_x, float target_y,
                                    float rotation, float zoom);
VLCR_FFI_API void vlcr_end_mode_2d(void);

VLCR_FFI_API void vlcr_begin_mode_3d(float position_x, float position_y, float position_z,
                                    float target_x, float target_y, float target_z,
                                    float up_x, float up_y, float up_z,
                                    float fovy, float near_plane, float far_plane);
VLCR_FFI_API void vlcr_end_mode_3d(void);

/* ------------------------------------------------------------------------- */
/* 2D shapes                                                                  */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API void vlcr_draw_pixel(int x, int y, uint32_t color);
VLCR_FFI_API void vlcr_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
VLCR_FFI_API void vlcr_draw_line_ex(float x0, float y0, float x1, float y1,
                                    float thickness, uint32_t color);
VLCR_FFI_API void vlcr_draw_circle(int center_x, int center_y, float radius, uint32_t color);
VLCR_FFI_API void vlcr_draw_circle_lines(int center_x, int center_y, float radius, uint32_t color);
VLCR_FFI_API void vlcr_draw_rectangle(int x, int y, int width, int height, uint32_t color);
VLCR_FFI_API void vlcr_draw_rectangle_lines(int x, int y, int width, int height, uint32_t color);
VLCR_FFI_API void vlcr_draw_rectangle_pro(float x, float y, float width, float height,
                                          float origin_x, float origin_y, float rotation,
                                          uint32_t color);
VLCR_FFI_API void vlcr_draw_triangle(float x1, float y1, float x2, float y2,
                                     float x3, float y3, uint32_t color);
VLCR_FFI_API void vlcr_draw_poly(float center_x, float center_y, int sides, float radius,
                                 float rotation, uint32_t color);

/* ------------------------------------------------------------------------- */
/* 3D primitives                                                              */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API void vlcr_draw_cube(float x, float y, float z,
                                 float width, float height, float depth,
                                 uint32_t color);
VLCR_FFI_API void vlcr_draw_sphere(float x, float y, float z, float radius, uint32_t color);
VLCR_FFI_API void vlcr_draw_line_3d(float x0, float y0, float z0,
                                    float x1, float y1, float z1, uint32_t color);
VLCR_FFI_API void vlcr_draw_grid(int slices, float spacing);

/* ------------------------------------------------------------------------- */
/* Text                                                                       */
/* ------------------------------------------------------------------------- */

/* Handle for the built-in font, or 0 when no default font is available. */
VLCR_FFI_API vlcr_handle vlcr_font_default(void);
/* Load a .ttf/.otf at `size` pixels. Returns 0 on failure. */
VLCR_FFI_API vlcr_handle vlcr_font_load(const char *path, int size);
VLCR_FFI_API void        vlcr_font_unload(vlcr_handle font);

/* Draw with the built-in font. */
VLCR_FFI_API void vlcr_draw_text(const char *text, int x, int y, int font_size, uint32_t color);
/* Draw with a font handle (pass 0 / vlcr_font_default() for the default font). */
VLCR_FFI_API void vlcr_draw_text_ex(vlcr_handle font, const char *text, float x, float y,
                                    float font_size, float spacing, uint32_t color);

VLCR_FFI_API int  vlcr_measure_text(const char *text, int font_size);
VLCR_FFI_API void vlcr_measure_text_ex(vlcr_handle font, const char *text,
                                       float font_size, float spacing,
                                       float *width, float *height);

/* ------------------------------------------------------------------------- */
/* Textures                                                                   */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API vlcr_handle vlcr_texture_load(const char *path);
VLCR_FFI_API void        vlcr_texture_size(vlcr_handle texture, int *width, int *height);
VLCR_FFI_API void        vlcr_texture_unload(vlcr_handle texture);

VLCR_FFI_API void vlcr_draw_texture(vlcr_handle texture, int x, int y, uint32_t color);
VLCR_FFI_API void vlcr_draw_texture_ex(vlcr_handle texture, float x, float y,
                                       float rotation, float scale, uint32_t color);
VLCR_FFI_API void vlcr_draw_texture_region(vlcr_handle texture,
                                           float source_x, float source_y,
                                           float source_width, float source_height,
                                           float x, float y, uint32_t color);

/* ------------------------------------------------------------------------- */
/* Audio                                                                      */
/* ------------------------------------------------------------------------- */

/* Open the audio device. Returns 1 on success; the API is a no-op otherwise. */
VLCR_FFI_API int  vlcr_audio_init(void);
VLCR_FFI_API void vlcr_audio_close(void);
VLCR_FFI_API int  vlcr_audio_available(void);

VLCR_FFI_API vlcr_handle vlcr_sound_load(const char *path);
VLCR_FFI_API void        vlcr_sound_unload(vlcr_handle sound);
VLCR_FFI_API void        vlcr_sound_play(vlcr_handle sound);
VLCR_FFI_API void        vlcr_sound_stop(vlcr_handle sound);
VLCR_FFI_API void        vlcr_sound_set_volume(vlcr_handle sound, float volume);
VLCR_FFI_API int         vlcr_sound_playing(vlcr_handle sound);

VLCR_FFI_API vlcr_handle vlcr_music_load(const char *path);
VLCR_FFI_API void        vlcr_music_unload(vlcr_handle music);
VLCR_FFI_API void        vlcr_music_play(vlcr_handle music);
VLCR_FFI_API void        vlcr_music_stop(vlcr_handle music);
VLCR_FFI_API void        vlcr_music_pause(vlcr_handle music);
VLCR_FFI_API void        vlcr_music_resume(vlcr_handle music);
VLCR_FFI_API void        vlcr_music_update(vlcr_handle music);
VLCR_FFI_API void        vlcr_music_set_volume(vlcr_handle music, float volume);
VLCR_FFI_API int         vlcr_music_playing(vlcr_handle music);
VLCR_FFI_API float       vlcr_music_length(vlcr_handle music);
VLCR_FFI_API float       vlcr_music_played(vlcr_handle music);

/* ------------------------------------------------------------------------- */
/* Files                                                                      */
/* ------------------------------------------------------------------------- */

VLCR_FFI_API int          vlcr_file_exists(const char *path);
VLCR_FFI_API int          vlcr_directory_exists(const char *path);
VLCR_FFI_API unsigned int vlcr_file_size(const char *path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VLCR_FFI_H */
