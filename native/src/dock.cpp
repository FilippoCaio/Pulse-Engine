// ─── Pulse Engine dock system implementation (multi-slot areas: 4-way docking) ───
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "dock.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

static const float TAB_H = 26;
static const float TITLE_H = 26;
static const float SPLIT_W = 11;
static const float EDGE_ZONE = 34;
static const Vec3 C_AREA_BG = { 0.09f, 0.10f, 0.115f };
static const Vec3 C_TAB = { 0.13f, 0.145f, 0.17f };
static const Vec3 C_TAB_ACTIVE = { 0.105f, 0.115f, 0.135f };
static const Vec3 C_TAB_TEXT = { 0.62f, 0.68f, 0.76f };
static const Vec3 C_ACCENT = { 0.30f, 0.62f, 0.99f };
static const Vec3 C_TITLE = { 0.15f, 0.17f, 0.20f };
static const Vec3 C_DROP = { 0.30f, 0.62f, 0.99f };
static const Vec3 C_BORDER = { 0.05f, 0.055f, 0.065f };
static const Vec3 C_HANDLE = { 0.18f, 0.22f, 0.28f };
static const Vec3 C_TAB_GRIP = { 0.40f, 0.45f, 0.53f };   // move-grip: grey, accent on hover
static const float TAB_GRIP_W = 12;                        // width the grip reserves at the tab's left
static const Vec3 C_CMP_BG = { 0.14f, 0.17f, 0.22f };     // docking-compass button fill
static const Vec3 C_CMP_BORDER = { 0.03f, 0.04f, 0.05f }; // docking-compass button outline
static const float CMP_B = 30.0f;                          // compass button size
static const float CMP_G = 6.0f;                           // gap between compass buttons

// The five docking-compass targets, centred on `cell`.
// [0] center, [1] left, [2] right, [3] top, [4] bottom.
static void compassRects(const UIRect& cell, UIRect out[5]) {
    float cx = cell.x + cell.w * 0.5f, cy = cell.y + cell.h * 0.5f;
    float h = CMP_B * 0.5f, s = CMP_B + CMP_G;
    out[0] = { cx - h,     cy - h,     CMP_B, CMP_B };
    out[1] = { cx - h - s, cy - h,     CMP_B, CMP_B };
    out[2] = { cx - h + s, cy - h,     CMP_B, CMP_B };
    out[3] = { cx - h,     cy - h - s, CMP_B, CMP_B };
    out[4] = { cx - h,     cy - h + s, CMP_B, CMP_B };
}

void DockManager::addWindow(const char* id, const char* title, int area, int order) {
    DockWindow w;
    w.id = id;
    w.title = title;
    w.area = area;
    w.order = order;
    wins_.push_back(w);
}

DockWindow* DockManager::find(const char* id) {
    for (auto& w : wins_) if (w.id == id) return &w;
    return nullptr;
}

void DockManager::toggle(const char* id) {
    DockWindow* w = find(id);
    if (!w) return;
    clearPrimarySplits(w->area);
    w->open = !w->open;
    if (w->open && w->area != DOCK_FLOAT) active_[cellKey(w->area, w->slot, w->sub)] = w->order;
    else if (!w->open) normalizeSlots(w->area);
}

void DockManager::clearPrimarySplits(int area) {
    for (auto it = primarySplit_.begin(); it != primarySplit_.end();) {
        if (it->first / 100 == area) it = primarySplit_.erase(it); else ++it;
    }
}

std::vector<int> DockManager::cellWins(int area, int slot, int sub) const {
    std::vector<int> out;
    for (int i = 0; i < (int)wins_.size(); i++) {
        const auto& w = wins_[i];
        if (w.open && w.area == area && w.slot == slot && w.sub == sub) out.push_back(i);
    }
    std::sort(out.begin(), out.end(), [&](int a, int b) { return wins_[a].order < wins_[b].order; });
    return out;
}

int DockManager::subCount(int area, int slot) const {
    bool has0 = false, has1 = false;
    for (const auto& w : wins_) {
        if (w.open && w.area == area && w.slot == slot) { if (w.sub == 0) has0 = true; else has1 = true; }
    }
    return (has0 && has1) ? 2 : 1;
}

int DockManager::slotCount(int area) const {
    int maxSlot = -1;
    for (const auto& w : wins_) {
        if (w.open && w.area == area && w.slot > maxSlot) maxSlot = w.slot;
    }
    return maxSlot + 1;
}

void DockManager::normalizeSlots(int area) {
    // compact slot indices to 0..n-1 preserving ordering
    std::vector<int> used;
    for (const auto& w : wins_) {
        if (w.open && w.area == area) used.push_back(w.slot);
    }
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    for (auto& w : wins_) {
        if (!(w.open && w.area == area)) continue;
        for (int i = 0; i < (int)used.size(); i++) {
            if (w.slot == used[i]) { w.slot = i; break; }
        }
    }
    // per slot: if the sub-split collapsed (only sub 1 left), pull it back to sub 0
    int n = slotCount(area);
    for (int s = 0; s < n; s++) {
        bool has0 = false, has1 = false;
        for (const auto& w : wins_) {
            if (w.open && w.area == area && w.slot == s) { if (w.sub == 0) has0 = true; else has1 = true; }
        }
        if (!has0 && has1) {
            for (auto& w : wins_) if (w.open && w.area == area && w.slot == s) w.sub = 0;
        }
    }
}

UIRect DockManager::subRect(int area, int slot, int sub) const {
    UIRect rc = slotRect(area, slot);
    if (subCount(area, slot) < 2) return rc;
    float r = 0.5f;
    auto it = subSplit_.find(area * 100 + slot);
    if (it != subSplit_.end()) r = it->second;
    if (crossHoriz(area)) {   // side areas split into left/right columns
        float w0 = rc.w * r;
        return sub == 0 ? UIRect{ rc.x, rc.y, w0, rc.h } : UIRect{ rc.x + w0, rc.y, rc.w - w0, rc.h };
    }
    float h0 = rc.h * r;      // bottom/center split into top/bottom rows
    return sub == 0 ? UIRect{ rc.x, rc.y, rc.w, h0 } : UIRect{ rc.x, rc.y + h0, rc.w, rc.h - h0 };
}

UIRect DockManager::areaRect(int area) const {
    float bh = slotCount(DOCK_BOTTOM) == 0 ? 0 : bottomH;
    float lw = slotCount(DOCK_LEFT) == 0 ? 0 : leftW;
    float rw = slotCount(DOCK_RIGHT) == 0 ? 0 : rightW;
    float mainH = screenH_ - menuH_ - bh;
    switch (area) {
    case DOCK_LEFT: return { 0, menuH_, lw, mainH };
    case DOCK_RIGHT: return { (float)screenW_ - rw, menuH_, rw, mainH };
    case DOCK_BOTTOM: return { 0, menuH_ + mainH, (float)screenW_, bh };
    case DOCK_CENTER: return { lw, menuH_, screenW_ - lw - rw, mainH };  // leftover middle
    }
    return {};
}

