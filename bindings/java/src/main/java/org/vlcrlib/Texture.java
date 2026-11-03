package org.vlcrlib;

/** A 2D texture loaded from disk. */
public final class Texture implements AutoCloseable {

    private int handle;

    private Texture(int handle) {
        if (handle <= 0) {
            throw new IllegalArgumentException("invalid texture handle");
        }
        this.handle = handle;
    }

    public static Texture load(String path) {
        int handle = Native.textureLoad(path);
        if (handle <= 0) {
            throw new IllegalStateException("vlcrlib: cannot load texture '" + path + "'");
        }
        return new Texture(handle);
    }

    public int handle() {
        return handle;
    }

    public int width() {
        int[] width = new int[1];
        int[] height = new int[1];
        Native.textureSize(handle, width, height);
        return width[0];
    }

    public int height() {
        int[] width = new int[1];
        int[] height = new int[1];
        Native.textureSize(handle, width, height);
        return height[0];
    }

    public void draw(float x, float y, Color tint) {
        Native.drawTexture(handle, (int) x, (int) y, tint.packed());
    }

    public void draw(Vector2 position, float rotation, float scale, Color tint) {
        Native.drawTextureEx(handle, position.x, position.y, rotation, scale, tint.packed());
    }

    public void drawRegion(Rectangle source, Vector2 position, Color tint) {
        Native.drawTextureRegion(handle, source.x, source.y, source.width, source.height,
                                 position.x, position.y, tint.packed());
    }

    @Override
    public void close() {
        if (handle > 0) {
            Native.textureUnload(handle);
            handle = 0;
        }
    }
}
