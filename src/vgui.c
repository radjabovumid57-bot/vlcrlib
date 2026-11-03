#include "vgui.h"
#include "vcore.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== Static current context ==================== */
static VGuiContext *g_currentGui = NULL;

/* ==================== Forward declarations ==================== */
static VGuiId hashBytes(VGuiId hash, const void *data, size_t size);
static VGuiId hashText (VGuiId hash, const char *text);
static bool   isDisabled(void);
static VGuiId itemId(const VGuiContext *gui, Rectangle bounds, const char *label);
static void   setItemState(Rectangle bounds, bool hovered, bool active,
                           bool clicked, bool edited);
static Color  dimColor(Color c);
static Color  accentDim(VGuiContext *gui);

/* ==================== Hash / helpers ==================== */

static VGuiId hashBytes(VGuiId hash, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static VGuiId hashText(VGuiId hash, const char *text) {
    return text ? hashBytes(hash, text, strlen(text)) : hash;
}

static int guiId(const VGuiContext *gui, Rectangle bounds) {
    /* Include the caller's ID scope and preserve fractional layout coordinates.
     * The old ID used integer-truncated bounds and collided for nearby controls. */
    VGuiId hash = (gui && gui->idStackSize > 0)
                ? gui->idStack[gui->idStackSize - 1] : 1469598103934665603ull;
    const float values[4] = { bounds.x, bounds.y, bounds.width, bounds.height };
    hash = hashBytes(hash, values, sizeof(values));
    return (int)(hash & 0x7fffffff);
}

static bool contains(Rectangle bounds, Vector2 point) {
    return point.x >= bounds.x && point.x <= bounds.x + bounds.width &&
           point.y >= bounds.y && point.y <= bounds.y + bounds.height;
}

static VGuiId itemId(const VGuiContext *gui, Rectangle bounds, const char *label) {
    VGuiId hash = VGuiGetId(gui, label);
    const float values[4] = { bounds.x, bounds.y, bounds.width, bounds.height };
    return hashBytes(hash, values, sizeof(values));
}

static void drawLabel(VGuiContext *gui, const char *label, Vector2 position) {
    if (label && gui->font.ok)
        DrawTextEx(gui->font, label, position, 18.0f, 1.0f, gui->theme.text);
}

static void drawLabelCenteredV(VGuiContext *gui, const char *label, Rectangle bounds) {
    if (!label || !gui->font.ok) return;
    Vector2 ts = MeasureTextEx(gui->font, label, 18.0f, 1.0f);
    float ty = bounds.y + (bounds.height - ts.y) * 0.5f;
    DrawTextEx(gui->font, label, (Vector2){ bounds.x, ty },
               18.0f, 1.0f, gui->theme.text);
}

static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static Color dimColor(Color c) {
    return (Color){ (unsigned char)(c.r / 2),
                    (unsigned char)(c.g / 2),
                    (unsigned char)(c.b / 2),
                    c.a };
}

static Color accentDim(VGuiContext *gui) {
    return isDisabled() ? dimColor(gui->theme.accent) : gui->theme.accent;
}

static bool isDisabled(void) {
    return g_currentGui && g_currentGui->disabledDepth > 0;
}

static void setItemState(Rectangle bounds, bool hovered, bool active,
                         bool clicked, bool edited) {
    if (!g_currentGui) return;
    g_currentGui->lastItemBounds  = bounds;
    g_currentGui->lastItemHovered = hovered;
    g_currentGui->lastItemActive  = active;
    g_currentGui->lastItemClicked = clicked;
    g_currentGui->lastItemEdited  = edited;
    g_currentGui->lastItemTooltip = NULL;
}

static bool beginControl(VGuiContext *gui, Rectangle bounds, int *id) {
    *id = guiId(gui, bounds);
    if (!gui || isDisabled() || bounds.width <= 0.0f || bounds.height <= 0.0f) return false;
    bool hovered = contains(bounds, gui->mouse);
    if (hovered) gui->hotId = *id;
    if (hovered && gui->mousePressed) gui->activeId = *id;
    return hovered;
}

static bool endControl(VGuiContext *gui, int id, bool hovered) {
    if (isDisabled()) return false;
    return hovered && gui->mouseReleased && gui->activeId == id;
}

/* ==================== Theme / lifecycle ==================== */

VGuiTheme VGuiDefaultTheme(void) {
    return (VGuiTheme){
        .panel         = { 28,  32,  44, 245},
        .panelBorder   = { 90, 100, 125, 255},
        .control       = { 48,  56,  75, 255},
        .controlHover  = { 67,  84, 118, 255},
        .controlActive = { 40, 125, 180, 255},
        .accent        = {255, 109, 194, 255},
        .text          = {245, 245, 250, 255},
        .mutedText     = {170, 180, 200, 255}
    };
}

void VGuiBegin(VGuiContext *gui, Font font) {
    if (!gui) return;
    g_currentGui = gui;
    gui->font          = font;
    gui->mouse         = GetMousePosition();
    /* SDL reports mouse coordinates in logical window units; the renderer and
     * UI bounds use drawable pixels. Scale on high-DPI displays to keep input
     * aligned with the actual rendered controls. */
    SDL_Window *window = (SDL_Window *)GetWindowHandle();
    int logicalW = 0, logicalH = 0, pixelW = 0, pixelH = 0;
    if (window && SDL_GetWindowSize(window, &logicalW, &logicalH) &&
        logicalW > 0 && logicalH > 0) {
        vcore_GetScreenSize(&pixelW, &pixelH);
        if (pixelW > 0 && pixelH > 0) {
            gui->mouse.x *= (float)pixelW / (float)logicalW;
            gui->mouse.y *= (float)pixelH / (float)logicalH;
        }
    }
    gui->mousePressed  = IsMouseButtonPressed(1) ? 1 : 0;
    gui->mouseDown     = IsMouseButtonDown(1) ? 1 : 0;
    gui->mouseReleased = IsMouseButtonReleased(1) ? 1 : 0;
    gui->hotId         = 0;
    gui->hotGuiId      = 0;

    if (gui->idStackSize == 0)
        gui->idStack[gui->idStackSize++] = 1469598103934665603ull;
    if (gui->theme.panel.a == 0)   gui->theme = VGuiDefaultTheme();
    if (gui->itemSpacing <= 0.0f)  gui->itemSpacing = 8.0f;
    if (gui->framePadding <= 0.0f) gui->framePadding = 8.0f;

    gui->disabledDepth   = 0;
    gui->lastItemTooltip = NULL;
    gui->lastItemClicked = false;
    gui->lastItemEdited  = false;
}

void VGuiEnd(VGuiContext *gui) {
    if (gui && gui->mouseReleased) {
        gui->activeId = 0;
        gui->activeGuiId = 0;
        gui->dragActiveId = 0;
    }
}

void VGuiBeginFrame(VGuiContext *gui, Font font, float deltaTime) {
    if (!gui) return;
    g_currentGui = gui;
    VGuiBegin(gui, font);
    gui->deltaTime = deltaTime > 0.0f ? deltaTime : 1.0f / 60.0f;
    int w = 0, h = 0;
    vcore_GetScreenSize(&w, &h);
    gui->displaySize = (Vector2){ (float)w, (float)h };
}

void VGuiEndFrame(VGuiContext *gui) {
    if (!gui) return;

    /* тултип последнего виджета */
    if (gui->lastItemTooltip && gui->lastItemHovered) {
        Vector2 ts = MeasureTextEx(gui->font, gui->lastItemTooltip, 16.0f, 1.0f);
        Vector2 pos = { gui->mouse.x + 16.0f, gui->mouse.y + 18.0f };
        Rectangle box = { pos.x, pos.y, ts.x + 16.0f, ts.y + 12.0f };
        DrawRectangleRec(box, gui->theme.panel);
        DrawRectangleLines((int)box.x, (int)box.y,
                           (int)box.width, (int)box.height, gui->theme.panelBorder);
        DrawTextEx(gui->font, gui->lastItemTooltip,
                   (Vector2){ box.x + 8, box.y + 6 },
                   16.0f, 1.0f, gui->theme.text);
    }

    VGuiEnd(gui);
    g_currentGui = NULL;
}

/* ==================== Item state API ==================== */

bool VGuiIsItemHovered(void) { return g_currentGui ? g_currentGui->lastItemHovered : false; }
bool VGuiIsItemActive(void)  { return g_currentGui ? g_currentGui->lastItemActive  : false; }
bool VGuiIsItemClicked(void) { return g_currentGui ? g_currentGui->lastItemClicked : false; }
bool VGuiIsItemEdited(void)  { return g_currentGui ? g_currentGui->lastItemEdited  : false; }

Rectangle VGuiGetItemRectMin(void) {
    return g_currentGui ? g_currentGui->lastItemBounds : (Rectangle){0};
}
Rectangle VGuiGetItemRectMax(void) {
    Rectangle r = g_currentGui ? g_currentGui->lastItemBounds : (Rectangle){0};
    return (Rectangle){ r.x + r.width, r.y + r.height, 0, 0 };
}
Vector2 VGuiGetItemSize(void) {
    Rectangle r = g_currentGui ? g_currentGui->lastItemBounds : (Rectangle){0};
    return (Vector2){ r.width, r.height };
}

void VGuiSetItemTooltip(const char *text) {
    if (g_currentGui) g_currentGui->lastItemTooltip = text;
}

/* ==================== Disabled ==================== */

void VGuiBeginDisabled(bool disabled) {
    if (!g_currentGui) return;
    if (disabled) g_currentGui->disabledDepth++;
}

void VGuiEndDisabled(void) {
    if (!g_currentGui) return;
    if (g_currentGui->disabledDepth > 0) g_currentGui->disabledDepth--;
}

/* ==================== Layout helpers ==================== */

void VGuiSeparator(void) {
    if (!g_currentGui) return;
    VGuiContext *gui = g_currentGui;
    float y = gui->cursor.y + 4.0f;
    float x0 = gui->windowOpen ? gui->windowStart.x : 0.0f;
    float x1 = gui->windowOpen
             ? gui->windowStart.x + gui->windowSize.x
             : gui->displaySize.x;
    DrawLineEx((Vector2){ x0, y }, (Vector2){ x1, y }, 1.0f, gui->theme.panelBorder);
    gui->cursor.y += 9.0f;
    setItemState((Rectangle){ x0, y, x1 - x0, 1 }, false, false, false, false);
}

void VGuiSeparatorText(const char *text) {
    if (!g_currentGui || !text) return;
    VGuiContext *gui = g_currentGui;
    Vector2 ts = MeasureTextEx(gui->font, text, 16.0f, 1.0f);
    float y = gui->cursor.y + 9.0f;
    float x0 = gui->windowOpen ? gui->windowStart.x : 0.0f;
    float x1 = gui->windowOpen
             ? gui->windowStart.x + gui->windowSize.x
             : gui->displaySize.x;

    float textX = x0 + 8.0f;
    float textW = ts.x + 12.0f;

    DrawLineEx((Vector2){ x0, y }, (Vector2){ textX, y }, 1.0f, gui->theme.panelBorder);
    DrawLineEx((Vector2){ textX + textW, y }, (Vector2){ x1, y }, 1.0f, gui->theme.panelBorder);

    DrawTextEx(gui->font, text,
               (Vector2){ textX + 6.0f, y - ts.y * 0.5f },
               16.0f, 1.0f, gui->theme.mutedText);

    gui->cursor.y += 22.0f;
    setItemState((Rectangle){ x0, y, x1 - x0, 1 }, false, false, false, false);
}

void VGuiDummy(float width, float height) {
    if (!g_currentGui) return;
    VGuiContext *gui = g_currentGui;
    Rectangle r = { gui->cursor.x, gui->cursor.y, width, height };
    gui->cursor.y += height;
    setItemState(r, false, false, false, false);
}

void VGuiBeginGroup(void) {
    if (!g_currentGui) return;
    g_currentGui->groupStart = g_currentGui->cursor;
    g_currentGui->groupDepth++;
}

void VGuiEndGroup(void) {
    if (!g_currentGui || g_currentGui->groupDepth <= 0) return;
    g_currentGui->groupDepth--;
}

/* ==================== ID stack ==================== */

void VGuiPushId(VGuiContext *gui, const char *id) {
    if (!gui || gui->idStackSize >= 16) return;
    gui->idStack[gui->idStackSize] = hashText(gui->idStack[gui->idStackSize - 1], id);
    gui->idStackSize++;
}

void VGuiPushIdInt(VGuiContext *gui, int id) {
    if (!gui || gui->idStackSize >= 16) return;
    gui->idStack[gui->idStackSize] =
        hashBytes(gui->idStack[gui->idStackSize - 1], &id, sizeof(id));
    gui->idStackSize++;
}

void VGuiPopId(VGuiContext *gui) {
    if (gui && gui->idStackSize > 1) gui->idStackSize--;
}

VGuiId VGuiGetId(const VGuiContext *gui, const char *label) {
    VGuiId hash = (gui && gui->idStackSize > 0)
                ? gui->idStack[gui->idStackSize - 1]
                : 1469598103934665603ull;
    const char *separator = label ? strstr(label, "##") : NULL;
    return hashText(hash, separator ? separator + 2 : label);
}

VGuiRectState VGuiItemState(VGuiContext *gui, Rectangle bounds, const char *label) {
    VGuiRectState state = {0};
    if (!gui) return state;

    VGuiId id = itemId(gui, bounds, label);
    bool disabled = gui->disabledDepth > 0;
    state.hovered = contains(bounds, gui->mouse) && !disabled &&
                    bounds.width > 0.0f && bounds.height > 0.0f;

    if (state.hovered) gui->hotGuiId = id;
    if (state.hovered && gui->mousePressed) gui->activeGuiId = id;

    state.active   = (gui->activeGuiId == id) && !disabled;
    state.held     = state.active && gui->mouseDown;
    state.pressed  = state.hovered && gui->mousePressed;
    state.released = gui->mouseReleased && state.active;
    return state;
}

/* ==================== Window / cursor ==================== */

bool VGuiBeginWindow(VGuiContext *gui, const char *title, Rectangle bounds, bool *open) {
    if (!gui || (open && !*open)) return false;
    VGuiPanel(gui, bounds, title);
    gui->windowStart = (Vector2){ bounds.x + gui->framePadding, bounds.y + 42.0f };
    gui->windowSize  = (Vector2){ bounds.width  - gui->framePadding * 2.0f,
                                  bounds.height - 50.0f };
    gui->cursor     = gui->windowStart;
    gui->windowOpen = true;
    VGuiPushId(gui, title ? title : "window");
    return true;
}

void VGuiEndWindow(VGuiContext *gui) {
    if (!gui || !gui->windowOpen) return;
    VGuiPopId(gui);
    gui->windowOpen = false;
}

void VGuiSameLine(VGuiContext *gui, float spacing) {
    if (gui) gui->cursor.x += spacing >= 0.0f ? spacing : gui->itemSpacing;
}

void VGuiSpacing(VGuiContext *gui, float amount) {
    if (gui) gui->cursor.y += amount >= 0.0f ? amount : gui->itemSpacing;
}

Rectangle VGuiNextItem(VGuiContext *gui, float width, float height) {
    Rectangle bounds = {0};
    if (!gui) return bounds;
    bounds = (Rectangle){ gui->cursor.x, gui->cursor.y, width, height };
    gui->cursor.y += height + gui->itemSpacing;
    return bounds;
}

/* ==================== Text / panels ==================== */

void VGuiPanel(VGuiContext *gui, Rectangle bounds, const char *title) {
    if (!gui) return;
    DrawRectangleRec(bounds, gui->theme.panel);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    if (title) drawLabel(gui, title, (Vector2){ bounds.x + 16, bounds.y + 12 });
}

void VGuiLabel(VGuiContext *gui, Vector2 position, const char *text, Color color) {
    if (gui && text && gui->font.ok)
        DrawTextEx(gui->font, text, position, 18.0f, 1.0f, color);
}

void VGuiText(VGuiContext *gui, Rectangle bounds, const char *text,
              float fontSize, Color color) {
    if (!gui || !text || !gui->font.ok) return;
    if (fontSize <= 0.0f) fontSize = 18.0f;
    DrawTextEx(gui->font, text, (Vector2){ bounds.x, bounds.y },
               fontSize, 1.0f, color);
}

void VGuiTextColored(VGuiContext *gui, Vector2 position, const char *text, Color color) {
    VGuiLabel(gui, position, text, color);
}

void VGuiTextDisabled(VGuiContext *gui, Vector2 position, const char *text) {
    if (!gui) return;
    VGuiLabel(gui, position, text, gui->theme.mutedText);
}

void VGuiBulletText(VGuiContext *gui, Vector2 position, const char *text) {
    if (!gui) return;
    DrawCircleV((Vector2){ position.x + 4, position.y + 8 }, 3.0f, gui->theme.accent);
    drawLabel(gui, text, (Vector2){ position.x + 14, position.y });
}

/* ==================== Buttons ==================== */

bool VGuiButton(VGuiContext *gui, Rectangle bounds, const char *label) {
    if (!gui) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);
    bool clicked = endControl(gui, id, hovered);

    Color fill = gui->theme.control;
    if (gui->activeId == id && gui->mouseDown) fill = gui->theme.controlActive;
    else if (hovered)                          fill = gui->theme.controlHover;
    if (isDisabled())                          fill = dimColor(fill);

    DrawRectangleRec(bounds, fill);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    if (label) {
        Rectangle textRect = { bounds.x + 14, bounds.y,
                               bounds.width - 28, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }

    setItemState(bounds, hovered, gui->activeId == id, clicked, false);
    return clicked;
}

bool VGuiSmallButton(VGuiContext *gui, Rectangle bounds, const char *label) {
    if (!gui) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);
    bool clicked = endControl(gui, id, hovered);

    Color fill = gui->theme.control;
    if (gui->activeId == id && gui->mouseDown) fill = gui->theme.controlActive;
    else if (hovered)                          fill = gui->theme.controlHover;
    if (isDisabled())                          fill = dimColor(fill);

    DrawRectangleRec(bounds, fill);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    if (label) {
        Rectangle textRect = { bounds.x + 6, bounds.y,
                               bounds.width - 12, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }

    setItemState(bounds, hovered, gui->activeId == id, clicked, false);
    return clicked;
}

bool VGuiInvisibleButton(VGuiContext *gui, Rectangle bounds, const char *id) {
    (void)id;
    if (!gui) return false;
    int bid;
    bool hovered = beginControl(gui, bounds, &bid);
    bool clicked = endControl(gui, bid, hovered);
    setItemState(bounds, hovered, gui->activeId == bid, clicked, false);
    return clicked;
}

/* ==================== Toggles ==================== */

bool VGuiCheckBox(VGuiContext *gui, Rectangle bounds, bool *checked, const char *label) {
    if (!gui || !checked) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);

    float boxSize = bounds.height - 4.0f;
    Rectangle box = { bounds.x, bounds.y + 2, boxSize, boxSize };

    Color boxFill = *checked ? gui->theme.accent : gui->theme.control;
    if (isDisabled()) boxFill = dimColor(boxFill);
    DrawRectangleRec(box, boxFill);
    DrawRectangleLines((int)box.x, (int)box.y, (int)box.width, (int)box.height,
                       hovered ? gui->theme.text : gui->theme.panelBorder);

    if (*checked) {
        DrawLineEx((Vector2){ box.x + 5, box.y + box.height * 0.55f },
                   (Vector2){ box.x + box.width * 0.42f, box.y + box.height - 5 },
                   2, (Color){ 255, 255, 255, 255 });
        DrawLineEx((Vector2){ box.x + box.width * 0.42f, box.y + box.height - 5 },
                   (Vector2){ box.x + box.width - 5, box.y + 5 },
                   2, (Color){ 255, 255, 255, 255 });
    }

    if (label) {
        Rectangle textRect = { bounds.x + boxSize + 10, bounds.y,
                               bounds.width - boxSize - 10, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }

    bool changed = endControl(gui, id, hovered);
    if (changed) *checked = !*checked;
    setItemState(bounds, hovered, gui->activeId == id, changed, changed);
    return changed;
}

bool VGuiRadioButton(VGuiContext *gui, Rectangle bounds, bool *selected, const char *label) {
    if (!gui || !selected) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);

    Vector2 center = { bounds.x + bounds.height * 0.5f,
                       bounds.y + bounds.height * 0.5f };
    float outer = bounds.height * 0.42f;

    DrawCircleV(center, outer, gui->theme.control);
    DrawCircleV(center, outer, hovered ? gui->theme.text : gui->theme.panelBorder);
    DrawCircleV(center, outer - 2.0f,
                *selected ? gui->theme.controlActive : gui->theme.control);
    if (*selected) DrawCircleV(center, outer * 0.55f, gui->theme.text);

    if (label) {
        Rectangle textRect = { bounds.x + bounds.height + 10, bounds.y,
                               bounds.width - bounds.height - 10, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }

    bool changed = endControl(gui, id, hovered);
    if (changed) *selected = true;
    setItemState(bounds, hovered, gui->activeId == id, changed, changed);
    return changed;
}

/* ==================== Numeric widgets ==================== */

bool VGuiSlider(VGuiContext *gui, Rectangle bounds, float *value,
                float minValue, float maxValue) {
    if (!gui || !value || maxValue <= minValue || bounds.width <= 0.0f || bounds.height <= 0.0f) return false;
    int id;
    bool hovered  = beginControl(gui, bounds, &id);
    bool dragging = (gui->activeId == id) && gui->mouseDown;

    if (dragging || (hovered && gui->mousePressed)) {
        float ratio = (gui->mouse.x - bounds.x) / bounds.width;
        *value = minValue + clamp01(ratio) * (maxValue - minValue);
    }
    float ratio = clamp01((*value - minValue) / (maxValue - minValue));

    DrawRectangleRec((Rectangle){ bounds.x, bounds.y + bounds.height * 0.4f,
                                  bounds.width, bounds.height * 0.2f },
                     gui->theme.control);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y + bounds.height * 0.4f,
                                  bounds.width * ratio, bounds.height * 0.2f },
                     gui->theme.accent);
    DrawCircle((int)(bounds.x + bounds.width * ratio),
               (int)(bounds.y + bounds.height * 0.5f), bounds.height * 0.42f,
               dragging ? gui->theme.controlActive : gui->theme.text);

    bool changed = dragging || endControl(gui, id, hovered);
    setItemState(bounds, hovered, dragging, changed, changed);
    return changed;
}