UIRect DockManager::slotRect(int area, int slot) const {
    UIRect rc = areaRect(area);
    int n = slotCount(area);
    if (n <= 1) return rc;
    auto boundary = [&](int index) {
        if (index <= 0) return 0.0f;
        if (index >= n) return 1.0f;
        auto it = primarySplit_.find(area * 100 + index);
        return it != primarySplit_.end() ? it->second : (float)index / n;
    };
    float a = boundary(slot), b = boundary(slot + 1);
    if (area == DOCK_BOTTOM || area == DOCK_CENTER) {   // side-by-side columns
        return { rc.x + rc.w * a, rc.y, rc.w * (b - a), rc.h };
    }
    return { rc.x, rc.y + rc.h * a, rc.w, rc.h * (b - a) };
}

UIRect DockManager::viewportRect(int screenW, int screenH, float menuH) {
    screenW_ = screenW; screenH_ = screenH; menuH_ = menuH;
    DockWindow* viewport = find("viewport");
    if (!viewport || !viewport->open || viewport->area == DOCK_NATIVE) return {};
    if (viewport->area == DOCK_FLOAT) {
        return { viewport->rect.x, viewport->rect.y + TITLE_H,
                 viewport->rect.w, std::max(0.0f, viewport->rect.h - TITLE_H) };
    }
    std::vector<int> tabs = cellWins(viewport->area, viewport->slot, viewport->sub);
    if (tabs.empty()) return {};
    int key = cellKey(viewport->area, viewport->slot, viewport->sub);
    int activeOrder = tabs.front() >= 0 ? wins_[tabs.front()].order : viewport->order;
    auto it = active_.find(key);
    if (it != active_.end()) activeOrder = it->second;
    if (activeOrder != viewport->order) return {};
    UIRect rc = subRect(viewport->area, viewport->slot, viewport->sub);
    rc.y += TAB_H; rc.h = std::max(0.0f, rc.h - TAB_H);
    return rc;
}

// Find the docked cell (area/slot/sub + its rect) under the cursor. Returns
// false for empty areas / outside the docked regions (handled by dropTarget).
bool DockManager::cellUnder(float mx, float my, int& area, int& slot, int& sub, UIRect& cell) const {
    const int areas[4] = { DOCK_LEFT, DOCK_RIGHT, DOCK_BOTTOM, DOCK_CENTER };
    for (int ai = 0; ai < 4; ai++) {
        int a = areas[ai];
        UIRect rc = areaRect(a);
        if (rc.w <= 0 || rc.h <= 0) continue;
        if (mx < rc.x || mx >= rc.x + rc.w || my < rc.y || my >= rc.y + rc.h) continue;
        int n = slotCount(a);
        if (n == 0) return false;
        bool primHoriz = (a == DOCK_BOTTOM || a == DOCK_CENTER);
        int s = -1;
        for (int i = 0; i < n; i++) {
            UIRect sr = slotRect(a, i);
            bool in = primHoriz ? (mx >= sr.x && mx < sr.x + sr.w) : (my >= sr.y && my < sr.y + sr.h);
            if (in) { s = i; break; }
        }
        if (s < 0) s = n - 1;
        int nSub = subCount(a, s);
        int su = 0;
        if (nSub == 2) {
            UIRect s0 = subRect(a, s, 0);
            su = crossHoriz(a) ? (mx >= s0.x + s0.w ? 1 : 0) : (my >= s0.y + s0.h ? 1 : 0);
        }
        area = a; slot = s; sub = su; cell = subRect(a, s, su);
        return true;
    }
    return false;
}

// Direction the cursor is aiming at within a cell. The five compass buttons at
// the cell centre are "sticky" hit-targets; outside them we fall back to the
// nearest-edge margin so the whole cell still maps to a zone (0..4).
int DockManager::cellDir(const UIRect& cell, float mx, float my) const {
    UIRect z[5];
    compassRects(cell, z);
    for (int k = 0; k < 5; k++)
        if (mx >= z[k].x && mx < z[k].x + z[k].w && my >= z[k].y && my < z[k].y + z[k].h) return k;
    float fx = (mx - cell.x) / cell.w, fy = (my - cell.y) / cell.h;
    const float M = 0.30f;
    float d[4] = { fx, 1 - fx, fy, 1 - fy };   // left, right, top, bottom
    int which = 0;
    for (int k = 1; k < 4; k++) if (d[k] < d[which]) which = k;
    if (d[which] > M) return 0;                // center → tab into this cell
    return which + 1;                          // 1 left, 2 right, 3 top, 4 bottom
}

// Map an aimed direction to a concrete drop (tab / new slot / new sub-split),
// respecting each area's primary vs cross axis.
DockManager::DropSpot DockManager::spotFromDir(int area, int slot, int sub, int dir) const {
    DropSpot spot;
    spot.area = area; spot.slot = slot; spot.sub = sub;
    if (dir == 0) { spot.mode = 0; return spot; }   // tab into this cell
    bool primHoriz = (area == DOCK_BOTTOM || area == DOCK_CENTER);
    bool horizDir = (dir == 1 || dir == 2);         // left/right
    bool primary = primHoriz ? horizDir : !horizDir;
    int nSub = subCount(area, slot);
    if (primary) {
        spot.mode = 1;                              // new slot before/after
        bool after = (dir == 2 || dir == 4);        // right/bottom
        spot.insertAt = slot + (after ? 1 : 0);
    } else if (nSub == 2) {
        spot.mode = 0;                              // cell already split: tab into this half
    } else {
        spot.mode = 2;                              // split this slot into two sub-cells
        spot.sub = (dir == 2 || dir == 4) ? 1 : 0;  // right/bottom → sub 1
    }
    return spot;
}

