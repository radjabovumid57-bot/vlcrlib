package org.vlcrlib;

/** Immutable 3D vector. */
public final class Vector3 {

    public final float x;
    public final float y;
    public final float z;

    public Vector3(float x, float y, float z) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public static Vector3 zero() {
        return new Vector3(0f, 0f, 0f);
    }

    public static Vector3 unitY() {
        return new Vector3(0f, 1f, 0f);
    }

    public Vector3 add(Vector3 other) {
        return new Vector3(x + other.x, y + other.y, z + other.z);
    }

    public Vector3 sub(Vector3 other) {
        return new Vector3(x - other.x, y - other.y, z - other.z);
    }

    public Vector3 scale(float factor) {
        return new Vector3(x * factor, y * factor, z * factor);
    }

    public float dot(Vector3 other) {
        return x * other.x + y * other.y + z * other.z;
    }

    public Vector3 cross(Vector3 other) {
        return new Vector3(y * other.z - z * other.y,
                           z * other.x - x * other.z,
                           x * other.y - y * other.x);
    }

    public float length() {
        return (float) Math.sqrt(x * x + y * y + z * z);
    }

    public Vector3 normalized() {
        float length = length();
        return length > 1e-8f ? new Vector3(x / length, y / length, z / length) : zero();
    }

    @Override
    public String toString() {
        return "Vector3(" + x + ", " + y + ", " + z + ")";
    }
}