bool VGuiSliderInt(VGuiContext *gui, Rectangle bounds, int *value,
                   int minValue, int maxValue) {
    if (!gui || !value || maxValue <= minValue || bounds.width <= 0.0f || bounds.height <= 0.0f) return false;
    int id;
    bool hovered  = beginControl(gui, bounds, &id);
    bool dragging = (gui->activeId == id) && gui->mouseDown;
    bool changed = false;

    if (dragging || (hovered && gui->mousePressed)) {
        float ratio = (gui->mouse.x - bounds.x) / bounds.width;
        int newValue = minValue + (int)floorf(clamp01(ratio) * (float)(maxValue - minValue) + 0.5f);
        if (newValue != *value) { *value = newValue; changed = true; }
    }
    float ratio = clamp01((float)(*value - minValue) / (float)(maxValue - minValue));

    DrawRectangleRec((Rectangle){ bounds.x, bounds.y + bounds.height * 0.4f,
                                  bounds.width, bounds.height * 0.2f },
                     gui->theme.control);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y + bounds.height * 0.4f,
                                  bounds.width * ratio, bounds.height * 0.2f },
                     gui->theme.accent);
    DrawCircle((int)(bounds.x + bounds.width * ratio),
               (int)(bounds.y + bounds.height * 0.5f), bounds.height * 0.42f,
               dragging ? gui->theme.controlActive : gui->theme.text);

    setItemState(bounds, hovered, dragging, changed, changed);
    return changed;
}

