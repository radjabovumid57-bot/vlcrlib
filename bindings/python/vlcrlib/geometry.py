"""Value types shared by the Python binding."""

from __future__ import annotations

from dataclasses import dataclass


def _rgba(r: int, g: int, b: int, a: int) -> int:
    return ((r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | ((a & 0xFF) << 24))


@dataclass(frozen=True)
class Color:
    """Straight 8-bit RGBA color."""

    r: int
    g: int
    b: int
    a: int = 255

    def packed(self) -> int:
        return _rgba(self.r, self.g, self.b, self.a)

    @classmethod
    def from_hex(cls, value: int, alpha: int = 255) -> "Color":
        """``0xRRGGBB`` -> Color."""
        return cls((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, alpha)


@dataclass(frozen=True)
class Vector2:
    x: float = 0.0
    y: float = 0.0

    def __add__(self, other: "Vector2") -> "Vector2":
        return Vector2(self.x + other.x, self.y + other.y)

    def __sub__(self, other: "Vector2") -> "Vector2":
        return Vector2(self.x - other.x, self.y - other.y)

    def __mul__(self, scalar: float) -> "Vector2":
        return Vector2(self.x * scalar, self.y * scalar)

    __rmul__ = __mul__

    def dot(self, other: "Vector2") -> float:
        return self.x * other.x + self.y * other.y

    def length(self) -> float:
        return (self.x * self.x + self.y * self.y) ** 0.5

    def normalized(self) -> "Vector2":
        length = self.length()
        return Vector2(self.x / length, self.y / length) if length > 1e-8 else Vector2()


@dataclass(frozen=True)
class Vector3:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def __add__(self, other: "Vector3") -> "Vector3":
        return Vector3(self.x + other.x, self.y + other.y, self.z + other.z)

    def __sub__(self, other: "Vector3") -> "Vector3":
        return Vector3(self.x - other.x, self.y - other.y, self.z - other.z)

    def __mul__(self, scalar: float) -> "Vector3":
        return Vector3(self.x * scalar, self.y * scalar, self.z * scalar)

    __rmul__ = __mul__

    def dot(self, other: "Vector3") -> float:
        return self.x * other.x + self.y * other.y + self.z * other.z

    def cross(self, other: "Vector3") -> "Vector3":
        return Vector3(
            self.y * other.z - self.z * other.y,
            self.z * other.x - self.x * other.z,
            self.x * other.y - self.y * other.x,
        )

    def length(self) -> float:
        return (self.x * self.x + self.y * self.y + self.z * self.z) ** 0.5

    def normalized(self) -> "Vector3":
        length = self.length()
        if length <= 1e-8:
            return Vector3()
        return Vector3(self.x / length, self.y / length, self.z / length)


@dataclass(frozen=True)
class Rectangle:
    x: float
    y: float
    width: float
    height: float


@dataclass(frozen=True)
class Camera2D:
    offset: Vector2 = Vector2()
    target: Vector2 = Vector2()
    rotation: float = 0.0
    zoom: float = 1.0


@dataclass(frozen=True)
class Camera3D:
    position: Vector3 = Vector3(0.0, 5.0, 10.0)
    target: Vector3 = Vector3()
    up: Vector3 = Vector3(0.0, 1.0, 0.0)
    fovy: float = 45.0
    near_plane: float = 0.1
    far_plane: float = 1000.0


# A few named colors, matching the C header.
LIGHTGRAY = Color(200, 200, 200)
GRAY = Color(130, 130, 130)
DARKGRAY = Color(80, 80, 80)
YELLOW = Color(253, 249, 0)
GOLD = Color(255, 203, 0)
ORANGE = Color(255, 161, 0)
PINK = Color(255, 109, 194)
RED = Color(230, 41, 55)
MAROON = Color(190, 33, 55)
GREEN = Color(0, 228, 48)
LIME = Color(0, 158, 47)
DARKGREEN = Color(0, 117, 44)
SKYBLUE = Color(102, 191, 255)
BLUE = Color(0, 121, 241)
DARKBLUE = Color(0, 82, 172)
PURPLE = Color(200, 122, 255)
VIOLET = Color(135, 60, 190)
DARKPURPLE = Color(112, 31, 126)
BEIGE = Color(211, 176, 131)
BROWN = Color(127, 106, 79)
DARKBROWN = Color(76, 63, 47)
WHITE = Color(255, 255, 255)
BLACK = Color(0, 0, 0)
BLANK = Color(0, 0, 0, 0)
MAGENTA = Color(255, 0, 255)
RAYWHITE = Color(245, 245, 245)
