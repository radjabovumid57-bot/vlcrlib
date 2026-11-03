package org.vlcrlib;

/** A baked bitmap font. */
public final class Font implements AutoCloseable {

    private int handle;

    private Font(int handle) {
        if (handle <= 0) {
            throw new IllegalArgumentException("invalid font handle");
        }
        this.handle = handle;
    }

    /** Load a .ttf/.otf, baked at {@code size} pixels. */
    public static Font load(String path, int size) {
        int handle = Native.fontLoad(path, size);
        if (handle <= 0) {
            throw new IllegalStateException("vlcrlib: cannot load font '" + path + "'");
        }
        return new Font(handle);
    }

    /** The font baked by the engine on first use. */
    public static Font defaultFont() {
        int handle = Native.fontDefault();
        if (handle <= 0) {
            throw new IllegalStateException("vlcrlib: no default font available");
        }
        return new Font(handle);
    }

    public int handle() {
        return handle;
    }

    public Vector2 measure(String text, float size, float spacing) {
        float[] width = new float[1];
        float[] height = new float[1];
        Native.measureTextEx(handle, text, size, spacing, width, height);
        return new Vector2(width[0], height[0]);
    }

    public void draw(String text, float x, float y, float size, float spacing, Color tint) {
        Native.drawTextEx(handle, text, x, y, size, spacing, tint.packed());
    }

    @Override
    public void close() {
        if (handle > 0) {
            Native.fontUnload(handle);
            handle = 0;
        }
    }
}
