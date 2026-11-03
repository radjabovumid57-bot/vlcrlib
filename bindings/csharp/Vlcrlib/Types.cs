using System;

namespace Vlcrlib
{
    /// <summary>8-bit RGBA color.</summary>
    public readonly struct Color : IEquatable<Color>
    {
        public readonly byte R;
        public readonly byte G;
        public readonly byte B;
        public readonly byte A;

        public Color(byte r, byte g, byte b, byte a = 255)
        {
            R = r; G = g; B = b; A = a;
        }

        /// <summary>Packed as 0xAABBGGRR, the layout the native layer expects.</summary>
        public uint Packed => (uint)(R | (G << 8) | (B << 16) | (A << 24));

        /// <summary>Build from 0xRRGGBB.</summary>
        public static Color FromHex(uint rgb, byte alpha = 255) =>
            new Color((byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb, alpha);

        public bool Equals(Color other) => Packed == other.Packed;
        public override bool Equals(object? obj) => obj is Color c && Equals(c);
        public override int GetHashCode() => (int)Packed;
        public override string ToString() => $"Color({R}, {G}, {B}, {A})";

        public static bool operator ==(Color a, Color b) => a.Equals(b);
        public static bool operator !=(Color a, Color b) => !a.Equals(b);
    }

    public readonly struct Vector2 : IEquatable<Vector2>
    {
        public readonly float X;
        public readonly float Y;

        public Vector2(float x, float y) { X = x; Y = y; }

        public static Vector2 Zero => new Vector2(0f, 0f);

        public float Length() => MathF.Sqrt(X * X + Y * Y);

        public Vector2 Normalized()
        {
            float len = Length();
            return len > 1e-8f ? new Vector2(X / len, Y / len) : Zero;
        }

        public float Dot(Vector2 other) => X * other.X + Y * other.Y;

        public static Vector2 operator +(Vector2 a, Vector2 b) => new Vector2(a.X + b.X, a.Y + b.Y);
        public static Vector2 operator -(Vector2 a, Vector2 b) => new Vector2(a.X - b.X, a.Y - b.Y);
        public static Vector2 operator *(Vector2 a, float s) => new Vector2(a.X * s, a.Y * s);
        public static Vector2 operator *(float s, Vector2 a) => a * s;

        public bool Equals(Vector2 other) => X.Equals(other.X) && Y.Equals(other.Y);
        public override bool Equals(object? obj) => obj is Vector2 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(X, Y);
        public override string ToString() => $"Vector2({X}, {Y})";
    }

    public readonly struct Vector3 : IEquatable<Vector3>
    {
        public readonly float X;
        public readonly float Y;
        public readonly float Z;

        public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }

        public static Vector3 Zero => new Vector3(0f, 0f, 0f);
        public static Vector3 UnitY => new Vector3(0f, 1f, 0f);

        public float Length() => MathF.Sqrt(X * X + Y * Y + Z * Z);

        public Vector3 Normalized()
        {
            float len = Length();
            return len > 1e-8f ? new Vector3(X / len, Y / len, Z / len) : Zero;
        }

        public float Dot(Vector3 other) => X * other.X + Y * other.Y + Z * other.Z;

        public Vector3 Cross(Vector3 other) =>
            new Vector3(Y * other.Z - Z * other.Y,
                        Z * other.X - X * other.Z,
                        X * other.Y - Y * other.X);

        public static Vector3 operator +(Vector3 a, Vector3 b) =>
            new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);

        public static Vector3 operator -(Vector3 a, Vector3 b) =>
            new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);

        public static Vector3 operator *(Vector3 a, float s) => new Vector3(a.X * s, a.Y * s, a.Z * s);
        public static Vector3 operator *(float s, Vector3 a) => a * s;

        public bool Equals(Vector3 other) => X.Equals(other.X) && Y.Equals(other.Y) && Z.Equals(other.Z);
        public override bool Equals(object? obj) => obj is Vector3 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z);
        public override string ToString() => $"Vector3({X}, {Y}, {Z})";
    }

    public readonly struct Rectangle
    {
        public readonly float X;
        public readonly float Y;
        public readonly float Width;
        public readonly float Height;

        public Rectangle(float x, float y, float width, float height)
        {
            X = x; Y = y; Width = width; Height = height;
        }
    }

    public readonly struct Camera2D
    {
        public readonly Vector2 Offset;
        public readonly Vector2 Target;
        public readonly float Rotation;
        public readonly float Zoom;

        public Camera2D(Vector2 offset, Vector2 target, float rotation = 0f, float zoom = 1f)
        {
            Offset = offset; Target = target; Rotation = rotation; Zoom = zoom;
        }
    }

    public readonly struct Camera3D
    {
        public readonly Vector3 Position;
        public readonly Vector3 Target;
        public readonly Vector3 Up;
        public readonly float FovY;
        public readonly float NearPlane;
        public readonly float FarPlane;

        public Camera3D(Vector3 position, Vector3 target, Vector3? up = null,
                        float fovY = 45f, float nearPlane = 0.1f, float farPlane = 1000f)
        {
            Position = position;
            Target = target;
            Up = up ?? Vector3.UnitY;
            FovY = fovY;
            NearPlane = nearPlane;
            FarPlane = farPlane;
        }
    }

    /// <summary>Named colors, matching the C header.</summary>
    public static class Colors
    {
        public static readonly Color LightGray = new Color(200, 200, 200);
        public static readonly Color Gray = new Color(130, 130, 130);
        public static readonly Color DarkGray = new Color(80, 80, 80);
        public static readonly Color Yellow = new Color(253, 249, 0);
        public static readonly Color Gold = new Color(255, 203, 0);
        public static readonly Color Orange = new Color(255, 161, 0);
        public static readonly Color Pink = new Color(255, 109, 194);
        public static readonly Color Red = new Color(230, 41, 55);
        public static readonly Color Maroon = new Color(190, 33, 55);
        public static readonly Color Green = new Color(0, 228, 48);
        public static readonly Color Lime = new Color(0, 158, 47);
        public static readonly Color DarkGreen = new Color(0, 117, 44);
        public static readonly Color SkyBlue = new Color(102, 191, 255);
        public static readonly Color Blue = new Color(0, 121, 241);
        public static readonly Color DarkBlue = new Color(0, 82, 172);
        public static readonly Color Purple = new Color(200, 122, 255);
        public static readonly Color Violet = new Color(135, 60, 190);
        public static readonly Color DarkPurple = new Color(112, 31, 126);
        public static readonly Color Beige = new Color(211, 176, 131);
        public static readonly Color Brown = new Color(127, 106, 79);
        public static readonly Color DarkBrown = new Color(76, 63, 47);
        public static readonly Color White = new Color(255, 255, 255);
        public static readonly Color Black = new Color(0, 0, 0);
        public static readonly Color Blank = new Color(0, 0, 0, 0);
        public static readonly Color Magenta = new Color(255, 0, 255);
        public static readonly Color RayWhite = new Color(245, 245, 245);
    }
}
