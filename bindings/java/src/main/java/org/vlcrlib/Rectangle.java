package org.vlcrlib;

/** Axis-aligned rectangle in screen space. */
public final class Rectangle {

    public final float x;
    public final float y;
    public final float width;
    public final float height;

    public Rectangle(float x, float y, float width, float height) {
        this.x = x;
        this.y = y;
        this.width = width;
        this.height = height;
    }

    @Override
    public String toString() {
        return "Rectangle(" + x + ", " + y + ", " + width + ", " + height + ")";
    }
}
