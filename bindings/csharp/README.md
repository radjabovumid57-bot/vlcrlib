# vlcrlib — C# binding

A `net8.0` class library that P/Invokes the stable `vlcr_ffi` C ABI
(`bindings/ffi/vlcr_ffi.h`). No NuGet dependencies.

```csharp
using Vlcrlib;

using var window = new Window(1280, 720, "Hello vlcrlib", targetFps: 60);
using var font = Font.Default();

while (window.Running)
{
    using (window.BeginFrame())
    {
        Engine.Clear(Colors.RayWhite);
        Draw.Rectangle(40, 40, 200, 120, Colors.Orange);
        Draw.Circle(400, 300, 80, Colors.SkyBlue);
        font.Draw("Hello from C#", 40, 200, 30);
    }
}
```

## Build

```powershell
# native library, from the repo root
scripts/build_bindings.ps1
# or
cmake --build build-bindings --config Release --target vlcr_ffi

# managed library + example
dotnet build bindings/csharp/Vlcrlib/Vlcrlib.csproj -c Release
dotnet build bindings/csharp/examples/Hello/Hello.csproj -c Release
```

## Run

```powershell
# from a directory containing shaders/ (build-bindings/Release does)
$env:VLCR_FRAMES = 120          # optional: exit after 120 frames
bindings/csharp/examples/Hello/bin/Release/net8.0/HelloVlcrlib.exe
```

The resolver (`NativeResolver.cs`) looks for the native library in
`VLCRLIB_LIBRARY`, the application directory, `runtimes/<rid>/native`, and the
CMake build tree. It preloads `SDL3.dll` / `vulkan-1.dll` from the same
directory, so a build-tree run needs no `PATH` changes.

## API

| Type | Purpose |
| --- | --- |
| `Window` | owns the window; `BeginFrame()` returns a `Frame` scope |
| `Engine` | static window/frame state (`Version`, `Clear`, `Fps`, `FrameTime`, `BeginMode2D/3D`, ...) |
| `Draw` | 2D and 3D primitives |
| `Text`, `Font` | text drawing and measurement |
| `Texture` | textures |
| `Audio`, `Audio.Sound`, `Audio.Music` | playback |
| `Input`, `MouseButton`, `Keys` | polling (SDL3 scancodes) |
| `Files` | file helpers |
| `Color`, `Colors`, `Vector2`, `Vector3`, `Rectangle`, `Camera2D`, `Camera3D` | value types |

`Font`, `Texture`, `Sound`, `Music` implement `IDisposable`.

`NativeMethods.g.cs` and `Keys.g.cs` are generated — edit
`bindings/ffi/vlcr_ffi.h` and re-run `python bindings/tools/gen_bindings.py`.

## Notes

* String *returns* are marshalled by hand (`IntPtr` + `Marshal.PtrToStringUTF8`)
  because .NET's `LPUTF8Str` return marshalling frees memory it does not own and
  corrupts the heap. Keep that pattern for new string-returning functions.
* The native library is single-threaded around the window: call the API from
  the thread that created the `Window`.
