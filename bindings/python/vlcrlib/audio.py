"""Sound effects and music streams.

The engine's only backend is libVLC; when it is unavailable the device opens
with ``init_audio()`` returning ``False`` and every call here is a no-op.
"""

from __future__ import annotations

from . import _ffi

__all__ = ["init_audio", "close_audio", "audio_available", "Sound", "Music"]


def _lib():
    return _ffi.library()


def init_audio() -> bool:
    return bool(_lib().vlcr_audio_init())


def close_audio() -> None:
    _lib().vlcr_audio_close()


def audio_available() -> bool:
    return bool(_lib().vlcr_audio_available())


class Sound:
    """A short, fully decoded-in-memory clip."""

    __slots__ = ("_handle", "_volume")

    def __init__(self, handle: int) -> None:
        if handle <= 0:
            raise ValueError("invalid sound handle")
        self._handle = int(handle)
        self._volume = 1.0

    @classmethod
    def load(cls, path: str) -> "Sound":
        handle = _lib().vlcr_sound_load(path.encode("utf-8"))
        if handle <= 0:
            raise IOError(f"vlcrlib: cannot load sound {path!r}")
        return cls(handle)

    @property
    def handle(self) -> int:
        return self._handle

    def play(self) -> None:
        _lib().vlcr_sound_play(self._handle)

    def stop(self) -> None:
        _lib().vlcr_sound_stop(self._handle)

    @property
    def playing(self) -> bool:
        return bool(_lib().vlcr_sound_playing(self._handle))

    @property
    def volume(self) -> float:
        return self._volume

    @volume.setter
    def volume(self, value: float) -> None:
        self._volume = float(value)
        _lib().vlcr_sound_set_volume(self._handle, float(value))

    def unload(self) -> None:
        if self._handle > 0:
            _lib().vlcr_sound_unload(self._handle)
            self._handle = 0

    def __enter__(self) -> "Sound":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.unload()


class Music:
    """A streamed clip; call :meth:`update` every frame to keep it alive."""

    __slots__ = ("_handle", "_volume")

    def __init__(self, handle: int) -> None:
        if handle <= 0:
            raise ValueError("invalid music handle")
        self._handle = int(handle)
        self._volume = 1.0

    @classmethod
    def load(cls, path: str) -> "Music":
        handle = _lib().vlcr_music_load(path.encode("utf-8"))
        if handle <= 0:
            raise IOError(f"vlcrlib: cannot load music {path!r}")
        return cls(handle)

    @property
    def handle(self) -> int:
        return self._handle

    def play(self) -> None:
        _lib().vlcr_music_play(self._handle)

    def stop(self) -> None:
        _lib().vlcr_music_stop(self._handle)

    def pause(self) -> None:
        _lib().vlcr_music_pause(self._handle)

    def resume(self) -> None:
        _lib().vlcr_music_resume(self._handle)

    def update(self) -> None:
        _lib().vlcr_music_update(self._handle)

    @property
    def playing(self) -> bool:
        return bool(_lib().vlcr_music_playing(self._handle))

    @property
    def length(self) -> float:
        return float(_lib().vlcr_music_length(self._handle))

    @property
    def played(self) -> float:
        return float(_lib().vlcr_music_played(self._handle))

    @property
    def volume(self) -> float:
        return self._volume

    @volume.setter
    def volume(self, value: float) -> None:
        self._volume = float(value)
        _lib().vlcr_music_set_volume(self._handle, float(value))

    def unload(self) -> None:
        if self._handle > 0:
            _lib().vlcr_music_unload(self._handle)
            self._handle = 0

    def __enter__(self) -> "Music":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.unload()
