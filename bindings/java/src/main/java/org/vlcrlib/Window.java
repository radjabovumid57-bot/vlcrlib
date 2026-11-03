package org.vlcrlib;

/**
 * Owns the window and the frame loop; closing it shuts the engine down.
 *
 * <pre>{@code
 * try (Window window = new Window(1280, 720, "Hello")) {
 *     while (window.running()) {
 *         try (Vlcrlib.Frame frame = window.frame()) {
 *             Vlcrlib.clear(Colors.RAY_WHITE);
 *         }
 *     }
 * }
 * }</pre>
 */
public final class Window implements AutoCloseable {

    private boolean closed;

    public Window(int width, int height, String title) {
        this(width, height, title, 0);
    }

    public Window(int width, int height, String title, int targetFps) {
        if (!Vlcrlib.init(width, height, title)) {
            throw new IllegalStateException("vlcrlib: failed to create the window");
        }
        if (targetFps > 0) {
            Vlcrlib.setTargetFps(targetFps);
        }
    }

    public boolean ready() {
        return Vlcrlib.isReady();
    }

    public boolean running() {
        return !Vlcrlib.shouldClose();
    }

    public int width() {
        return Vlcrlib.width();
    }

    public int height() {
        return Vlcrlib.height();
    }

    /** Begins a frame; close the returned value to end it. */
    public Vlcrlib.Frame frame() {
        return new Vlcrlib.Frame();
    }

    @Override
    public void close() {
        if (!closed) {
            closed = true;
            if (Vlcrlib.isReady()) {
                Vlcrlib.shutdown();
            }
        }
    }
}
