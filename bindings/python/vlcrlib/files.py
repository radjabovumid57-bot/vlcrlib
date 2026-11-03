"""Filesystem helpers."""

from __future__ import annotations

from . import _ffi

__all__ = ["file_exists", "directory_exists", "file_size"]


def file_exists(path: str) -> bool:
    return bool(_ffi.library().vlcr_file_exists(path.encode("utf-8")))


def directory_exists(path: str) -> bool:
    return bool(_ffi.library().vlcr_directory_exists(path.encode("utf-8")))


def file_size(path: str) -> int:
    return int(_ffi.library().vlcr_file_size(path.encode("utf-8")))
