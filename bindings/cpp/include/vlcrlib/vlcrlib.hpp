// =============================================================================
//  vlcrlib.hpp — modern C++ wrapper for vlcrlib
// =============================================================================
//
// Header-only. Include it and link `vlcrlib` (the static library) exactly as a
// C program would; there is no extra runtime, no dynamic loading and no
// marshalling. Everything here is a thin inline wrapper over the C API:
//
//   #include <vlcrlib/vlcrlib.hpp>
//
//   int main() {
//       vlcr::Window window(1280, 720, "Hello vlcrlib");
//       vlcr::Font  font = vlcr::Font::load("C:/Windows/Fonts/segoeui.ttf", 32);
//
//       while (!window.should_close()) {
//           vlcr::Frame frame;                       // BeginDrawing / EndDrawing
//           vlcr::clear(vlcr::colors::raywhite);
//           vlcr::draw_rectangle(40, 40, 200, 120, vlcr::colors::orange);
//           font.draw("Hello from C++", 40, 200, 30);
//       }
//   }
//
// Names are snake_case to match the C API; RAII types (Window, Frame, Font,
// Texture, Sound, Music) own and release their resource.
//
// Requires C++17.

#ifndef VLCRLIB_CPP_HPP
#define VLCRLIB_CPP_HPP

#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace vlcr {

// =============================================================================
//  Value types
// =============================================================================

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2(Vector2 v) : x(v.x), y(v.y) {}

    constexpr operator Vector2() const { return Vector2{ x, y }; }

    constexpr Vec2 operator+(Vec2 o) const { return { x + o.x, y + o.y }; }
    constexpr Vec2 operator-(Vec2 o) const { return { x - o.x, y - o.y }; }
    constexpr Vec2 operator*(float s) const { return { x * s, y * s }; }

    float length() const { return std::sqrt(x * x + y * y); }

    Vec2 normalized() const {
        const float len = length();
        return len > 1e-8f ? Vec2{ x / len, y / len } : Vec2{};
    }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3(Vector3 v) : x(v.x), y(v.y), z(v.z) {}

    constexpr operator Vector3() const { return Vector3{ x, y, z }; }

    constexpr Vec3 operator+(Vec3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    constexpr Vec3 operator-(Vec3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    constexpr Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

    float dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }

    constexpr Vec3 cross(Vec3 o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }

    float length() const { return std::sqrt(x * x + y * y + z * z); }

    Vec3 normalized() const {
        const float len = length();
        return len > 1e-8f ? Vec3{ x / len, y / len, z / len } : Vec3{};
    }
};

struct Color {
    unsigned char r = 255, g = 255, b = 255, a = 255;

    constexpr Color() = default;
    constexpr Color(unsigned char r_, unsigned char g_, unsigned char b_,
                    unsigned char a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    constexpr operator ::Color() const { return ::Color{ r, g, b, a }; }
};

struct Rect {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w, float h)
        : x(x_), y(y_), width(w), height(h) {}
    constexpr Rect(Rectangle r) : x(r.x), y(r.y), width(r.width), height(r.height) {}

    constexpr operator Rectangle() const { return Rectangle{ x, y, width, height }; }
};

struct Camera2D {
    Vec2 offset{};
    Vec2 target{};
    float rotation = 0.0f;
    float zoom = 1.0f;
};

struct Camera3D {
    Vec3 position{ 0.0f, 5.0f, 10.0f };
    Vec3 target{};
    Vec3 up{ 0.0f, 1.0f, 0.0f };
    float fovy = 45.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;

    constexpr operator ::Camera3D() const {
        return ::Camera3D{ position, target, up, fovy, near_plane, far_plane };
    }
};

