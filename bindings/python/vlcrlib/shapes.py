"""Immediate-mode drawing primitives (2D and a few 3D ones)."""

from __future__ import annotations

from . import _ffi
from .geometry import Color, Rectangle

__all__ = [
    "draw_pixel",
    "draw_line",
    "draw_line_ex",
    "draw_circle",
    "draw_circle_lines",
    "draw_rectangle",
    "draw_rectangle_lines",
    "draw_rectangle_pro",
    "draw_triangle",
    "draw_poly",
    "draw_cube",
    "draw_sphere",
    "draw_line_3d",
    "draw_grid",
]


def _lib():
    return _ffi.library()


def draw_pixel(x: int, y: int, color: Color) -> None:
    _lib().vlcr_draw_pixel(int(x), int(y), color.packed())


def draw_line(x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
    _lib().vlcr_draw_line(int(x0), int(y0), int(x1), int(y1), color.packed())


def draw_line_ex(start: tuple, end: tuple, thickness: float = 1.0,
                 color: Color = Color(255, 255, 255)) -> None:
    _lib().vlcr_draw_line_ex(
        float(start[0]), float(start[1]), float(end[0]), float(end[1]),
        float(thickness), color.packed())


def draw_circle(center_x: float, center_y: float, radius: float, color: Color) -> None:
    _lib().vlcr_draw_circle(int(center_x), int(center_y), float(radius), color.packed())


def draw_circle_lines(center_x: float, center_y: float, radius: float, color: Color) -> None:
    _lib().vlcr_draw_circle_lines(int(center_x), int(center_y), float(radius), color.packed())


def draw_rectangle(x: float, y: float, width: float, height: float, color: Color) -> None:
    _lib().vlcr_draw_rectangle(int(x), int(y), int(width), int(height), color.packed())


def draw_rectangle_lines(x: float, y: float, width: float, height: float,
                         color: Color) -> None:
    _lib().vlcr_draw_rectangle_lines(int(x), int(y), int(width), int(height), color.packed())


def draw_rectangle_pro(rectangle: Rectangle, origin: tuple = (0.0, 0.0),
                       rotation: float = 0.0, color: Color = Color(255, 255, 255)) -> None:
    _lib().vlcr_draw_rectangle_pro(
        float(rectangle.x), float(rectangle.y),
        float(rectangle.width), float(rectangle.height),
        float(origin[0]), float(origin[1]), float(rotation), color.packed())


def draw_triangle(v1: tuple, v2: tuple, v3: tuple, color: Color) -> None:
    _lib().vlcr_draw_triangle(
        float(v1[0]), float(v1[1]), float(v2[0]), float(v2[1]),
        float(v3[0]), float(v3[1]), color.packed())


def draw_poly(center: tuple, sides: int, radius: float, rotation: float,
              color: Color) -> None:
    _lib().vlcr_draw_poly(float(center[0]), float(center[1]), int(sides),
                          float(radius), float(rotation), color.packed())


def draw_cube(x: float, y: float, z: float, width: float = 1.0, height: float = 1.0,
              depth: float = 1.0, color: Color = Color(255, 255, 255)) -> None:
    """Draw a cube; call inside :func:`vlcrlib.begin_mode_3d`."""
    _lib().vlcr_draw_cube(float(x), float(y), float(z), float(width), float(height),
                          float(depth), color.packed())


def draw_sphere(x: float, y: float, z: float, radius: float = 1.0,
                color: Color = Color(255, 255, 255)) -> None:
    _lib().vlcr_draw_sphere(float(x), float(y), float(z), float(radius), color.packed())


def draw_line_3d(start: tuple, end: tuple, color: Color) -> None:
    _lib().vlcr_draw_line_3d(float(start[0]), float(start[1]), float(start[2]),
                             float(end[0]), float(end[1]), float(end[2]), color.packed())


def draw_grid(slices: int = 10, spacing: float = 1.0) -> None:
    _lib().vlcr_draw_grid(int(slices), float(spacing))
