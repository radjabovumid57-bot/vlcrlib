"""Window, frame loop and window-level state."""

from __future__ import annotations

import contextlib
from typing import Iterator, Tuple

from . import _ffi
from .geometry import Camera2D, Camera3D, Color

__all__ = [
    "Window",
    "version",
    "init",
    "shutdown",
    "is_ready",
    "should_close",
    "begin_drawing",
    "end_drawing",
    "clear",
    "set_target_fps",
    "fps",
    "frame_time",
    "elapsed_time",
    "screen_size",
    "set_window_title",
    "set_window_size",
    "set_window_position",
    "toggle_fullscreen",
    "set_exit_key",
    "native_window",
    "begin_mode_2d",
    "end_mode_2d",
    "begin_mode_3d",
    "end_mode_3d",
]


def _lib():
    return _ffi.library()


def version() -> str:
    """Version of the binding ABI shipped by the native library."""
    return _lib().vlcr_version().decode("utf-8")


def init(width: int, height: int, title: str = "vlcrlib") -> bool:
    """Create the window and initialise the renderer."""
    return bool(_lib().vlcr_init(int(width), int(height), title.encode("utf-8")))


def shutdown() -> None:
    """Release every resource and close the window."""
    _lib().vlcr_shutdown()


def is_ready() -> bool:
    return bool(_lib().vlcr_is_ready())


def should_close() -> bool:
    return bool(_lib().vlcr_should_close())


def begin_drawing() -> None:
    _lib().vlcr_begin_drawing()


def end_drawing() -> None:
    _lib().vlcr_end_drawing()


def clear(color: Color) -> None:
    _lib().vlcr_clear(color.packed())


def set_target_fps(fps_value: int) -> None:
    _lib().vlcr_set_target_fps(int(fps_value))


def fps() -> int:
    return int(_lib().vlcr_fps())


def frame_time() -> float:
    """Seconds elapsed during the previous frame."""
    return float(_lib().vlcr_frame_time())


def elapsed_time() -> float:
    """Seconds since the window was created."""
    return float(_lib().vlcr_time())


def screen_size() -> Tuple[int, int]:
    import ctypes

    width = ctypes.c_int(0)
    height = ctypes.c_int(0)
    _lib().vlcr_window_size(ctypes.byref(width), ctypes.byref(height))
    return int(width.value), int(height.value)


def set_window_title(title: str) -> None:
    _lib().vlcr_set_window_title(title.encode("utf-8"))


def set_window_size(width: int, height: int) -> None:
    _lib().vlcr_set_window_size(int(width), int(height))


def set_window_position(x: int, y: int) -> None:
    _lib().vlcr_set_window_position(int(x), int(y))


def toggle_fullscreen() -> None:
    _lib().vlcr_toggle_fullscreen()


def set_exit_key(key: int) -> None:
    _lib().vlcr_set_exit_key(int(key))


def native_window() -> int:
    """Native window handle (HWND on Windows), or 0."""
    return int(_lib().vlcr_native_window() or 0)


def begin_mode_2d(camera: Camera2D) -> None:
    _lib().vlcr_begin_mode_2d(
        camera.offset.x,
        camera.offset.y,
        camera.target.x,
        camera.target.y,
        camera.rotation,
        camera.zoom,
    )


def end_mode_2d() -> None:
    _lib().vlcr_end_mode_2d()


def begin_mode_3d(camera: Camera3D) -> None:
    _lib().vlcr_begin_mode_3d(
        camera.position.x,
        camera.position.y,
        camera.position.z,
        camera.target.x,
        camera.target.y,
        camera.target.z,
        camera.up.x,
        camera.up.y,
        camera.up.z,
        camera.fovy,
        camera.near_plane,
        camera.far_plane,
    )


def end_mode_3d() -> None:
    _lib().vlcr_end_mode_3d()


class Window:
    """Context manager owning the window and the frame loop.

    >>> with Window(1280, 720, "demo") as win:
    ...     while win.running:
    ...         with win.frame():
    ...             clear(RAYWHITE)
    """

    def __init__(self, width: int, height: int, title: str = "vlcrlib",
                 target_fps: int = 0) -> None:
        if not init(width, height, title):
            raise RuntimeError("vlcrlib: failed to create the window")
        if target_fps:
            set_target_fps(target_fps)

    def __enter__(self) -> "Window":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def close(self) -> None:
        if is_ready():
            _lib().vlcr_shutdown()

    @property
    def running(self) -> bool:
        return not should_close()

    @property
    def size(self) -> Tuple[int, int]:
        return screen_size()

    @contextlib.contextmanager
    def frame(self) -> Iterator[None]:
        """``BeginDrawing()`` / ``EndDrawing()`` as a context manager."""
        begin_drawing()
        try:
            yield
        finally:
            end_drawing()
