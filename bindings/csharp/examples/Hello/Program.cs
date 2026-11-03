using System;
using System.Globalization;
using Vlcrlib;

// A small tour of the binding: a 2D overlay, a 3D scene and the frame loop.
// Set VLCR_FRAMES=120 to make it exit after 120 frames (used by smoke tests).

int frameLimit = int.TryParse(Environment.GetEnvironmentVariable("VLCR_FRAMES"),
                             NumberStyles.Integer, CultureInfo.InvariantCulture, out int n)
    ? n
    : 0;

using var window = new Window(960, 540, "vlcrlib — C# binding", targetFps: 60);

using var font = Font.Default();

Camera3D camera = new Camera3D(
    position: new Vector3(6f, 4f, 8f),
    target: new Vector3(0f, 1f, 0f),
    up: Vector3.UnitY,
    fovY: 55f,
    nearPlane: 0.1f,
    farPlane: 200f);

float angle = 0f;
int frame = 0;

Console.WriteLine($"vlcrlib C# binding, ABI {Engine.Version}");

while (window.Running)
{
    float dt = Engine.FrameTime;
    angle += 40f * dt;

    using (window.BeginFrame())
    {
        Engine.Clear(Colors.RayWhite);

        Engine.BeginMode3D(camera);
        {
            Draw.Grid(20, 1f);
            Draw.Cube(new Vector3(-2f, 0.5f, 0f), new Vector3(1f, 1f, 1f), Colors.Red);
            Draw.Sphere(new Vector3(1.5f, 1f, 0f), 1f, Colors.SkyBlue);
            Draw.Line3D(new Vector3(0f, 0.05f, -6f), new Vector3(0f, 0.05f, 6f), Colors.DarkGray);
        }
        Engine.EndMode3D();

        // A 2D box whose rotation uses the CPU-side math (no FFI).
        Vector2 pivot = new Vector2(140f, 110f);
        Draw.RectanglePro(new Rectangle(pivot.X - 60f, pivot.Y - 30f, 120f, 60f),
                          new Vector2(60f, 30f), angle, Colors.Orange);

        Draw.Rectangle(20f, 20f, 260f, 54f, new Color(20, 20, 30, 190));
        font.Draw("vlcrlib + C#", 34f, 34f, 24f, 1f, Colors.White);

        Text.Draw($"frame {frame}  {Engine.Fps} fps  {Engine.FrameTime * 1000f:F1} ms",
                  34, 500, 18, Colors.DarkGray);

        Vector2 mouse = Input.MousePosition;
        Draw.Circle(mouse.X, mouse.Y, 8f, Colors.Green);
    }

    if (Input.KeyDown(Keys.Escape))
        break;

    frame++;
    if (frameLimit > 0 && frame >= frameLimit)
        break;
}

Console.WriteLine($"vlcrlib C# binding finished after {frame} frames");
