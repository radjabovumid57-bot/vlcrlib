#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>
#include <stdio.h>
#include <math.h>

#define WIN_W 1600
#define WIN_H 900

/* ---- helper: radio-style tree item ---- */
static bool TreeItem(VGuiContext *gui, Rectangle bounds, const char *label,
                     int *sel, int index)
{
    bool wasSelected = (*sel == index);
    bool nowSelected = wasSelected;

    bool clicked = VGuiTreeSelectable(gui, bounds, label, &nowSelected);

    if (clicked) {
        *sel = wasSelected ? -1 : index;
    }
    return (*sel == index);
}

int main(void) {
    if (!InitWindow(WIN_W, WIN_H, "vlcrlib — all GUI widgets")) return 1;
    SetTargetFPS(60);

    Font font = {0};
#ifdef _WIN32
    font = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
#endif
    printf("[main] font.ok=%d\n", font.ok);

    VGuiContext gui = {0};
    gui.theme = VGuiDefaultTheme();

    /* ================= state ================= */
    float  sliderF    = 0.50f;
    int    sliderI    = 40;
    float  dragF      = 0.35f;
    int    dragI      = 25;
    float  knob1      = 0.30f;
    float  knob2      = 0.55f;
    float  knob3      = 0.80f;
    float  progress   = 0.62f;
    bool   checkA     = true;
    bool   checkB     = false;
    bool   radioHi    = true;
    bool   radioMed   = false;
    bool   radioLo    = false;
    bool   loading    = true;
    bool   disabledOn = false;
    bool   openA      = false;
    bool   openB      = true;
    Vector2 aim       = {0.5f, 0.5f};
    Vector3 gizmoPos  = {0, 0, 0};
    Vector3 gizmoRot  = {0, 0, 0};
    Vector3 gizmoScl  = {1, 1, 1};
    int    intVal     = 12;
    float  floatVal   = 0.5f;
    int    tabIdx     = 0;
    int    comboIdx   = 0;
    int    listSel    = 1;
    bool   sel1       = true, sel2 = false, sel3 = false;
    int    treeSel    = -1;   /* -1=нет, 0=A.1, 1=A.2, 2=A.3, 3=B.1, 4=B.2 */

    char   textBuf[64] = "hello";
    Color  pickColor  = PINK;

    const char *tabs[]      = { "General", "Advanced", "About" };
    const char *modes[]     = { "Editor", "Preview", "Runtime" };
    const char *listItems[] = { "First", "Second", "Third", "Fourth", "Fifth" };

    /* -------- table data -------- */
    static float tblVolume[4]   = { 0.5f, 0.8f, 0.2f, 0.9f };
    static float tblMetallic[4] = { 0.0f, 1.0f, 0.5f, 0.3f };
    static bool  tblEnabled[4]  = { true, false, true, true };
    const char  *tblNames[]     = { "Steel", "Gold", "Rubber", "Glass" };
    const float  tblColW[]      = { 80, 90, 90, 60 };

    /* ================= plots ================= */
    float wave[64]   = {0};
    float hist[32]   = {0};
    VGuiRingPlot fps = VGuiRingPlotCreate(120);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        float t  = (float)GetTime();

        if (IsKeyDown(SDL_SCANCODE_RIGHT)) sliderF += 0.02f;
        if (IsKeyDown(SDL_SCANCODE_LEFT))  sliderF -= 0.02f;
        if (sliderF > 1.0f) sliderF = 1.0f;
        if (sliderF < 0.0f) sliderF = 0.0f;

        for (int i = 0; i < 64; ++i)
            wave[i] = 0.5f + 0.4f * sinf((float)i * 0.3f + t * 2.0f);
        for (int i = 0; i < 32; ++i)
            hist[i] = 0.1f + 0.85f * fabsf(sinf((float)i * 0.35f + t));
        VGuiRingPlotPush(&fps, 1.0f / (dt > 0.001f ? dt : 0.016f));

        SetVulkanClearColor(15.0f / 255.0f, 18.0f / 255.0f, 28.0f / 255.0f, 1.0f);
        BeginDrawing();
        VGuiBeginFrame(&gui, font, dt);

        /* --------- Menu bar --------- */
        if (VGuiBeginMenuBar(&gui, (Rectangle){ 0, 0, WIN_W, 28 })) {
            if (VGuiBeginMenu(&gui, "File", true)) {
                VGuiMenuItemPopup(&gui, "New",    true);
                VGuiMenuItemPopup(&gui, "Open",   true);
                VGuiMenuItemPopup(&gui, "Save",   true);
                VGuiMenuItemPopup(&gui, "Save As", false);
                VGuiEndMenu(&gui);
            }
            if (VGuiBeginMenu(&gui, "Edit", true)) {
                VGuiMenuItemPopup(&gui, "Undo", true);
                VGuiMenuItemPopup(&gui, "Redo", true);
                VGuiMenuItemPopup(&gui, "Cut",  true);
                VGuiMenuItemPopup(&gui, "Copy", true);
                VGuiMenuItemPopup(&gui, "Paste", true);
                VGuiEndMenu(&gui);
            }
            if (VGuiBeginMenu(&gui, "View", true)) {
                VGuiMenuItemPopup(&gui, "Zoom in",  true);
                VGuiMenuItemPopup(&gui, "Zoom out", true);
                VGuiMenuItemPopup(&gui, "Reset",    true);
                VGuiEndMenu(&gui);
            }
            VGuiEndMenuBar(&gui);
        }

        /* ========================================================= */
        /*  PANEL A                                                  */
        /* ========================================================= */
        VGuiPanel(&gui, (Rectangle){ 10, 38, 380, 850 }, "A — Buttons / Toggles / Numeric");

        VGuiLabel(&gui, (Vector2){ 30, 90 }, "Buttons", gui.theme.mutedText);
        VGuiButton(&gui, (Rectangle){ 30, 115, 170, 38 }, "Reset values");
        VGuiButton(&gui, (Rectangle){ 210, 115, 160, 38 }, "Apply");
        VGuiSmallButton(&gui, (Rectangle){ 30, 165, 100, 26 }, "Small A");
        VGuiSmallButton(&gui, (Rectangle){ 140, 165, 100, 26 }, "Small B");
        VGuiSmallButton(&gui, (Rectangle){ 250, 165, 120, 26 }, "Small C");

        VGuiLabel(&gui, (Vector2){ 30, 215 }, "Checkboxes", gui.theme.mutedText);
        VGuiCheckBox(&gui, (Rectangle){ 30, 240, 170, 30 }, &checkA, "Enable A");
        VGuiCheckBox(&gui, (Rectangle){ 210, 240, 160, 30 }, &checkB, "Enable B");

        VGuiLabel(&gui, (Vector2){ 30, 285 }, "Radio buttons", gui.theme.mutedText);
        if (VGuiRadioButton(&gui, (Rectangle){ 30, 310, 340, 28 }, &radioHi,  "Quality: High"))
            { radioMed = false; radioLo = false; }
        if (VGuiRadioButton(&gui, (Rectangle){ 30, 342, 340, 28 }, &radioMed, "Quality: Medium"))
            { radioHi = false; radioLo = false; }
        if (VGuiRadioButton(&gui, (Rectangle){ 30, 374, 340, 28 }, &radioLo,  "Quality: Low"))
            { radioHi = false; radioMed = false; }

        VGuiLabel(&gui, (Vector2){ 30, 420 }, "Slider float", gui.theme.mutedText);
        VGuiSlider(&gui, (Rectangle){ 30, 445, 260, 30 }, &sliderF, 0.0f, 1.0f);
        {
            char b[32];
            snprintf(b, sizeof(b), "%.2f", sliderF);
            VGuiLabel(&gui, (Vector2){ 300, 450 }, b, gui.theme.accent);
        }

        VGuiLabel(&gui, (Vector2){ 30, 490 }, "Slider int", gui.theme.mutedText);
        VGuiSliderInt(&gui, (Rectangle){ 30, 515, 260, 30 }, &sliderI, 0, 100);
        {
            char b[32];
            snprintf(b, sizeof(b), "%d", sliderI);
            VGuiLabel(&gui, (Vector2){ 300, 520 }, b, gui.theme.accent);
        }

        VGuiLabel(&gui, (Vector2){ 30, 560 }, "Drag float", gui.theme.mutedText);
        VGuiDragFloat(&gui, (Rectangle){ 30, 585, 340, 32 }, &dragF,
                      0.005f, 0.0f, 1.0f, "%.3f");

        VGuiLabel(&gui, (Vector2){ 30, 630 }, "Drag int", gui.theme.mutedText);
        VGuiDragInt(&gui, (Rectangle){ 30, 655, 340, 32 }, &dragI,
                    1, 0, 100, "%d");

        VGuiLabel(&gui, (Vector2){ 30, 700 }, "Knobs", gui.theme.mutedText);
        VGuiKnob(&gui, (Rectangle){ 30, 725, 78, 78 }, &knob1, 0.0f, 1.0f);
        VGuiKnob(&gui, (Rectangle){ 118, 725, 78, 78 }, &knob2, 0.0f, 1.0f);
        VGuiKnob(&gui, (Rectangle){ 206, 725, 78, 78 }, &knob3, 0.0f, 1.0f);
        {
            char b1[16], b2[16], b3[16];
            snprintf(b1, sizeof(b1), "%.2f", knob1);
            snprintf(b2, sizeof(b2), "%.2f", knob2);
            snprintf(b3, sizeof(b3), "%.2f", knob3);
            VGuiLabel(&gui, (Vector2){ 50,  810 }, b1, gui.theme.mutedText);
            VGuiLabel(&gui, (Vector2){ 138, 810 }, b2, gui.theme.mutedText);
            VGuiLabel(&gui, (Vector2){ 226, 810 }, b3, gui.theme.mutedText);
        }

        VGuiLabel(&gui, (Vector2){ 30, 845 }, "Progress", gui.theme.mutedText);
        VGuiProgressBar(&gui, (Rectangle){ 30, 868, 180, 16 }, progress, 0.0f, 1.0f);
        VGuiLoadingBar(&gui, (Rectangle){ 220, 868, 90, 16 },
                       loading ? 0.0f : progress, loading);
        VGuiSpinner(&gui, (Vector2){ 350, 876 }, 12, gui.theme.accent);

        /* ========================================================= */
        /*  PANEL B                                                  */
        /* ========================================================= */
        VGuiPanel(&gui, (Rectangle){ 400, 38, 380, 850 }, "B — Inputs / Selectors");

        VGuiLabel(&gui, (Vector2){ 420, 90 }, "Text input", gui.theme.mutedText);
        VGuiInputText(&gui, (Rectangle){ 420, 115, 340, 32 }, textBuf, sizeof(textBuf));

        VGuiLabel(&gui, (Vector2){ 420, 160 }, "Int input", gui.theme.mutedText);
        VGuiInputInt(&gui, (Rectangle){ 420, 185, 340, 30 }, &intVal, 0, 100);
        {
            char b[32];
            snprintf(b, sizeof(b), "= %d", intVal);
            VGuiLabel(&gui, (Vector2){ 420, 222 }, b, gui.theme.accent);
        }

        VGuiLabel(&gui, (Vector2){ 420, 255 }, "Float input", gui.theme.mutedText);
        VGuiInputFloat(&gui, (Rectangle){ 420, 280, 340, 30 },
                       &floatVal, 0.0f, 1.0f, 0.05f);
        {
            char b[32];
            snprintf(b, sizeof(b), "= %.3f", floatVal);
            VGuiLabel(&gui, (Vector2){ 420, 317 }, b, gui.theme.accent);
        }

        VGuiLabel(&gui, (Vector2){ 420, 355 }, "Combo dropdown", gui.theme.mutedText);
        VGuiCombo(&gui, (Rectangle){ 420, 380, 340, 32 }, "Mode", modes, 3, &comboIdx);

        VGuiLabel(&gui, (Vector2){ 420, 430 }, "Tabs with content", gui.theme.mutedText);
        VGuiTabs(&gui, (Rectangle){ 420, 455, 340, 34 }, tabs, 3, &tabIdx);

        /* --------- TAB CONTENT --------- */
        {
            VGuiTabPanel panel = VGuiBeginTabPanel(&gui,
                (Rectangle){ 420, 495, 340, 120 });

            float cx = panel.content.x + 4;
            float cy = panel.content.y + 4;

            if (tabIdx == 0) {
                VGuiLabel(&gui, (Vector2){ cx, cy },
                          "General settings", gui.theme.text);
                VGuiCheckBox(&gui, (Rectangle){ cx, cy + 28, 280, 24 },
                             &checkA, "Enable feature X");
                VGuiCheckBox(&gui, (Rectangle){ cx, cy + 58, 280, 24 },
                             &checkB, "Verbose logging");
            } else if (tabIdx == 1) {
                VGuiLabel(&gui, (Vector2){ cx, cy },
                          "Advanced parameters", gui.theme.text);
                VGuiSlider(&gui, (Rectangle){ cx, cy + 28, 260, 24 },
                           &sliderF, 0.0f, 1.0f);
                VGuiLabel(&gui, (Vector2){ cx, cy + 60 },
                          "Tune the slider above", gui.theme.mutedText);
            } else {
                VGuiLabel(&gui, (Vector2){ cx, cy },
                          "vlcrlib GUI demo", gui.theme.text);
                VGuiBulletText(&gui, (Vector2){ cx, cy + 28 }, "Immediate mode");
                VGuiBulletText(&gui, (Vector2){ cx, cy + 54 }, "Pure C99");
            }
            VGuiEndTabPanel(&gui, panel);
        }

        /* --------- TABLE (Excel-style) --------- */
        VGuiLabel(&gui, (Vector2){ 420, 630 }, "Table (editable)", gui.theme.mutedText);

        if (VGuiBeginTable(&gui, "materials",
                (Rectangle){ 420, 655, 340, 190 },
                4, 4, 30.0f, tblColW))
        {
            VGuiTableHeaderRow(&gui);
            VGuiTableHeaderText(&gui, 0, "Name");
            VGuiTableHeaderText(&gui, 1, "Volume");
            VGuiTableHeaderText(&gui, 2, "Metallic");
            VGuiTableHeaderText(&gui, 3, "On");

            for (int r = 0; r < 4; ++r) {
                VGuiTableNextRow(&gui);

                VGuiTableSetColumn(&gui, 0);
                VGuiTableCellText(&gui, 0, tblNames[r]);

                VGuiTableSetColumn(&gui, 1);
                VGuiTableDragFloat(&gui, 1, &tblVolume[r], 0.0f, 1.0f, "%.2f");

                VGuiTableSetColumn(&gui, 2);
                VGuiTableDragFloat(&gui, 2, &tblMetallic[r], 0.0f, 1.0f, "%.2f");

                VGuiTableSetColumn(&gui, 3);
                VGuiTableCheckbox(&gui, 3, &tblEnabled[r]);
            }
            VGuiEndTable(&gui);
        }

        /* ========================================================= */
        /*  PANEL C                                                  */
        /* ========================================================= */
        VGuiPanel(&gui, (Rectangle){ 790, 38, 380, 850 }, "C — Text / Trees / Disabled");

        VGuiLabel(&gui, (Vector2){ 810, 90 },  "Regular label", gui.theme.text);
        VGuiLabel(&gui, (Vector2){ 810, 115 }, "Muted label",   gui.theme.mutedText);
        VGuiText(&gui, (Rectangle){ 810, 145, 340, 30 },
                 "VGuiText — 22px", 22.0f, WHITE);
        VGuiTextColored(&gui, (Vector2){ 810, 180 }, "Colored text!", GOLD);
        VGuiTextDisabled(&gui, (Vector2){ 810, 205 }, "Disabled-styled text");
        VGuiBulletText(&gui, (Vector2){ 810, 230 }, "Bullet one");
        VGuiBulletText(&gui, (Vector2){ 810, 255 }, "Bullet two");

        gui.cursor.y   = 295.0f;
        gui.windowOpen = true;
        gui.windowStart = (Vector2){ 810, 0 };
        gui.windowSize  = (Vector2){ 340, 0 };
        VGuiSeparatorText("Separators");
        gui.windowOpen = false;

        VGuiSeparator();
        VGuiDummy(0.0f, 10.0f);

        VGuiLabel(&gui, (Vector2){ 810, 335 }, "Collapsing headers", gui.theme.mutedText);

        VGuiCollapsingHeader(&gui, (Rectangle){ 810, 360, 340, 28 }, "Section A", &openA);
        if (openA) {
            TreeItem(&gui, (Rectangle){ 830, 395, 320, 26 }, "Child A.1", &treeSel, 0);
            TreeItem(&gui, (Rectangle){ 830, 425, 320, 26 }, "Child A.2", &treeSel, 1);
            TreeItem(&gui, (Rectangle){ 830, 455, 320, 26 }, "Child A.3", &treeSel, 2);
        }

        VGuiTreeNode(&gui, (Rectangle){ 810, 495, 340, 28 }, "Section B", &openB);
        if (openB) {
            TreeItem(&gui, (Rectangle){ 830, 530, 320, 26 }, "Child B.1", &treeSel, 3);
            TreeItem(&gui, (Rectangle){ 830, 560, 320, 26 }, "Child B.2", &treeSel, 4);
        }
        VGuiTreePop(&gui);

        {
            const char *name = "none";
            switch (treeSel) {
                case 0: name = "A.1"; break;
                case 1: name = "A.2"; break;
                case 2: name = "A.3"; break;
                case 3: name = "B.1"; break;
                case 4: name = "B.2"; break;
            }
            char b[64];
            snprintf(b, sizeof(b), "Selected: %s", name);
            VGuiLabel(&gui, (Vector2){ 810, 605 }, b, gui.theme.accent);
        }

        VGuiLabel(&gui, (Vector2){ 810, 640 }, "Menu items (inline)", gui.theme.mutedText);
        VGuiMenuItem(&gui, (Rectangle){ 810, 665, 340, 28 }, "Open…",         true);
        VGuiMenuItem(&gui, (Rectangle){ 810, 697, 340, 28 }, "Save",          true);
        VGuiMenuItem(&gui, (Rectangle){ 810, 729, 340, 28 }, "Disabled item", false);

        VGuiCheckBox(&gui, (Rectangle){ 810, 775, 340, 26 },
                     &disabledOn, "Enable buttons below");
        VGuiBeginDisabled(!disabledOn);
        VGuiButton(&gui, (Rectangle){ 810, 815, 160, 34 }, "Disabled A");
        VGuiButton(&gui, (Rectangle){ 990, 815, 160, 34 }, "Disabled B");
        VGuiEndDisabled();

          /* ========================================================= */
        /*  PANEL D                                                  */
        /* ========================================================= */
        VGuiPanel(&gui, (Rectangle){ 1180, 38, 410, 850 }, "D — Color / Gizmo / Plots");

        /* -------- Color picker (ring + square + RGB/HSV/Hex + sliders) -------- */
        VGuiLabel(&gui, (Vector2){ 1200, 90 }, "Color picker", gui.theme.mutedText);
        VGuiColorPicker(&gui, (Rectangle){ 1200, 115, 370, 360 }, &pickColor);

        /* -------- Gizmo -------- */
        VGuiLabel(&gui, (Vector2){ 1200, 495 }, "3D Gizmo", gui.theme.mutedText);
        VGuiGizmo3D(&gui, (Rectangle){ 1200, 520, 370, 60 },
                    &gizmoPos, &gizmoRot, &gizmoScl);

        /* -------- Sniper -------- */
        VGuiLabel(&gui, (Vector2){ 1200, 600 }, "Sniper / aim",
                  gui.theme.mutedText);
        VGuiSniper(&gui, (Rectangle){ 1200, 625, 370, 60 }, &aim);
        {
            char b[64];
            snprintf(b, sizeof(b), "Aim: %.2f, %.2f", aim.x, aim.y);
            VGuiLabel(&gui, (Vector2){ 1200, 692 }, b, gui.theme.accent);
        }

        /* -------- PlotLines2 -------- */
        VGuiLabel(&gui, (Vector2){ 1200, 720 }, "PlotLines2",
                  gui.theme.mutedText);
        VGuiPlotLines2(&gui, (Rectangle){ 1200, 745, 370, 55 },
                       wave, 64, 0.0f, 1.0f, SKYBLUE,
                       (VGuiPlotConfig){
                           .overlayText = NULL,
                           .gridX = 8, .gridY = 4,
                           .fill  = true,
                           .fillColor = (Color){ 102, 191, 255, 60 }
                       });

        /* -------- Histogram -------- */
        VGuiLabel(&gui, (Vector2){ 1200, 810 }, "Histogram", gui.theme.mutedText);
        VGuiPlotHistogram(&gui, (Rectangle){ 1200, 835, 370, 45 },
                          hist, 32, 0.0f, 1.0f, GOLD, NULL);

        VGuiLabel(&gui, (Vector2){ 1200, 818 }, "RingPlot (FPS, live)",
                  gui.theme.mutedText);
        VGuiRingPlotDraw(&gui, (Rectangle){ 1200, 843, 370, 40 },
                         &fps, 0.0f, 144.0f, GREEN);

        VGuiButton(&gui, (Rectangle){ 400, 855, 260, 28 }, "Hover for tooltip");
        if (VGuiIsItemHovered())
            VGuiSetItemTooltip("VGuiSetItemTooltip works on any widget");

        VGuiEndFrame(&gui);
        EndDrawing();
    }

    VGuiRingPlotDestroy(&fps);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