bool VGuiDragFloat(VGuiContext *gui, Rectangle bounds, float *value,
                   float step, float minValue, float maxValue, const char *format) {
    if (!gui || !value) return false;
    if (step <= 0.0f) step = 0.01f;
    if (!format) format = "%.3f";

    VGuiRectState s = VGuiItemState(gui, bounds, "dragf");
    VGuiId id = itemId(gui, bounds, "dragf");
    bool changed = false;

    if (s.active && gui->mouseDown) {
        if (gui->dragActiveId != id) {
            gui->dragActiveId  = id;
            gui->dragLastMouse = gui->mouse;
        }
        float dx = gui->mouse.x - gui->dragLastMouse.x;
        gui->dragLastMouse = gui->mouse;

        if (fabsf(dx) > 0.01f) {
            *value += dx * step;
            if (*value < minValue) *value = minValue;
            if (*value > maxValue) *value = maxValue;
            changed = true;
        }
    } else if (gui->dragActiveId == id) {
        gui->dragActiveId = 0;
    }

    char text[64];
    snprintf(text, sizeof(text), format, *value);

    DrawRectangleRec(bounds, s.hovered ? gui->theme.controlHover : gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height,
                       s.active ? gui->theme.accent : gui->theme.panelBorder);
    {
        Rectangle textRect = { bounds.x + 10, bounds.y,
                               bounds.width - 20, bounds.height };
        drawLabelCenteredV(gui, text, textRect);
    }

    setItemState(bounds, s.hovered, s.active, s.pressed, changed);
    return changed;
}

bool VGuiDragInt(VGuiContext *gui, Rectangle bounds, int *value,
                 int step, int minValue, int maxValue, const char *format) {
    if (!gui || !value) return false;
    if (step <= 0) step = 1;
    if (!format) format = "%d";

    VGuiRectState s = VGuiItemState(gui, bounds, "dragi");
    VGuiId id = itemId(gui, bounds, "dragi");
    bool changed = false;

    if (s.active && gui->mouseDown) {
        if (gui->dragActiveId != id) {
            gui->dragActiveId  = id;
            gui->dragLastMouse = gui->mouse;
        }
        float dx = gui->mouse.x - gui->dragLastMouse.x;

        if (fabsf(dx) >= 1.0f) {
            int delta = (int)(dx / 3.0f) * step;
            if (delta == 0) delta = (dx > 0.0f) ? step : -step;
            *value += delta;
            if (*value < minValue) *value = minValue;
            if (*value > maxValue) *value = maxValue;
            gui->dragLastMouse = gui->mouse;
            changed = true;
        }
    } else if (gui->dragActiveId == id) {
        gui->dragActiveId = 0;
    }

    char text[64];
    snprintf(text, sizeof(text), format, *value);

    DrawRectangleRec(bounds, s.hovered ? gui->theme.controlHover : gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height,
                       s.active ? gui->theme.accent : gui->theme.panelBorder);
    {
        Rectangle textRect = { bounds.x + 10, bounds.y,
                               bounds.width - 20, bounds.height };
        drawLabelCenteredV(gui, text, textRect);
    }

    setItemState(bounds, s.hovered, s.active, s.pressed, changed);
    return changed;
}

bool VGuiKnob(VGuiContext *gui, Rectangle bounds, float *value,
              float minValue, float maxValue) {
    if (!gui || !value || maxValue <= minValue || bounds.width <= 0.0f || bounds.height <= 0.0f) return false;
    int id;
    bool hovered  = beginControl(gui, bounds, &id);
    bool dragging = (gui->activeId == id) && gui->mouseDown;
    Vector2 center = { bounds.x + bounds.width  * 0.5f,
                       bounds.y + bounds.height * 0.5f };
    float radius = bounds.width < bounds.height
                 ? bounds.width * 0.5f : bounds.height * 0.5f;

    if (dragging || (hovered && gui->mousePressed)) {
        float ratio = 0.5f - (gui->mouse.y - center.y) / (bounds.height * 1.5f);
        *value = minValue + clamp01(ratio) * (maxValue - minValue);
    }
    float ratio = clamp01((*value - minValue) / (maxValue - minValue));

    DrawCircleV(center, radius, gui->theme.control);
    DrawCircleV(center, radius - 3.0f,
                dragging ? gui->theme.controlActive : gui->theme.controlHover);

    float angle = (-225.0f + 270.0f * ratio) * (float)VLCRLIB_PI / 180.0f;
    Vector2 end = { center.x + cosf(angle) * (radius - 7.0f),
                    center.y + sinf(angle) * (radius - 7.0f) };
    DrawLineEx(center, end, 4.0f, gui->theme.accent);

    bool changed = dragging || endControl(gui, id, hovered);
    setItemState(bounds, hovered, dragging, changed, changed);
    return changed;
}

void VGuiProgressBar(VGuiContext *gui, Rectangle bounds,
                     float value, float minValue, float maxValue) {
    if (!gui || maxValue <= minValue) return;
    float ratio = clamp01((value - minValue) / (maxValue - minValue));
    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y,
                                  bounds.width * ratio, bounds.height },
                     gui->theme.accent);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    setItemState(bounds, false, false, false, false);
}

void VGuiLoadingBar(VGuiContext *gui, Rectangle bounds, float progress, bool animated) {
    if (!gui) return;
    float ratio = clamp01(progress);
    DrawRectangleRec(bounds, gui->theme.control);

    if (animated && ratio <= 0.0f) {
        float phase = (float)fmod(GetTime() * 0.45, 1.0);
        Rectangle moving = { bounds.x + (bounds.width - bounds.width * 0.25f) * phase,
                             bounds.y, bounds.width * 0.25f, bounds.height };
        DrawRectangleRec(moving, gui->theme.accent);
    } else {
        DrawRectangleRec((Rectangle){ bounds.x, bounds.y,
                                      bounds.width * ratio, bounds.height },
                         gui->theme.accent);
    }
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    setItemState(bounds, false, false, false, false);
}

void VGuiSpinner(VGuiContext *gui, Vector2 center, float radius, Color color) {
    if (!gui || radius <= 0.0f) return;
    float phase = (float)fmod(GetTime() * 2.0, 6.2831853);

    for (int i = 0; i < 8; ++i) {
        float angle = phase + (float)i * 6.2831853f / 8.0f;
        float alpha = 35.0f + 220.0f * (float)(i + 1) / 8.0f;
        Color segment = { color.r, color.g, color.b, (unsigned char)alpha };
        Vector2 start = { center.x + cosf(angle) * radius * 0.45f,
                          center.y + sinf(angle) * radius * 0.45f };
        Vector2 end   = { center.x + cosf(angle) * radius,
                          center.y + sinf(angle) * radius };
        DrawLineEx(start, end, radius * 0.16f, segment);
    }
}

/* ==================== Inputs ==================== */

bool VGuiInputInt(VGuiContext *gui, Rectangle bounds, int *value,
                  int minValue, int maxValue) {
    if (!gui || !value || maxValue < minValue) return false;
    VGuiRectState state = VGuiItemState(gui, bounds, "input_int");
    bool changed = false;

    if (state.pressed) {
        int direction = (gui->mouse.x < bounds.x + bounds.width * 0.5f) ? -1 : 1;
        *value += direction;
        changed = true;
    }
    if (state.active && IsKeyPressed(SDL_SCANCODE_LEFT))  { (*value)--; changed = true; }
    if (state.active && IsKeyPressed(SDL_SCANCODE_RIGHT)) { (*value)++; changed = true; }
    if (*value < minValue) *value = minValue;
    if (*value > maxValue) *value = maxValue;

    char text[32];
    snprintf(text, sizeof(text), "%d", *value);

    DrawRectangleRec(bounds, state.hovered ? gui->theme.controlHover
                                           : gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height,
                       state.active ? gui->theme.accent : gui->theme.panelBorder);
    {
        Rectangle textRect = { bounds.x + 10, bounds.y,
                               bounds.width - 20, bounds.height };
        drawLabelCenteredV(gui, text, textRect);
    }

    setItemState(bounds, state.hovered, state.active, state.pressed, changed);
    return changed;
}

bool VGuiInputFloat(VGuiContext *gui, Rectangle bounds, float *value,
                    float minValue, float maxValue, float step) {
    if (!gui || !value || maxValue < minValue) return false;
    if (step <= 0.0f) step = 0.01f;
    VGuiRectState state = VGuiItemState(gui, bounds, "input_float");
    bool changed = false;

    if (state.pressed) {
        *value += (gui->mouse.x < bounds.x + bounds.width * 0.5f) ? -step : step;
        changed = true;
    }
    if (state.active && IsKeyPressed(SDL_SCANCODE_LEFT))  { *value -= step; changed = true; }
    if (state.active && IsKeyPressed(SDL_SCANCODE_RIGHT)) { *value += step; changed = true; }
    if (*value < minValue) *value = minValue;
    if (*value > maxValue) *value = maxValue;

    char text[32];
    snprintf(text, sizeof(text), "%.3f", *value);

    DrawRectangleRec(bounds, state.hovered ? gui->theme.controlHover
                                           : gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height,
                       state.active ? gui->theme.accent : gui->theme.panelBorder);
    {
        Rectangle textRect = { bounds.x + 10, bounds.y,
                               bounds.width - 20, bounds.height };
        drawLabelCenteredV(gui, text, textRect);
    }

    setItemState(bounds, state.hovered, state.active, state.pressed, changed);
    return changed;
}

bool VGuiInputText(VGuiContext *gui, Rectangle bounds, char *buffer, int capacity) {
    if (!gui || !buffer || capacity < 2) return false;
    VGuiRectState state = VGuiItemState(gui, bounds, "input_text");
    bool changed = false;

    if (state.active) {
        char input[64];
        int got = GetCharPressed(input, sizeof(input));
        if (got > 0) {
            size_t length = strlen(buffer);
            for (int i = 0; input[i] && length < (size_t)capacity - 1; ++i) {
                buffer[length++] = input[i];
            }
            buffer[length] = '\0';
            changed = true;
        }
        if (IsKeyPressed(SDL_SCANCODE_BACKSPACE)) {
            size_t length = strlen(buffer);
            if (length > 0) {
                buffer[length - 1] = '\0';
                changed = true;
            }
        }
    }

    DrawRectangleRec(bounds, state.hovered ? gui->theme.controlHover
                                           : gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height,
                       state.active ? gui->theme.accent : gui->theme.panelBorder);
    {
        Rectangle textRect = { bounds.x + 10, bounds.y,
                               bounds.width - 20, bounds.height };
        drawLabelCenteredV(gui, buffer, textRect);
    }

    if (state.active) {
        Vector2 ts = MeasureTextEx(gui->font, buffer, 18.0f, 1.0f);
        float cx = bounds.x + 10 + ts.x + 1;
        float blink = fmodf((float)GetTime(), 1.0f);
        if (blink < 0.5f) {
            DrawLineEx((Vector2){ cx, bounds.y + 6 },
                       (Vector2){ cx, bounds.y + bounds.height - 6 },
                       2.0f, gui->theme.text);
        }
    }

    setItemState(bounds, state.hovered, state.active, state.pressed, changed);
    return changed;
}

