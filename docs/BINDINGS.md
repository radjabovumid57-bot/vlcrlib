# Language bindings

vlcrlib ships bindings for C++, Python, C# and Java. They share one stable C
ABI (`bindings/ffi/vlcr_ffi.h`) built as a shared library, so the same native
artifacts serve every runtime.

See [bindings/README.md](../bindings/README.md) for the architecture, build
instructions and troubleshooting, and the per-language READMEs:

* [C++](../bindings/cpp/include/vlcrlib/vlcrlib.hpp) — header-only wrapper, links the static library
* [Python](../bindings/python/README.md) — `ctypes`, no dependencies
* [C#](../bindings/csharp/README.md) — `net8.0` class library, P/Invoke
* [Java](../bindings/java/README.md) — JNI bridge (`vlcr_jni`), Gradle project

Build everything and run the smoke tests:

```powershell
scripts/build_bindings.ps1
```

## Coverage

| Area | C++ | Python | C# | Java |
| --- | --- | --- | --- | --- |
| Window / frame loop | ✔ | ✔ | ✔ | ✔ |
| Input (keyboard, mouse, gamepad) | ✔ | ✔ | ✔ | ✔ |
| 2D shapes, 3D primitives | ✔ | ✔ | ✔ | ✔ |
| Cameras (2D/3D) | ✔ | ✔ | ✔ | ✔ |
| Text and fonts | ✔ | ✔ | ✔ | ✔ |
| Textures | ✔ | ✔ | ✔ | ✔ |
| Audio (sounds, music) | ✔ | ✔ | ✔ | ✔ |
| File helpers | ✔ | ✔ | ✔ | ✔ |
| Physics, models, GUI, particles, animation, networking | C API only | — | — | — |

The C++ binding is one header, so it also gives access to the full C API in the
`src/` headers (`vlcrlib.h` and friends); the FFI-based bindings cover the
cross-language surface above.
