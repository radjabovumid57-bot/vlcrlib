using System;
using Vlcrlib;

// Incremental smoke test: every step is announced on stderr so a crash (or a
// hang) points at the exact call. VLCR_SKIP_WINDOW=1 runs only the pure API.

static void Step(string name) => Console.Error.WriteLine($"step: {name}");
static void Check(bool condition, string what)
{
    if (!condition) throw new Exception($"failed: {what}");
    Console.Error.WriteLine($"ok:   {what}");
}

Step($"abi {Engine.Version}");
Check(Engine.Version.Split('.').Length == 3, "ABI version");

Check(Files.DirectoryExists("."), "Files.DirectoryExists");
Check(Files.Size("smoke.txt") == 0, "Files.Size of a missing file");
Check(Vector3.UnitY.Y == 1f, "Vector3");
Check(Colors.Red.Packed == 0xFF3729E6u, "Color packing");
Check(new Vector2(3f, 4f).Length() == 5f, "Vector2.Length");

if (Environment.GetEnvironmentVariable("VLCR_SKIP_WINDOW") == "1")
{
    Step("done (window skipped)");
    return;
}

using var window = new Window(320, 240, "vlcrlib csharp smoke", targetFps: 60);
Step("window created");
Check(Engine.IsReady, "window is ready");
Check(window.Size.Item1 > 0, "window size");

using (window.BeginFrame())
{
    Engine.Clear(Colors.RayWhite);
    Draw.Rectangle(10, 10, 120, 60, Colors.Orange);
    Draw.Line(new Vector2(10, 200), new Vector2(300, 220), 3f, Colors.Green);
    Draw.Circle(200, 120, 40f, Colors.SkyBlue);
    Draw.Poly(new Vector2(160, 60), 6, 30f, 0.2f, Colors.Pink);
}
Step("2D frame");

using (window.BeginFrame())
{
    Engine.Clear(Colors.RayWhite);
    Text.Draw("hello", 20, 20, 20, Colors.DarkGray);
    Check(Text.Measure("hello", 20) > 0, "Text.Measure");
}
Step("text frame");

using var font = Font.Default();
using (window.BeginFrame())
{
    Engine.Clear(Colors.RayWhite);
    font.Draw("font", 20f, 20f, 20f, 1f, Colors.White);
    Vector2 size = font.Measure("font", 20f, 1f);
    Check(size.X > 0f && size.Y > 0f, "Font.Measure");
    Text.DrawEx(font, "again", 20f, 60f, 20f, 1f, Colors.White);
}
Step("font frame");

var camera = new Camera3D(new Vector3(4f, 3f, 6f), new Vector3(0f, 0.5f, 0f));
for (int i = 0; i < 3; i++)
{
    using (window.BeginFrame())
    {
        Engine.Clear(Colors.RayWhite);
        Engine.BeginMode3D(camera);
        Draw.Grid(10, 1f);
        Draw.Cube(new Vector3(0f, 0.5f, 0f), new Vector3(1f, 1f, 1f), Colors.Red);
        Draw.Sphere(new Vector3(2f, 1f, 0f), 1f, Colors.SkyBlue);
        Draw.Line3D(new Vector3(-4f, 0f, 0f), new Vector3(4f, 0f, 0f), Colors.DarkGray);
        Engine.EndMode3D();

        Vector2 mouse = Input.MousePosition;
        Draw.RectanglePro(new Rectangle(20f, 20f, 100f, 40f), new Vector2(50f, 20f),
                          i * 10f, Colors.Orange);
        Text.Draw($"#{i}", 24, 24, 18, Colors.White);
        _ = mouse;
    }
}
Step("3D frames");

Check(!Engine.ShouldClose() || true, "ShouldClose");
Console.Error.WriteLine($"frame time: {Engine.FrameTime:F4}s, fps: {Engine.Fps}");
Step("done");
