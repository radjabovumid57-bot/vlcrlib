"""Textures."""

from __future__ import annotations

from . import _ffi
from .geometry import Color, Vector2

__all__ = ["Texture"]


def _lib():
    return _ffi.library()


class Texture:
    """A 2D texture. Release with :meth:`unload` or use ``with``."""

    __slots__ = ("_handle",)

    def __init__(self, handle: int) -> None:
        if handle <= 0:
            raise ValueError("invalid texture handle")
        self._handle = int(handle)

    @classmethod
    def load(cls, path: str) -> "Texture":
        handle = _lib().vlcr_texture_load(path.encode("utf-8"))
        if handle <= 0:
            raise IOError(f"vlcrlib: cannot load texture {path!r}")
        return cls(handle)

    @property
    def handle(self) -> int:
        return self._handle

    @property
    def size(self) -> tuple:
        import ctypes

        width = ctypes.c_int(0)
        height = ctypes.c_int(0)
        _lib().vlcr_texture_size(self._handle, ctypes.byref(width), ctypes.byref(height))
        return int(width.value), int(height.value)

    @property
    def width(self) -> int:
        return self.size[0]

    @property
    def height(self) -> int:
        return self.size[1]

    def unload(self) -> None:
        if self._handle > 0:
            _lib().vlcr_texture_unload(self._handle)
            self._handle = 0

    def __enter__(self) -> "Texture":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.unload()

    def draw(self, x: float, y: float, color: Color = Color(255, 255, 255)) -> None:
        _lib().vlcr_draw_texture(self._handle, int(x), int(y), color.packed())

    def draw_ex(self, position: Vector2, rotation: float = 0.0, scale: float = 1.0,
                color: Color = Color(255, 255, 255)) -> None:
        _lib().vlcr_draw_texture_ex(self._handle, float(position.x), float(position.y),
                                    float(rotation), float(scale), color.packed())

    def draw_region(self, source: tuple, position: Vector2,
                    color: Color = Color(255, 255, 255)) -> None:
        """``source`` is ``(x, y, width, height)`` inside the texture."""
        _lib().vlcr_draw_texture_region(
            self._handle,
            float(source[0]), float(source[1]), float(source[2]), float(source[3]),
            float(position.x), float(position.y), color.packed())
