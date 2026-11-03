"""Locating and loading the native `vlcr_ffi` shared library.

Search order:

1. ``VLCRLIB_LIBRARY`` — full path to the shared library;
2. the ``vlcrlib`` package directory (wheels ship the library next to the code);
3. ``build-bindings/bindings/<config>/`` and ``build/bindings/<config>/`` in a
   source checkout;
4. the platform loader (``PATH`` / ``LD_LIBRARY_PATH`` / ``DYLD_LIBRARY_PATH``).
"""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

from . import _native

if sys.platform == "win32":
    _LIBRARY_NAME = "vlcr_ffi.dll"
elif sys.platform == "darwin":
    _LIBRARY_NAME = "libvlcr_ffi.dylib"
else:
    _LIBRARY_NAME = "libvlcr_ffi.so"

_library = None


def _package_dir() -> Path:
    return Path(__file__).resolve().parent


def _repo_root() -> Path:
    # bindings/python/vlcrlib -> bindings/python -> bindings -> repo root
    return _package_dir().parents[2]


def _candidate_paths():
    env = os.environ.get("VLCRLIB_LIBRARY")
    if env:
        yield Path(env)

    yield _package_dir() / _LIBRARY_NAME

    root = _repo_root()
    for build in ("build-bindings", "build", "build-cpu"):
        for config in ("Release", "Debug", ""):
            yield root / build / "bindings" / config / _LIBRARY_NAME


def _native_search_dirs():
    """Directories holding runtime dependencies (SDL3.dll, vulkan-1.dll, ...)."""
    root = _repo_root()
    candidates = [
        _package_dir(),
        root / "build-bindings" / "bindings" / "Release",
        root / "vcpkg_installed" / "x64-windows" / "bin",
    ]
    for path in candidates:
        if path.is_dir():
            yield path


def _register_dll_dirs() -> None:
    if sys.platform != "win32" or not hasattr(os, "add_dll_directory"):
        return
    for directory in _native_search_dirs():
        try:
            os.add_dll_directory(str(directory))
        except OSError:
            pass


def library() -> ctypes.CDLL:
    """Return the loaded native library, loading it on first use."""
    global _library
    if _library is not None:
        return _library

    attempts = []
    for path in _candidate_paths():
        if path.is_file():
            try:
                _register_dll_dirs()
                _library = ctypes.CDLL(str(path))
                break
            except OSError as exc:  # missing dependency, wrong bitness, ...
                attempts.append(f"{path}: {exc}")
        else:
            attempts.append(f"{path}: not found")

    if _library is None:
        try:
            _register_dll_dirs()
            _library = ctypes.CDLL(_LIBRARY_NAME)
        except OSError as exc:
            attempts.append(f"{_LIBRARY_NAME}: {exc}")
            raise RuntimeError(
                "cannot load the native vlcrlib binding library "
                f"({_LIBRARY_NAME}).\nSearched:\n  " + "\n  ".join(attempts) + "\n"
                "Build it with scripts/build_bindings.ps1 or set VLCRLIB_LIBRARY "
                "to the full path of the shared library."
            ) from exc

    _native.declare(_library)
    return _library