/* ==================== Selectors ==================== */

int VGuiTabs(VGuiContext *gui, Rectangle bounds,
             const char * const *labels, int count, int *selected) {
    if (!gui || !labels || count <= 0 || !selected) return -1;
    if (*selected < 0)      *selected = 0;
    if (*selected >= count) *selected = count - 1;

    float width = bounds.width / (float)count;
    for (int i = 0; i < count; ++i) {
        Rectangle tab = { bounds.x + width * i, bounds.y, width, bounds.height };
        VGuiPushIdInt(gui, i);
        VGuiRectState state = VGuiItemState(gui, tab, labels[i]);
        VGuiPopId(gui);

        Color fill = (i == *selected) ? gui->theme.controlActive
                   : state.hovered     ? gui->theme.controlHover
                                       : gui->theme.control;
        DrawRectangleRec(tab, fill);
        DrawRectangleLines((int)tab.x, (int)tab.y,
                           (int)tab.width, (int)tab.height, gui->theme.panelBorder);
        if (labels[i]) {
            Rectangle textRect = { tab.x + 12, tab.y, tab.width - 24, tab.height };
            drawLabelCenteredV(gui, labels[i], textRect);
        }
        if (state.released) *selected = i;
    }
    return *selected;
}

bool VGuiCombo(VGuiContext *gui, Rectangle bounds, const char *label,
               const char * const *items, int count, int *selected)
{
    if (!gui || !items || count <= 0 || !selected) return false;
    if (*selected < 0)      *selected = 0;
    if (*selected >= count) *selected = count - 1;

    if (label) drawLabel(gui, label, (Vector2){ bounds.x, bounds.y - 22 });

    VGuiId id = VGuiGetId(gui, label ? label : "combo");
    bool wasOpen = (gui->openPopupId == id);
    bool headerHovered = contains(bounds, gui->mouse) && !isDisabled();

    /* ---------- header ---------- */
    DrawRectangleRec(bounds, headerHovered ? gui->theme.controlHover
                                           : gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height,
                       wasOpen ? gui->theme.accent : gui->theme.panelBorder);

    {
        Rectangle textRect = { bounds.x + 10, bounds.y,
                               bounds.width - 32, bounds.height };
        drawLabelCenteredV(gui, items[*selected], textRect);
    }

    /* стрелка ▼ */
    float cx = bounds.x + bounds.width - 16;
    float cy = bounds.y + bounds.height * 0.5f;
    DrawTriangle((Vector2){ cx - 6, cy - 3 },
                 (Vector2){ cx + 6, cy - 3 },
                 (Vector2){ cx,     cy + 4 }, gui->theme.text);

    /* ---------- toggle popup on click ---------- */
    bool open = wasOpen;
    if (headerHovered && gui->mousePressed && !isDisabled()) {
        gui->openPopupId = wasOpen ? 0 : id;
        open = !wasOpen;
    }

    /* ---------- popup items ---------- */
    bool clickedOutside = false;
    if (open) {
        Rectangle popup = { bounds.x, bounds.y + bounds.height,
                            bounds.width, bounds.height * count };
        DrawRectangleRec(popup, gui->theme.panel);
        DrawRectangleLines((int)popup.x, (int)popup.y,
                           (int)popup.width, (int)popup.height, gui->theme.panelBorder);

        for (int i = 0; i < count; ++i) {
            Rectangle item = { bounds.x, bounds.y + bounds.height * (i + 1),
                               bounds.width, bounds.height };
            bool itemHovered = contains(item, gui->mouse);
            Color fill = (i == *selected) ? gui->theme.controlActive
                       : itemHovered      ? gui->theme.controlHover
                                          : gui->theme.panel;
            DrawRectangleRec(item, fill);

            Rectangle textRect = { item.x + 10, item.y,
                                   item.width - 20, item.height };
            drawLabelCenteredV(gui, items[i], textRect);

            /* выбор пункта — по нажатию */
            if (itemHovered && gui->mousePressed && !isDisabled()) {
                *selected = i;
                gui->openPopupId = 0;
                open = false;
            }
        }

        /* если кликнули мимо popup — закрыть */
        if (headerHovered && gui->mousePressed && !isDisabled()) {
            gui->openPopupId = wasOpen ? 0 : id;
            open = !wasOpen;
        }
    }
    (void)clickedOutside;

    setItemState(bounds, headerHovered, open, false, false);
    return open;
}

bool VGuiSelectable(VGuiContext *gui, Rectangle bounds, const char *label, bool *selected) {
    if (!gui) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);
    Color fill = (selected && *selected)
               ? gui->theme.controlActive
               : (hovered ? gui->theme.controlHover : gui->theme.control);
    DrawRectangleRec(bounds, fill);
    if (label) {
        Rectangle textRect = { bounds.x + 8, bounds.y,
                               bounds.width - 16, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }
    bool clicked = endControl(gui, id, hovered);
    if (clicked && selected) *selected = !*selected;
    setItemState(bounds, hovered, gui->activeId == id, clicked, clicked);
    return clicked;
}

int VGuiListBox(VGuiContext *gui, Rectangle bounds,
                const char * const *items, int count, int *selected) {
    if (!gui || !items || count <= 0) return -1;
    VGuiPanel(gui, bounds, NULL);
    float itemHeight = 24.0f;
    int result = selected ? *selected : -1;

    for (int i = 0; i < count; ++i) {
        Rectangle item = { bounds.x + 4, bounds.y + 4 + i * itemHeight,
                           bounds.width - 8, itemHeight };
        VGuiPushIdInt(gui, i);
        int id;
        bool hovered = beginControl(gui, item, &id);
        bool isSel   = (selected && *selected == i);
        Color fill   = isSel ? gui->theme.controlActive
                             : (hovered ? gui->theme.controlHover : gui->theme.control);
        DrawRectangleRec(item, fill);
        if (items[i]) {
            Rectangle textRect = { item.x + 8, item.y,
                                   item.width - 16, item.height };
            drawLabelCenteredV(gui, items[i], textRect);
        }
        if (endControl(gui, id, hovered)) {
            if (selected) *selected = i;
            result = i;
        }
        VGuiPopId(gui);
    }
    return result;
}

/* ==================== Tree / collapsing ==================== */

bool VGuiCollapsingHeader(VGuiContext *gui, Rectangle bounds,
                          const char *label, bool *open) {
    if (!gui || !open) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);
    Color fill = hovered ? gui->theme.controlHover : gui->theme.control;
    DrawRectangleRec(bounds, fill);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    float cx = bounds.x + 12, cy = bounds.y + bounds.height * 0.5f;
    if (*open) {
        DrawTriangle((Vector2){ cx - 5, cy - 3 }, (Vector2){ cx + 5, cy - 3 },
                     (Vector2){ cx, cy + 4 }, gui->theme.text);
    } else {
        DrawTriangle((Vector2){ cx - 3, cy - 5 }, (Vector2){ cx - 3, cy + 5 },
                     (Vector2){ cx + 4, cy }, gui->theme.text);
    }
    if (label) {
        Rectangle textRect = { bounds.x + 26, bounds.y,
                               bounds.width - 32, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }

    bool clicked = endControl(gui, id, hovered);
    if (clicked) *open = !*open;
    setItemState(bounds, hovered, gui->activeId == id, clicked, false);
    return clicked;
}

bool VGuiTreeNode(VGuiContext *gui, Rectangle bounds, const char *label, bool *open) {
    return VGuiCollapsingHeader(gui, bounds, label, open);
}

bool VGuiTreeSelectable(VGuiContext *gui, Rectangle bounds,
                        const char *label, bool *selected)
{
    if (!gui) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);

    bool isSelected = (selected && *selected);
    Color fill = isSelected ? gui->theme.controlActive
               : hovered    ? gui->theme.controlHover
                            : gui->theme.panel;

    if (isSelected || hovered) {
        DrawRectangleRec(bounds, fill);
    }

    /* маркер (треугольник справа, если selected) */
    if (isSelected) {
        float cx = bounds.x + 6;
        float cy = bounds.y + bounds.height * 0.5f;
        DrawTriangle((Vector2){ cx - 3, cy - 5 },
                     (Vector2){ cx + 4, cy },
                     (Vector2){ cx - 3, cy + 5 },
                     gui->theme.text);
    }

    if (label) {
        Rectangle textRect = { bounds.x + 16, bounds.y,
                               bounds.width - 20, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }

    bool clicked = endControl(gui, id, hovered);
    if (clicked && selected) *selected = true;
    setItemState(bounds, hovered, gui->activeId == id, clicked, clicked);
    return clicked;
}

void VGuiTreePop(VGuiContext *gui) {
    VGuiPopId(gui);
}

void VGuiTooltip(VGuiContext *gui, const char *text) {
    if (!gui || !text || !gui->font.ok) return;
    Vector2 pos = gui->mouse;
    pos.x += 16;
    pos.y += 16;
    Vector2 size = MeasureTextEx(gui->font, text, 16.0f, 1.0f);
    Rectangle bounds = { pos.x, pos.y, size.x + 16, size.y + 12 };
    DrawRectangleRec(bounds, gui->theme.panel);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    DrawTextEx(gui->font, text, (Vector2){ bounds.x + 8, bounds.y + 6 },
               16.0f, 1.0f, gui->theme.text);
}

bool VGuiMenuItem(VGuiContext *gui, Rectangle bounds, const char *label, bool enabled) {
    if (!gui || !enabled) return false;
    int id;
    bool hovered = beginControl(gui, bounds, &id);
    Color fill = hovered ? gui->theme.controlHover : gui->theme.panel;
    DrawRectangleRec(bounds, fill);
    if (label) {
        Rectangle textRect = { bounds.x + 12, bounds.y,
                               bounds.width - 20, bounds.height };
        drawLabelCenteredV(gui, label, textRect);
    }
    bool clicked = endControl(gui, id, hovered);
    setItemState(bounds, hovered, gui->activeId == id, clicked, false);
    return clicked;
}

/* ==================== Menu bar ==================== */

bool VGuiBeginMenuBar(VGuiContext *gui, Rectangle bounds) {
    if (!gui) return false;
    DrawRectangleRec(bounds, gui->theme.panel);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    gui->cursor     = (Vector2){ bounds.x + 8, bounds.y + 4 };
    gui->windowSize = (Vector2){ bounds.width - 16, bounds.height - 8 };
    gui->windowStart = gui->cursor;
    VGuiPushId(gui, "menubar");
    return true;
}

void VGuiEndMenuBar(VGuiContext *gui) {
    if (!gui) return;
    VGuiPopId(gui);
    /* клик мимо меню — закрыть */
    if (gui->mousePressed && gui->hotGuiId == 0) gui->activeGuiId = 0;
}

bool VGuiBeginMenu(VGuiContext *gui, const char *label, bool enabled) {
    if (!gui || !label || !enabled) return false;

    Vector2 ts = MeasureTextEx(gui->font, label, 16.0f, 1.0f);
    Rectangle bar = { gui->cursor.x, gui->cursor.y, ts.x + 20.0f, 24.0f };

    VGuiId id = VGuiGetId(gui, label);
    VGuiRectState s = VGuiItemState(gui, bar, label);

    bool wasOpen = (gui->activeGuiId == id);
    if (s.released) gui->activeGuiId = wasOpen ? 0 : id;
    bool open = (gui->activeGuiId == id);

    Color fill = open ? gui->theme.controlActive
               : s.hovered ? gui->theme.controlHover
                           : gui->theme.panel;
    DrawRectangleRec(bar, fill);
    DrawTextEx(gui->font, label,
               (Vector2){ bar.x + 10.0f, bar.y + (bar.height - ts.y) * 0.5f },
               16.0f, 1.0f, gui->theme.text);

    gui->cursor.x += bar.width + 4.0f;

    if (open) {
        VGuiPushId(gui, label);
        gui->cursor = (Vector2){ bar.x, bar.y + bar.height };
        gui->windowSize = (Vector2){ 160.0f, 200.0f };
    }
    return open;
}

