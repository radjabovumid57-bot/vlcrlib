"""Fonts and text drawing."""

from __future__ import annotations

import ctypes

from . import _ffi
from .geometry import Color, Vector2

__all__ = ["Font", "draw_text", "draw_text_ex", "measure_text", "measure_text_ex"]


def _lib():
    return _ffi.library()


class Font:
    """A baked bitmap font. Release with :meth:`unload` or use ``with``."""

    __slots__ = ("_handle",)

    def __init__(self, handle: int) -> None:
        if handle <= 0:
            raise ValueError("invalid font handle")
        self._handle = int(handle)

    @classmethod
    def load(cls, path: str, size: int = 32) -> "Font":
        handle = _lib().vlcr_font_load(path.encode("utf-8"), int(size))
        if handle <= 0:
            raise IOError(f"vlcrlib: cannot load font {path!r}")
        return cls(handle)

    @classmethod
    def default(cls) -> "Font":
        handle = _lib().vlcr_font_default()
        if handle <= 0:
            raise IOError("vlcrlib: no default font available")
        return cls(handle)

    @property
    def handle(self) -> int:
        return self._handle

    def unload(self) -> None:
        if self._handle > 0:
            _lib().vlcr_font_unload(self._handle)
            self._handle = 0

    def __enter__(self) -> "Font":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.unload()

    def draw(self, text: str, x: float, y: float, size: float = 20.0,
             spacing: float = 1.0, color: Color = Color(255, 255, 255)) -> None:
        draw_text_ex(self, text, x, y, size, spacing, color)

    def measure(self, text: str, size: float = 20.0, spacing: float = 1.0) -> Vector2:
        return measure_text_ex(self, text, size, spacing)


def draw_text(text: str, x: int, y: int, size: int = 20,
              color: Color = Color(255, 255, 255)) -> None:
    """Draw with the built-in default font."""
    _lib().vlcr_draw_text(text.encode("utf-8"), int(x), int(y), int(size), color.packed())


def draw_text_ex(font, text: str, x: float, y: float, size: float = 20.0,
                 spacing: float = 1.0, color: Color = Color(255, 255, 255)) -> None:
    handle = font.handle if isinstance(font, Font) else int(font or 0)
    _lib().vlcr_draw_text_ex(handle, text.encode("utf-8"), float(x), float(y),
                             float(size), float(spacing), color.packed())


def measure_text(text: str, size: int = 20) -> int:
    return int(_lib().vlcr_measure_text(text.encode("utf-8"), int(size)))


def measure_text_ex(font, text: str, size: float = 20.0,
                    spacing: float = 1.0) -> Vector2:
    handle = font.handle if isinstance(font, Font) else int(font or 0)
    width = ctypes.c_float(0.0)
    height = ctypes.c_float(0.0)
    _lib().vlcr_measure_text_ex(handle, text.encode("utf-8"), float(size), float(spacing),
                                ctypes.byref(width), ctypes.byref(height))
    return Vector2(width.value, height.value)