DockManager::DropSpot DockManager::dropTarget(float mx, float my) const {
    int area, slot, sub;
    UIRect cell;
    if (cellUnder(mx, my, area, slot, sub, cell))
        return spotFromDir(area, slot, sub, cellDir(cell, mx, my));

    DropSpot spot;
    // empty CENTER (leftover middle with no panels): new slot
    const int areas[4] = { DOCK_LEFT, DOCK_RIGHT, DOCK_BOTTOM, DOCK_CENTER };
    for (int ai = 0; ai < 4; ai++) {
        int a = areas[ai];
        UIRect rc = areaRect(a);
        if (rc.w <= 0 || rc.h <= 0) continue;
        if (mx < rc.x || mx >= rc.x + rc.w || my < rc.y || my >= rc.y + rc.h) continue;
        if (slotCount(a) == 0) { spot.area = a; spot.slot = 0; spot.sub = 0; spot.mode = 1; spot.insertAt = 0; return spot; }
    }
    // screen edges (for empty side/bottom areas that have zero width/height)
    if (mx < EDGE_ZONE && my > menuH_) { spot.area = DOCK_LEFT; spot.slot = 0; spot.mode = slotCount(DOCK_LEFT) == 0 ? 1 : 0; }
    else if (mx > screenW_ - EDGE_ZONE && my > menuH_) { spot.area = DOCK_RIGHT; spot.slot = 0; spot.mode = slotCount(DOCK_RIGHT) == 0 ? 1 : 0; }
    else if (my > screenH_ - EDGE_ZONE) { spot.area = DOCK_BOTTOM; spot.slot = 0; spot.mode = slotCount(DOCK_BOTTOM) == 0 ? 1 : 0; }
    if (spot.area >= 0 && spot.mode == 1) spot.insertAt = 0;
    return spot;
}

// Draw the five-target docking compass at a cell, highlighting the aimed zone.
void DockManager::drawDockCompass(UI& ui, const UIRect& cell, int activeDir) const {
    Renderer* r = ui.r;
    UIRect z[5];
    compassRects(cell, z);
    r->setUIScissor(0, 0, 0, 0, false);
    // backing plate behind the cluster
    float px = z[1].x - 8, py = z[3].y - 8;
    float pw = (z[2].x + z[2].w) - z[1].x + 16, ph = (z[4].y + z[4].h) - z[3].y + 16;
    r->drawRectPx(px, py, pw, ph, { 0.06f, 0.07f, 0.09f }, 0.6f);
    for (int k = 0; k < 5; k++) {
        bool on = (k == activeDir);
        r->drawRectPx(z[k].x - 1, z[k].y - 1, z[k].w + 2, z[k].h + 2, C_CMP_BORDER, 0.9f);
        r->drawRectPx(z[k].x, z[k].y, z[k].w, z[k].h, on ? C_ACCENT : C_CMP_BG, on ? 0.95f : 0.9f);
        // inner glyph: which portion of the cell this target fills
        Vec3 fg = on ? Vec3{ 0.06f, 0.09f, 0.13f } : C_ACCENT;
        float a = on ? 0.95f : 0.6f, pad = 6;
        float ix = z[k].x + pad, iy = z[k].y + pad, iw = z[k].w - 2 * pad, ih = z[k].h - 2 * pad;
        if (k == 0)      r->drawRectPx(ix, iy, iw, ih, fg, a);
        else if (k == 1) r->drawRectPx(ix, iy, iw * 0.5f, ih, fg, a);
        else if (k == 2) r->drawRectPx(ix + iw * 0.5f, iy, iw * 0.5f, ih, fg, a);
        else if (k == 3) r->drawRectPx(ix, iy, iw, ih * 0.5f, fg, a);
        else             r->drawRectPx(ix, iy + ih * 0.5f, iw, ih * 0.5f, fg, a);
    }
}

int DockManager::topFloatingUnderMouse(float mx, float my) const {
    for (int i = (int)wins_.size() - 1; i >= 0; i--) {
        const DockWindow& w = wins_[i];
        if (!w.open || w.area != DOCK_FLOAT) continue;
        if (mx >= w.rect.x && mx < w.rect.x + w.rect.w && my >= w.rect.y && my < w.rect.y + w.rect.h) return i;
    }
    return -1;
}

void DockManager::raiseWindow(int idx) {
    if (idx < 0 || idx >= (int)wins_.size()) return;
    DockWindow w = wins_[idx];
    wins_.erase(wins_.begin() + idx);
    wins_.push_back(w);
}

void DockManager::dockWindow(int idx, const DropSpot& spot) {
    DockWindow& w = wins_[idx];
    int oldArea = w.area;
    clearPrimarySplits(oldArea);
    clearPrimarySplits(spot.area);
    w.area = spot.area;
    if (spot.mode == 1) {                 // new slot along the primary axis
        for (auto& o : wins_) {
            if (&o != &w && o.open && o.area == spot.area && o.slot >= spot.insertAt) o.slot++;
        }
        w.slot = spot.insertAt;
        w.sub = 0;
    } else if (spot.mode == 2) {           // split the target slot into two sub-cells
        w.slot = spot.slot;
        int newSub = spot.sub;
        for (auto& o : wins_)
            if (&o != &w && o.open && o.area == spot.area && o.slot == spot.slot) o.sub = 1 - newSub;
        w.sub = newSub;
        subSplit_[spot.area * 100 + spot.slot] = 0.5f;
    } else {                               // tab into the target cell
        w.slot = spot.slot;
        w.sub = spot.sub;
    }
    int maxOrder = -1;
    for (auto& o : wins_) {
        if (&o != &w && o.area == spot.area && o.slot == w.slot && o.sub == w.sub && o.order > maxOrder) maxOrder = o.order;
    }
    w.order = maxOrder + 1;
    active_[cellKey(w.area, w.slot, w.sub)] = w.order;
    normalizeSlots(spot.area);
}

bool DockManager::windowHovered(const char* id, float mx, float my) const {
    const DockWindow* w = nullptr;
    for (const auto& x : wins_) if (x.id == id) w = &x;
    if (!w || !w->open || w->area == DOCK_NATIVE) return false;
    if (w->area == DOCK_FLOAT) {
        return mx >= w->rect.x && mx < w->rect.x + w->rect.w &&
               my >= w->rect.y && my < w->rect.y + w->rect.h;
    }
    auto it = active_.find(cellKey(w->area, w->slot, w->sub));
    if (it != active_.end() && it->second != w->order) return false;
    UIRect rc = subRect(w->area, w->slot, w->sub);
    return mx >= rc.x && mx < rc.x + rc.w && my >= rc.y && my < rc.y + rc.h;
}

void DockManager::setActive(const char* id) {
    DockWindow* w = find(id);
    if (w && w->area != DOCK_FLOAT) active_[cellKey(w->area, w->slot, w->sub)] = w->order;
}

