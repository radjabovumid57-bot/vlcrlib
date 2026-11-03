package org.vlcrlib;

/** Immutable 2D vector. */
public final class Vector2 {

    public final float x;
    public final float y;

    public Vector2(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public static Vector2 zero() {
        return new Vector2(0f, 0f);
    }

    public Vector2 add(Vector2 other) {
        return new Vector2(x + other.x, y + other.y);
    }

    public Vector2 sub(Vector2 other) {
        return new Vector2(x - other.x, y - other.y);
    }

    public Vector2 scale(float factor) {
        return new Vector2(x * factor, y * factor);
    }

    public float dot(Vector2 other) {
        return x * other.x + y * other.y;
    }

    public float length() {
        return (float) Math.sqrt(x * x + y * y);
    }

    public Vector2 normalized() {
        float length = length();
        return length > 1e-8f ? new Vector2(x / length, y / length) : zero();
    }

    @Override
    public String toString() {
        return "Vector2(" + x + ", " + y + ")";
    }
}
