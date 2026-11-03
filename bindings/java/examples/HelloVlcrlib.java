import org.vlcrlib.*;

/**
 * A small tour of the Java binding: 3D scene, 2D overlay, input and the frame
 * loop. Set {@code VLCR_FRAMES=120} to exit after 120 frames (smoke tests).
 */
public final class HelloVlcrlib {

    public static void main(String[] args) {
        int frameLimit = 0;
        String framesEnv = System.getenv("VLCR_FRAMES");
        if (framesEnv != null && !framesEnv.isBlank()) {
            frameLimit = Integer.parseInt(framesEnv.trim());
        }

        try (Window window = new Window(960, 540, "vlcrlib — Java binding", 60);
             Font font = Font.defaultFont()) {

            Camera3D camera = new Camera3D(
                    new Vector3(6f, 4f, 8f),
                    new Vector3(0f, 1f, 0f),
                    Vector3.unitY(),
                    55f, 0.1f, 200f);

            float angle = 0f;
            int frames = 0;

            System.out.println("vlcrlib Java binding, ABI " + Vlcrlib.version());

            while (window.running()) {
                float dt = Vlcrlib.frameTime();
                angle += 40f * dt;

                try (Vlcrlib.Frame frame = window.frame()) {
                    Vlcrlib.clear(Colors.RAY_WHITE);

                    Vlcrlib.beginMode3D(camera);
                    Vlcrlib.drawGrid(20, 1f);
                    Vlcrlib.drawCube(new Vector3(-2f, 0.5f, 0f), new Vector3(1f, 1f, 1f), Colors.RED);
                    Vlcrlib.drawSphere(new Vector3(1.5f, 1f, 0f), 1f, Colors.SKY_BLUE);
                    Vlcrlib.drawLine3D(new Vector3(0f, 0.05f, -6f), new Vector3(0f, 0.05f, 6f),
                                       Colors.DARK_GRAY);
                    Vlcrlib.endMode3D();

                    Vlcrlib.drawRectanglePro(new Rectangle(80f, 80f, 120f, 60f),
                                             new Vector2(60f, 30f), angle, Colors.ORANGE);
                    Vlcrlib.drawRectangle(20f, 20f, 260f, 54f, new Color(20, 20, 30, 190));
                    font.draw("vlcrlib + Java", 34f, 34f, 24f, 1f, Colors.WHITE);

                    Vlcrlib.drawText("frame " + frames + "  " + Vlcrlib.fps() + " fps",
                                     34, 500, 18, Colors.DARK_GRAY);

                    Vector2 mouse = Vlcrlib.mousePosition();
                    Vlcrlib.drawCircle(mouse.x, mouse.y, 8f, Colors.GREEN);
                }

                if (Vlcrlib.keyDown(Key.Escape)) {
                    break;
                }
                frames++;
                if (frameLimit > 0 && frames >= frameLimit) {
                    break;
                }
            }

            System.out.println("vlcrlib Java binding finished after " + frames + " frames");
        }
    }
}
