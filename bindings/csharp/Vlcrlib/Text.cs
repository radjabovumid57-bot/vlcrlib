using System;

namespace Vlcrlib
{
    /// <summary>A baked bitmap font.</summary>
    public sealed class Font : IDisposable
    {
        private int _handle;

        internal Font(int handle)
        {
            if (handle <= 0) throw new ArgumentException("invalid font handle", nameof(handle));
            _handle = handle;
        }

        /// <summary>Load a .ttf/.otf, baked at <paramref name="size"/> pixels.</summary>
        public static Font Load(string path, int size = 32)
        {
            int handle = Native.fontLoad(path, size);
            if (handle <= 0) throw new InvalidOperationException($"vlcrlib: cannot load font '{path}'");
            return new Font(handle);
        }

        /// <summary>The font baked by the engine on first use.</summary>
        public static Font Default()
        {
            int handle = Native.fontDefault();
            if (handle <= 0) throw new InvalidOperationException("vlcrlib: no default font available");
            return new Font(handle);
        }

        public int Handle => _handle;

        public Vector2 Measure(string text, float size = 20f, float spacing = 1f)
        {
            Native.measureTextEx(_handle, text, size, spacing, out float width, out float height);
            return new Vector2(width, height);
        }

        public void Draw(string text, float x, float y, float size = 20f,
                         float spacing = 1f, Color? tint = null)
        {
            Native.drawTextEx(_handle, text, x, y, size, spacing, (tint ?? Colors.White).Packed);
        }

        public void Dispose()
        {
            if (_handle <= 0) return;
            Native.fontUnload(_handle);
            _handle = 0;
        }
    }

    /// <summary>Text drawing.</summary>
    public static class Text
    {
        /// <summary>Draw with the built-in font.</summary>
        public static void Draw(string text, int x, int y, int size = 20, Color? color = null) =>
            Native.drawText(text, x, y, size, (color ?? Colors.White).Packed);

        public static int Measure(string text, int size = 20) => Native.measureText(text, size);

        public static void DrawEx(Font font, string text, float x, float y, float size = 20f,
                                  float spacing = 1f, Color? color = null) =>
            font.Draw(text, x, y, size, spacing, color);

        public static Vector2 MeasureEx(Font font, string text, float size = 20f, float spacing = 1f) =>
            font.Measure(text, size, spacing);
    }

    /// <summary>A 2D texture loaded from disk.</summary>
    public sealed class Texture : IDisposable
    {
        private int _handle;

        private Texture(int handle)
        {
            if (handle <= 0) throw new ArgumentException("invalid texture handle", nameof(handle));
            _handle = handle;
        }

        public static Texture Load(string path)
        {
            int handle = Native.textureLoad(path);
            if (handle <= 0) throw new InvalidOperationException($"vlcrlib: cannot load texture '{path}'");
            return new Texture(handle);
        }

        public int Handle => _handle;

        public (int Width, int Height) Size
        {
            get
            {
                Native.textureSize(_handle, out int width, out int height);
                return (width, height);
            }
        }

        public void Draw(float x, float y, Color? tint = null) =>
            Native.drawTexture(_handle, (int)x, (int)y, (tint ?? Colors.White).Packed);

        public void Draw(Vector2 position, float rotation = 0f, float scale = 1f, Color? tint = null) =>
            Native.drawTextureEx(_handle, position.X, position.Y, rotation, scale,
                                 (tint ?? Colors.White).Packed);

        public void DrawRegion(Rectangle source, Vector2 position, Color? tint = null) =>
            Native.drawTextureRegion(_handle, source.X, source.Y, source.Width, source.Height,
                                     position.X, position.Y, (tint ?? Colors.White).Packed);

        public void Dispose()
        {
            if (_handle <= 0) return;
            Native.textureUnload(_handle);
            _handle = 0;
        }
    }
}
