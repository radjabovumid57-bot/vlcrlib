using System;

namespace Vlcrlib
{
    /// <summary>Immediate-mode drawing.</summary>
    public static class Draw
    {
        public static void Pixel(int x, int y, Color color) =>
            Native.drawPixel(x, y, color.Packed);

        public static void Line(int x0, int y0, int x1, int y1, Color color) =>
            Native.drawLine(x0, y0, x1, y1, color.Packed);

        public static void Line(Vector2 start, Vector2 end, float thickness, Color color) =>
            Native.drawLineEx(start.X, start.Y, end.X, end.Y, thickness, color.Packed);

        public static void Circle(float centerX, float centerY, float radius, Color color) =>
            Native.drawCircle((int)centerX, (int)centerY, radius, color.Packed);

        public static void CircleLines(float centerX, float centerY, float radius, Color color) =>
            Native.drawCircleLines((int)centerX, (int)centerY, radius, color.Packed);

        public static void Rectangle(float x, float y, float width, float height, Color color) =>
            Native.drawRectangle((int)x, (int)y, (int)width, (int)height, color.Packed);

        public static void RectangleLines(float x, float y, float width, float height, Color color) =>
            Native.drawRectangleLines((int)x, (int)y, (int)width, (int)height, color.Packed);

        public static void Rectangle(Rectangle rectangle, Color color) =>
            Rectangle(rectangle.X, rectangle.Y, rectangle.Width, rectangle.Height, color);

        public static void RectanglePro(Rectangle rectangle, Vector2 origin, float rotation, Color color) =>
            Native.drawRectanglePro(rectangle.X, rectangle.Y, rectangle.Width, rectangle.Height,
                                    origin.X, origin.Y, rotation, color.Packed);

        public static void Triangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) =>
            Native.drawTriangle(v1.X, v1.Y, v2.X, v2.Y, v3.X, v3.Y, color.Packed);

        public static void Poly(Vector2 center, int sides, float radius, float rotation, Color color) =>
            Native.drawPoly(center.X, center.Y, sides, radius, rotation, color.Packed);

        public static void Cube(Vector3 position, Vector3 size, Color color) =>
            Native.drawCube(position.X, position.Y, position.Z,
                            size.X, size.Y, size.Z, color.Packed);

        public static void Sphere(Vector3 center, float radius, Color color) =>
            Native.drawSphere(center.X, center.Y, center.Z, radius, color.Packed);

        public static void Line3D(Vector3 start, Vector3 end, Color color) =>
            Native.drawLine3D(start.X, start.Y, start.Z, end.X, end.Y, end.Z, color.Packed);

        public static void Grid(int slices = 10, float spacing = 1f) =>
            Native.drawGrid(slices, spacing);
    }

    /// <summary>Keyboard, mouse and gamepad polling.</summary>
    public static class Input
    {
        public static bool KeyDown(int key) => Native.keyDown(key) != 0;
        public static bool KeyPressed(int key) => Native.keyPressed(key) != 0;
        public static bool KeyReleased(int key) => Native.keyReleased(key) != 0;

        public static bool MouseDown(MouseButton button = MouseButton.Left) =>
            Native.mouseDown((int)button) != 0;

        public static bool MousePressed(MouseButton button = MouseButton.Left) =>
            Native.mousePressed((int)button) != 0;

        public static bool MouseReleased(MouseButton button = MouseButton.Left) =>
            Native.mouseReleased((int)button) != 0;

        public static Vector2 MousePosition
        {
            get
            {
                Native.mousePosition(out float x, out float y);
                return new Vector2(x, y);
            }
        }

        public static float MouseWheel => Native.mouseWheel();

        public static bool GamepadAvailable(int index = 0) => Native.gamepadAvailable(index) != 0;
        public static bool GamepadDown(int index, int button) => Native.gamepadDown(index, button) != 0;
        public static float GamepadAxis(int index, int axis) => Native.gamepadAxis(index, axis);
    }

    public enum MouseButton
    {
        Left = 1,
        Middle = 2,
        Right = 3,
    }

    /// <summary>Filesystem helpers.</summary>
    public static class Files
    {
        public static bool Exists(string path) => Native.fileExists(path) != 0;
        public static bool DirectoryExists(string path) => Native.directoryExists(path) != 0;
        public static uint Size(string path) => Native.fileSize(path);
    }
}