void DockManager::drawAll(UI& ui, int screenW, int screenH, float menuH,
                          const std::function<void(UI&, const std::string&)>& content) {
    screenW_ = screenW;
    screenH_ = screenH;
    menuH_ = menuH;
    const bool outerInteractionBlocked = ui.interactionBlocked();
    const UIInput& in = ui.input();
    Renderer* r = ui.r;

    if (outerInteractionBlocked) {
        // A drawer/modal drawn later owns the pointer. Cancel captures instead
        // of completing a dock/resize operation with coordinates from below it.
        splitter_ = resizeWin_ = dragWin_ = -1;
        resizeEdges_ = 0;
        primarySplitArea_ = primarySplitBoundary_ = -1;
        subSplitArea_ = subSplitSlot_ = -1;
        dragDetached_ = false;
    }

    int topFloat = topFloatingUnderMouse(in.mouseX, in.mouseY);

    // ── outer splitters ──
    if (splitter_ >= 0) {
        if (!in.mouseDown) splitter_ = -1;
        else {
            if (splitter_ == DOCK_LEFT) leftW = clampf(in.mouseX, 160, screenW * 0.45f);
            if (splitter_ == DOCK_RIGHT) rightW = clampf(screenW - in.mouseX, 180, screenW * 0.45f);
            if (splitter_ == DOCK_BOTTOM) bottomH = clampf(screenH - in.mouseY, 100, screenH * 0.6f);
        }
    }
    UIRect la = areaRect(DOCK_LEFT), ra = areaRect(DOCK_RIGHT), ba = areaRect(DOCK_BOTTOM);
    UIRect splitters[3] = {
        { la.w - SPLIT_W*.5f, la.y, SPLIT_W, la.h },
        { ra.x - SPLIT_W*.5f, ra.y, SPLIT_W, ra.h },
        { ba.x, ba.y - SPLIT_W*.5f, ba.w, SPLIT_W },
    };
    int hoverSplitter = -1;
    if (splitter_ < 0 && dragWin_ < 0 && resizeWin_ < 0 && topFloat < 0) {
        for (int a = 0; a < 3; a++) {
            UIRect rc = areaRect(a);
            if (rc.w <= 0 || rc.h <= 0) continue;
            const UIRect& sp = splitters[a];
            if (in.mouseX >= sp.x && in.mouseX < sp.x + sp.w && in.mouseY >= sp.y && in.mouseY < sp.y + sp.h) {
                hoverSplitter = a;
                ui.registerBlockingRect(sp);
                if (in.mousePressed) splitter_ = a;
            }
        }
    }
    // Resize every boundary between adjacent slots. Center/bottom boundaries
    // are vertical; left/right boundaries are horizontal.
    auto boundaryRatio = [&](int area, int boundary) {
        int n = slotCount(area);
        if (boundary <= 0) return 0.0f;
        if (boundary >= n) return 1.0f;
        auto it = primarySplit_.find(area * 100 + boundary);
        return it != primarySplit_.end() ? it->second : (float)boundary / n;
    };
    if (primarySplitArea_ >= 0) {
        if (!in.mouseDown) { primarySplitArea_ = -1; primarySplitBoundary_ = -1; }
        else {
            UIRect ar = areaRect(primarySplitArea_);
            float ratio = (primarySplitArea_ == DOCK_BOTTOM || primarySplitArea_ == DOCK_CENTER)
                        ? (in.mouseX - ar.x) / ar.w : (in.mouseY - ar.y) / ar.h;
            float lo = boundaryRatio(primarySplitArea_, primarySplitBoundary_ - 1) + 0.08f;
            float hi = boundaryRatio(primarySplitArea_, primarySplitBoundary_ + 1) - 0.08f;
            primarySplit_[primarySplitArea_ * 100 + primarySplitBoundary_] = clampf(ratio, lo, hi);
        }
    }
    if (splitter_ < 0 && dragWin_ < 0 && resizeWin_ < 0 && topFloat < 0 && primarySplitArea_ < 0) {
        const int splitAreas[4] = { DOCK_LEFT, DOCK_RIGHT, DOCK_BOTTOM, DOCK_CENTER };
        for (int area : splitAreas) {
            int n = slotCount(area); UIRect ar = areaRect(area);
            for (int boundary = 1; boundary < n; boundary++) {
                UIRect before = slotRect(area, boundary - 1);
                bool vertical = area == DOCK_BOTTOM || area == DOCK_CENTER;
                float pos = vertical ? before.x + before.w : before.y + before.h;
                UIRect hit = vertical ? UIRect{ pos - SPLIT_W*.5f, ar.y, SPLIT_W, ar.h }
                                      : UIRect{ ar.x, pos - SPLIT_W*.5f, ar.w, SPLIT_W };
                if (in.mouseX >= hit.x && in.mouseX < hit.x + hit.w && in.mouseY >= hit.y && in.mouseY < hit.y + hit.h) {
                    ui.registerBlockingRect(hit);
                    if (in.mousePressed) { primarySplitArea_ = area; primarySplitBoundary_ = boundary; }
                }
            }
        }
    }
    centerDragging_ = false;
    // sub-splitter drag: resize the two sub-cells of a slot (4-way docked layout)
    if (subSplitArea_ >= 0) {
        if (!in.mouseDown) { subSplitArea_ = -1; subSplitSlot_ = -1; }
        else {
            UIRect sr = slotRect(subSplitArea_, subSplitSlot_);
            float ratio = crossHoriz(subSplitArea_) ? (in.mouseX - sr.x) / sr.w : (in.mouseY - sr.y) / sr.h;
            subSplit_[subSplitArea_ * 100 + subSplitSlot_] = clampf(ratio, 0.15f, 0.85f);
        }
    }

    // ── window drag ──
    if (dragWin_ >= 0) {
        DockWindow& w = wins_[dragWin_];
        float dx = in.mouseX - pressX_, dy = in.mouseY - pressY_;

        // ── in-strip reorder ──
        // while the grabbed tab is still docked and the cursor stays over its own
        // tab strip, slide it left/right past its siblings instead of detaching:
        // the drop position is decided by which half of a sibling the cursor is on.
        bool handledReorder = false;
        if (!dragDetached_ && w.area != DOCK_FLOAT) {
            UIRect cell = subRect(w.area, w.slot, w.sub);
            bool overOwnStrip = in.mouseX >= cell.x && in.mouseX < cell.x + cell.w &&
                                in.mouseY >= cell.y && in.mouseY < cell.y + TAB_H;
            if (overOwnStrip) {
                std::vector<int> tabs = cellWins(w.area, w.slot, w.sub);
                std::vector<int> others;
                for (int t : tabs) if (t != dragWin_) others.push_back(t);
                // lay the siblings out left→right; insertion index = how many of
                // their midpoints the cursor has passed
                int insertIdx = 0;
                float tx = cell.x + 2;
                for (int t : others) {
                    float tw = r->textWidth(wins_[t].title) + 34 + TAB_GRIP_W;
                    if (in.mouseX < tx + tw * 0.5f) break;
                    insertIdx++;
                    tx += tw + 2;
                }
                // renumber the cell so the grabbed tab lands at insertIdx
                int ord = 0;
                for (int k = 0; k < (int)others.size(); k++) {
                    if (k == insertIdx) w.order = ord++;
                    wins_[others[k]].order = ord++;
                }
                if (insertIdx >= (int)others.size()) w.order = ord++;
                active_[cellKey(w.area, w.slot, w.sub)] = w.order;
                handledReorder = true;
            }
        }

        if (!dragDetached_ && !handledReorder && (fabsf(dx) > 8 || fabsf(dy) > 8)) {
            int oldArea = w.area;
            if (w.area != DOCK_FLOAT) {
                w.area = DOCK_FLOAT;
                w.rect.w = clampf(w.rect.w, 280, 640);
                w.rect.h = clampf(w.rect.h, 200, 560);
            }
            if (oldArea != DOCK_FLOAT) { clearPrimarySplits(oldArea); normalizeSlots(oldArea); }
            dragDetached_ = true;
            raiseWindow(dragWin_);
            dragWin_ = (int)wins_.size() - 1;
        }
        if (dragDetached_) {
            DockWindow& wd = wins_[dragWin_];
            wd.rect.x = in.mouseX - dragOffX_;
            wd.rect.y = clampf(in.mouseY - dragOffY_, menuH_, (float)screenH_ - 30);
        }
        if (!in.mouseDown) {
            if (dragDetached_) {
                bool outside = in.mouseX < -4 || in.mouseY < -4 ||
                               in.mouseX > screenW_ + 4 || in.mouseY > screenH_ + 4;
                if (outside) {
                    // dropped beyond the window: promote to a native OS window
                    popOutRequest = wins_[dragWin_].id;
                    popOutX = in.mouseX - dragOffX_;
                    popOutY = in.mouseY - dragOffY_;
                } else {
                    DropSpot spot = dropTarget(in.mouseX, in.mouseY);
                    if (spot.area >= 0) dockWindow(dragWin_, spot);
                }
            }
            dragWin_ = -1;
            dragDetached_ = false;
        }
    }

    // ── floating resize ──
    if (resizeWin_ >= 0) {
        if (!in.mouseDown) {
            resizeWin_ = -1;
            resizeEdges_ = 0;
        }
        else {
            DockWindow& w = wins_[resizeWin_];
            const float minW = 240.0f, minH = 150.0f;
            if (resizeEdges_ & 1) {
                float right = w.rect.x + w.rect.w;
                float nx = clampf(in.mouseX, 0.0f, right - minW);
                w.rect.x = nx;
                w.rect.w = right - nx;
            }
            if (resizeEdges_ & 2) {
                w.rect.w = clampf(in.mouseX - w.rect.x, minW, (float)screenW_);
            }
            if (resizeEdges_ & 4) {
                float bottom = w.rect.y + w.rect.h;
                float ny = clampf(in.mouseY, menuH_, bottom - minH);
                w.rect.y = ny;
                w.rect.h = bottom - ny;
            }
            if (resizeEdges_ & 8) {
                w.rect.h = clampf(in.mouseY - w.rect.y, minH, (float)screenH_);
            }
        }
    }

    ui.setExternalCapture(dragActive());

    // ═══ docked areas: slots ═══
    const int dockedAreas[4] = { DOCK_LEFT, DOCK_RIGHT, DOCK_BOTTOM, DOCK_CENTER };
    for (int ai = 0; ai < 4; ai++) {
        int a = dockedAreas[ai];
        int nSlots = slotCount(a);
        for (int slot = 0; slot < nSlots; slot++) {
            int nSub = subCount(a, slot);
            for (int sub = 0; sub < nSub; sub++) {
                std::vector<int> tabs = cellWins(a, slot, sub);
                if (tabs.empty()) continue;
                UIRect rc = subRect(a, slot, sub);

                int key = cellKey(a, slot, sub);
                int active = -1;
                for (int i : tabs) if (wins_[i].order == active_[key]) active = i;
                if (active < 0) { active = tabs[0]; active_[key] = wins_[active].order; }

                // the viewport tab leaves its content transparent so the 3D shows through:
                // fill only the tab strip, and only block UI over the tab strip (so the
                // 3D area stays interactive for orbit / picking)
                bool viewportActive = wins_[active].id == "viewport";
                ui.registerBlockingRect(viewportActive ? UIRect{ rc.x, rc.y, rc.w, TAB_H } : rc);
                r->setUIScissor(rc.x, rc.y, rc.w, rc.h, true);
                if (viewportActive) r->drawRectPx(rc.x, rc.y, rc.w, TAB_H, C_AREA_BG, 1);
                else r->drawRectPx(rc.x, rc.y, rc.w, rc.h, C_AREA_BG, 1);
                if (slot > 0) {
                    if (a == DOCK_BOTTOM || a == DOCK_CENTER) r->drawRectPx(rc.x, rc.y, 2, rc.h, C_BORDER, 1);
                    else r->drawRectPx(rc.x, rc.y, rc.w, 2, C_BORDER, 1);
                }
                if (sub > 0) {   // border between the two sub-cells
                    if (crossHoriz(a)) r->drawRectPx(rc.x, rc.y, 2, rc.h, C_BORDER, 1);
                    else r->drawRectPx(rc.x, rc.y, rc.w, 2, C_BORDER, 1);
                }

                float tx = rc.x + 2;
                for (int i : tabs) {
                    DockWindow& w = wins_[i];
                    float tw = r->textWidth(w.title) + 34 + TAB_GRIP_W;
                    UIRect trc = { tx, rc.y + 2, tw, TAB_H - 2 };
                    bool isActive = i == active;
                    bool over = topFloat < 0 && in.mouseX >= trc.x && in.mouseX < trc.x + trc.w &&
                                in.mouseY >= trc.y && in.mouseY < trc.y + trc.h;
                    r->drawRectPx(trc.x, trc.y, trc.w, trc.h, isActive ? C_TAB_ACTIVE : C_TAB, 1);
                    if (isActive) r->drawRectPx(trc.x, trc.y, trc.w, 2, C_ACCENT, 1);
                    // drag grip: two short bars, grey by default and accent-blue while the
                    // tab is hovered — the same handle language as the panel splitters.
                    Vec3 gripCol = over ? C_ACCENT : C_TAB_GRIP;
                    float gripA = over ? 0.95f : 0.75f;
                    float gx = trc.x + 6, gy = trc.y + (TAB_H - 2 - 12) * 0.5f;
                    r->drawRectPx(gx,     gy, 2, 12, gripCol, gripA);
                    r->drawRectPx(gx + 4, gy, 2, 12, gripCol, gripA);
                    r->drawTextLine(trc.x + 8 + TAB_GRIP_W, trc.y + 5, w.title, isActive ? Vec3{ 0.85f, 0.9f, 0.97f } : C_TAB_TEXT, 1);
                    UIRect xrc = { trc.x + trc.w - 20, trc.y + 5, 15, 15 };
                    bool overX = over && in.mouseX >= xrc.x && in.mouseX < xrc.x + xrc.w &&
                                 in.mouseY >= xrc.y && in.mouseY < xrc.y + xrc.h;
                    r->drawTextLine(xrc.x + 3, xrc.y - 1, "x", overX ? Vec3{ 1, 0.5f, 0.5f } : C_TAB_TEXT, 1);
                    if (over && in.mousePressed && dragWin_ < 0 && splitter_ < 0 && primarySplitArea_ < 0 && subSplitArea_ < 0) {
                        if (overX) {
                            w.open = false;
                            clearPrimarySplits(a);
                            normalizeSlots(a);
                        } else {
                            active_[key] = w.order;
                            dragWin_ = i;
                            dragDetached_ = false;
                            pressX_ = in.mouseX;
                            pressY_ = in.mouseY;
                            dragOffX_ = 130;
                            dragOffY_ = 12;
                        }
                    }
                    tx += tw + 2;
                }

                // viewport draws no panel (its content is the 3D scene behind the UI)
                if (!viewportActive && active >= 0 && active < (int)wins_.size() && wins_[active].open) {
                    ui.setInteractionBlocked(outerInteractionBlocked || topFloat >= 0 || dragActive());
                    char pid[80];
                    snprintf(pid, sizeof(pid), "dock_%s", wins_[active].id.c_str());
                    ui.panelBegin(pid, rc.x, rc.y + TAB_H, rc.w, rc.h - TAB_H, nullptr);
                    content(ui, wins_[active].id);
                    ui.panelEnd();
                    ui.setInteractionBlocked(outerInteractionBlocked);
                }
            }
            // sub-splitter: drag the boundary between the two sub-cells
            if (subCount(a, slot) == 2) {
                UIRect s0 = subRect(a, slot, 0), sr = slotRect(a, slot);
                UIRect sp = crossHoriz(a) ? UIRect{ s0.x + s0.w - SPLIT_W*.5f, sr.y, SPLIT_W, sr.h }
                                          : UIRect{ sr.x, s0.y + s0.h - SPLIT_W*.5f, sr.w, SPLIT_W };
                bool dragging = subSplitArea_ == a && subSplitSlot_ == slot;
                bool hovering = false;
                if (splitter_ < 0 && dragWin_ < 0 && resizeWin_ < 0 && topFloat < 0 && subSplitArea_ < 0 && primarySplitArea_ < 0 &&
                    in.mouseX >= sp.x && in.mouseX < sp.x + sp.w && in.mouseY >= sp.y && in.mouseY < sp.y + sp.h) {
                    hovering = true;
                    ui.registerBlockingRect(sp);
                    if (in.mousePressed) { subSplitArea_ = a; subSplitSlot_ = slot; }
                }
                r->setUIScissor(0, 0, 0, 0, false);
                Vec3 col = (dragging || hovering) ? C_ACCENT : C_HANDLE;
                float alpha = (dragging || hovering) ? 0.95f : 0.42f;
                if (crossHoriz(a)) r->drawRectPx(s0.x + s0.w - 2, sr.y, 4, sr.h, col, alpha);
                else r->drawRectPx(sr.x, s0.y + s0.h - 2, sr.w, 4, col, alpha);
            }
        }
        // outer splitter visual (center is the leftover, no outer splitter of its own)
        if (nSlots > 0 && a != DOCK_CENTER) {
            const UIRect& sp = splitters[a];
            r->setUIScissor(0, 0, 0, 0, false);
            bool activeOrHover = splitter_ == a || hoverSplitter == a;
            Vec3 col = activeOrHover ? C_ACCENT : C_HANDLE;
            float alpha = activeOrHover ? 0.95f : 0.42f;
            r->drawRectPx(a == DOCK_LEFT ? la.w - 2 : a == DOCK_RIGHT ? ra.x - 2 : sp.x,
                          a == DOCK_BOTTOM ? ba.y - 2 : sp.y,
                          a == DOCK_BOTTOM ? sp.w : 4, a == DOCK_BOTTOM ? 4 : sp.h,
                          col, alpha);
        }
    }
    // Visible primary separators; the hit area is wider still (SPLIT_W).
    for (int area : dockedAreas) {
        int n = slotCount(area); UIRect ar = areaRect(area);
        for (int boundary = 1; boundary < n; boundary++) {
            UIRect before = slotRect(area, boundary - 1);
            bool vertical = area == DOCK_BOTTOM || area == DOCK_CENTER;
            float pos = vertical ? before.x + before.w : before.y + before.h;
            bool active = primarySplitArea_ == area && primarySplitBoundary_ == boundary;
            UIRect hit = vertical ? UIRect{ pos - SPLIT_W*.5f, ar.y, SPLIT_W, ar.h }
                                  : UIRect{ ar.x, pos - SPLIT_W*.5f, ar.w, SPLIT_W };
            bool hovering = topFloat < 0 && dragWin_ < 0 && resizeWin_ < 0 && splitter_ < 0 &&
                             in.mouseX >= hit.x && in.mouseX < hit.x + hit.w &&
                             in.mouseY >= hit.y && in.mouseY < hit.y + hit.h;
            Vec3 col = (active || hovering) ? C_ACCENT : C_HANDLE;
            float alpha = (active || hovering) ? 0.95f : 0.42f;
            r->setUIScissor(0, 0, 0, 0, false);
            if (vertical) r->drawRectPx(pos - 2, ar.y, 4, ar.h, col, alpha);
            else r->drawRectPx(ar.x, pos - 2, ar.w, 4, col, alpha);
        }
    }

    // ═══ floating windows ═══
    for (int i = 0; i < (int)wins_.size(); i++) {
        DockWindow& w = wins_[i];
        if (!w.open || w.area != DOCK_FLOAT) continue;
        UIRect rc = w.rect;
        rc.y = clampf(rc.y, menuH_, (float)screenH_ - 40);
        w.rect = rc;
        bool viewportWin = w.id == "viewport";   // transparent content: show the 3D
        // block UI only over the title bar for the viewport, so its 3D stays interactive
        ui.registerBlockingRect(viewportWin ? UIRect{ rc.x, rc.y, rc.w, TITLE_H } : rc);
        r->setUIScissor(0, 0, 0, 0, false);
        if (!viewportWin) r->drawRectPx(rc.x + 4, rc.y + 5, rc.w, rc.h, { 0, 0, 0 }, 0.35f);
        r->drawRectPx(rc.x - 1, rc.y - 1, rc.w + 2, rc.h + 2, C_ACCENT, i == (int)wins_.size() - 1 ? 0.55f : 0.25f);
        r->drawRectPx(rc.x, rc.y, rc.w, TITLE_H, C_TITLE, 1);
        r->drawTextLine(rc.x + 9, rc.y + 5, w.title, Vec3{ 0.85f, 0.9f, 0.97f }, 1);
        UIRect xrc = { rc.x + rc.w - 22, rc.y + 5, 16, 16 };
        bool overX = topFloat == i && in.mouseX >= xrc.x && in.mouseX < xrc.x + xrc.w &&
                     in.mouseY >= xrc.y && in.mouseY < xrc.y + xrc.h;
        r->drawTextLine(xrc.x + 4, xrc.y - 1, "x", overX ? Vec3{ 1, 0.5f, 0.5f } : C_TAB_TEXT, 1);
        // pop-out: promote this panel to a native OS window (drag it to another monitor)
        UIRect prc = { rc.x + rc.w - 44, rc.y + 5, 16, 16 };
        bool overP = topFloat == i && in.mouseX >= prc.x && in.mouseX < prc.x + prc.w &&
                      in.mouseY >= prc.y && in.mouseY < prc.y + prc.h;
        r->drawTextLine(prc.x + 4, prc.y - 1, "^", overP ? C_ACCENT : C_TAB_TEXT, 1);

        const float edge = 8.0f;
        bool overLeft = topFloat == i && in.mouseX >= rc.x - edge * 0.5f && in.mouseX < rc.x + edge * 0.5f &&
                        in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
        bool overRight = topFloat == i && in.mouseX >= rc.x + rc.w - edge * 0.5f && in.mouseX < rc.x + rc.w + edge * 0.5f &&
                         in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
        bool overTop = topFloat == i && in.mouseY >= rc.y - edge * 0.5f && in.mouseY < rc.y + edge * 0.5f &&
                       in.mouseX >= rc.x && in.mouseX < rc.x + rc.w;
        bool overBottom = topFloat == i && in.mouseY >= rc.y + rc.h - edge * 0.5f && in.mouseY < rc.y + rc.h + edge * 0.5f &&
                          in.mouseX >= rc.x && in.mouseX < rc.x + rc.w;
        int hitEdges = (overLeft ? 1 : 0) | (overRight ? 2 : 0) | (overTop ? 4 : 0) | (overBottom ? 8 : 0);
        bool overResize = hitEdges != 0 && !overX && !overP;
        if (overResize && in.mousePressed && dragWin_ < 0 && resizeWin_ < 0) {
            resizeWin_ = i;
            resizeEdges_ = hitEdges;
        }

        bool overTitle = topFloat == i && in.mouseY >= rc.y && in.mouseY < rc.y + TITLE_H &&
                          in.mouseX >= rc.x && in.mouseX < rc.x + rc.w;
        if (overTitle && !overResize && in.mousePressed && dragWin_ < 0 && resizeWin_ < 0) {
            if (overP) {
                popOutRequest = w.id;
                popOutX = rc.x;
                popOutY = rc.y;
            } else if (overX) {
                w.open = false;
            } else {
                raiseWindow(i);
                dragWin_ = (int)wins_.size() - 1;
                dragDetached_ = true;
                pressX_ = in.mouseX;
                pressY_ = in.mouseY;
                dragOffX_ = in.mouseX - rc.x;
                dragOffY_ = in.mouseY - rc.y;
                break;
            }
        }
        if (topFloat == i && in.mousePressed && !overTitle && !overResize && dragWin_ < 0) {
            raiseWindow(i);
            topFloat = (int)wins_.size() - 1;
            i = topFloat;
        }

        DockWindow& wref = wins_[i];
        if (!viewportWin) {   // viewport shows the 3D behind; draws no panel content
            ui.setInteractionBlocked(outerInteractionBlocked || topFloat != i || resizeWin_ == i || dragWin_ >= 0);
            char pid[80];
            snprintf(pid, sizeof(pid), "float_%s", wref.id.c_str());
            ui.panelBegin(pid, rc.x, rc.y + TITLE_H, rc.w, rc.h - TITLE_H, nullptr);
            content(ui, wref.id);
            ui.panelEnd();
            ui.setInteractionBlocked(outerInteractionBlocked);
        } else if (renderViewportOverlay) {
            // repaint the 3D on top of the docked panels drawn earlier, at this
            // window's z-order, so a floating viewport is never hidden by them
            renderViewportOverlay({ rc.x, rc.y + TITLE_H, rc.w, std::max(0.0f, rc.h - TITLE_H) });
        }

        r->setUIScissor(0, 0, 0, 0, false);
        auto edgeColor = [&](int bit, bool hover) {
            bool active = resizeWin_ == i && (resizeEdges_ & bit);
            return (active || hover) ? C_ACCENT : C_HANDLE;
        };
        auto edgeAlpha = [&](int bit, bool hover) {
            bool active = resizeWin_ == i && (resizeEdges_ & bit);
            return (active || hover) ? 0.95f : 0.42f;
        };
        r->drawRectPx(rc.x - 1, rc.y, 3, rc.h, edgeColor(1, overLeft), edgeAlpha(1, overLeft));
        r->drawRectPx(rc.x + rc.w - 2, rc.y, 3, rc.h, edgeColor(2, overRight), edgeAlpha(2, overRight));
        r->drawRectPx(rc.x, rc.y - 1, rc.w, 3, edgeColor(4, overTop), edgeAlpha(4, overTop));
        r->drawRectPx(rc.x, rc.y + rc.h - 2, rc.w, 3, edgeColor(8, overBottom), edgeAlpha(8, overBottom));
    }

    // ═══ drop-target highlight ═══
    if (dragWin_ >= 0 && dragDetached_) {
        int cArea, cSlot, cSub;
        UIRect cCell;
        bool onCell = cellUnder(in.mouseX, in.mouseY, cArea, cSlot, cSub, cCell);
        int cDir = onCell ? cellDir(cCell, in.mouseX, in.mouseY) : -1;
        DropSpot spot = onCell ? spotFromDir(cArea, cSlot, cSub, cDir) : dropTarget(in.mouseX, in.mouseY);
        if (spot.area >= 0) {
            UIRect rc;
            int n = slotCount(spot.area);
            if (n == 0) {
                if (spot.area == DOCK_LEFT) rc = { 0, menuH_, leftW, screenH_ - menuH_ };
                else if (spot.area == DOCK_RIGHT) rc = { screenW_ - rightW, menuH_, rightW, screenH_ - menuH_ };
                else if (spot.area == DOCK_BOTTOM) rc = { 0, screenH_ - bottomH, (float)screenW_, bottomH };
                else rc = areaRect(spot.area);
            } else if (spot.mode == 1) {
                // new slot: preview half of the slot at the insertion edge
                int refSlot = spot.insertAt < n ? spot.insertAt : n - 1;
                UIRect sr = slotRect(spot.area, refSlot);
                if (spot.area == DOCK_BOTTOM || spot.area == DOCK_CENTER) {
                    rc = { spot.insertAt < n ? sr.x : sr.x + sr.w / 2, sr.y, sr.w / 2, sr.h };
                } else {
                    rc = { sr.x, spot.insertAt < n ? sr.y : sr.y + sr.h / 2, sr.w, sr.h / 2 };
                }
            } else if (spot.mode == 2) {
                // new sub-split: preview the half of the cell that the window will take
                UIRect cell = subRect(spot.area, spot.slot, subCount(spot.area, spot.slot) == 2 ? spot.sub : 0);
                if (crossHoriz(spot.area)) {
                    rc = { spot.sub == 0 ? cell.x : cell.x + cell.w / 2, cell.y, cell.w / 2, cell.h };
                } else {
                    rc = { cell.x, spot.sub == 0 ? cell.y : cell.y + cell.h / 2, cell.w, cell.h / 2 };
                }
            } else {
                rc = subRect(spot.area, spot.slot, spot.sub);   // tab into this cell
            }
            r->setUIScissor(0, 0, 0, 0, false);
            r->drawRectPx(rc.x, rc.y, rc.w, rc.h, C_DROP, 0.18f);
            r->drawRectPx(rc.x, rc.y, rc.w, 2, C_DROP, 0.9f);
            r->drawRectPx(rc.x, rc.y + rc.h - 2, rc.w, 2, C_DROP, 0.9f);
            r->drawRectPx(rc.x, rc.y, 2, rc.h, C_DROP, 0.9f);
            r->drawRectPx(rc.x + rc.w - 2, rc.y, 2, rc.h, C_DROP, 0.9f);
        }
        // directional compass over the hovered cell (drawn on top of the preview)
        if (onCell) drawDockCompass(ui, cCell, cDir);
    }
}