void VGuiEndMenu(VGuiContext *gui) {
    if (!gui) return;
    VGuiPopId(gui);
}

bool VGuiMenuItemPopup(VGuiContext *gui, const char *label, bool enabled) {
    if (!gui || !label) return false;

    Vector2 ts = MeasureTextEx(gui->font, label, 16.0f, 1.0f);
    Rectangle item = { gui->cursor.x, gui->cursor.y,
                       gui->windowSize.x, 22.0f };

    int id;
    bool hovered = enabled ? beginControl(gui, item, &id) : false;

    Color fill = hovered ? gui->theme.controlActive : gui->theme.panel;
    if (!enabled) fill = dimColor(fill);
    DrawRectangleRec(item, fill);
    DrawTextEx(gui->font, label,
               (Vector2){ item.x + 12.0f, item.y + (item.height - ts.y) * 0.5f },
               16.0f, 1.0f, enabled ? gui->theme.text : gui->theme.mutedText);

    gui->cursor.y += item.height;

    bool clicked = enabled && endControl(gui, id, hovered);
    if (clicked) gui->activeGuiId = 0;
    setItemState(item, hovered, gui->activeId == id, clicked, false);
    return clicked;
}

/* ==================== Color picker ==================== */

static Color hsvToRgb(float hue, float saturation, float value) {
    float sector   = hue * 6.0f;
    int   index    = (int)floorf(sector) % 6;
    float fraction = sector - floorf(sector);
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - fraction * saturation);
    float t = value * (1.0f - (1.0f - fraction) * saturation);
    float red[]   = { value, q, p, p, t, value };
    float green[] = { t, value, value, q, p, p };
    float blue[]  = { p, p, t, value, value, q };
    return (Color){
        (unsigned char)(red[index]   * 255),
        (unsigned char)(green[index] * 255),
        (unsigned char)(blue[index]  * 255),
        255
    };
}

/* ---------- вспомогательный: слайдер с градиентной дорожкой ---------- */
static bool ColorSliderGradient(VGuiContext *gui, Rectangle bounds,
                                float *value01,
                                Color colorL, Color colorR,
                                const char *label, const char *fmt)
{
    if (!gui || !value01) return false;
    int id;
    bool hovered  = beginControl(gui, bounds, &id);
    bool dragging = (gui->activeId == id) && gui->mouseDown;
    bool changed  = false;

    if (dragging || (hovered && gui->mousePressed)) {
        float ratio = (gui->mouse.x - bounds.x) / bounds.width;
        *value01 = clamp01(ratio);
        changed = true;
    }
    float ratio = clamp01(*value01);

    /* фон дорожки — горизонтальный градиент цветов */
    const int STEPS = 32;
    float trackY = bounds.y + bounds.height * 0.15f;
    float trackH = bounds.height * 0.70f;
    for (int i = 0; i < STEPS; ++i) {
        float t0 = (float)i / STEPS;
        float t1 = (float)(i + 1) / STEPS;
        unsigned char r0 = (unsigned char)(colorL.r + (colorR.r - colorL.r) * t0);
        unsigned char g0 = (unsigned char)(colorL.g + (colorR.g - colorL.g) * t0);
        unsigned char b0 = (unsigned char)(colorL.b + (colorR.b - colorL.b) * t0);
        unsigned char a0 = (unsigned char)(colorL.a + (colorR.a - colorL.a) * t0);
        unsigned char r1 = (unsigned char)(colorL.r + (colorR.r - colorL.r) * t1);
        unsigned char g1 = (unsigned char)(colorL.g + (colorR.g - colorL.g) * t1);
        unsigned char b1 = (unsigned char)(colorL.b + (colorR.b - colorL.b) * t1);
        unsigned char a1 = (unsigned char)(colorL.a + (colorR.a - colorL.a) * t1);
        (void)r1; (void)g1; (void)b1; (void)a1;
        DrawRectangleRec((Rectangle){
            bounds.x + t0 * bounds.width,
            trackY,
            bounds.width / STEPS + 1.0f,
            trackH
        }, (Color){ r0, g0, b0, a0 });
    }

    DrawRectangleLines((int)bounds.x, (int)trackY,
                       (int)bounds.width, (int)trackH, gui->theme.panelBorder);

    /* label слева */
    if (label) {
        Vector2 ts = MeasureTextEx(gui->font, label, 16.0f, 1.0f);
        DrawTextEx(gui->font, label,
                   (Vector2){ bounds.x + 6, bounds.y + (bounds.height - ts.y) * 0.5f },
                   16.0f, 1.0f, gui->theme.text);
    }

    /* значение справа */
    if (fmt) {
        char vbuf[32];
        snprintf(vbuf, sizeof(vbuf), fmt, *value01);
        Vector2 ts = MeasureTextEx(gui->font, vbuf, 16.0f, 1.0f);
        DrawTextEx(gui->font, vbuf,
                   (Vector2){ bounds.x + bounds.width - ts.x - 6,
                              bounds.y + (bounds.height - ts.y) * 0.5f },
                   16.0f, 1.0f, gui->theme.text);
    }

    /* knob */
    float knobX = bounds.x + ratio * bounds.width;
    float knobY = bounds.y + bounds.height * 0.5f;
    float knobR = bounds.height * 0.36f;
    DrawCircle((int)knobX, (int)knobY, knobR,
               dragging ? gui->theme.controlActive : (Color){ 240, 240, 240, 255 });
       for (int i = 0; i < 24; ++i) {
        float a1 = (float)i / 24 * 2.0f * VLCRLIB_PI;
        float a2 = (float)(i + 1) / 24 * 2.0f * VLCRLIB_PI;
        DrawLineEx(
            (Vector2){ knobX + cosf(a1) * knobR, knobY + sinf(a1) * knobR },
            (Vector2){ knobX + cosf(a2) * knobR, knobY + sinf(a2) * knobR },
            1.5f, gui->theme.panelBorder);
        }

    setItemState(bounds, hovered, dragging, changed, changed);
    return changed;
}

