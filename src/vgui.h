#ifndef VLCRLIB_VGUI_H
#define VLCRLIB_VGUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vtext.h"
#include "vshapes.h"
#include <stdint.h>
#include <stdbool.h>

typedef uint64_t VGuiId;

typedef struct VGuiRectState {
    bool hovered;
    bool held;
    bool pressed;
    bool released;
    bool active;
} VGuiRectState;

typedef struct VGuiTheme {
    Color panel;
    Color panelBorder;
    Color control;
    Color controlHover;
    Color controlActive;
    Color accent;
    Color text;
    Color mutedText;
} VGuiTheme;

typedef struct VGuiContext {
    Font font;
    Vector2 mouse;
    int mousePressed;
    int mouseDown;
    int mouseReleased;
    int activeId;
    int hotId;
    VGuiId idStack[16];
    int idStackSize;
    VGuiId activeGuiId;
    VGuiId hotGuiId;
    float deltaTime;
    Vector2 displaySize;
    Vector2 cursor;
    Vector2 windowStart;
    Vector2 windowSize;
    float itemSpacing;
    float framePadding;
    bool windowOpen;
    VGuiTheme theme;

    /* ---- item state ---- */
    Rectangle lastItemBounds;
    bool      lastItemHovered;
    bool      lastItemActive;
    bool      lastItemClicked;
    bool      lastItemEdited;
    const char *lastItemTooltip;

    /* ---- disabled stack ---- */
    int disabledDepth;

    /* ---- drag widget tracking ---- */
    VGuiId dragActiveId;
    Vector2 dragLastMouse;

    /* ---- group ---- */
    Vector2 groupStart;
    int     groupDepth;

    /* ---- popup (combo / menus) ---- */
    VGuiId openPopupId;
} VGuiContext;

/* ============================================================ */
/*  Theme / lifecycle                                            */
/* ============================================================ */
VGuiTheme VGuiDefaultTheme(void);
void VGuiBegin(VGuiContext *gui, Font font);
void VGuiEnd(VGuiContext *gui);
void VGuiBeginFrame(VGuiContext *gui, Font font, float deltaTime);
void VGuiEndFrame(VGuiContext *gui);

/* ============================================================ */
/*  ID stack                                                     */
/* ============================================================ */
void   VGuiPushId(VGuiContext *gui, const char *id);
void   VGuiPushIdInt(VGuiContext *gui, int id);
void   VGuiPopId(VGuiContext *gui);
VGuiId VGuiGetId(const VGuiContext *gui, const char *label);
VGuiRectState VGuiItemState(VGuiContext *gui, Rectangle bounds, const char *label);

/* ============================================================ */
/*  Item state API                                               */
/* ============================================================ */
bool      VGuiIsItemHovered(void);
bool      VGuiIsItemActive(void);
bool      VGuiIsItemClicked(void);
bool      VGuiIsItemEdited(void);
Rectangle VGuiGetItemRectMin(void);
Rectangle VGuiGetItemRectMax(void);
Vector2   VGuiGetItemSize(void);
void      VGuiSetItemTooltip(const char *text);

/* ============================================================ */
/*  Disabled / layout                                            */
/* ============================================================ */
void VGuiBeginDisabled(bool disabled);
void VGuiEndDisabled(void);
void VGuiSeparator(void);
void VGuiSeparatorText(const char *text);
void VGuiDummy(float width, float height);
void VGuiBeginGroup(void);
void VGuiEndGroup(void);

/* ============================================================ */
/*  Window / cursor                                              */
/* ============================================================ */
bool      VGuiBeginWindow(VGuiContext *gui, const char *title, Rectangle bounds, bool *open);
void      VGuiEndWindow(VGuiContext *gui);
void      VGuiSameLine(VGuiContext *gui, float spacing);
void      VGuiSpacing(VGuiContext *gui, float amount);
Rectangle VGuiNextItem(VGuiContext *gui, float width, float height);

/* ============================================================ */
/*  Text / panels                                                */
/* ============================================================ */
void VGuiPanel(VGuiContext *gui, Rectangle bounds, const char *title);
void VGuiLabel(VGuiContext *gui, Vector2 position, const char *text, Color color);
void VGuiText(VGuiContext *gui, Rectangle bounds, const char *text, float fontSize, Color color);
void VGuiTextColored(VGuiContext *gui, Vector2 position, const char *text, Color color);
void VGuiTextDisabled(VGuiContext *gui, Vector2 position, const char *text);
void VGuiBulletText(VGuiContext *gui, Vector2 position, const char *text);

