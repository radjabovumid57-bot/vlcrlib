using System;
using System.Runtime.InteropServices;

namespace Vlcrlib
{
    /// <summary>Window, frame loop and global engine state.</summary>
    public static class Engine
    {
        /// <summary>Version of the binding ABI exposed by the native library.</summary>
        public static string Version => Marshal.PtrToStringUTF8(Native.version()) ?? string.Empty;

        public static bool Init(int width, int height, string title = "vlcrlib") =>
            Native.init(width, height, title) != 0;

        public static void Shutdown() => Native.shutdown();

        public static bool IsReady => Native.isReady() != 0;

        public static bool ShouldClose() => Native.shouldClose() != 0;

        public static void BeginDrawing() => Native.beginDrawing();

        public static void EndDrawing() => Native.endDrawing();

        public static void Clear(Color color) => Native.clear(color.Packed);

        public static void SetTargetFps(int fps) => Native.setTargetFps(fps);

        /// <summary>Frames rendered during the last second.</summary>
        public static int Fps => Native.fps();

        /// <summary>Seconds spent in the previous frame.</summary>
        public static float FrameTime => Native.frameTime();

        /// <summary>Seconds since the window was created.</summary>
        public static double ElapsedTime => Native.time();

        public static (int Width, int Height) ScreenSize
        {
            get
            {
                Native.windowSize(out int width, out int height);
                return (width, height);
            }
        }

        public static void SetWindowTitle(string title) => Native.setWindowTitle(title);
        public static void SetWindowSize(int width, int height) => Native.setWindowSize(width, height);
        public static void SetWindowPosition(int x, int y) => Native.setWindowPosition(x, y);
        public static void ToggleFullscreen() => Native.toggleFullscreen();
        public static void SetExitKey(int key) => Native.setExitKey(key);

        /// <summary>Native window handle (HWND on Windows), or IntPtr.Zero.</summary>
        public static IntPtr NativeWindow => Native.nativeWindow();

        public static void BeginMode2D(Camera2D camera) =>
            Native.beginMode2D(camera.Offset.X, camera.Offset.Y, camera.Target.X, camera.Target.Y,
                               camera.Rotation, camera.Zoom);

        public static void EndMode2D() => Native.endMode2D();

        public static void BeginMode3D(Camera3D camera) =>
            Native.beginMode3D(camera.Position.X, camera.Position.Y, camera.Position.Z,
                               camera.Target.X, camera.Target.Y, camera.Target.Z,
                               camera.Up.X, camera.Up.Y, camera.Up.Z,
                               camera.FovY, camera.NearPlane, camera.FarPlane);

        public static void EndMode3D() => Native.endMode3D();
    }

    /// <summary>Owns the window and draws frames; dispose to shut down.</summary>
    public sealed class Window : IDisposable
    {
        private bool _disposed;

        public Window(int width, int height, string title = "vlcrlib", int targetFps = 0)
        {
            if (!Engine.Init(width, height, title))
                throw new InvalidOperationException("vlcrlib: failed to create the window");
            if (targetFps > 0)
                Engine.SetTargetFps(targetFps);
        }

        public bool IsReady => Engine.IsReady;

        /// <summary>True until the user closes the window.</summary>
        public bool Running => !Engine.ShouldClose();

        public (int Width, int Height) Size => Engine.ScreenSize;

        /// <summary>Begins a frame; dispose the scope to end it.</summary>
        public Frame BeginFrame() => new Frame();

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            if (Engine.IsReady)
                Engine.Shutdown();
        }

        /// <summary><c>using (window.BeginFrame()) { ... }</c></summary>
        public readonly struct Frame : IDisposable
        {
            public Frame() => Engine.BeginDrawing();
            public void Dispose() => Engine.EndDrawing();
        }
    }
}
