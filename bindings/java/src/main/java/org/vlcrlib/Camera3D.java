package org.vlcrlib;

/** 3D camera for {@link Vlcrlib#beginMode3D(Camera3D)}. */
public final class Camera3D {

    public final Vector3 position;
    public final Vector3 target;
    public final Vector3 up;
    public final float fovy;
    public final float nearPlane;
    public final float farPlane;

    public Camera3D(Vector3 position, Vector3 target, Vector3 up,
                    float fovy, float nearPlane, float farPlane) {
        this.position = position;
        this.target = target;
        this.up = up;
        this.fovy = fovy;
        this.nearPlane = nearPlane;
        this.farPlane = farPlane;
    }

    public Camera3D(Vector3 position, Vector3 target) {
        this(position, target, Vector3.unitY(), 45f, 0.1f, 1000f);
    }
}
