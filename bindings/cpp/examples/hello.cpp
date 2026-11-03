// C++ binding example: a 3D scene, a 2D overlay and the frame loop.
//
// Run it from a directory that contains the compiled `shaders/` folder (the
// CMake build tree copies them next to the library). Set VLCR_FRAMES=120 to
// exit after 120 frames, which is what scripts/build_bindings.ps1 does when it
// verifies the bindings.

#include <vlcrlib/vlcrlib.hpp>

#include <cstdio>
#include <cstdlib>

int main() {
    vlcr::Window window(960, 540, "vlcrlib — C++ binding");
    if (!window.ok()) {
        std::fprintf(stderr, "vlcrlib: failed to create the window\n");
        return 1;
    }
    window.target_fps(60);

    vlcr::Font font = vlcr::Font::default_font();
    if (!font) {
        std::fprintf(stderr, "vlcrlib: no default font\n");
        return 1;
    }

    const int frame_limit = [] {
        const char* value = std::getenv("VLCR_FRAMES");
        return value ? std::atoi(value) : 0;
    }();

    vlcr::Camera3D camera;
    camera.position = { 6.0f, 4.0f, 8.0f };
    camera.target = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 55.0f;
    camera.far_plane = 200.0f;

    float angle = 0.0f;
    int frame = 0;

    std::printf("vlcrlib C++ binding\n"); std::fflush(stdout);

    while (!window.should_close()) {
        angle += 40.0f * window.frame_time();

        {
            vlcr::Frame drawing;
            vlcr::clear(vlcr::colors::raywhite);

            vlcr::begin_mode_3d(camera);
            vlcr::draw_grid(20, 1.0f);
            vlcr::draw_cube({ -2.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, vlcr::colors::red);
            vlcr::draw_sphere({ 1.5f, 1.0f, 0.0f }, 1.0f, vlcr::colors::skyblue);
            vlcr::draw_line_3d({ 0.0f, 0.05f, -6.0f }, { 0.0f, 0.05f, 6.0f },
                               vlcr::colors::darkgray);
            vlcr::end_mode_3d();

            vlcr::draw_rectangle_pro({ 80.0f, 80.0f, 120.0f, 60.0f }, { 60.0f, 30.0f },
                                     angle, vlcr::colors::orange);
            vlcr::draw_rectangle(20.0f, 20.0f, 260.0f, 54.0f, vlcr::Color{ 20, 20, 30, 190 });
            font.draw("vlcrlib + C++", 34.0f, 34.0f, 24.0f, 1.0f, vlcr::colors::white);

            char status[96];
            std::snprintf(status, sizeof(status), "frame %d  %d fps  %.1f ms", frame,
                          window.fps(), window.frame_time() * 1000.0f);
            vlcr::draw_text(status, 34, 500, 18, vlcr::colors::darkgray);

            const vlcr::Vec2 cursor = vlcr::mouse_position();
            vlcr::draw_circle(cursor.x, cursor.y, 8.0f, vlcr::colors::green);
        }

        if (vlcr::key_down(vlcr::key::escape)) break;

        ++frame;
        if (frame_limit > 0 && frame >= frame_limit) break;
    }

    std::printf("vlcrlib C++ binding finished after %d frames\n", frame); std::fflush(stdout);
    return 0;
}