/* ============================================================ */
/*  Buttons / toggles                                            */
/* ============================================================ */
bool VGuiButton(VGuiContext *gui, Rectangle bounds, const char *label);
bool VGuiSmallButton(VGuiContext *gui, Rectangle bounds, const char *label);
bool VGuiInvisibleButton(VGuiContext *gui, Rectangle bounds, const char *id);
bool VGuiCheckBox(VGuiContext *gui, Rectangle bounds, bool *checked, const char *label);
bool VGuiRadioButton(VGuiContext *gui, Rectangle bounds, bool *selected, const char *label);

/* ============================================================ */
/*  Numeric                                                      */
/* ============================================================ */
bool VGuiSlider(VGuiContext *gui, Rectangle bounds, float *value, float minValue, float maxValue);
bool VGuiSliderInt(VGuiContext *gui, Rectangle bounds, int *value, int minValue, int maxValue);
bool VGuiDragFloat(VGuiContext *gui, Rectangle bounds, float *value,
                   float step, float minValue, float maxValue, const char *format);
bool VGuiDragInt(VGuiContext *gui, Rectangle bounds, int *value,
                 int step, int minValue, int maxValue, const char *format);
bool VGuiKnob(VGuiContext *gui, Rectangle bounds, float *value, float minValue, float maxValue);
void VGuiProgressBar(VGuiContext *gui, Rectangle bounds, float value, float minValue, float maxValue);
void VGuiLoadingBar(VGuiContext *gui, Rectangle bounds, float progress, bool animated);
void VGuiSpinner(VGuiContext *gui, Vector2 center, float radius, Color color);

/* ============================================================ */
/*  Inputs                                                       */
/* ============================================================ */
bool VGuiInputInt(VGuiContext *gui, Rectangle bounds, int *value, int minValue, int maxValue);
bool VGuiInputFloat(VGuiContext *gui, Rectangle bounds, float *value,
                    float minValue, float maxValue, float step);
bool VGuiInputText(VGuiContext *gui, Rectangle bounds, char *buffer, int capacity);

/* ============================================================ */
/*  Selectors                                                    */
/* ============================================================ */
int  VGuiTabs(VGuiContext *gui, Rectangle bounds, const char * const *labels,
              int count, int *selected);
bool VGuiCombo(VGuiContext *gui, Rectangle bounds, const char *label,
               const char * const *items, int count, int *selected);
bool VGuiSelectable(VGuiContext *gui, Rectangle bounds, const char *label, bool *selected);
int  VGuiListBox(VGuiContext *gui, Rectangle bounds, const char * const *items,
                 int count, int *selected);

/* ============================================================ */
/*  Tab panel (рамка + область контента)                          */
/* ============================================================ */
typedef struct VGuiTabPanel {
    Rectangle content;
} VGuiTabPanel;

VGuiTabPanel VGuiBeginTabPanel(VGuiContext *gui, Rectangle bounds);
void         VGuiEndTabPanel(VGuiContext *gui, VGuiTabPanel panel);

/* ============================================================ */
/*  Table (Excel-подобная сетка)                                 */
/* ============================================================ */
#define VGUI_TABLE_MAX_COLS 16

typedef struct VGuiTableState {
    Rectangle bounds;
    int       rows;
    int       cols;
    float     rowHeight;
    float     headerHeight;
    float     colX[VGUI_TABLE_MAX_COLS];
    float     colW[VGUI_TABLE_MAX_COLS];
    int       currentRow;
    int       currentCol;
    float     cursorY;
    bool      active;
} VGuiTableState;

bool VGuiBeginTable(VGuiContext *gui, const char *id, Rectangle bounds,
                    int rows, int cols, float rowHeight,
                    const float *colWidths);
void VGuiEndTable(VGuiContext *gui);

void VGuiTableHeaderRow(VGuiContext *gui);
void VGuiTableNextRow(VGuiContext *gui);
void VGuiTableSetColumn(VGuiContext *gui, int col);

void VGuiTableHeaderText(VGuiContext *gui, int col, const char *text);
void VGuiTableCellText(VGuiContext *gui, int col, const char *text);
bool VGuiTableCheckbox(VGuiContext *gui, int col, bool *value);
bool VGuiTableDragFloat(VGuiContext *gui, int col, float *value,
                        float minV, float maxV, const char *fmt);
bool VGuiTableDragInt(VGuiContext *gui, int col, int *value,
                      int minV, int maxV, const char *fmt);
bool VGuiTableButton(VGuiContext *gui, int col, const char *label);

/* ============================================================ */
/*  Tree / collapsing                                            */
/* ============================================================ */
bool VGuiCollapsingHeader(VGuiContext *gui, Rectangle bounds, const char *label, bool *open);
bool VGuiTreeNode(VGuiContext *gui, Rectangle bounds, const char *label, bool *open);
bool VGuiTreeSelectable(VGuiContext *gui, Rectangle bounds, const char *label, bool *selected);
void VGuiTreePop(VGuiContext *gui);
void VGuiTooltip(VGuiContext *gui, const char *text);
bool VGuiMenuItem(VGuiContext *gui, Rectangle bounds, const char *label, bool enabled);

