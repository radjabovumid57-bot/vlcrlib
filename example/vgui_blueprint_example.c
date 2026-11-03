#include <vlcrlib.h>
#include <stdio.h>

int main(void) {
    if (!InitWindow(1280, 800, "vlcrlib — Blueprint graph (native VGui)")) return 1;
    SetTargetFPS(60);

    Font font = {0};
#ifdef _WIN32
    font = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 28, 32, 96);
#endif
    VGuiContext gui = {0};
    gui.theme = VGuiDefaultTheme();
    VGuiNodeGraph graph;
    VGuiNodeGraphInit(&graph);

    const char *startOut[] = {"Exec"};
    const char *mathIn[] = {"Exec", "A", "B"};
    const char *mathOut[] = {"Exec", "Result"};
    const char *printIn[] = {"Exec", "Value"};
    VGuiNodeGraphAddNode(&graph, 1, "On Start", (Vector2){70,90},
        (Vector2){190,72}, NULL, 0, startOut, 1, (Color){80,135,90,255});
    VGuiNodeGraphAddNode(&graph, 2, "Add", (Vector2){360,70},
        (Vector2){210,150}, mathIn, 3, mathOut, 2, (Color){70,105,165,255});
    VGuiNodeGraphAddNode(&graph, 3, "Print", (Vector2){700,130},
        (Vector2){190,100}, printIn, 2, NULL, 0, (Color){160,100,60,255});
    VGuiNodeGraphAddLink(&graph, 1, 0, 2, 0);
    VGuiNodeGraphAddLink(&graph, 2, 0, 3, 0);
    VGuiNodeGraphAddLink(&graph, 2, 1, 3, 1);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){18,21,29,255});
        VGuiBeginFrame(&gui, font, GetFrameTime());
        VGuiNodeGraphDraw(&gui, &graph, (Rectangle){16,58,1248,726});
        VGuiLabel(&gui, (Vector2){20,18},
            "Blueprint-style graph | Drag node headers | Middle mouse: pan | Wheel: zoom | Click output then input: connect",
            gui.theme.text);
        VGuiEndFrame(&gui);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
