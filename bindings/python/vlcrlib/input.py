"""Keyboard, mouse and gamepad polling.

Key codes are SDL3 scancodes; see :class:`vlcrlib.keys.Key`.
"""

from __future__ import annotations

from . import _ffi
from .geometry import Vector2

__all__ = [
    "key_down",
    "key_pressed",
    "key_released",
    "mouse_down",
    "mouse_pressed",
    "mouse_released",
    "mouse_position",
    "mouse_wheel",
    "gamepad_available",
    "gamepad_down",
    "gamepad_axis",
]

LEFT_BUTTON = 1
MIDDLE_BUTTON = 2
RIGHT_BUTTON = 3
MOUSE_LEFT = LEFT_BUTTON
MOUSE_MIDDLE = MIDDLE_BUTTON
MOUSE_RIGHT = RIGHT_BUTTON


def _lib():
    return _ffi.library()


def key_down(key: int) -> bool:
    return bool(_lib().vlcr_key_down(int(key)))


def key_pressed(key: int) -> bool:
    return bool(_lib().vlcr_key_pressed(int(key)))


def key_released(key: int) -> bool:
    return bool(_lib().vlcr_key_released(int(key)))


def mouse_down(button: int = LEFT_BUTTON) -> bool:
    return bool(_lib().vlcr_mouse_down(int(button)))


def mouse_pressed(button: int = LEFT_BUTTON) -> bool:
    return bool(_lib().vlcr_mouse_pressed(int(button)))


def mouse_released(button: int = LEFT_BUTTON) -> bool:
    return bool(_lib().vlcr_mouse_released(int(button)))


def mouse_position() -> Vector2:
    import ctypes

    x = ctypes.c_float(0.0)
    y = ctypes.c_float(0.0)
    _lib().vlcr_mouse_position(ctypes.byref(x), ctypes.byref(y))
    return Vector2(x.value, y.value)


def mouse_wheel() -> float:
    return float(_lib().vlcr_mouse_wheel())


def gamepad_available(index: int = 0) -> bool:
    return bool(_lib().vlcr_gamepad_available(int(index)))


def gamepad_down(index: int, button: int) -> bool:
    return bool(_lib().vlcr_gamepad_down(int(index), int(button)))


def gamepad_axis(index: int, axis: int) -> float:
    return float(_lib().vlcr_gamepad_axis(int(index), int(axis)))