namespace colors {
inline constexpr Color lightgray{ 200, 200, 200 };
inline constexpr Color gray{ 130, 130, 130 };
inline constexpr Color darkgray{ 80, 80, 80 };
inline constexpr Color yellow{ 253, 249, 0 };
inline constexpr Color gold{ 255, 203, 0 };
inline constexpr Color orange{ 255, 161, 0 };
inline constexpr Color pink{ 255, 109, 194 };
inline constexpr Color red{ 230, 41, 55 };
inline constexpr Color maroon{ 190, 33, 55 };
inline constexpr Color green{ 0, 228, 48 };
inline constexpr Color lime{ 0, 158, 47 };
inline constexpr Color darkgreen{ 0, 117, 44 };
inline constexpr Color skyblue{ 102, 191, 255 };
inline constexpr Color blue{ 0, 121, 241 };
inline constexpr Color darkblue{ 0, 82, 172 };
inline constexpr Color purple{ 200, 122, 255 };
inline constexpr Color violet{ 135, 60, 190 };
inline constexpr Color darkpurple{ 112, 31, 126 };
inline constexpr Color beige{ 211, 176, 131 };
inline constexpr Color brown{ 127, 106, 79 };
inline constexpr Color darkbrown{ 76, 63, 47 };
inline constexpr Color white{ 255, 255, 255 };
inline constexpr Color black{ 0, 0, 0 };
inline constexpr Color blank{ 0, 0, 0, 0 };
inline constexpr Color magenta{ 255, 0, 255 };
inline constexpr Color raywhite{ 245, 245, 245 };
} // namespace colors

// =============================================================================
//  Window and frame
// =============================================================================

class Window {
public:
    Window(int width, int height, const char* title = "vlcrlib") {
        ok_ = InitWindow(width, height, title);
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept : ok_(std::exchange(other.ok_, false)) {}

    ~Window() { close(); }

    bool ok() const { return ok_; }
    bool should_close() const { return ok_ && WindowShouldClose(); }

    void close() {
        if (ok_) {
            CloseWindow();
            ok_ = false;
        }
    }

    void title(const char* value) { SetWindowTitle(value); }
    void size(int width, int height) { SetWindowSize(width, height); }
    void position(int x, int y) { SetWindowPosition(x, y); }
    void toggle_fullscreen() { ToggleFullscreen(); }
    void exit_key(int key) { SetExitKey(key); }
    void target_fps(int fps) { SetTargetFPS(fps); }

    Vec2 size() const {
        int w = 0, h = 0;
        vcore_GetScreenSize(&w, &h);
        return Vec2{ static_cast<float>(w), static_cast<float>(h) };
    }

    int fps() const { return GetFPS(); }
    float frame_time() const { return GetFrameTime(); }
    double time() const { return GetTime(); }
    void* native_handle() const { return GetWindowHandle(); }

private:
    bool ok_ = false;
};

/// BeginDrawing()/EndDrawing() as an RAII scope.
class Frame {
public:
    Frame() { BeginDrawing(); }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    ~Frame() { EndDrawing(); }
};

inline bool is_ready() { return GetWindowHandle() != nullptr; }
inline void clear(Color color) { ClearBackground(color); }
inline void begin_mode_2d(Camera2D camera) {
    BeginMode2D(::Camera2D{ camera.offset, camera.target, camera.rotation, camera.zoom });
}
inline void end_mode_2d() { EndMode2D(); }
inline void begin_mode_3d(Camera3D camera) { BeginMode3D(camera); }
inline void end_mode_3d() { EndMode3D(); }

// =============================================================================
//  Drawing
// =============================================================================

inline void draw_pixel(int x, int y, Color color) { DrawPixel(x, y, color); }
inline void draw_line(int x0, int y0, int x1, int y1, Color color) {
    DrawLine(x0, y0, x1, y1, color);
}
inline void draw_line(Vec2 start, Vec2 end, float thickness, Color color) {
    DrawLineEx(start, end, thickness, color);
}
inline void draw_circle(float center_x, float center_y, float radius, Color color) {
    DrawCircle(static_cast<int>(center_x), static_cast<int>(center_y), radius, color);
}
inline void draw_circle_lines(float center_x, float center_y, float radius, Color color) {
    DrawCircleLines(static_cast<int>(center_x), static_cast<int>(center_y), radius, color);
}
inline void draw_rectangle(float x, float y, float width, float height, Color color) {
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width),
                  static_cast<int>(height), color);
}
inline void draw_rectangle(Rect rect, Color color) {
    DrawRectangleRec(rect, color);
}
inline void draw_rectangle_lines(float x, float y, float width, float height, Color color) {
    DrawRectangleLines(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width),
                       static_cast<int>(height), color);
}
inline void draw_rectangle_pro(Rect rect, Vec2 origin, float rotation, Color color) {
    DrawRectanglePro(rect, origin, rotation, color);
}
inline void draw_triangle(Vec2 a, Vec2 b, Vec2 c, Color color) {
    DrawTriangle(a, b, c, color);
}
inline void draw_poly(Vec2 center, int sides, float radius, float rotation, Color color) {
    DrawPoly(center, sides, radius, rotation, color);
}
inline void draw_line_3d(Vec3 start, Vec3 end, Color color) { DrawLine3D(start, end, color); }
inline void draw_cube(Vec3 position, Vec3 size, Color color) {
    DrawCube(position, size, color);
}
inline void draw_sphere(Vec3 center, float radius, Color color) {
    DrawSphere(center, radius, color);
}
inline void draw_grid(int slices = 10, float spacing = 1.0f) { DrawGrid(slices, spacing); }