bool VGuiColorPicker(VGuiContext *gui, Rectangle bounds, Color *color) {
    if (!gui || !color) return false;

    static int mode = 1;   /* 0=RGB, 1=HSV, 2=Hex */

    VGuiId baseId = VGuiGetId(gui, "colpicker");
    bool changed = false;

    /* ---------- Color → HSV ---------- */
    static float lastHue = 0.0f;
    float hue, sat, val;
    {
        float r = color->r / 255.0f;
        float g = color->g / 255.0f;
        float b = color->b / 255.0f;
        float mx = fmaxf(fmaxf(r, g), b);
        float mn = fminf(fminf(r, g), b);
        float d  = mx - mn;

        val = mx;
        sat = (mx > 0.0001f) ? (d / mx) : 0.0f;

        if (d < 0.0001f) {
            hue = lastHue;
        } else if (mx == r) {
            float hh = (g - b) / d;
            if (hh < 0.0f) hh += 6.0f;
            hue = hh / 6.0f;
        } else if (mx == g) {
            hue = ((b - r) / d + 2.0f) / 6.0f;
        } else {
            hue = ((r - g) / d + 4.0f) / 6.0f;
        }
        lastHue = hue;
    }

    /* ---------- Layout верхней части (ring + square) ---------- */
    float topHeight = 190.0f;
    float ringSize = bounds.width;
    if (ringSize > topHeight) ringSize = topHeight;

    float cx = bounds.x + bounds.width * 0.5f;
    float cy = bounds.y + ringSize * 0.5f;

    float outerR    = ringSize * 0.5f - 4.0f;
    float ringThick = outerR * 0.15f;
    float innerR    = outerR - ringThick;

    float side = innerR * 1.41421356f * 0.90f;
    Rectangle sv = { cx - side * 0.5f, cy - side * 0.5f, side, side };

    /* ---------- Hue ring ---------- */
    const int SEG = 90;
    for (int i = 0; i < SEG; ++i) {
        float a1 = (float)i / SEG * 2.0f * VLCRLIB_PI - VLCRLIB_PI * 0.5f;
        float a2 = (float)(i + 1) / SEG * 2.0f * VLCRLIB_PI - VLCRLIB_PI * 0.5f;
        Color segColor = hsvToRgb((float)i / SEG, 1.0f, 1.0f);

        Vector2 p1i = { cx + cosf(a1) * innerR, cy + sinf(a1) * innerR };
        Vector2 p2i = { cx + cosf(a2) * innerR, cy + sinf(a2) * innerR };
        Vector2 p1o = { cx + cosf(a1) * outerR, cy + sinf(a1) * outerR };
        Vector2 p2o = { cx + cosf(a2) * outerR, cy + sinf(a2) * outerR };

        DrawTriangle(p1i, p1o, p2o, segColor);
        DrawTriangle(p1i, p2o, p2i, segColor);
    }

    /* ---------- SV square ---------- */
    const int SN = 24;
    for (int yy = 0; yy < SN; ++yy) {
        for (int xx = 0; xx < SN; ++xx) {
            float ss = (float)xx / (SN - 1);
            float vv = 1.0f - (float)yy / (SN - 1);
            Color c = hsvToRgb(hue, ss, vv);
            DrawRectangleRec((Rectangle){
                sv.x + (float)xx / SN * sv.width,
                sv.y + (float)yy / SN * sv.height,
                sv.width  / SN + 1.0f,
                sv.height / SN + 1.0f
            }, c);
        }
    }

    /* ---------- Индикаторы ---------- */
    {
        float ang = hue * 2.0f * VLCRLIB_PI - VLCRLIB_PI * 0.5f;
        float midR = (innerR + outerR) * 0.5f;
        float hx = cx + cosf(ang) * midR;
        float hy = cy + sinf(ang) * midR;
        float ir = ringThick * 0.35f;
        for (int i = 0; i < 24; ++i) {
            float a1 = (float)i / 24 * 2.0f * VLCRLIB_PI;
            float a2 = (float)(i + 1) / 24 * 2.0f * VLCRLIB_PI;
            DrawLineEx(
                (Vector2){ hx + cosf(a1) * ir, hy + sinf(a1) * ir },
                (Vector2){ hx + cosf(a2) * ir, hy + sinf(a2) * ir },
                2.0f, (Color){ 255, 255, 255, 255 });
        }
    }
    {
        float sx = sv.x + sat * sv.width;
        float sy = sv.y + (1.0f - val) * sv.height;
        float ir = 6.0f;
        for (int i = 0; i < 24; ++i) {
            float a1 = (float)i / 24 * 2.0f * VLCRLIB_PI;
            float a2 = (float)(i + 1) / 24 * 2.0f * VLCRLIB_PI;
            DrawLineEx(
                (Vector2){ sx + cosf(a1) * ir, sy + sinf(a1) * ir },
                (Vector2){ sx + cosf(a2) * ir, sy + sinf(a2) * ir },
                2.0f, (Color){ 255, 255, 255, 255 });
        }
    }

    /* ---------- Ring interaction ---------- */
    {
        VGuiId id = baseId ^ 0xAAull;
        float dx = gui->mouse.x - cx;
        float dy = gui->mouse.y - cy;
        float d  = sqrtf(dx * dx + dy * dy);
        bool over = (d >= innerR && d <= outerR);

        if (over && gui->mousePressed) gui->activeGuiId = id;

        if (gui->activeGuiId == id && gui->mouseDown) {
            float ang = atan2f(dy, dx) + VLCRLIB_PI * 0.5f;
            float h = ang / (2.0f * VLCRLIB_PI);
            while (h < 0.0f) h += 1.0f;
            while (h > 1.0f) h -= 1.0f;
            lastHue = h;
            *color = hsvToRgb(h, sat, val);
            changed = true;
        }
    }

    /* ---------- SV interaction ---------- */
    {
        VGuiId id = baseId ^ 0xBBull;
        bool over = contains(sv, gui->mouse);
        if (over && gui->mousePressed) gui->activeGuiId = id;

        if (gui->activeGuiId == id && gui->mouseDown) {
            sat = clamp01((gui->mouse.x - sv.x) / sv.width);
            val = clamp01(1.0f - (gui->mouse.y - sv.y) / sv.height);
            *color = hsvToRgb(hue, sat, val);
            changed = true;
        }
    }

    /* ============================================================ */
    /*  Нижняя часть: вкладки RGB / HSV / Hex + слайдеры            */
    /* ============================================================ */
    float y = bounds.y + topHeight + 4.0f;

    /* -------- Tabs -------- */
    {
        Rectangle tabsRect = { bounds.x, y, bounds.width, 28.0f };
        const char *tabLabels[] = { "RGB", "HSV", "Hex" };
        int prevMode = mode;
        VGuiTabs(gui, tabsRect, tabLabels, 3, &mode);
        if (mode != prevMode) changed = true;
        y += 32.0f;
    }

    /* -------- Sliders по режиму -------- */
    if (mode == 0) {
        /* RGB */
        float r01 = color->r / 255.0f;
        float g01 = color->g / 255.0f;
        float b01 = color->b / 255.0f;
        float a01 = color->a / 255.0f;

        Rectangle rRow = { bounds.x, y, bounds.width, 24.0f };
        if (ColorSliderGradient(gui, rRow, &r01,
                (Color){0, color->g, color->b, 255},
                (Color){255, color->g, color->b, 255}, "R", NULL))
        { color->r = (unsigned char)(r01 * 255); changed = true; }
        y += 26.0f;

        Rectangle gRow = { bounds.x, y, bounds.width, 24.0f };
        if (ColorSliderGradient(gui, gRow, &g01,
                (Color){color->r, 0, color->b, 255},
                (Color){color->r, 255, color->b, 255}, "G", NULL))
        { color->g = (unsigned char)(g01 * 255); changed = true; }
        y += 26.0f;

        Rectangle bRow = { bounds.x, y, bounds.width, 24.0f };
        if (ColorSliderGradient(gui, bRow, &b01,
                (Color){color->r, color->g, 0, 255},
                (Color){color->r, color->g, 255, 255}, "B", NULL))
        { color->b = (unsigned char)(b01 * 255); changed = true; }
        y += 26.0f;

        Rectangle aRow = { bounds.x, y, bounds.width, 24.0f };
        if (ColorSliderGradient(gui, aRow, &a01,
                (Color){color->r, color->g, color->b, 0},
                (Color){color->r, color->g, color->b, 255}, "A", NULL))
        { color->a = (unsigned char)(a01 * 255); changed = true; }
        y += 26.0f;
    }
    else if (mode == 1) {
        /* HSV */
        float a01 = color->a / 255.0f;

        Rectangle hRow = { bounds.x, y, bounds.width, 24.0f };
        float newHue = hue;
        if (ColorSliderGradient(gui, hRow, &newHue,
                (Color){255, 0,   0,   255},
                (Color){255, 0,   255, 255}, "H", NULL))
        {
            lastHue = newHue;
            *color = hsvToRgb(newHue, sat, val);
            changed = true;
        }
        y += 26.0f;

        Rectangle sRow = { bounds.x, y, bounds.width, 24.0f };
        if (ColorSliderGradient(gui, sRow, &sat,
                (Color){255, 255, 255, 255},
                hsvToRgb(hue, 1.0f, 1.0f), "S", NULL))
        {
            *color = hsvToRgb(hue, sat, val);
            changed = true;
        }
        y += 26.0f;

        Rectangle vRow = { bounds.x, y, bounds.width, 24.0f };
        if (ColorSliderGradient(gui, vRow, &val,
                (Color){0, 0, 0, 255},
                hsvToRgb(hue, sat, 1.0f), "V", NULL))
        {
            *color = hsvToRgb(hue, sat, val);
            changed = true;
        }
        y += 26.0f;

        Rectangle aRow = { bounds.x, y, bounds.width, 24.0f };
        if (ColorSliderGradient(gui, aRow, &a01,
                (Color){color->r, color->g, color->b, 0},
                (Color){color->r, color->g, color->b, 255}, "A", NULL))
        { color->a = (unsigned char)(a01 * 255); changed = true; }
        y += 26.0f;
    }
    else {
        /* Hex — поле ввода + свотч */
        static char hexBuf[16] = "#FF3B79DB";
        snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X%02X",
                 color->r, color->g, color->b, color->a);

        Rectangle hexRow = { bounds.x, y, bounds.width - 40.0f, 30.0f };
        DrawRectangleRec(hexRow, gui->theme.control);
        DrawRectangleLines((int)hexRow.x, (int)hexRow.y,
                           (int)hexRow.width, (int)hexRow.height,
                           gui->theme.panelBorder);
        DrawTextEx(gui->font, hexBuf,
                   (Vector2){ hexRow.x + 10, hexRow.y + 6 },
                   18.0f, 1.0f, gui->theme.text);

        Rectangle swatch = { bounds.x + bounds.width - 34.0f, y, 34.0f, 30.0f };
        DrawRectangleRec(swatch, *color);
        DrawRectangleLines((int)swatch.x, (int)swatch.y,
                           (int)swatch.width, (int)swatch.height,
                           gui->theme.panelBorder);
        y += 32.0f;
    }

    /* -------- Preview + hex под слайдерами -------- */
    {
        Rectangle preview = { bounds.x, y, 32, 26 };
        DrawRectangleRec(preview, *color);
        DrawRectangleLines((int)preview.x, (int)preview.y,
                           (int)preview.width, (int)preview.height,
                           gui->theme.panelBorder);

        char hex[16];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                 color->r, color->g, color->b);
        Vector2 ts = MeasureTextEx(gui->font, hex, 18.0f, 1.0f);
        DrawTextEx(gui->font, hex,
                   (Vector2){ bounds.x + bounds.width - ts.x, y + 4 },
                   18.0f, 1.0f, gui->theme.text);
    }

    setItemState(bounds, false, false, false, changed);
    return changed;
}

/* ==================== 3D gizmo / sniper ==================== */

bool VGuiGizmo3D(VGuiContext *gui, Rectangle bounds, Vector3 *position,
                 Vector3 *rotation, Vector3 *scale) {
    if (!gui || !position || !rotation || !scale) return false;
    VGuiRectState state = VGuiItemState(gui, bounds, "gizmo3d");
    bool changed = false;
    Vector2 center = { bounds.x + bounds.width  * 0.5f,
                       bounds.y + bounds.height * 0.5f };

    if (state.held) {
        position->x += (gui->mouse.x - center.x) * 0.002f;
        position->y -= (gui->mouse.y - center.y) * 0.002f;
        changed = true;
    }
    (void)rotation;
    (void)scale;

    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    DrawLineEx(center, (Vector2){ center.x + 72, center.y }, 3.0f, (Color){ 230,  41,  55, 255 });
    DrawLineEx(center, (Vector2){ center.x, center.y - 72 }, 3.0f, (Color){   0, 228,  48, 255 });
    DrawLineEx(center, (Vector2){ center.x - 48, center.y + 48 }, 3.0f, (Color){  0, 121, 241, 255 });
    DrawCircle((int)center.x, (int)center.y, state.held ? 9.0f : 6.0f,
               state.hovered ? gui->theme.accent : gui->theme.text);
    setItemState(bounds, state.hovered, state.held, state.pressed, changed);
    return changed;
}

bool VGuiSniper(VGuiContext *gui, Rectangle bounds, Vector2 *aim01) {
    if (!gui || !aim01) return false;
    int id;
    bool hovered  = beginControl(gui, bounds, &id);
    bool dragging = (gui->activeId == id) && gui->mouseDown;

    if (dragging || (hovered && gui->mousePressed)) {
        aim01->x = clamp01((gui->mouse.x - bounds.x) / bounds.width);
        aim01->y = clamp01((gui->mouse.y - bounds.y) / bounds.height);
    }
    float x = bounds.x + clamp01(aim01->x) * bounds.width;
    float y = bounds.y + clamp01(aim01->y) * bounds.height;

    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    DrawLine((int)x, (int)bounds.y,
             (int)x, (int)(bounds.y + bounds.height), gui->theme.accent);
    DrawLine((int)bounds.x, (int)y,
             (int)(bounds.x + bounds.width), (int)y, gui->theme.accent);
    DrawCircle((int)x, (int)y, 8.0f,
               dragging ? gui->theme.text : gui->theme.accent);
    DrawCircle((int)x, (int)y, 3.0f, gui->theme.controlActive);

    bool changed = dragging || endControl(gui, id, hovered);
    setItemState(bounds, hovered, dragging, changed, changed);
    return changed;
}

/* ==================== Plots ==================== */

void VGuiPlotLines(VGuiContext *gui, Rectangle bounds, const float *values,
                   int count, float minValue, float maxValue, Color color) {
    if (!gui || !values || count < 2) return;
    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    float range = maxValue - minValue;
    if (range <= 0.0f) range = 1.0f;

    Vector2 prev = {
        bounds.x,
        bounds.y + bounds.height * (1.0f - (values[0] - minValue) / range)
    };
    for (int i = 1; i < count; ++i) {
        float x = bounds.x + bounds.width * (float)i / (count - 1);
        float y = bounds.y + bounds.height * (1.0f - (values[i] - minValue) / range);
        DrawLineEx(prev, (Vector2){ x, y }, 2.0f, color);
        prev = (Vector2){ x, y };
    }
    setItemState(bounds, false, false, false, false);
}

void VGuiPlotLines2(VGuiContext *gui, Rectangle bounds,
                    const float *values, int count,
                    float minValue, float maxValue,
                    Color lineColor, VGuiPlotConfig cfg)
{
    if (!gui || !values || count < 2) return;

    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    float range = maxValue - minValue;
    if (range <= 0.0f) range = 1.0f;

    /* сетка */
    if (cfg.gridY > 0) {
        for (int i = 1; i < cfg.gridY; ++i) {
            float y = bounds.y + bounds.height * (float)i / cfg.gridY;
            DrawLineEx((Vector2){ bounds.x, y },
                       (Vector2){ bounds.x + bounds.width, y },
                       1.0f, (Color){ 80, 90, 110, 120 });
        }
    }
    if (cfg.gridX > 0) {
        for (int i = 1; i < cfg.gridX; ++i) {
            float x = bounds.x + bounds.width * (float)i / cfg.gridX;
            DrawLineEx((Vector2){ x, bounds.y },
                       (Vector2){ x, bounds.y + bounds.height },
                       1.0f, (Color){ 80, 90, 110, 120 });
        }
    }

    /* заливка */
    if (cfg.fill && cfg.fillColor.a > 0) {
        for (int i = 0; i < count - 1; ++i) {
            float x0 = bounds.x + bounds.width * (float)i / (count - 1);
            float x1 = bounds.x + bounds.width * (float)(i + 1) / (count - 1);
            float y0 = bounds.y + bounds.height * (1.0f - (values[i] - minValue) / range);
            float y1 = bounds.y + bounds.height * (1.0f - (values[i+1] - minValue) / range);
            float yBase = bounds.y + bounds.height;

            DrawTriangle((Vector2){ x0, yBase }, (Vector2){ x0, y0 }, (Vector2){ x1, y1 },
                         cfg.fillColor);
            DrawTriangle((Vector2){ x0, yBase }, (Vector2){ x1, y1 }, (Vector2){ x1, yBase },
                         cfg.fillColor);
        }
    }

    /* кривая */
    Vector2 prev = {
        bounds.x,
        bounds.y + bounds.height * (1.0f - (values[0] - minValue) / range)
    };
    for (int i = 1; i < count; ++i) {
        float x = bounds.x + bounds.width * (float)i / (count - 1);
        float y = bounds.y + bounds.height * (1.0f - (values[i] - minValue) / range);
        DrawLineEx(prev, (Vector2){ x, y }, 2.0f, lineColor);
        prev = (Vector2){ x, y };
    }

    /* overlay */
    char buf[128];
    const char *overlay = cfg.overlayText;
    if (!overlay) {
        float vmin = values[0], vmax = values[0];
        for (int i = 1; i < count; ++i) {
            if (values[i] < vmin) vmin = values[i];
            if (values[i] > vmax) vmax = values[i];
        }
        snprintf(buf, sizeof(buf), "min=%.2f max=%.2f last=%.2f",
                 vmin, vmax, values[count - 1]);
        overlay = buf;
    }
    DrawTextEx(gui->font, overlay,
               (Vector2){ bounds.x + 8, bounds.y + 6 },
               14.0f, 1.0f, gui->theme.mutedText);

    setItemState(bounds, false, false, false, false);
}

