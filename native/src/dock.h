// ─── Pulse Engine dock system: Unreal-style dockable/floating/resizable windows ───
#pragma once
#include "ui.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

enum { DOCK_LEFT = 0, DOCK_RIGHT = 1, DOCK_BOTTOM = 2, DOCK_FLOAT = 3, DOCK_NATIVE = 4, DOCK_CENTER = 5 };

struct DockWindow {
    std::string id, title;
    int area = DOCK_LEFT;
    int slot = 0;                          // index along the area's primary axis
    int sub = 0;                           // 0/1: cross-axis half within the slot (4-way docking)
    int order = 0;
    bool open = true;
    UIRect rect = { 260, 120, 360, 420 };  // floating rect
};

class DockManager {
public:
    float leftW = 260, rightW = 310, bottomH = 210;

    void addWindow(const char* id, const char* title, int area, int order);
    DockWindow* find(const char* id);
    void toggle(const char* id);

    // handles all chrome input + draws areas, tabs, floating windows.
    // `content(ui, id)` is invoked for each visible window, inside its panel.
    void drawAll(UI& ui, int screenW, int screenH, float menuH,
                 const std::function<void(UI&, const std::string&)>& content);

    // rect where the 3D scene shows through: the "viewport" window's content when
    // it is the active/visible tab (docked or floating), else empty (w==0)
    UIRect viewportRect(int screenW, int screenH, float menuH);
    bool dragActive() const { return dragWin_ >= 0 || splitter_ >= 0 || resizeWin_ >= 0 || subSplitArea_ >= 0 || primarySplitArea_ >= 0; }
    bool draggingWindow() const { return dragWin_ >= 0; }
    bool windowHovered(const char* id, float mx, float my) const;
    void setActive(const char* id);
    void drawDragOverlay(UI& ui) const;

    std::string saveLayout() const;
    void loadLayout(const std::string& text);

    // set when a window asks to become a native OS window (pop-out button, or a
    // tab dropped outside the main window); the app consumes it and creates the window
    std::string popOutRequest;
    float popOutX = 0, popOutY = 0;   // client coords of the drop

    // repaints the 3D scene into `contentRect` on top of whatever is already drawn.
    // Invoked while laying out the viewport window so that, when it floats over other
    // panels, its 3D shows above them (the plain framebuffer render is covered by the
    // docked panels drawn earlier). Set by the app; may be null.
    std::function<void(const UIRect&)> renderViewportOverlay;

private:
    // mode: 0 = tab into (area,slot,sub); 1 = new slot at insertAt; 2 = new sub-split (sub = side)
    struct DropSpot { int area = -1; int slot = 0; int sub = 0; int mode = 0; int insertAt = 0; };

    std::vector<DockWindow> wins_;          // vector order = z-order for floating
    std::map<int, int> active_;             // cellKey(area,slot,sub) → active window ORDER
    std::map<int, float> subSplit_;         // (area*100+slot) → ratio of sub 0
    std::map<int, float> primarySplit_;
    int dragWin_ = -1;
    float dragOffX_ = 0, dragOffY_ = 0, pressX_ = 0, pressY_ = 0;
    bool dragDetached_ = false;
    int splitter_ = -1;
    float centerSplit = 0.5f;        // ratio of the first center column (2-slot split)
    bool centerDragging_ = false;
    int subSplitArea_ = -1, subSplitSlot_ = -1;   // sub-splitter being dragged
    int primarySplitArea_ = -1, primarySplitBoundary_ = -1;
    int resizeWin_ = -1;
    int resizeEdges_ = 0;
    int screenW_ = 0, screenH_ = 0;
    float menuH_ = 0;

    static int cellKey(int area, int slot, int sub) { return (area * 100 + slot) * 4 + sub; }
    static bool crossHoriz(int area) { return area == DOCK_LEFT || area == DOCK_RIGHT; }
    std::vector<int> cellWins(int area, int slot, int sub) const;
    int slotCount(int area) const;
    int subCount(int area, int slot) const;         // 1 or 2
    void normalizeSlots(int area);
    void clearPrimarySplits(int area);
    UIRect areaRect(int area) const;
    UIRect slotRect(int area, int slot) const;
    UIRect subRect(int area, int slot, int sub) const;
    DropSpot dropTarget(float mx, float my) const;
    // ── docking compass (Unreal-style 5-zone drop indicator) ──
    // dir: 0 = center (tab into), 1 = left, 2 = right, 3 = top, 4 = bottom
    bool cellUnder(float mx, float my, int& area, int& slot, int& sub, UIRect& cell) const;
    int cellDir(const UIRect& cell, float mx, float my) const;
    DropSpot spotFromDir(int area, int slot, int sub, int dir) const;
    void drawDockCompass(UI& ui, const UIRect& cell, int activeDir) const;
    int topFloatingUnderMouse(float mx, float my) const;
    void raiseWindow(int idx);
    void dockWindow(int idx, const DropSpot& spot);
};