// ═══ layout persistence ═══
void DockManager::drawDragOverlay(UI& ui) const {
    if (dragWin_ < 0 || dragWin_ >= (int)wins_.size() || !ui.input().mouseDown) return;
    const DockWindow& w = wins_[dragWin_];
    Renderer* r = ui.r;
    float width = std::max(120.0f, r->textWidth(w.title) + 42.0f);
    float x = ui.input().mouseX + 14.0f, y = ui.input().mouseY + 12.0f;
    if (x + width > screenW_) x = ui.input().mouseX - width - 14.0f;
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(x + 4, y + 4, width, TAB_H, {0,0,0}, .42f);
    r->drawRectPx(x, y, width, TAB_H, C_TAB_ACTIVE, .98f);
    r->drawRectPx(x, y, width, 3, C_ACCENT, 1);
    r->drawTextLine(x + 10, y + 5, w.title, { .92f,.96f,1 }, 1);
}

std::string DockManager::saveLayout() const {
    std::ostringstream o;
    o << "LAYOUT 5\n";
    o << "sizes " << leftW << " " << rightW << " " << bottomH << "\n";
    for (const auto& kv : subSplit_) o << "subsplit " << kv.first << " " << kv.second << "\n";
    for (const auto& kv : primarySplit_) o << "primarysplit " << kv.first << " " << kv.second << "\n";
    for (const auto& w : wins_) {
        // native OS windows are saved as in-app floating (recreated inside on restart)
        int area = w.area == DOCK_NATIVE ? DOCK_FLOAT : w.area;
        o << "win " << w.id << " " << area << " " << w.slot << " " << w.sub << " " << w.order << " " << (w.open ? 1 : 0)
          << " " << w.rect.x << " " << w.rect.y << " " << w.rect.w << " " << w.rect.h << "\n";
    }
    return o.str();
}