void VGuiPlotHistogram(VGuiContext *gui, Rectangle bounds,
                       const float *values, int count,
                       float minValue, float maxValue,
                       Color barColor, const char *overlayText)
{
    if (!gui || !values || count <= 0) return;

    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    float range = maxValue - minValue;
    if (range <= 0.0f) range = 1.0f;

    for (int i = 1; i < 4; ++i) {
        float y = bounds.y + bounds.height * (float)i / 4.0f;
        DrawLineEx((Vector2){ bounds.x, y },
                   (Vector2){ bounds.x + bounds.width, y },
                   1.0f, (Color){ 80, 90, 110, 120 });
    }

    float barW = bounds.width / (float)count;
    for (int i = 0; i < count; ++i) {
        float t = (values[i] - minValue) / range;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float h = bounds.height * t;
        float x = bounds.x + i * barW;
        float y = bounds.y + bounds.height - h;
        DrawRectangleRec((Rectangle){ x + 1, y, barW - 2, h }, barColor);
    }

    char buf[128];
    if (!overlayText) {
        float vmin = values[0], vmax = values[0];
        for (int i = 1; i < count; ++i) {
            if (values[i] < vmin) vmin = values[i];
            if (values[i] > vmax) vmax = values[i];
        }
        snprintf(buf, sizeof(buf), "min=%.2f max=%.2f", vmin, vmax);
        overlayText = buf;
    }
    DrawTextEx(gui->font, overlayText,
               (Vector2){ bounds.x + 8, bounds.y + 6 },
               14.0f, 1.0f, gui->theme.mutedText);

    setItemState(bounds, false, false, false, false);
}
/* ==================== Tab panel ==================== */

VGuiTabPanel VGuiBeginTabPanel(VGuiContext *gui, Rectangle bounds) {
    VGuiTabPanel panel = {0};
    if (!gui) return panel;

    DrawRectangleRec(bounds, gui->theme.panel);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    panel.content = (Rectangle){
        bounds.x + 8.0f, bounds.y + 8.0f,
        bounds.width - 16.0f, bounds.height - 16.0f
    };
    VGuiPushId(gui, "tabpanel");
    return panel;
}

void VGuiEndTabPanel(VGuiContext *gui, VGuiTabPanel panel) {
    (void)panel;
    if (!gui) return;
    VGuiPopId(gui);
}

/* ==================== Table ==================== */

static VGuiTableState g_tableState = {0};

bool VGuiBeginTable(VGuiContext *gui, const char *id, Rectangle bounds,
                    int rows, int cols, float rowHeight,
                    const float *colWidths)
{
    if (!gui || rows <= 0 || cols <= 0 || cols > VGUI_TABLE_MAX_COLS) return false;
    if (g_tableState.active) return false;

    g_tableState.bounds       = bounds;
    g_tableState.rows         = rows;
    g_tableState.cols         = cols;
    g_tableState.rowHeight    = rowHeight;
    g_tableState.headerHeight = rowHeight;
    g_tableState.currentRow   = -1;
    g_tableState.currentCol   = -1;
    g_tableState.cursorY      = bounds.y;
    g_tableState.active       = true;

    float totalWidth = bounds.width;
    if (colWidths) {
        float sum = 0.0f;
        for (int i = 0; i < cols; ++i) sum += colWidths[i];
        float scale = (sum > 0.0f) ? totalWidth / sum : 1.0f;
        float x = bounds.x;
        for (int i = 0; i < cols; ++i) {
            g_tableState.colX[i] = x;
            g_tableState.colW[i] = colWidths[i] * scale;
            x += g_tableState.colW[i];
        }
    } else {
        float w = totalWidth / (float)cols;
        for (int i = 0; i < cols; ++i) {
            g_tableState.colX[i] = bounds.x + i * w;
            g_tableState.colW[i] = w;
        }
    }

    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    VGuiPushId(gui, id ? id : "table");
    return true;
}

void VGuiEndTable(VGuiContext *gui) {
    if (!gui || !g_tableState.active) return;
    VGuiPopId(gui);
    g_tableState.active = false;
}

void VGuiTableHeaderRow(VGuiContext *gui) {
    if (!gui || !g_tableState.active) return;
    g_tableState.currentRow = -1;
    g_tableState.cursorY    = g_tableState.bounds.y;
}

void VGuiTableNextRow(VGuiContext *gui) {
    if (!gui || !g_tableState.active) return;
    g_tableState.currentRow++;
    g_tableState.cursorY += g_tableState.rowHeight;

    if (g_tableState.currentRow < g_tableState.rows) {
        float y = g_tableState.cursorY;
        DrawLineEx((Vector2){ g_tableState.bounds.x, y },
                   (Vector2){ g_tableState.bounds.x + g_tableState.bounds.width, y },
                   1.0f, (Color){ 90, 100, 125, 100 });
    }
}

void VGuiTableSetColumn(VGuiContext *gui, int col) {
    if (!gui || !g_tableState.active) return;
    if (col < 0 || col >= g_tableState.cols) return;
    g_tableState.currentCol = col;
}

void VGuiTableHeaderText(VGuiContext *gui, int col, const char *text) {
    if (!gui || !text || col < 0 || col >= g_tableState.cols) return;

    float x = g_tableState.colX[col];
    float w = g_tableState.colW[col];
    float y = g_tableState.bounds.y;

    DrawRectangleRec((Rectangle){ x, y, w, g_tableState.headerHeight },
                     gui->theme.controlActive);
    DrawRectangleLines((int)x, (int)y, (int)w, (int)g_tableState.headerHeight,
                       gui->theme.panelBorder);

    Vector2 ts = MeasureTextEx(gui->font, text, 16.0f, 1.0f);
    float tx = x + (w - ts.x) * 0.5f;
    float ty = y + (g_tableState.headerHeight - ts.y) * 0.5f;
    DrawTextEx(gui->font, text, (Vector2){ tx, ty },
               16.0f, 1.0f, gui->theme.text);
}

void VGuiTableCellText(VGuiContext *gui, int col, const char *text) {
    if (!gui || !text || col < 0 || col >= g_tableState.cols) return;

    float x = g_tableState.colX[col];
    float y = g_tableState.cursorY;

    Vector2 ts = MeasureTextEx(gui->font, text, 16.0f, 1.0f);
    float ty = y + (g_tableState.rowHeight - ts.y) * 0.5f;
    DrawTextEx(gui->font, text, (Vector2){ x + 8, ty },
               16.0f, 1.0f, gui->theme.text);
}

bool VGuiTableCheckbox(VGuiContext *gui, int col, bool *value) {
    if (!gui || !value || col < 0 || col >= g_tableState.cols) return false;

    float x = g_tableState.colX[col];
    float w = g_tableState.colW[col];
    float y = g_tableState.cursorY;

    float boxSize = g_tableState.rowHeight - 8.0f;
    Rectangle cbBounds = { x + (w - boxSize) * 0.5f, y + 4, boxSize, boxSize };
    return VGuiCheckBox(gui, cbBounds, value, NULL);
}

bool VGuiTableDragFloat(VGuiContext *gui, int col, float *value,
                        float minV, float maxV, const char *fmt)
{
    if (!gui || !value || col < 0 || col >= g_tableState.cols) return false;

    float x = g_tableState.colX[col];
    float w = g_tableState.colW[col];
    float y = g_tableState.cursorY;

    Rectangle rf = { x + 4, y + 3, w - 8, g_tableState.rowHeight - 6 };
    return VGuiDragFloat(gui, rf, value, 0.01f, minV, maxV, fmt ? fmt : "%.2f");
}

bool VGuiTableDragInt(VGuiContext *gui, int col, int *value,
                      int minV, int maxV, const char *fmt)
{
    if (!gui || !value || col < 0 || col >= g_tableState.cols) return false;

    float x = g_tableState.colX[col];
    float w = g_tableState.colW[col];
    float y = g_tableState.cursorY;

    Rectangle rf = { x + 4, y + 3, w - 8, g_tableState.rowHeight - 6 };
    return VGuiDragInt(gui, rf, value, 1, minV, maxV, fmt ? fmt : "%d");
}

bool VGuiTableButton(VGuiContext *gui, int col, const char *label) {
    if (!gui || col < 0 || col >= g_tableState.cols) return false;

    float x = g_tableState.colX[col];
    float w = g_tableState.colW[col];
    float y = g_tableState.cursorY;

    Rectangle rf = { x + 4, y + 3, w - 8, g_tableState.rowHeight - 6 };
    return VGuiSmallButton(gui, rf, label);
}
/* ==================== Ring plot ==================== */

VGuiRingPlot VGuiRingPlotCreate(int capacity) {
    VGuiRingPlot p = {0};
    if (capacity < 2) capacity = 2;
    p.values   = calloc((size_t)capacity, sizeof(float));
    p.capacity = capacity;
    p.head     = 0;
    p.count    = 0;
    return p;
}

void VGuiRingPlotDestroy(VGuiRingPlot *plot) {
    if (!plot) return;
    if (plot->values) free(plot->values);
    memset(plot, 0, sizeof(*plot));
}

void VGuiRingPlotPush(VGuiRingPlot *plot, float value) {
    if (!plot || !plot->values || plot->capacity <= 0 || !isfinite(value)) return;
    plot->values[plot->head] = value;
    plot->head = (plot->head + 1) % plot->capacity;
    if (plot->count < plot->capacity) plot->count++;
}

void VGuiRingPlotDraw(VGuiContext *gui, Rectangle bounds,
                      VGuiRingPlot *plot,
                      float minValue, float maxValue,
                      Color lineColor)
{
    if (!gui || !plot || !plot->values || plot->capacity <= 0 ||
        bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    DrawRectangleRec(bounds, gui->theme.control);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);
    if (plot->count < 2) {
        setItemState(bounds, false, false, false, false);
        return;
    }

    float range = maxValue - minValue;
    if (!isfinite(range) || range <= 0.0f) range = 1.0f;
    for (int i = 1; i < 4; ++i) {
        float y = bounds.y + bounds.height * (float)i / 4.0f;
        DrawLineEx((Vector2){ bounds.x, y },
                   (Vector2){ bounds.x + bounds.width, y },
                   1.0f, (Color){ 80, 90, 110, 120 });
    }

    int start = (plot->head - plot->count + plot->capacity) % plot->capacity;
    Vector2 previous = {0};
    for (int i = 0; i < plot->count; ++i) {
        float v = plot->values[(start + i) % plot->capacity];
        if (!isfinite(v)) v = minValue;
        float ratio = clamp01((v - minValue) / range);
        Vector2 point = {
            bounds.x + bounds.width * (float)i / (float)(plot->count - 1),
            bounds.y + bounds.height * (1.0f - ratio)
        };
        if (i > 0) DrawLineEx(previous, point, 2.0f, lineColor);
        previous = point;
    }
    setItemState(bounds, false, false, false, false);
}

