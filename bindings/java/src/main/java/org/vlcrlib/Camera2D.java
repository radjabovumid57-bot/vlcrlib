package org.vlcrlib;

/** 2D camera for {@link Vlcrlib#beginMode2D(Camera2D)}. */
public final class Camera2D {

    public final Vector2 offset;
    public final Vector2 target;
    public final float rotation;
    public final float zoom;

    public Camera2D(Vector2 offset, Vector2 target, float rotation, float zoom) {
        this.offset = offset;
        this.target = target;
        this.rotation = rotation;
        this.zoom = zoom;
    }

    public Camera2D(Vector2 offset, Vector2 target) {
        this(offset, target, 0f, 1f);
    }
}
