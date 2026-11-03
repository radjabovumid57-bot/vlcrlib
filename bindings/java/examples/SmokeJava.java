import org.vlcrlib.*;

/**
 * Incremental smoke test for the Java binding.
 *
 * <p>Run from a directory containing {@code shaders/} (the CMake build tree
 * copies them next to the library):
 *
 * <pre>
 * java -cp classes:vlcr_jni-dir SmokeJava
 * </pre>
 *
 * Set {@code VLCR_SKIP_WINDOW=1} to exercise only the non-window API.
 */
public final class SmokeJava {

    public static void main(String[] args) {
        step("abi " + Vlcrlib.version());
        check(Vlcrlib.version().split("\\.").length == 3, "ABI version");

        check(Vlcrlib.directoryExists("."), "directoryExists");
        check(Vlcrlib.fileSize("definitely-missing.bin") == 0, "fileSize of a missing file");
        check(Colors.RED.packed() == 0xFF3729E6, "Color packing");
        check(new Vector3(1f, 2f, 3f).add(new Vector3(1f, 0f, 0f)).x == 2f, "Vector3.add");
        check(Math.abs(new Vector2(3f, 4f).length() - 5f) < 1e-5f, "Vector2.length");

        if ("1".equals(System.getenv("VLCR_SKIP_WINDOW"))) {
            step("done (window skipped)");
            return;
        }

        try (Window window = new Window(320, 240, "vlcrlib java smoke", 60)) {
            step("window created");
            check(Vlcrlib.isReady(), "window is ready");
            check(window.width() > 0 && window.height() > 0, "window size");

            try (Vlcrlib.Frame frame = window.frame()) {
                Vlcrlib.clear(Colors.RAY_WHITE);
                Vlcrlib.drawRectangle(10, 10, 120, 60, Colors.ORANGE);
                Vlcrlib.drawCircle(200, 120, 40f, Colors.SKY_BLUE);
                Vlcrlib.drawLine(new Vector2(10, 200), new Vector2(300, 220), 3f, Colors.GREEN);
                Vlcrlib.drawPoly(new Vector2(160, 60), 6, 30f, 0.2f, Colors.PINK);
            }
            step("2D frame");

            try (Vlcrlib.Frame frame = window.frame()) {
                Vlcrlib.clear(Colors.RAY_WHITE);
                Vlcrlib.drawText("hello", 20, 20, 20, Colors.DARK_GRAY);
                check(Vlcrlib.measureText("hello", 20) > 0, "measureText");
            }
            step("text frame");

            try (Font font = Font.defaultFont()) {
                try (Vlcrlib.Frame frame = window.frame()) {
                    Vlcrlib.clear(Colors.RAY_WHITE);
                    font.draw("font", 20f, 20f, 20f, 1f, Colors.WHITE);
                    Vector2 size = font.measure("font", 20f, 1f);
                    check(size.x > 0f && size.y > 0f, "font measure");
                }
                step("font frame");
            }

            Camera3D camera = new Camera3D(new Vector3(4f, 3f, 6f), new Vector3(0f, 0.5f, 0f));
            for (int i = 0; i < 3; i++) {
                try (Vlcrlib.Frame frame = window.frame()) {
                    Vlcrlib.clear(Colors.RAY_WHITE);
                    Vlcrlib.beginMode3D(camera);
                    Vlcrlib.drawGrid(10, 1f);
                    Vlcrlib.drawCube(new Vector3(0f, 0.5f, 0f), new Vector3(1f, 1f, 1f), Colors.RED);
                    Vlcrlib.drawSphere(new Vector3(2f, 1f, 0f), 1f, Colors.SKY_BLUE);
                    Vlcrlib.endMode3D();

                    Vector2 mouse = Vlcrlib.mousePosition();
                    check(mouse != null, "mouse position");
                    Vlcrlib.drawRectanglePro(new Rectangle(20f, 20f, 100f, 40f),
                                             new Vector2(50f, 20f), i * 10f, Colors.ORANGE);
                }
            }
            step("3D frames");

            System.out.println("frame time: " + Vlcrlib.frameTime() + "s, fps: " + Vlcrlib.fps());
        }

        check(!Vlcrlib.isReady(), "window closed");
        step("done");
    }

    private static void step(String name) {
        System.err.println("step: " + name);
    }

    private static void check(boolean condition, String what) {
        if (!condition) {
            throw new AssertionError("failed: " + what);
        }
        System.err.println("ok:   " + what);
    }
}