/* ==================== Blueprint-style graph ==================== */

static int graphFindNode(const VGuiNodeGraph *graph, int id) {
    for (int i = 0; i < graph->nodeCount; ++i)
        if (graph->nodes[i].id == id) return i;
    return -1;
}

static Vector2 graphPinPosition(const VGuiNodeGraph *graph, int nodeId,
                                int pin, bool output, Rectangle canvas) {
    int index = graphFindNode(graph, nodeId);
    if (index < 0) return (Vector2){0, 0};
    const VGuiGraphNode *node = &graph->nodes[index];
    float x = output ? node->position.x + node->size.x : node->position.x;
    float y = node->position.y + 31.0f + 24.0f * ((float)pin + 0.5f);
    return (Vector2){ canvas.x + graph->pan.x + x * graph->zoom,
                      canvas.y + graph->pan.y + y * graph->zoom };
}

void VGuiNodeGraphInit(VGuiNodeGraph *graph) {
    if (!graph) return;
    memset(graph, 0, sizeof(*graph));
    graph->zoom = 1.0f;
    graph->selectedNode = -1;
    graph->draggingNode = -1;
    graph->pendingNode = -1;
    graph->pendingPin = -1;
}

bool VGuiNodeGraphAddNode(VGuiNodeGraph *graph, int id, const char *title,
                          Vector2 position, Vector2 size,
                          const char * const *inputs, int inputCount,
                          const char * const *outputs, int outputCount,
                          Color titleColor) {
    if (!graph || graph->nodeCount >= VGUI_GRAPH_MAX_NODES ||
        graphFindNode(graph, id) >= 0) return false;
    if (inputCount < 0 || outputCount < 0 ||
        inputCount > VGUI_GRAPH_MAX_PINS || outputCount > VGUI_GRAPH_MAX_PINS)
        return false;
    VGuiGraphNode *node = &graph->nodes[graph->nodeCount++];
    memset(node, 0, sizeof(*node));
    node->id = id;
    snprintf(node->title, sizeof(node->title), "%s", title ? title : "Node");
    node->position = position;
    node->size = (Vector2){ size.x > 80.0f ? size.x : 180.0f,
                            size.y > 50.0f ? size.y : 80.0f };
    node->inputCount = inputCount;
    node->outputCount = outputCount;
    node->titleColor = titleColor;
    for (int i = 0; i < inputCount; ++i)
        snprintf(node->inputs[i], sizeof(node->inputs[i]), "%s",
                 inputs && inputs[i] ? inputs[i] : "In");
    for (int i = 0; i < outputCount; ++i)
        snprintf(node->outputs[i], sizeof(node->outputs[i]), "%s",
                 outputs && outputs[i] ? outputs[i] : "Out");
    float rows = (float)(inputCount > outputCount ? inputCount : outputCount);
    float minHeight = 40.0f + rows * 24.0f;
    if (node->size.y < minHeight) node->size.y = minHeight;
    return true;
}

bool VGuiNodeGraphAddLink(VGuiNodeGraph *graph, int fromNode, int fromPin,
                          int toNode, int toPin) {
    if (!graph || graph->linkCount >= VGUI_GRAPH_MAX_LINKS) return false;
    int fi = graphFindNode(graph, fromNode), ti = graphFindNode(graph, toNode);
    if (fi < 0 || ti < 0 || fromPin < 0 || toPin < 0 ||
        fromPin >= graph->nodes[fi].outputCount ||
        toPin >= graph->nodes[ti].inputCount || fromNode == toNode) return false;
    /* One incoming connection per input pin. */
    for (int i = 0; i < graph->linkCount; ++i)
        if (graph->links[i].toNode == toNode && graph->links[i].toPin == toPin)
            return false;
    graph->links[graph->linkCount++] = (VGuiGraphLink){fromNode, fromPin, toNode, toPin};
    return true;
}

void VGuiNodeGraphRemoveLink(VGuiNodeGraph *graph, int index) {
    if (!graph || index < 0 || index >= graph->linkCount) return;
    for (int i = index + 1; i < graph->linkCount; ++i)
        graph->links[i - 1] = graph->links[i];
    --graph->linkCount;
}

static void graphDrawWire(Vector2 a, Vector2 b, Color color) {
    Vector2 previous = a;
    float curve = fabsf(b.x - a.x) * 0.5f;
    if (curve < 36.0f) curve = 36.0f;
    for (int i = 1; i <= 20; ++i) {
        float t = (float)i / 20.0f, u = 1.0f - t;
        Vector2 p = {
            u*u*u*a.x + 3*u*u*t*(a.x+curve) + 3*u*t*t*(b.x-curve) + t*t*t*b.x,
            u*u*u*a.y + 3*u*u*t*a.y + 3*u*t*t*b.y + t*t*t*b.y
        };
        DrawLineEx(previous, p, 3.0f, color);
        previous = p;
    }
}

bool VGuiNodeGraphDraw(VGuiContext *gui, VGuiNodeGraph *graph, Rectangle bounds) {
    if (!gui || !graph || bounds.width <= 0 || bounds.height <= 0) return false;
    bool changed = false;
    const Color canvasColor = {20, 23, 31, 255};
    DrawRectangleRec(bounds, canvasColor);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, gui->theme.panelBorder);

    float spacing = 24.0f * graph->zoom;
    if (spacing < 8.0f) spacing = 8.0f;
    float gridX = fmodf(graph->pan.x, spacing);
    float gridY = fmodf(graph->pan.y, spacing);
    for (float x = bounds.x + gridX; x < bounds.x + bounds.width; x += spacing)
        DrawLineEx((Vector2){x,bounds.y}, (Vector2){x,bounds.y+bounds.height},
                   1.0f, (Color){55,60,72,150});
    for (float y = bounds.y + gridY; y < bounds.y + bounds.height; y += spacing)
        DrawLineEx((Vector2){bounds.x,y}, (Vector2){bounds.x+bounds.width,y},
                   1.0f, (Color){55,60,72,150});

    bool overCanvas = contains(bounds, gui->mouse);
    if (overCanvas && gui->mousePressed) {
        graph->selectedNode = -1;
        graph->pendingNode = -1;
    }
    if (overCanvas && IsMouseButtonDown(2)) {
        if (!graph->panning) { graph->lastMouse = gui->mouse; graph->panning = true; }
        graph->pan.x += gui->mouse.x - graph->lastMouse.x;
        graph->pan.y += gui->mouse.y - graph->lastMouse.y;
        graph->lastMouse = gui->mouse;
    } else graph->panning = false;
    if (overCanvas && GetMouseWheelMove() != 0.0f) {
        float oldZoom = graph->zoom;
        graph->zoom *= GetMouseWheelMove() > 0 ? 1.1f : 0.9f;
        if (graph->zoom < 0.35f) graph->zoom = 0.35f;
        if (graph->zoom > 2.5f) graph->zoom = 2.5f;
        float factor = graph->zoom / oldZoom;
        graph->pan.x = gui->mouse.x - bounds.x - (gui->mouse.x - bounds.x - graph->pan.x) * factor;
        graph->pan.y = gui->mouse.y - bounds.y - (gui->mouse.y - bounds.y - graph->pan.y) * factor;
    }

    for (int i = 0; i < graph->linkCount; ++i) {
        VGuiGraphLink *link = &graph->links[i];
        Vector2 a = graphPinPosition(graph, link->fromNode, link->fromPin, true, bounds);
        Vector2 b = graphPinPosition(graph, link->toNode, link->toPin, false, bounds);
        graphDrawWire(a, b, (Color){90,180,250,255});
    }
    if (graph->pendingNode >= 0) {
        Vector2 a = graphPinPosition(graph, graph->pendingNode, graph->pendingPin, true, bounds);
        graphDrawWire(a, gui->mouse, gui->theme.accent);
    }

    for (int i = 0; i < graph->nodeCount; ++i) {
        VGuiGraphNode *node = &graph->nodes[i];
        float z = graph->zoom;
        Rectangle r = {bounds.x+graph->pan.x+node->position.x*z,
                       bounds.y+graph->pan.y+node->position.y*z,
                       node->size.x*z,node->size.y*z};
        Rectangle header = {r.x,r.y,r.width,28.0f*z};
        if (overCanvas && gui->mousePressed && contains(header, gui->mouse)) {
            graph->selectedNode = node->id;
            graph->draggingNode = node->id;
            graph->lastMouse = gui->mouse;
        }
        if (graph->draggingNode == node->id && gui->mouseDown) {
            node->position.x += (gui->mouse.x-graph->lastMouse.x)/z;
            node->position.y += (gui->mouse.y-graph->lastMouse.y)/z;
            graph->lastMouse = gui->mouse;
            changed = true;
            r.x = bounds.x+graph->pan.x+node->position.x*z;
            r.y = bounds.y+graph->pan.y+node->position.y*z;
            header.x=r.x; header.y=r.y;
        }
        if (gui->mouseReleased && graph->draggingNode == node->id)
            graph->draggingNode = -1;

        DrawRectangleRec(r, (Color){37,42,53,255});
        DrawRectangleLines((int)r.x,(int)r.y,(int)r.width,(int)r.height,
            graph->selectedNode==node->id ? gui->theme.accent : gui->theme.panelBorder);
        DrawRectangleRec(header, node->titleColor.a ? node->titleColor : (Color){55,95,145,255});
        if (gui->font.ok) DrawTextEx(gui->font,node->title,
            (Vector2){header.x+8*z,header.y+5*z},16*z,1,gui->theme.text);

        for (int p = 0; p < node->inputCount; ++p) {
            Vector2 pin = graphPinPosition(graph,node->id,p,false,bounds);
            bool hit = overCanvas && Vector2Distance(pin,gui->mouse) < 9*z;
            DrawCircleV(pin,6*z,hit?gui->theme.accent:(Color){110,190,130,255});
            if(gui->font.ok) DrawTextEx(gui->font,node->inputs[p],
                (Vector2){r.x+12*z,pin.y-8*z},14*z,1,gui->theme.text);
            if(hit && gui->mousePressed && graph->pendingNode>=0) {
                if(VGuiNodeGraphAddLink(graph,graph->pendingNode,graph->pendingPin,node->id,p)) changed=true;
                graph->pendingNode=-1;
            }
        }
        for (int p = 0; p < node->outputCount; ++p) {
            Vector2 pin = graphPinPosition(graph,node->id,p,true,bounds);
            bool hit = overCanvas && Vector2Distance(pin,gui->mouse) < 9*z;
            DrawCircleV(pin,6*z,hit?gui->theme.accent:(Color){230,170,80,255});
            if(gui->font.ok) {
                Vector2 ts=MeasureTextEx(gui->font,node->outputs[p],14*z,1);
                DrawTextEx(gui->font,node->outputs[p],
                    (Vector2){r.x+r.width-ts.x-12*z,pin.y-8*z},14*z,1,gui->theme.text);
            }
            if(hit && gui->mousePressed) {
                graph->pendingNode=node->id; graph->pendingPin=p;
            }
        }
    }
    setItemState(bounds,overCanvas,false,false,changed);
    return changed;
}
