# vlcrlib language bindings

The bindings are thin, idiomatic packages over a *stable C ABI* so that the
same native build serves every language:

```
                     src/*.c, src/*.cpp  (vlcrlib static library)
                                │
                                ▼
              bindings/ffi/vlcr_ffi.h + vlcr_ffi.c
                    (scalars, handles, out-params)
                                │  shared library: vlcr_ffi.dll / libvlcr_ffi.so
        ┌───────────────┬───────┴────────┬────────────────┐
        ▼               ▼                ▼                ▼
   C++ (header)     Python ctypes     C# P/Invoke     Java JNI (vlcr_jni.dll)
   no FFI, links                     (vlcr_ffi)       → vlcr_ffi
   the static lib
```

* **C++** talks to the C API directly (zero overhead, full access to structs).
* **Python / C#** load `vlcr_ffi` and call its exported `vlcr_*` functions.
* **Java** needs JNI, so `vlcr_jni` (generated glue, built by CMake) forwards to
  `vlcr_ffi`.

Everything below is generated from `bindings/ffi/vlcr_ffi.h` by
`bindings/tools/gen_bindings.py`:

* `bindings/python/vlcrlib/_native.py` — ctypes signatures
* `bindings/csharp/Vlcrlib/NativeMethods.g.cs` — P/Invoke declarations
* `bindings/java/src/main/java/org/vlcrlib/Native.java` — native declarations
* `bindings/java/native/vlcr_jni.c` — JNI glue
* `bindings/python/vlcrlib/keys.py`, `bindings/csharp/Vlcrlib/Keys.g.cs`,
  `bindings/java/src/main/java/org/vlcrlib/Key.java` — SDL3 scancodes

The idiomatic wrappers (`Window`, `Draw`, `Font`, ...) are written by hand on
top of those declarations.

## Quick start

```powershell
# build the native libraries + every binding, run the smoke tests
scripts/build_bindings.ps1

# or manually
cmake -B build-bindings -DVLCRLIB_BUILD_BINDINGS=ON -DVLCRLIB_BUILD_EXAMPLES=OFF
cmake --build build-bindings --config Release --target vlcr_ffi vlcr_jni vlcr_cpp_hello
```

| Language | Docs | Example |
| --- | --- | --- |
| C++ | [bindings/cpp](cpp) | `bindings/cpp/examples/hello.cpp` |
| Python | [bindings/python/README.md](python/README.md) | `bindings/python/examples/hello.py` |
| C# | [bindings/csharp/README.md](csharp/README.md) | `bindings/csharp/examples/Hello` |
| Java | [bindings/java/README.md](java/README.md) | `bindings/java/examples/HelloVlcrlib.java` |

`scripts/build_bindings.ps1` stages everything into `bindings/dist/`:

```
bindings/dist/
  vlcr_ffi.dll  vlcr_jni.dll  SDL3.dll  vulkan-1.dll
  shaders/            # SPIR-V, required at run time
  java/classes/       # compiled Java package + examples
```

Run programs with `bindings/dist` as the working directory so the engine finds
`shaders/`; the JVM/`dotnet` also find the native libraries there.

## What is covered

The binding ABI exposes the parts of vlcrlib that make sense across languages:
window and frame loop, window state, input (keyboard/mouse/gamepad), 2D and
basic 3D drawing, cameras, text and fonts, textures, audio, files.

Not exposed yet (still available from C/C++ directly): physics, glTF/model
loading, GUI widgets, particles, animation, networking and the full render
graph. Adding one is a three-step change: declare it in `vlcr_ffi.h`, implement
it in `vlcr_ffi.c`, run the generator, then add the wrapper in each language.

## Design rules

* Only scalars, UTF-8 strings, `void*` and out-parameters cross the FFI.
  Structs by value are flattened (`begin_mode_3d` takes 12 floats).
* Colors travel as `0xAABBGGRR` (`VLCR_RGBA` / `Color.packed()`).
* Resources (fonts, textures, sounds, music) are integer handles ≥ 1, 0 means
  invalid; `vlcr_shutdown()` releases whatever is left.
* The library is single-threaded: use it from the thread that owns the window.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `InitWindow` fails / `InitGraphicsDevice failed` | run from a directory containing the compiled `shaders/` (`build-bindings/Release/shaders`) |
| `cannot load the native vlcrlib binding library` (Python) | build `vlcr_ffi`, or set `VLCRLIB_LIBRARY` to its full path |
| `UnsatisfiedLinkError` / `DllNotFoundException` | same; make sure `SDL3.dll` and `vulkan-1.dll` are next to `vlcr_ffi` or on `PATH` |
| Java `NoClassDefFoundError: org/vlcrlib/...` | add `bindings/dist/java/classes` (or `build/classes/java/main`) to `-cp` |
