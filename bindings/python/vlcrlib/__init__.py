"""Python bindings for vlcrlib.

vlcrlib is a C game/graphics library (SDL3 + Vulkan/WebGPU, with a CPU-only
backend). This package talks to it through the stable `vlcr_ffi` C ABI, so the
same native library also serves the C#, Java and C++ bindings.

Quick start::

    import vlcrlib as vl

    with vl.Window(1280, 720, "Hello vlcrlib", target_fps=60) as win:
        angle = 0.0
        while win.running:
            angle += 60.0 * win.frame_time()
            with win.frame():
                vl.clear(vl.RAYWHITE)
                vl.draw_rectangle(40, 40, 200, 120, vl.ORANGE)
                vl.draw_circle(400, 300, 80, vl.SKYBLUE)
                vl.draw_text("Hello from Python", 40, 200, 30, vl.DARKGRAY)

The native library is found automatically in a source checkout; see
``vlcrlib._ffi`` for the search order and the ``VLCRLIB_LIBRARY`` override.
"""

from __future__ import annotations

from ._ffi import library as _library
from .audio import (
    Music,
    Sound,
    audio_available,
    close_audio,
    init_audio,
)
from .files import directory_exists, file_exists, file_size
from .geometry import (
    BEIGE,
    BLACK,
    BLANK,
    BLUE,
    BROWN,
    Camera2D,
    Camera3D,
    Color,
    DARKBLUE,
    DARKBROWN,
    DARKGRAY,
    DARKGREEN,
    DARKPURPLE,
    GOLD,
    GRAY,
    GREEN,
    LIGHTGRAY,
    LIME,
    MAGENTA,
    MAROON,
    ORANGE,
    PINK,
    PURPLE,
    RAYWHITE,
    RED,
    Rectangle,
    SKYBLUE,
    Vector2,
    Vector3,
    VIOLET,
    WHITE,
    YELLOW,
)
from .input import (
    LEFT_BUTTON,
    MIDDLE_BUTTON,
    MOUSE_LEFT,
    MOUSE_MIDDLE,
    MOUSE_RIGHT,
    RIGHT_BUTTON,
    gamepad_available,
    gamepad_axis,
    gamepad_down,
    key_down,
    key_pressed,
    key_released,
    mouse_down,
    mouse_position,
    mouse_pressed,
    mouse_released,
    mouse_wheel,
)
from .keys import Key
from .shapes import (
    draw_circle,
    draw_circle_lines,
    draw_cube,
    draw_grid,
    draw_line,
    draw_line_3d,
    draw_line_ex,
    draw_pixel,
    draw_poly,
    draw_rectangle,
    draw_rectangle_lines,
    draw_rectangle_pro,
    draw_sphere,
    draw_triangle,
)
from .text import Font, draw_text, draw_text_ex, measure_text, measure_text_ex
from .textures import Texture
from .window import (
    Window,
    begin_drawing,
    begin_mode_2d,
    begin_mode_3d,
    clear,
    elapsed_time,
    end_drawing,
    end_mode_2d,
    end_mode_3d,
    fps,
    frame_time,
    init,
    is_ready,
    native_window,
    screen_size,
    set_exit_key,
    set_target_fps,
    set_window_position,
    set_window_size,
    set_window_title,
    should_close,
    shutdown,
    toggle_fullscreen,
    version,
)

__version__ = "0.1.0"

__all__ = [
    # window / frame
    "Window", "version", "init", "shutdown", "is_ready", "should_close",
    "begin_drawing", "end_drawing", "clear", "set_target_fps", "fps",
    "frame_time", "elapsed_time", "screen_size", "set_window_title",
    "set_window_size", "set_window_position", "toggle_fullscreen",
    "set_exit_key", "native_window", "begin_mode_2d", "end_mode_2d",
    "begin_mode_3d", "end_mode_3d",
    # input
    "Key", "key_down", "key_pressed", "key_released",
    "mouse_down", "mouse_pressed", "mouse_released", "mouse_position",
    "mouse_wheel", "LEFT_BUTTON", "MIDDLE_BUTTON", "RIGHT_BUTTON",
    "MOUSE_LEFT", "MOUSE_MIDDLE", "MOUSE_RIGHT",
    "gamepad_available", "gamepad_down", "gamepad_axis",
    # types
    "Color", "Vector2", "Vector3", "Rectangle", "Camera2D", "Camera3D",
    # drawing
    "draw_pixel", "draw_line", "draw_line_ex", "draw_circle",
    "draw_circle_lines", "draw_rectangle", "draw_rectangle_lines",
    "draw_rectangle_pro", "draw_triangle", "draw_poly",
    "draw_cube", "draw_sphere", "draw_line_3d", "draw_grid",
    # text
    "Font", "draw_text", "draw_text_ex", "measure_text", "measure_text_ex",
    # textures
    "Texture",
    # audio
    "Sound", "Music", "init_audio", "close_audio", "audio_available",
    # files
    "file_exists", "directory_exists", "file_size",
]
