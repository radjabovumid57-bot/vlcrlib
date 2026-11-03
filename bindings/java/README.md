# vlcrlib — Java binding

JNI bindings for [vlcrlib](../../README.md). The Java side is a plain Gradle
project with no external dependencies; the native side is two small libraries
built by CMake:

| Library | Purpose |
| --- | --- |
| `vlcr_ffi` | stable C ABI shared by every language binding (`bindings/ffi`) |
| `vlcr_jni` | JNI glue generated from the Java declarations by `javac -h` |

## Build

```powershell
# 1. native libraries (from the repo root)
cmake -B build-bindings -DVLCRLIB_BUILD_BINDINGS=ON -DVLCRLIB_BUILD_EXAMPLES=OFF `
      -DVLCRLIB_SKIP_VCPKG=ON -DCMAKE_PREFIX_PATH="$PWD/vcpkg_installed/x64-windows"
cmake --build build-bindings --config Release --target vlcr_jni

# 2. Java classes
cd bindings/java
gradle build              # build/libs/vlcrlib.jar
```

JNI header generation is part of the CMake build (`javac -h`), so the JDK must
be discoverable: `JAVA_HOME` or `java`/`javac` on `PATH`.

## Run

```powershell
$env:VLCR_FRAMES = 120
java -cp build/classes/java/main;build/examples HelloVlcrlib
```

The native loader searches `VLCRLIB_LIBRARY`, the working directory and the
CMake build tree for `vlcr_jni.dll` (plus `vlcr_ffi.dll`, `SDL3.dll`,
`vulkan-1.dll`). The engine loads SPIR-V shaders relative to the working
directory, so run from a folder containing `shaders/` (the build tree has one
at `build-bindings/Release/shaders`).

Without Gradle, plain `javac` works just as well:

```powershell
javac -d classes (Get-ChildItem src/main/java -Recurse -Filter *.java).FullName `
                  (Get-ChildItem examples -Filter *.java).FullName
java -cp classes SmokeJava
```

## API

```java
import org.vlcrlib.*;

try (Window window = new Window(1280, 720, "Hello", 60)) {
    while (window.running()) {
        try (Vlcrlib.Frame frame = window.frame()) {
            Vlcrlib.clear(Colors.RAY_WHITE);
            Vlcrlib.drawRectangle(40, 40, 200, 120, Colors.ORANGE);
            Vlcrlib.drawText("Hello from Java", 40, 200, 30, Colors.DARK_GRAY);
        }
    }
}
```

* `Vlcrlib` — window/frame state, drawing, input, files.
* `Window`, `Font`, `Texture`, `Audio.Sound`, `Audio.Music` — `AutoCloseable`.
* `Color`, `Colors`, `Vector2`, `Vector3`, `Rectangle`, `Camera2D`, `Camera3D`.
* `Key` — SDL3 scancodes (`Key.A`, `Key.Escape`, `Key.Space`, ...).

`bindings/java/src/main/java/org/vlcrlib/Native.java` and `Key.java` are
generated; edit `bindings/ffi/vlcr_ffi.h` and re-run
`python bindings/tools/gen_bindings.py` instead of editing them.
