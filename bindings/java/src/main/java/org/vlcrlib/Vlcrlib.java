package org.vlcrlib;

/**
 * Static facade over the native library: window, frame loop, drawing, input
 * and filesystem helpers.
 *
 * <pre>{@code
 * try (Window window = new Window(1280, 720, "Hello vlcrlib")) {
 *     while (window.running()) {
 *         try (Vlcrlib.Frame frame = window.frame()) {
 *             Vlcrlib.clear(Colors.RAY_WHITE);
 *             Vlcrlib.drawRectangle(40, 40, 200, 120, Colors.ORANGE);
 *             Vlcrlib.drawText("Hello from Java", 40, 200, 30, Colors.DARK_GRAY);
 *         }
 *     }
 * }
 * }</pre>
 */
public final class Vlcrlib {

    static {
        NativeLoader.load();
    }

    private Vlcrlib() {
    }

    /** A drawing frame; close it to call {@code EndDrawing()}. */
    public static final class Frame implements AutoCloseable {

        Frame() {
            beginDrawing();
        }

        @Override
        public void close() {
            endDrawing();
        }
    }

    /* ---------------------------------------------------------------- */
    /* Engine                                                            */
    /* ---------------------------------------------------------------- */

    public static String version() {
        return Native.version();
    }

    public static boolean init(int width, int height, String title) {
        return Native.init(width, height, title) != 0;
    }

    public static void shutdown() {
        Native.shutdown();
    }

    public static boolean isReady() {
        return Native.isReady() != 0;
    }

    public static boolean shouldClose() {
        return Native.shouldClose() != 0;
    }

    public static void beginDrawing() {
        Native.beginDrawing();
    }

    public static void endDrawing() {
        Native.endDrawing();
    }

    public static void clear(Color color) {
        Native.clear(color.packed());
    }

    public static void setTargetFps(int fps) {
        Native.setTargetFps(fps);
    }

    public static int fps() {
        return Native.fps();
    }

    /** Seconds spent in the previous frame. */
    public static float frameTime() {
        return Native.frameTime();
    }

    /** Seconds since the window was created. */
    public static double time() {
        return Native.time();
    }

    public static int width() {
        int[] width = new int[1];
        int[] height = new int[1];
        Native.windowSize(width, height);
        return width[0];
    }

    public static int height() {
        int[] width = new int[1];
        int[] height = new int[1];
        Native.windowSize(width, height);
        return height[0];
    }

    public static void setWindowTitle(String title) {
        Native.setWindowTitle(title);
    }

    public static void setWindowSize(int width, int height) {
        Native.setWindowSize(width, height);
    }

    public static void setWindowPosition(int x, int y) {
        Native.setWindowPosition(x, y);
    }

    public static void toggleFullscreen() {
        Native.toggleFullscreen();
    }

    public static void setExitKey(int key) {
        Native.setExitKey(key);
    }

    public static long nativeWindow() {
        return Native.nativeWindow();
    }

    public static void beginMode2D(Camera2D camera) {
        Native.beginMode2D(camera.offset.x, camera.offset.y,
                           camera.target.x, camera.target.y,
                           camera.rotation, camera.zoom);
    }

    public static void endMode2D() {
        Native.endMode2D();
    }

    public static void beginMode3D(Camera3D camera) {
        Native.beginMode3D(camera.position.x, camera.position.y, camera.position.z,
                           camera.target.x, camera.target.y, camera.target.z,
                           camera.up.x, camera.up.y, camera.up.z,
                           camera.fovy, camera.nearPlane, camera.farPlane);
    }

    public static void endMode3D() {
        Native.endMode3D();
    }

    /* ---------------------------------------------------------------- */
    /* Drawing                                                           */
    /* ---------------------------------------------------------------- */

    public static void drawPixel(int x, int y, Color color) {
        Native.drawPixel(x, y, color.packed());
    }

    public static void drawLine(int x0, int y0, int x1, int y1, Color color) {
        Native.drawLine(x0, y0, x1, y1, color.packed());
    }

    public static void drawLine(Vector2 start, Vector2 end, float thickness, Color color) {
        Native.drawLineEx(start.x, start.y, end.x, end.y, thickness, color.packed());
    }