/* ============================================================ */
/*  Menu bar                                                     */
/* ============================================================ */
bool VGuiBeginMenuBar(VGuiContext *gui, Rectangle bounds);
void VGuiEndMenuBar(VGuiContext *gui);
bool VGuiBeginMenu(VGuiContext *gui, const char *label, bool enabled);
void VGuiEndMenu(VGuiContext *gui);
bool VGuiMenuItemPopup(VGuiContext *gui, const char *label, bool enabled);

/* ============================================================ */
/*  Color picker / gizmo / sniper                                */
/* ============================================================ */
bool VGuiColorPicker(VGuiContext *gui, Rectangle bounds, Color *color);
bool VGuiGizmo3D(VGuiContext *gui, Rectangle bounds, Vector3 *position,
                 Vector3 *rotation, Vector3 *scale);
bool VGuiSniper(VGuiContext *gui, Rectangle bounds, Vector2 *aim01);

/* ============================================================ */
/*  Plots                                                        */
/* ============================================================ */
typedef struct VGuiPlotConfig {
    const char *overlayText;
    int         gridX;
    int         gridY;
    bool        fill;
    Color       fillColor;
} VGuiPlotConfig;

void VGuiPlotLines(VGuiContext *gui, Rectangle bounds, const float *values,
                   int count, float minValue, float maxValue, Color color);
void VGuiPlotLines2(VGuiContext *gui, Rectangle bounds,
                    const float *values, int count,
                    float minValue, float maxValue,
                    Color lineColor, VGuiPlotConfig cfg);
void VGuiPlotHistogram(VGuiContext *gui, Rectangle bounds,
                       const float *values, int count,
                       float minValue, float maxValue,
                       Color barColor, const char *overlayText);

/* ============================================================ */
/*  Ring plot                                                    */
/* ============================================================ */
typedef struct VGuiRingPlot {
    float *values;
    int    capacity;
    int    head;
    int    count;
} VGuiRingPlot;

VGuiRingPlot VGuiRingPlotCreate(int capacity);
void         VGuiRingPlotDestroy(VGuiRingPlot *plot);
void         VGuiRingPlotPush(VGuiRingPlot *plot, float value);
void         VGuiRingPlotDraw(VGuiContext *gui, Rectangle bounds,
                              VGuiRingPlot *plot,
                              float minValue, float maxValue,
                              Color lineColor);

/* ============================================================ */
/*  Blueprint-style graph (native VGui; no external node lib)    */
/* ============================================================ */
#define VGUI_GRAPH_MAX_NODES 64
#define VGUI_GRAPH_MAX_PINS  8
#define VGUI_GRAPH_MAX_LINKS 128
#define VGUI_GRAPH_LABEL_LEN 32

typedef struct VGuiGraphNode {
    int id;
    char title[64];
    Vector2 position;             /* graph-space position */
    Vector2 size;                 /* graph-space size */
    int inputCount, outputCount;
    char inputs[VGUI_GRAPH_MAX_PINS][VGUI_GRAPH_LABEL_LEN];
    char outputs[VGUI_GRAPH_MAX_PINS][VGUI_GRAPH_LABEL_LEN];
    Color titleColor;
} VGuiGraphNode;

typedef struct VGuiGraphLink {
    int fromNode, fromPin;
    int toNode, toPin;
} VGuiGraphLink;

typedef struct VGuiNodeGraph {
    VGuiGraphNode nodes[VGUI_GRAPH_MAX_NODES];
    VGuiGraphLink links[VGUI_GRAPH_MAX_LINKS];
    int nodeCount, linkCount;
    int selectedNode;
    int draggingNode;
    int pendingNode, pendingPin;
    Vector2 pan;
    float zoom;
    Vector2 lastMouse;
    bool panning;
} VGuiNodeGraph;

void VGuiNodeGraphInit(VGuiNodeGraph *graph);
bool VGuiNodeGraphAddNode(VGuiNodeGraph *graph, int id, const char *title,
                          Vector2 position, Vector2 size,
                          const char * const *inputs, int inputCount,
                          const char * const *outputs, int outputCount,
                          Color titleColor);
bool VGuiNodeGraphAddLink(VGuiNodeGraph *graph, int fromNode, int fromPin,
                          int toNode, int toPin);
void VGuiNodeGraphRemoveLink(VGuiNodeGraph *graph, int index);
/* Draws and handles the canvas. Returns true when graph data changed. */
bool VGuiNodeGraphDraw(VGuiContext *gui, VGuiNodeGraph *graph, Rectangle bounds);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VGUI_H */
