package org.vlcrlib;

/** Immutable 8-bit RGBA color. */
public final class Color {

    public final int r;
    public final int g;
    public final int b;
    public final int a;

    public Color(int r, int g, int b) {
        this(r, g, b, 255);
    }

    public Color(int r, int g, int b, int a) {
        this.r = r & 0xFF;
        this.g = g & 0xFF;
        this.b = b & 0xFF;
        this.a = a & 0xFF;
    }

    /** 0xAABBGGRR, the layout the native layer expects. */
    public int packed() {
        return r | (g << 8) | (b << 16) | (a << 24);
    }

    /** Build from 0xRRGGBB. */
    public static Color fromHex(int rgb) {
        return fromHex(rgb, 255);
    }

    public static Color fromHex(int rgb, int alpha) {
        return new Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
    }

    public Color withAlpha(int alpha) {
        return new Color(r, g, b, alpha);
    }

    @Override
    public String toString() {
        return "Color(" + r + ", " + g + ", " + b + ", " + a + ")";
    }
}