    public static void drawCircle(float centerX, float centerY, float radius, Color color) {
        Native.drawCircle((int) centerX, (int) centerY, radius, color.packed());
    }

    public static void drawCircleLines(float centerX, float centerY, float radius, Color color) {
        Native.drawCircleLines((int) centerX, (int) centerY, radius, color.packed());
    }

    public static void drawRectangle(float x, float y, float width, float height, Color color) {
        Native.drawRectangle((int) x, (int) y, (int) width, (int) height, color.packed());
    }

    public static void drawRectangle(Rectangle rectangle, Color color) {
        drawRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height, color);
    }

    public static void drawRectangleLines(float x, float y, float width, float height, Color color) {
        Native.drawRectangleLines((int) x, (int) y, (int) width, (int) height, color.packed());
    }

    public static void drawRectanglePro(Rectangle rectangle, Vector2 origin,
                                        float rotation, Color color) {
        Native.drawRectanglePro(rectangle.x, rectangle.y, rectangle.width, rectangle.height,
                                origin.x, origin.y, rotation, color.packed());
    }

    public static void drawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
        Native.drawTriangle(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, color.packed());
    }

    public static void drawPoly(Vector2 center, int sides, float radius, float rotation,
                                Color color) {
        Native.drawPoly(center.x, center.y, sides, radius, rotation, color.packed());
    }

    public static void drawCube(Vector3 position, Vector3 size, Color color) {
        Native.drawCube(position.x, position.y, position.z,
                        size.x, size.y, size.z, color.packed());
    }

    public static void drawSphere(Vector3 center, float radius, Color color) {
        Native.drawSphere(center.x, center.y, center.z, radius, color.packed());
    }

    public static void drawLine3D(Vector3 start, Vector3 end, Color color) {
        Native.drawLine3D(start.x, start.y, start.z, end.x, end.y, end.z, color.packed());
    }

    public static void drawGrid(int slices, float spacing) {
        Native.drawGrid(slices, spacing);
    }

    /* ---------------------------------------------------------------- */
    /* Text                                                              */
    /* ---------------------------------------------------------------- */

    public static void drawText(String text, int x, int y, int fontSize, Color color) {
        Native.drawText(text, x, y, fontSize, color.packed());
    }

    public static int measureText(String text, int fontSize) {
        return Native.measureText(text, fontSize);
    }

    /* ---------------------------------------------------------------- */
    /* Input                                                             */
    /* ---------------------------------------------------------------- */

    public static final int MOUSE_LEFT = 1;
    public static final int MOUSE_MIDDLE = 2;
    public static final int MOUSE_RIGHT = 3;

    public static boolean keyDown(int key) {
        return Native.keyDown(key) != 0;
    }

    public static boolean keyPressed(int key) {
        return Native.keyPressed(key) != 0;
    }

    public static boolean keyReleased(int key) {
        return Native.keyReleased(key) != 0;
    }

    public static boolean mouseDown(int button) {
        return Native.mouseDown(button) != 0;
    }

    public static boolean mousePressed(int button) {
        return Native.mousePressed(button) != 0;
    }

    public static boolean mouseReleased(int button) {
        return Native.mouseReleased(button) != 0;
    }

    public static Vector2 mousePosition() {
        float[] x = new float[1];
        float[] y = new float[1];
        Native.mousePosition(x, y);
        return new Vector2(x[0], y[0]);
    }

    public static float mouseWheel() {
        return Native.mouseWheel();
    }

    public static boolean gamepadAvailable(int index) {
        return Native.gamepadAvailable(index) != 0;
    }

    public static boolean gamepadDown(int index, int button) {
        return Native.gamepadDown(index, button) != 0;
    }

    public static float gamepadAxis(int index, int axis) {
        return Native.gamepadAxis(index, axis);
    }

    /* ---------------------------------------------------------------- */
    /* Files                                                             */
    /* ---------------------------------------------------------------- */

    public static boolean fileExists(String path) {
        return Native.fileExists(path) != 0;
    }

    public static boolean directoryExists(String path) {
        return Native.directoryExists(path) != 0;
    }

    public static int fileSize(String path) {
        return Native.fileSize(path);
    }
}
