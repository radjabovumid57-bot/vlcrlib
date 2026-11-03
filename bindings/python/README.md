# vlcrlib — Python binding

`ctypes` bindings for [vlcrlib](../../README.md), talking to the stable
`vlcr_ffi` C ABI (`bindings/ffi/vlcr_ffi.h`). No compilation step, no
third-party dependencies.

```python
import vlcrlib as vl

with vl.Window(1280, 720, "Hello vlcrlib", target_fps=60) as win:
    while win.running:
        with win.frame():
            vl.clear(vl.RAYWHITE)
            vl.draw_rectangle(40, 40, 200, 120, vl.ORANGE)
            vl.draw_circle(400, 300, 80, vl.SKYBLUE)
            vl.draw_text("Hello from Python", 40, 200, 30, vl.DARKGRAY)
```

## Requirements

* Python 3.9+
* the native library, built from the repo root:

```powershell
scripts/build_bindings.ps1          # or: cmake --build build-bindings --config Release --target vlcr_ffi
```

The loader looks for `vlcr_ffi.dll` (or `libvlcr_ffi.so` / `.dylib`) in:

1. `$VLCRLIB_LIBRARY` (full path),
2. the `vlcrlib/` package directory,
3. `build-bindings/bindings/<config>/`, `build/bindings/<config>/` in the checkout,
4. the platform loader (`PATH`, `LD_LIBRARY_PATH`, ...).

On Windows the loader also registers the directory holding `SDL3.dll` /
`vulkan-1.dll` (`vcpkg_installed/x64-windows/bin` in a checkout) so no manual
`PATH` edits are needed.

> The engine loads its SPIR-V shaders from `shaders/` relative to the working
> directory. Run your app from a folder that contains `shaders/` (the CMake
> build tree has one at `build-bindings/Release/shaders`), or copy the folder
> next to your executable.

## API surface

| Module | Contents |
| --- | --- |
| `vlcrlib` | `Window`, `clear`, `draw_*`, `Key`, `Color`, `Vector2/3`, `Rectangle`, `Camera2D/3D` |
| `vlcrlib.text` | `Font`, `draw_text`, `draw_text_ex`, `measure_text(_ex)` |
| `vlcrlib.textures` | `Texture` |
| `vlcrlib.audio` | `init_audio`, `close_audio`, `Sound`, `Music` |
| `vlcrlib.input` | `key_*`, `mouse_*`, `gamepad_*` |
| `vlcrlib.files` | `file_exists`, `directory_exists`, `file_size` |

Resource classes (`Font`, `Texture`, `Sound`, `Music`) are context managers and
release their handle on `unload()` / `close()`.

Colors are `Color(r, g, b, a=255)` objects; they are packed to `0xAABBGGRR`
for the native layer.

## Install

```powershell
pip install -e bindings/python      # editable, from the repo root
```

## Run the example / smoke test

```powershell
python bindings/python/examples/hello.py
python bindings/python/tests/test_smoke.py            # opens a window for 3 frames
$env:VLCR_SKIP_WINDOW=1; python bindings/python/tests/test_smoke.py
```

## Adding functions

`vlcrlib/_native.py` is generated — edit `bindings/ffi/vlcr_ffi.h`, then run
`python bindings/tools/gen_bindings.py` and add the wrapper to the matching
module.