// =============================================================================
//  Text
// =============================================================================

class Font {
public:
    Font() = default;

    /// Load a .ttf/.otf baked at `size` pixels (ASCII 32..127).
    static Font load(const char* path, int size = 32) {
        return Font{ LoadFontEx(path, size, 32, 96) };
    }

    /// The engine's built-in font (lazily baked on first use).
    static Font default_font() {
        vtext_EnsureDefaultFont();
        return Font{ GetFontDefault() };
    }

    explicit operator bool() const { return font_.ok != 0; }
    const ::Font& raw() const { return font_; }

    Vec2 measure(const char* text, float size = 20.0f, float spacing = 1.0f) const {
        const Vector2 v = MeasureTextEx(font_, text, size, spacing);
        return Vec2{ v.x, v.y };
    }

    void draw(const char* text, float x, float y, float size = 20.0f,
              float spacing = 1.0f, Color tint = colors::white) const {
        DrawTextEx(font_, text, Vector2{ x, y }, size, spacing, tint);
    }

    void unload() {
        if (font_.ok) {
            UnloadFont(font_);
            font_ = ::Font{};
        }
    }

    ~Font() { unload(); }

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&& other) noexcept : font_(std::exchange(other.font_, ::Font{})) {}
    Font& operator=(Font&& other) noexcept {
        if (this != &other) {
            unload();
            font_ = std::exchange(other.font_, ::Font{});
        }
        return *this;
    }

private:
    explicit Font(::Font font) : font_(font) {}
    ::Font font_{};
};

inline void draw_text(const char* text, int x, int y, int size = 20,
                      Color color = colors::white) {
    vtext_EnsureDefaultFont();
    DrawText(text, x, y, size, color);
}

inline int measure_text(const char* text, int size = 20) {
    vtext_EnsureDefaultFont();
    return MeasureText(text, size);
}

// =============================================================================
//  Textures
// =============================================================================

class Texture {
public:
    Texture() = default;

    static Texture load(const char* path) { return Texture{ LoadTexture(path) }; }

    explicit operator bool() const { return texture_.id != 0; }
    const ::Texture2D& raw() const { return texture_; }
    int width() const { return texture_.width; }
    int height() const { return texture_.height; }

    void draw(float x, float y, Color tint = colors::white) const {
        DrawTexture(texture_, static_cast<int>(x), static_cast<int>(y), tint);
    }

    void draw(Vec2 position, float rotation = 0.0f, float scale = 1.0f,
              Color tint = colors::white) const {
        DrawTextureEx(texture_, position, rotation, scale, tint);
    }

    void draw_region(Rect source, Vec2 position, Color tint = colors::white) const {
        DrawTextureRec(texture_, source, position, tint);
    }

    void unload() {
        if (texture_.id) {
            UnloadTexture(texture_);
            texture_ = ::Texture2D{};
        }
    }

    ~Texture() { unload(); }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept : texture_(std::exchange(other.texture_, ::Texture2D{})) {}

private:
    explicit Texture(::Texture2D texture) : texture_(texture) {}
    ::Texture2D texture_{};
};

// =============================================================================
//  Input
// =============================================================================

namespace mouse {
inline constexpr int left = MOUSE_BUTTON_LEFT;
inline constexpr int middle = MOUSE_BUTTON_MIDDLE;
inline constexpr int right = MOUSE_BUTTON_RIGHT;
} // namespace mouse

