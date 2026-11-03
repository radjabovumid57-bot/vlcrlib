# vlcrlib — C++ binding

`vlcrlib.hpp` is a header-only, zero-overhead wrapper over vlcrlib's C API. It
links the static library like any C program — no FFI, no dynamic loading, no
marshalling — and adds RAII (`Window`, `Frame`, `Font`, `Texture`, `Sound`,
`Music`), typed vectors/colors and namespaced drawing calls.

```cpp
#include <vlcrlib/vlcrlib.hpp>

int main() {
    vlcr::Window window(1280, 720, "Hello vlcrlib");
    vlcr::Font   font = vlcr::Font::default_font();

    while (!window.should_close()) {
        vlcr::Frame frame;                         // BeginDrawing / EndDrawing
        vlcr::clear(vlcr::colors::raywhite);
        vlcr::draw_rectangle(40, 40, 200, 120, vlcr::colors::orange);
        vlcr::draw_circle(400, 300, 80, vlcr::colors::skyblue);
        font.draw("Hello from C++", 40, 200, 30);
    }
}
```

## Build

```cmake
add_subdirectory(path/to/vlcrlib)              # or find_package(vlcrlib)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE vlcrlib)
target_include_directories(app PRIVATE path/to/vlcrlib/bindings/cpp/include)
```

When the bindings are enabled (`-DVLCRLIB_BUILD_BINDINGS=ON`), CMake provides
the `vlcrlib_cpp` interface target as well:

```cmake
target_link_libraries(app PRIVATE vlcrlib_cpp)
```

See `examples/hello.cpp` for a complete program (3D scene, 2D overlay, input).

## Notes

- Requires C++17 (`inline constexpr` colors, `std::exchange`).
- `Window`, `Font`, `Texture`, `Sound` and `Music` release their resource in
  the destructor and are move-only.
- Vectors convert implicitly to the C `Vector2`/`Vector3` types, colors to
  `Color`, rectangles to `Rectangle`, so the wrapper can be mixed freely with
  raw C calls from `vlcrlib.h`.
- The full C API remains reachable: the wrapper includes `vlcrlib.h`, so
  physics, models, GUI, particles and networking are one direct call away.