void DockManager::loadLayout(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    int version = 0;
    if (!std::getline(in, line) || sscanf(line.c_str(), "LAYOUT %d", &version) != 1) return;
    if (version < 3) return;   // pre-viewport-tab layout: keep the new defaults
    subSplit_.clear();
    primarySplit_.clear();
    while (std::getline(in, line)) {
        if (line.rfind("sizes ", 0) == 0) {
            sscanf(line.c_str(), "sizes %f %f %f", &leftW, &rightW, &bottomH);
        } else if (line.rfind("subsplit ", 0) == 0) {
            int k; float r;
            if (sscanf(line.c_str(), "subsplit %d %f", &k, &r) == 2) subSplit_[k] = clampf(r, 0.15f, 0.85f);
        } else if (line.rfind("primarysplit ", 0) == 0) {
            int k; float r;
            if (sscanf(line.c_str(), "primarysplit %d %f", &k, &r) == 2) primarySplit_[k] = clampf(r, 0.08f, 0.92f);
        } else if (line.rfind("win ", 0) == 0) {
            char id[64];
            int area, slot = 0, sub = 0, order, open;
            float x, y, w, h;
            bool ok;
            if (version >= 4) {
                ok = sscanf(line.c_str(), "win %63s %d %d %d %d %d %f %f %f %f", id, &area, &slot, &sub, &order, &open, &x, &y, &w, &h) == 10;
            } else if (version >= 2) {
                ok = sscanf(line.c_str(), "win %63s %d %d %d %d %f %f %f %f", id, &area, &slot, &order, &open, &x, &y, &w, &h) == 9;
            } else {
                ok = sscanf(line.c_str(), "win %63s %d %d %d %f %f %f %f", id, &area, &order, &open, &x, &y, &w, &h) == 8;
            }
            if (ok) {
                DockWindow* win = find(id);
                if (win) {
                    // valid docked areas: 0..3 (left/right/bottom/float) and 5 (center)
                    bool valid = (area >= 0 && area <= 3) || area == DOCK_CENTER;
                    win->area = valid ? area : DOCK_LEFT;
                    win->slot = slot >= 0 && slot < 8 ? slot : 0;
                    win->sub = (sub == 1) ? 1 : 0;
                    win->order = order;
                    win->open = open != 0;
                    win->rect = { x, y, w, h };
                }
            }
        }
    }
    leftW = clampf(leftW, 160, 900);
    rightW = clampf(rightW, 180, 900);
    bottomH = clampf(bottomH, 100, 700);
    const int areas[4] = { DOCK_LEFT, DOCK_RIGHT, DOCK_BOTTOM, DOCK_CENTER };
    for (int a : areas) normalizeSlots(a);
}