/// Keyboard scancodes (SDL3 values, a convenient subset).
namespace key {
inline constexpr int escape = SDL_SCANCODE_ESCAPE;
inline constexpr int enter = SDL_SCANCODE_RETURN;
inline constexpr int tab = SDL_SCANCODE_TAB;
inline constexpr int space = SDL_SCANCODE_SPACE;
inline constexpr int backspace = SDL_SCANCODE_BACKSPACE;
inline constexpr int left = SDL_SCANCODE_LEFT;
inline constexpr int right = SDL_SCANCODE_RIGHT;
inline constexpr int up = SDL_SCANCODE_UP;
inline constexpr int down = SDL_SCANCODE_DOWN;
inline constexpr int left_shift = SDL_SCANCODE_LSHIFT;
inline constexpr int left_ctrl = SDL_SCANCODE_LCTRL;
inline constexpr int left_alt = SDL_SCANCODE_LALT;
inline constexpr int a = SDL_SCANCODE_A;
inline constexpr int d = SDL_SCANCODE_D;
inline constexpr int s = SDL_SCANCODE_S;
inline constexpr int w = SDL_SCANCODE_W;
inline constexpr int f1 = SDL_SCANCODE_F1;
inline constexpr int f2 = SDL_SCANCODE_F2;
inline constexpr int f3 = SDL_SCANCODE_F3;
inline constexpr int f4 = SDL_SCANCODE_F4;
} // namespace key

inline bool key_down(int key) { return IsKeyDown(key); }
inline bool key_pressed(int key) { return IsKeyPressed(key); }
inline bool key_released(int key) { return IsKeyReleased(key); }
inline bool mouse_down(int button = mouse::left) { return IsMouseButtonDown(button); }
inline bool mouse_pressed(int button = mouse::left) { return IsMouseButtonPressed(button); }
inline bool mouse_released(int button = mouse::left) { return IsMouseButtonReleased(button); }
inline Vec2 mouse_position() {
    const Vector2 v = GetMousePosition();
    return Vec2{ v.x, v.y };
}
inline float mouse_wheel() { return GetMouseWheelMove(); }
inline bool gamepad_available(int index = 0) { return IsGamepadAvailable(index); }
inline bool gamepad_down(int index, int button) { return IsGamepadButtonDown(index, button); }
inline float gamepad_axis(int index, int axis) { return GetGamepadAxisMovement(index, axis); }

// =============================================================================
//  Audio
// =============================================================================

namespace audio {

inline bool init() { return InitAudioDevice(); }
inline void close() { CloseAudioDevice(); }

class Sound {
public:
    Sound() = default;
    static Sound load(const char* path) { return Sound{ LoadSound(path) }; }

    explicit operator bool() const { return sound_.player != nullptr; }
    bool playing() const { return IsSoundPlaying(sound_); }
    void play() const { PlaySound(sound_); }
    void stop() const { StopSound(sound_); }
    void volume(float value) const { SetSoundVolume(sound_, value); }

    void unload() {
        if (sound_.player) {
            UnloadSound(sound_);
            sound_ = ::Sound{};
        }
    }

    ~Sound() { unload(); }

    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;

private:
    explicit Sound(::Sound sound) : sound_(sound) {}
    ::Sound sound_{};
};

class Music {
public:
    Music() = default;
    static Music load(const char* path) { return Music{ LoadMusicStream(path) }; }

    explicit operator bool() const { return music_.player != nullptr; }
    bool playing() const { return IsMusicStreamPlaying(music_); }
    float length() const { return GetMusicTimeLength(music_); }
    float played() const { return GetMusicTimePlayed(music_); }
    void play() const { PlayMusicStream(music_); }
    void stop() const { StopMusicStream(music_); }
    void pause() const { PauseMusicStream(music_); }
    void resume() const { ResumeMusicStream(music_); }
    void update() const { UpdateMusicStream(music_); }
    void volume(float value) const { SetMusicVolume(music_, value); }

    void unload() {
        if (music_.player) {
            UnloadMusicStream(music_);
            music_ = ::Music{};
        }
    }

    ~Music() { unload(); }

    Music(const Music&) = delete;
    Music& operator=(const Music&) = delete;

private:
    explicit Music(::Music music) : music_(music) {}
    ::Music music_{};
};

} // namespace audio

// =============================================================================
//  Files
// =============================================================================

inline bool file_exists(const char* path) { return FileExists(path); }
inline bool directory_exists(const char* path) { return DirectoryExists(path); }
inline unsigned int file_size(const char* path) { return GetFileSizeBytes(path); }

} // namespace vlcr

#endif // VLCRLIB_CPP_HPP
