// ─── Pulse Engine UI implementation ───
#include "ui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>

// palette
static const Vec3 C_PANEL = { 0.105f, 0.115f, 0.135f };
static const Vec3 C_PANEL_HEAD = { 0.14f, 0.155f, 0.18f };
static const Vec3 C_WIDGET = { 0.17f, 0.185f, 0.215f };
static const Vec3 C_WIDGET_HOT = { 0.22f, 0.24f, 0.28f };
static const Vec3 C_WIDGET_ACT = { 0.13f, 0.30f, 0.50f };
static const Vec3 C_ACCENT = { 0.30f, 0.62f, 0.99f };
static const Vec3 C_TEXT = { 0.85f, 0.88f, 0.93f };
static const Vec3 C_TEXT_DIM = { 0.55f, 0.59f, 0.66f };
static const Vec3 C_SELECTED = { 0.12f, 0.24f, 0.40f };
static const Vec3 C_BORDER = { 0.05f, 0.055f, 0.065f };

static const float WH = 24;   // widget height
static const float GAP = 4;
static const float PAD = 8;

// "testo##id" → the id part is hashed but never displayed
static std::string stripId(const char* label) {
    const char* p = strstr(label, "##");
    return p ? std::string(label, p - label) : std::string(label);
}

static std::vector<std::string> wrapText(Renderer* r, const std::string& text, float maxW, float scale = 1.0f) {
    std::vector<std::string> lines;
    if (!r || text.empty()) { lines.push_back(text); return lines; }
    if (maxW <= 8.0f || r->textWidth(text, scale) <= maxW) { lines.push_back(text); return lines; }

    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (r->textWidth(candidate, scale) <= maxW) {
            line = candidate;
            word.clear();
            return;
        }
        if (!line.empty()) {
            lines.push_back(line);
            line.clear();
        }
        while (!word.empty() && r->textWidth(word, scale) > maxW) {
            int fit = 0;
            for (int i = 1; i <= (int)word.size(); i++) {
                if (r->textWidth(word.substr(0, i), scale) > maxW) break;
                fit = i;
            }
            if (fit <= 0) fit = 1;
            lines.push_back(word.substr(0, fit));
            word.erase(0, fit);
        }
        line = word;
        word.clear();
    };

    for (char ch : text) {
        if (ch == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
        } else if (std::isspace((unsigned char)ch)) {
            flushWord();
        } else {
            word.push_back(ch);
        }
    }
    flushWord();
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

uint32_t UI::hash(const char* s, uint32_t seed) const {
    uint32_t h = seed ? seed : 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ? h : 1;
}

bool UI::mouseIn(float x, float y, float w, float h) const {
    return in_.mouseX >= x && in_.mouseX < x + w && in_.mouseY >= y && in_.mouseY < y + h;
}

void UI::begin(Renderer* renderer, const UIInput& input) {
    r = renderer;
    in_ = input;
    blockedInput_ = {};
    blockedInput_.mouseX = blockedInput_.mouseY = -1000000.0f;
    frame_++;
    hotId_ = 0;
    componentResetProbe_ = false;
    menuClickedThisFrame_ = false;
    prevPanelRects_ = panelRects_;
    panelRects_.clear();
    if (!in_.mouseDown && !in_.mouseReleased) {
        // active widgets release only on mouseReleased; safety clear when button truly up
        if (activeId_ && !in_.mouseDown) activeId_ = 0;
    }
}

void UI::setInteractionBlocked(bool b, bool cancelCapture) {
    if (b && !blocked_ && cancelCapture) {
        // Cancel widget captures belonging to the covered panel. The overlay
        // may then start its own interaction later in the same frame.
        activeId_ = 0;
    }
    blocked_ = b;
}

void UI::end() {
    drawColorPicker();
    drawEnumPicker();
    // click outside popup closes menus
    if (openMenuId_ && in_.mousePressed && !menuClickedThisFrame_ &&
        !mouseIn(popupRect_.x, popupRect_.y, popupRect_.w, popupRect_.h)) {
        openMenuId_ = 0;
    }
    if (in_.keyEscape) { openMenuId_ = 0; focusId_ = 0; }
    if (in_.mouseReleased) activeId_ = 0;
    // tooltip queued this frame, drawn on top near the cursor
    if (!tip_.empty()) {
        r->setUIScissor(0, 0, 0, 0, false);
        float tw = r->textWidth(tip_);
        float x = tipX_ + 14, y = tipY_ + 18, w = tw + 12, h = 20;
        if (x + w > r->width()) x = r->width() - w - 2;
        if (y + h > r->fontHeight() + r->height()) y = tipY_ - h - 4;
        r->drawRectPx(x - 1, y - 1, w + 2, h + 2, { 0.05f, 0.055f, 0.065f }, 1);
        r->drawRectPx(x, y, w, h, { 0.16f, 0.17f, 0.20f }, 1);
        r->drawTextLine(x + 6, y + 4, tip_, { 0.9f, 0.93f, 0.98f }, 1);
        tip_.clear();
    }
    r->setUIScissor(0, 0, 0, 0, false);
    r->flushUI();
}

bool UI::wantMouse() const {
    if (activeId_ || openMenuId_ || externalCapture_) return true;
    for (const auto& rc : prevPanelRects_) {
        if (in_.mouseX >= rc.x && in_.mouseX < rc.x + rc.w &&
            in_.mouseY >= rc.y && in_.mouseY < rc.y + rc.h) return true;
    }
    return false;
}

UIRect UI::alloc(float h) {
    UIRect rc;
    float fullW = p_.w - PAD * 2 - 6; // leave room for scrollbar
    if (rowCols_ > 0) {
        float w = (fullW - GAP * (rowCols_ - 1)) / rowCols_;
        rc = { p_.x + PAD + rowIdx_ * (w + GAP), rowY_, w, h };
        rowIdx_++;
        if (rowIdx_ >= rowCols_) {
            rowCols_ = 0;
            p_.cy = rowY_ + h + GAP;
        }
    } else {
        rc = { p_.x + PAD, p_.cy, fullW, h };
        p_.cy += h + GAP;
    }
    if (rc.y + h > p_.contentBottom) p_.contentBottom = rc.y + h;
    lastItemRect_ = rc;
    return rc;
}

void UI::row(int cols) {
    rowCols_ = cols > 1 ? cols : 0;
    rowIdx_ = 0;
    rowY_ = p_.cy;
}

void UI::drawScrollbar(const UIRect& rc, float scroll, float contentHeight) {
    float maxScroll = contentHeight - rc.h;
    if (maxScroll <= 0 || rc.h <= 0 || contentHeight <= 0) return;
    float thumbH = rc.h * (rc.h / contentHeight);
    if (thumbH < 24) thumbH = 24;
    if (thumbH > rc.h) thumbH = rc.h;
    float t = clampf(scroll / maxScroll, 0, 1);
    float thumbY = rc.y + (rc.h - thumbH) * t;
    r->drawRectPx(rc.x + rc.w - 5, rc.y, 4, rc.h, { 0.08f, 0.085f, 0.10f }, 0.6f);
    r->drawRectPx(rc.x + rc.w - 5, thumbY, 4, thumbH, C_WIDGET_HOT, 0.95f);
}

void UI::beginScrollRegion(const char* id, const UIRect& rc) {
    ScrollRegion sr;
    sr.id = hash(id, p_.id ^ 0x5C011u);
    sr.rc = rc;
    sr.savedX = p_.x; sr.savedW = p_.w; sr.savedCy = p_.cy;
    sr.savedContentBottom = p_.contentBottom;
    sr.savedSx = p_.sx; sr.savedSy = p_.sy; sr.savedSw = p_.sw; sr.savedSh = p_.sh;

    float& scroll = storage_[sr.id];
    float content = storage_[sr.id ^ 0x9E37u];        // measured last frame
    float maxScroll = content - rc.h;
    if (maxScroll < 0) maxScroll = 0;
    // the wheel only acts while the pointer is inside, and it is consumed so the
    // panel underneath does not scroll as well
    if (mouseOk() && !blocked_ && in_.wheel != 0 &&
        in_.mouseX >= rc.x && in_.mouseX < rc.x + rc.w &&
        in_.mouseY >= rc.y && in_.mouseY < rc.y + rc.h) {
        scroll -= in_.wheel * 40;
        in_.wheel = 0;
    }
    scroll = clampf(scroll, 0, maxScroll);

    p_.x = rc.x; p_.w = rc.w;
    p_.cy = rc.y - scroll;
    p_.contentBottom = p_.cy;
    p_.sx = rc.x; p_.sy = rc.y; p_.sw = rc.w; p_.sh = rc.h;
    r->setUIScissor(rc.x, rc.y, rc.w, rc.h, true);
    sr.top = p_.cy;
    scrollRegions_.push_back(sr);
}

void UI::endScrollRegion() {
    if (scrollRegions_.empty()) return;
    ScrollRegion sr = scrollRegions_.back();
    scrollRegions_.pop_back();
    float content = p_.contentBottom - sr.top + PAD;
    storage_[sr.id ^ 0x9E37u] = content;
    drawScrollbar(sr.rc, storage_[sr.id], content);

    p_.x = sr.savedX; p_.w = sr.savedW; p_.cy = sr.savedCy;
    p_.contentBottom = sr.savedContentBottom;
    p_.sx = sr.savedSx; p_.sy = sr.savedSy; p_.sw = sr.savedSw; p_.sh = sr.savedSh;
    r->setUIScissor(p_.sx, p_.sy, p_.sw, p_.sh, true);
}

void UI::beginColumns(float leftWidthPx, float rightWidthPx) {
    colActive_ = true;
    colIndex_ = 0;
    colCount_ = rightWidthPx > 0 ? 3 : 2;
    colSavedX_ = p_.x;
    colSavedW_ = p_.w;
    colStartY_ = p_.cy;
    for (int i = 0; i < 3; i++) colEndY_[i] = p_.cy;
    colX_[0] = p_.x;
    colW_[0] = leftWidthPx;
    colX_[1] = p_.x + leftWidthPx + 6;
    colW_[1] = colSavedW_ - leftWidthPx - 6 - (colCount_ == 3 ? rightWidthPx + 6 : 0);
    colX_[2] = colX_[1] + colW_[1] + 6;
    colW_[2] = rightWidthPx;
    p_.x = colX_[0];
    p_.w = colW_[0];
}

void UI::nextColumn() {
    if (!colActive_ || colIndex_ >= colCount_ - 1) return;
    colEndY_[colIndex_] = p_.cy;
    colIndex_++;
    p_.x = colX_[colIndex_];
    p_.w = colW_[colIndex_];
    p_.cy = colStartY_;
}

void UI::endColumns() {
    if (!colActive_) return;
    colEndY_[colIndex_] = p_.cy;
    float maxY = colStartY_;
    for (int i = 0; i < colCount_; i++) if (colEndY_[i] > maxY) maxY = colEndY_[i];
    for (int i = 1; i < colCount_; i++) {
        r->drawRectPx(colX_[i] - 4, colStartY_, 1, maxY - colStartY_, C_BORDER, 1);
    }
    p_.x = colSavedX_;
    p_.w = colSavedW_;
    p_.cy = maxY + 4;
    colActive_ = false;
}

bool UI::dragInt(const char* label, int* v, float speed, int mn, int mx) {
    uint32_t id = hash(label, p_.id ^ 0x1477);
    float f = (float)*v + storage_[id + 5];
    UIRect rc = alloc(WH);
    if (numEditId_ == id) {
        double nv;
        if (numericEdit(id, rc, true, &nv)) {
            *v = (int)clampf((float)nv, (float)mn, (float)mx);
            return true;
        }
        return false;
    }
    bool hovered;
    bool clicked = behave(id, rc, &hovered);
    bool changed = false;
    float& moved = storage_[id + 21];
    if (clicked && moved < 3) {
        numEditId_ = id;
        focusId_ = id;
        snprintf(numEditBuf_, sizeof(numEditBuf_), "%d", *v);
        beginTextSelection(id,numEditBuf_,textIndexAt(numEditBuf_,in_.mouseX-std::max(rc.x+6,rc.x+rc.w-r->textWidth(numEditBuf_)-8)));
        lastTextClickId_=id;lastTextClickFrame_=frame_;
    }
    if (activeId_ == id && in_.mouseDown) {
        static std::unordered_map<uint32_t, float> lastX;
        if (in_.mousePressed) { lastX[id] = in_.mouseX; moved = 0; }
        float dx = in_.mouseX - lastX[id];
        lastX[id] = in_.mouseX;
        moved += fabsf(dx);
        if (dx != 0) {
            f = clampf(f + dx * speed, (float)mn, (float)mx);
            int nv = (int)floorf(f + 0.5f);
            if (nv != *v) { *v = nv; changed = true; }
            storage_[id + 5] = f - (float)*v;   // fractional accumulator
        }
    } else {
        storage_[id + 5] = 0;
    }
    Vec3 c = activeId_ == id ? C_WIDGET_ACT : (hovered ? C_WIDGET_HOT : C_WIDGET);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, c, 1);
    if (label[0] && label[0] != '#') {
        r->drawTextLine(rc.x + 6, textCenterY(rc), stripId(label), C_TEXT_DIM, 1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", *v);
    float tw = r->textWidth(buf);
    r->drawTextLine(rc.x + rc.w - tw - 8, textCenterY(rc), buf, C_TEXT, 1);
    return changed;
}

bool UI::behave(uint32_t id, const UIRect& rc, bool* hovered) {
    bool over = mouseOk() && !blocked_ && mouseIn(rc.x, rc.y, rc.w, rc.h) &&
                (!inPanel_ || mouseIn(p_.x, p_.y, p_.w, p_.h)); // clipped by panel
    if (hovered) *hovered = over;
    if (over) hotId_ = id;
    if (over && in_.mousePressed) activeId_ = id;
    return over && in_.mouseReleased && activeId_ == id;
}

// ═══ panels ═══
void UI::panelBegin(const char* id, float x, float y, float w, float h, const char* title) {
    inPanel_ = true;
    p_.id = hash(id, 0);
    p_.x = x; p_.y = y; p_.w = w; p_.h = h;
    panelRects_.push_back({ x, y, w, h });

    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(x, y, w, h, C_PANEL, 1);
    r->drawRectPx(x, y, w, 1, C_BORDER, 1);
    r->drawRectPx(x, y + h - 1, w, 1, C_BORDER, 1);
    r->drawRectPx(x, y, 1, h, C_BORDER, 1);
    r->drawRectPx(x + w - 1, y, 1, h, C_BORDER, 1);

    float top = y;
    if (title) {
        r->drawRectPx(x + 1, y + 1, w - 2, 24, C_PANEL_HEAD, 1);
        r->drawTextLine(x + PAD, y + 4, title, C_ACCENT, 1);
        top += 25;
    }

    // scrolling — the wheel itself is applied in panelEnd() so that an inner
    // zoom/scroll region (e.g. the Blueprint graph) can consumeWheel() first and
    // stop the panel from also scrolling under the cursor.
    float& scroll = storage_[p_.id];
    float viewH = h - (top - y) - 4;
    float lastContent = storage_[p_.id + 7];
    float maxScroll = lastContent - viewH;
    if (maxScroll < 0) maxScroll = 0;
    scroll = clampf(scroll, 0, maxScroll);
    p_.scroll = scroll;

    // scrollbar
    if (maxScroll > 0) {
        float track = viewH;
        float thumbH = track * (viewH / lastContent);
        if (thumbH < 24) thumbH = 24;
        float thumbY = top + 2 + (track - thumbH - 4) * (scroll / maxScroll);
        r->drawRectPx(x + w - 6, thumbY, 4, thumbH, C_WIDGET_HOT, 0.9f);
    }

    p_.sx = x + 1; p_.sy = top + 1; p_.sw = w - 2; p_.sh = y + h - top - 2;
    r->setUIScissor(p_.sx, p_.sy, p_.sw, p_.sh, true);
    p_.cx = x + PAD;
    p_.cy = top + PAD - scroll;
    p_.contentBottom = p_.cy;
    rowCols_ = 0;
}

void UI::panelEnd() {
    // content height = (contentBottom + scroll) - contentTop
    float contentTop = p_.y + 25 + PAD;
    float content = (p_.contentBottom + p_.scroll) - contentTop + PAD;
    storage_[p_.id + 7] = content;
    // Apply the mouse wheel here (not in panelBegin) so any inner region that
    // already handled it — the Blueprint graph zoom, list sub-scrolls — can
    // consumeWheel() to keep the panel from scrolling under the cursor.
    if (mouseOk() && mouseIn(p_.x, p_.y, p_.w, p_.h) && in_.wheel != 0) {
        float top = p_.sy - 1;                      // panelBegin set p_.sy = top + 1
        float viewH = (p_.y + p_.h) - top - 4;
        float maxScroll = content - viewH;
        if (maxScroll < 0) maxScroll = 0;
        float& scroll = storage_[p_.id];
        scroll = clampf(scroll - in_.wheel * 40, 0, maxScroll);
    }
    r->setUIScissor(0, 0, 0, 0, false);
    inPanel_ = false;
}

// ═══ widgets ═══
void UI::label(const std::string& text, Vec3 color) {
    const float h = WH * 0.8f;
    if (rowCols_ > 0 || !inPanel_) {
        UIRect rc = alloc(h);
        std::string shown = ellipsize(text, rc.w - 2.0f);
        r->drawTextLine(rc.x, rc.y + 1, shown, color, 1);
        hoverTip(text, rc, rc.w - 2.0f);
        return;
    }
    float maxW = (std::max)(24.0f, p_.w - PAD * 2 - 10.0f);
    std::vector<std::string> lines = wrapText(r, text, maxW);
    for (const std::string& line : lines) {
        UIRect rc = alloc(h);
        r->drawTextLine(rc.x, rc.y + 1, line, color, 1);
    }
}

void UI::labelWrapped(const std::string& text, Vec3 color) {
    float maxW = (std::max)(24.0f, p_.w - PAD * 2 - 10.0f);
    std::vector<std::string> lines = wrapText(r, text, maxW);
    for (const std::string& line : lines) {
        UIRect rc = alloc(WH * 0.75f);
        r->drawTextLine(rc.x, rc.y, line, color, 1);
    }
}

void UI::header(const char* text) {
    UIRect rc = alloc(WH * 0.9f);
    r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, C_PANEL_HEAD, 1);
    r->drawTextLine(rc.x + 2, rc.y + 2, text, C_ACCENT, 1);
}

bool UI::button(const char* label) {
    return buttonColored(label, C_WIDGET, C_TEXT);
}

bool UI::buttonColored(const char* label, Vec3 bg, Vec3 fg) {
    uint32_t id = hash(label, p_.id);
    UIRect rc = alloc(WH);
    bool hovered;
    bool clicked = behave(id, rc, &hovered);
    Vec3 c = activeId_ == id ? C_WIDGET_ACT : (hovered ? bg * 1.35f : bg);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, c, 1);
    std::string vis = stripId(label);
    float tw = r->textWidth(vis);
    r->drawTextLine(rc.x + (rc.w - tw) / 2, textCenterY(rc), vis, fg, 1);
    return clicked;
}

void UI::beginCenteredToolRow(int count, float squareSize, float gap) {
    centeredToolRow_ = count > 0;
    centeredToolCount_ = count > 0 ? count : 0;
    centeredToolIndex_ = 0;
    centeredToolSize_ = squareSize < 22.0f ? 22.0f : squareSize;
    centeredToolGap_ = gap < 2.0f ? 2.0f : gap;
    float groupW = centeredToolCount_ * centeredToolSize_ +
                   (centeredToolCount_ > 0 ? centeredToolCount_ - 1 : 0) * centeredToolGap_;
    centeredToolX_ = p_.x + (p_.w - groupW) * 0.5f;
    centeredToolY_ = p_.cy;
}

void UI::endCenteredToolRow() {
    if (!centeredToolRow_) return;
    p_.cy = centeredToolY_ + centeredToolSize_ + GAP;
    if (p_.cy - GAP > p_.contentBottom) p_.contentBottom = p_.cy - GAP;
    centeredToolRow_ = false;
    centeredToolCount_ = centeredToolIndex_ = 0;
}

bool UI::toolIconButton(const char* idText, int icon, bool active, const char* tooltip, bool dirty) {
    UIRect rc;
    if (centeredToolRow_) {
        rc = { centeredToolX_ + centeredToolIndex_ * (centeredToolSize_ + centeredToolGap_),
               centeredToolY_, centeredToolSize_, centeredToolSize_ };
        centeredToolIndex_++;
        if (rc.y + rc.h > p_.contentBottom) p_.contentBottom = rc.y + rc.h;
    } else {
        rc = alloc(WH * 1.18f);
    }
    return toolIconButtonRect(idText, rc, icon, active, tooltip, dirty);
}

bool UI::toolIconButtonRect(const char* idText, const UIRect& rc, int icon, bool active,
                            const char* tooltip, bool dirty) {
    uint32_t id = hash(idText, p_.id);
    bool hovered;
    bool clicked = behave(id, rc, &hovered);
    static const Vec3 TOOL_BG[5] = {
        { 0.12f, 0.22f, 0.34f }, { 0.18f, 0.18f, 0.34f }, { 0.12f, 0.28f, 0.22f },
        { 0.18f, 0.23f, 0.30f }, { 0.30f, 0.23f, 0.12f }
    };
    Vec3 base = TOOL_BG[icon >= 0 && icon < 5 ? icon : 4];
    // `dirty` (unsaved changes) turns the control green until it is saved —
    // the shared save affordance across every document editor
    if (dirty) base = { 0.13f, 0.33f, 0.21f };
    Vec3 bg = active ? Vec3{ 0.12f, 0.32f, 0.56f } : hovered ? base * 1.35f : base;
    Vec3 fg = dirty ? Vec3{ 0.58f, 0.96f, 0.74f }
            : active ? Vec3{ 0.78f, 0.91f, 1.0f } : Vec3{ 0.72f, 0.78f, 0.87f };
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, bg, 1);
    Vec3 border = dirty ? Vec3{ 0.35f, 0.85f, 0.5f }
                : (hovered || active) ? Vec3{ 0.42f, 0.68f, 0.96f } : Vec3{ 0.25f, 0.29f, 0.36f };
    r->drawRectPx(rc.x, rc.y, rc.w, 1, border, 1);
    r->drawRectPx(rc.x, rc.y + rc.h - 1, rc.w, 1, border, 1);
    r->drawRectPx(rc.x, rc.y, 1, rc.h, border, 1);
    r->drawRectPx(rc.x + rc.w - 1, rc.y, 1, rc.h, border, 1);
    if (active) r->drawRectPx(rc.x, rc.y + rc.h - 3, rc.w, 3, C_ACCENT, 1);
    float cx = rc.x + rc.w * 0.5f, cy = rc.y + rc.h * 0.5f;
    float s = clampf(std::min(rc.w, rc.h) / 28.0f, 1.0f, 2.0f);   // min side: a wide button must not clip the glyph
    auto line = [&](float x1, float y1, float x2, float y2, float w = 1.5f) {
        r->drawLinePx(x1, y1, x2, y2, w * s, fg, 1);
    };
    if (icon == 0) { // save / floppy
        r->drawRectPx(cx - 8*s, cy - 8*s, 16*s, 16*s, fg, 1);
        r->drawRectPx(cx - 5*s, cy - 7*s, 8*s, 5*s, bg, 1);
        r->drawRectPx(cx - 5*s, cy + 2*s, 10*s, 5*s, bg, 1);
    } else if (icon == 1) { // save as / two sheets
        r->drawRectPx(cx - 8*s, cy - 6*s, 12*s, 14*s, fg, 1);
        r->drawRectPx(cx - 5*s, cy - 9*s, 12*s, 14*s, bg, 1);
        line(cx - 5*s, cy - 9*s, cx + 7*s, cy - 9*s);
        line(cx + 7*s, cy - 9*s, cx + 7*s, cy + 5*s);
        line(cx + 1*s, cy + 7*s, cx + 9*s, cy + 7*s, 2);
        line(cx + 7*s, cy + 5*s, cx + 9*s, cy + 7*s, 2);
    } else if (icon == 2) { // assign / connected objects
        r->drawRectPx(cx - 9*s, cy - 6*s, 6*s, 6*s, fg, 1);
        r->drawRectPx(cx + 3*s, cy + 2*s, 6*s, 6*s, fg, 1);
        line(cx - 3*s, cy - 2*s, cx + 4*s, cy + 3*s, 2);
        line(cx + 1*s, cy + 3*s, cx + 4*s, cy + 3*s, 2);
    } else if (icon == 3) { // panel columns
        line(cx - 9*s, cy - 8*s, cx + 9*s, cy - 8*s);
        line(cx - 9*s, cy + 8*s, cx + 9*s, cy + 8*s);
        line(cx - 9*s, cy - 8*s, cx - 9*s, cy + 8*s);
        line(cx + 9*s, cy - 8*s, cx + 9*s, cy + 8*s);
        line(cx - 3*s, cy - 8*s, cx - 3*s, cy + 8*s);
        line(cx + 4*s, cy - 8*s, cx + 4*s, cy + 8*s);
    } else { // settings / compact gear
        r->drawRectPx(cx - 4*s, cy - 4*s, 8*s, 8*s, fg, 1);
        r->drawRectPx(cx - 1*s, cy - 1*s, 2*s, 2*s, bg, 1);
        line(cx, cy - 9*s, cx, cy - 5*s, 2);
        line(cx, cy + 5*s, cx, cy + 9*s, 2);
        line(cx - 9*s, cy, cx - 5*s, cy, 2);
        line(cx + 5*s, cy, cx + 9*s, cy, 2);
        line(cx - 7*s, cy - 7*s, cx - 4*s, cy - 4*s, 2);
        line(cx + 4*s, cy + 4*s, cx + 7*s, cy + 7*s, 2);
        line(cx + 4*s, cy - 4*s, cx + 7*s, cy - 7*s, 2);
        line(cx - 7*s, cy + 7*s, cx - 4*s, cy + 4*s, 2);
    }
    // At about 60 fps the explanatory tooltip appears after ~0.7 seconds.
    float& hoverFrames = storage_[id ^ 0x6E17A2u];
    if (hovered) {
        hoverFrames += 1.0f;
        if (tooltip && tooltip[0] && hoverFrames >= 42.0f) {
            tip_ = tooltip;
            tipX_ = in_.mouseX;
            tipY_ = in_.mouseY;
        }
    } else {
        hoverFrames = 0.0f;
    }
    return clicked;
}

// ── big asset picker ──
void UI::drawAssetThumb(const UIAssetOption& opt, float x, float y, float s) {
    r->drawRectPx(x, y, s, s, { 0.06f, 0.07f, 0.09f }, 1);
    if (opt.tex) r->drawImagePx((GLuint)opt.tex, x + 2, y + 2, s - 4, s - 4, { 1, 1, 1 }, 1);
    else if (opt.useSwatch) r->drawRectPx(x + 3, y + 3, s - 6, s - 6, opt.swatch, 1);
    else if (!opt.iconImage.empty()) {
        auto it = assetIcons_.find(opt.iconImage);
        if (it != assetIcons_.end()) r->drawImagePx(it->second, x + 3, y + 3, s - 6, s - 6, { 1, 1, 1 }, 1);
    }
    Vec3 bd{ .30f, .34f, .40f };
    r->drawRectPx(x, y, s, 1, bd, 1); r->drawRectPx(x, y + s - 1, s, 1, bd, 1);
    r->drawRectPx(x, y, 1, s, bd, 1); r->drawRectPx(x + s - 1, y, 1, s, bd, 1);
}

static const float ASSET_ROW_H = 24;

float UI::assetFieldHeight(const char* fieldId, float fieldH, int optionCount) const {
    return assetFieldOpen_ == fieldId ? fieldH + optionCount * ASSET_ROW_H : fieldH;
}

int UI::assetFieldRect(const char* fieldId, const UIRect& rc, int current,
                       const std::vector<UIAssetOption>& options, float* outHeight) {
    auto inRect = [&](const UIRect& q) {
        return !blocked_ && in_.mouseX >= q.x && in_.mouseX < q.x + q.w &&
               in_.mouseY >= q.y && in_.mouseY < q.y + q.h;
    };
    const float TH = rc.h - 10;
    bool overField = inRect(rc);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, overField ? Vec3{ .17f, .19f, .23f } : Vec3{ .13f, .145f, .175f }, 1);
    r->drawRectPx(rc.x, rc.y, rc.w, 1, { .28f, .32f, .40f }, .8f);
    const UIAssetOption* cur = (current >= 0 && current < (int)options.size()) ? &options[current] : nullptr;
    if (cur) drawAssetThumb(*cur, rc.x + 5, rc.y + 5, TH);
    else r->drawRectPx(rc.x + 5, rc.y + 5, TH, TH, { .06f, .07f, .09f }, 1);
    std::string name = cur ? cur->label : "None";
    r->drawTextLine(rc.x + TH + 16, textCenterY(rc), ellipsize(name, rc.w - TH - 46), { .88f, .92f, .98f }, 1);

    bool open = assetFieldOpen_ == fieldId;
    r->drawTextLine(rc.x + rc.w - 18, textCenterY(rc), open ? "^" : "v", C_ACCENT, 1);
    if (overField && in_.mousePressed) { assetFieldOpen_ = open ? std::string() : fieldId; open = !open; }

    int picked = -1;
    float bottom = rc.y + rc.h;
    if (open) {
        for (int i = 0; i < (int)options.size(); i++) {
            UIRect irc = { rc.x, bottom, rc.w, ASSET_ROW_H };
            bottom += ASSET_ROW_H;
            bool ihov = inRect(irc);
            r->drawRectPx(irc.x, irc.y, irc.w, irc.h,
                          i == current ? Vec3{ .12f, .24f, .40f } : (ihov ? Vec3{ .20f, .28f, .40f } : Vec3{ .10f, .11f, .135f }), 1);
            drawAssetThumb(options[i], irc.x + 3, irc.y + 3, 18);
            r->drawTextLine(irc.x + 28, textCenterY(irc), ellipsize(options[i].label, irc.w - 40), { .85f, .9f, .97f }, 1);
            if (ihov && in_.mousePressed) { picked = i; assetFieldOpen_.clear(); }
        }
        // a click anywhere outside the field and its list closes it
        bool insideBlock = in_.mouseX >= rc.x && in_.mouseX < rc.x + rc.w &&
                           in_.mouseY >= rc.y && in_.mouseY < bottom;
        if (in_.mousePressed && picked < 0 && !insideBlock) assetFieldOpen_.clear();
    }
    if (outHeight) *outHeight = bottom - rc.y;
    return picked;
}

bool UI::selectable(const char* id, const std::string& text, bool selected) {
    uint32_t wid = hash(id, p_.id);
    UIRect rc = alloc(WH * 0.92f);
    bool hovered;
    bool clicked = behave(wid, rc, &hovered);
    if (selected) r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, C_SELECTED, 1);
    else if (hovered) r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, C_WIDGET, 0.7f);
    std::string shown = ellipsize(text, rc.w - 6.0f);
    r->drawTextLine(rc.x + 2, textCenterY(rc), shown, selected ? Vec3{ 0.75f, 0.87f, 1.0f } : C_TEXT, 1);
    hoverTip(text, rc, rc.w - 6.0f);
    return clicked;
}

bool UI::checkbox(const char* label, bool* v) {
    uint32_t id = hash(label, p_.id);
    UIRect rc = alloc(WH * 0.9f);
    bool hovered;
    bool clicked = behave(id, rc, &hovered);
    float box = 15;
    r->drawRectPx(rc.x, rc.y + 3, box, box, hovered ? C_WIDGET_HOT : C_WIDGET, 1);
    if (*v) r->drawRectPx(rc.x + 3, rc.y + 6, box - 6, box - 6, C_ACCENT, 1);
    r->drawTextLine(rc.x + box + 8, textCenterY(rc), stripId(label), C_TEXT, 1);
    if (clicked) { *v = !*v; return true; }
    return false;
}

void UI::disabledField(const char* label, const std::string& value) {
    UIRect rc = alloc(WH);
    Vec3 bg = { 0.115f, 0.12f, 0.135f };
    Vec3 fg = { 0.38f, 0.41f, 0.46f };
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, bg, 1);
    if (label && label[0] && label[0] != '#')
        r->drawTextLine(rc.x + 6, textCenterY(rc), stripId(label), fg, 1);
    float tw = r->textWidth(value);
    r->drawTextLine(rc.x + rc.w - tw - 8, textCenterY(rc), value, fg, 1);
}

// shared editing state for numeric fields: click (without dragging) to type a value
int UI::textIndexAt(const char* text, float localX, float scale) const {
    if (localX <= 0) return 0;
    int len=(int)strlen(text);
    float x=0;
    for(int i=0;i<len;i++){
        char one[2]={text[i],0};float w=r->textWidth(one,scale);
        if(localX<x+w*.5f)return i;
        x+=w;
    }
    return len;
}

float UI::textCenterY(const UIRect& rc, float scale) const {
    return rc.y + (rc.h - r->fontHeight() * scale) * 0.5f;
}
float UI::fieldTextY(const UIRect& rc, float scale) const { return textCenterY(rc, scale); }

void UI::beginTextSelection(uint32_t id,const char* text,int cursor){
    textEditId_=id;textCursor_=textAnchor_=std::max(0,std::min((int)strlen(text),cursor));textSelecting_=false;
}

bool UI::eraseTextSelection(char* text,int& len){
    if(textCursor_==textAnchor_)return false;
    int a=std::min(textCursor_,textAnchor_),b=std::max(textCursor_,textAnchor_);
    memmove(text+a,text+b,(size_t)(len-b+1));len-=b-a;textCursor_=textAnchor_=a;return true;
}

bool UI::numericEdit(uint32_t id, const UIRect& rc, bool isInt, double* out, float scale) {
    // returns true when a value was committed into *out
    focusId_ = id;
    int len = (int)strlen(numEditBuf_);
    if(textEditId_!=id)beginTextSelection(id,numEditBuf_,len);
    float editX=std::max(rc.x+6*scale,rc.x+rc.w-r->textWidth(numEditBuf_,scale)-8*scale);
    bool over=mouseIn(rc.x,rc.y,rc.w,rc.h);
    if(over&&in_.mousePressed){
        activeId_=id;int cursor=textIndexAt(numEditBuf_,in_.mouseX-editX,scale);
        bool dbl=lastTextClickId_==id&&frame_-lastTextClickFrame_<=18;
        lastTextClickId_=id;lastTextClickFrame_=frame_;
        if(dbl){textAnchor_=0;textCursor_=len;textSelecting_=false;}
        else{textCursor_=textAnchor_=cursor;textSelecting_=true;}
    }
    if(textSelecting_&&activeId_==id&&(in_.mouseDown||in_.mouseReleased))
        textCursor_=textIndexAt(numEditBuf_,in_.mouseX-editX,scale);
    if(in_.mouseReleased)textSelecting_=false;
    if(in_.keySelectAll){textAnchor_=0;textCursor_=len;}
    if(in_.keyLeft){if(!in_.keyShift&&textCursor_!=textAnchor_)textCursor_=std::min(textCursor_,textAnchor_);else textCursor_=std::max(0,textCursor_-1);if(!in_.keyShift)textAnchor_=textCursor_;}
    if(in_.keyRight){if(!in_.keyShift&&textCursor_!=textAnchor_)textCursor_=std::max(textCursor_,textAnchor_);else textCursor_=std::min(len,textCursor_+1);if(!in_.keyShift)textAnchor_=textCursor_;}
    // Ctrl+C / Ctrl+V on a number: copy the selection, paste only digit-ish chars
    if(in_.keyCopy&&textCursor_!=textAnchor_){int a=std::min(textCursor_,textAnchor_),b=std::max(textCursor_,textAnchor_);requestCopyText(std::string(numEditBuf_+a,numEditBuf_+b));}
    if(in_.keyPaste&&!pasteText_.empty()){
        eraseTextSelection(numEditBuf_,len);
        for(char ch:pasteText_){
            bool ok=(ch>='0'&&ch<='9')||ch=='-'||(!isInt&&(ch=='.'||ch==','));
            if(!ok||len>=(int)sizeof(numEditBuf_)-1)continue;
            memmove(numEditBuf_+textCursor_+1,numEditBuf_+textCursor_,(size_t)(len-textCursor_+1));
            numEditBuf_[textCursor_++]=ch==','?'.':ch;len++;textAnchor_=textCursor_;
        }
    }
    for (int i = 0; i < in_.typedCount; i++) {
        char ch = in_.typed[i];
        bool ok = (ch >= '0' && ch <= '9') || ch == '-' || (!isInt && (ch == '.' || ch == ','));
        if (ok && len < (int)sizeof(numEditBuf_) - 1) {
            eraseTextSelection(numEditBuf_,len);
            memmove(numEditBuf_+textCursor_+1,numEditBuf_+textCursor_,(size_t)(len-textCursor_+1));
            numEditBuf_[textCursor_++]=ch==','?'.':ch;len++;textAnchor_=textCursor_;
        }
    }
    if(in_.keyBackspace){if(!eraseTextSelection(numEditBuf_,len)&&textCursor_>0){memmove(numEditBuf_+textCursor_-1,numEditBuf_+textCursor_,(size_t)(len-textCursor_+1));textCursor_--;textAnchor_=textCursor_;len--;}}
    if(in_.keyDelete){if(!eraseTextSelection(numEditBuf_,len)&&textCursor_<len){memmove(numEditBuf_+textCursor_,numEditBuf_+textCursor_+1,(size_t)(len-textCursor_));len--;}}

    bool commit = in_.keyEnter || (in_.mousePressed && !mouseIn(rc.x, rc.y, rc.w, rc.h));
    bool cancel = in_.keyEscape;
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.08f, 0.09f, 0.11f }, 1);
    r->drawRectPx(rc.x, rc.y, rc.w, 1, C_ACCENT, 1);
    r->drawRectPx(rc.x, rc.y + rc.h - 1, rc.w, 1, C_ACCENT, 1);
    editX=std::max(rc.x+6*scale,rc.x+rc.w-r->textWidth(numEditBuf_,scale)-8*scale);
    if(textCursor_!=textAnchor_){int a=std::min(textCursor_,textAnchor_),b=std::max(textCursor_,textAnchor_);float x0=r->textWidth(std::string(numEditBuf_,numEditBuf_+a),scale),x1=r->textWidth(std::string(numEditBuf_,numEditBuf_+b),scale);r->drawRectPx(editX+x0,rc.y+3,x1-x0,rc.h-6,C_SELECTED,1);}
    r->drawTextLine(editX, fieldTextY(rc, scale), numEditBuf_, C_TEXT, 1, scale);
    if ((frame_ / 30) % 2 == 0) {
        float tw = r->textWidth(std::string(numEditBuf_,numEditBuf_+textCursor_),scale);
        r->drawRectPx(editX + tw, rc.y + 4, 2 * scale, rc.h - 8, C_ACCENT, 1);
    }
    if (commit || cancel) {
        if (commit) *out = atof(numEditBuf_);
        numEditId_ = 0;
        textEditId_=0;textSelecting_=false;
        if (focusId_ == id) focusId_ = 0;
        return commit;
    }
    return false;
}

bool UI::dragFloat(const char* label, float* v, float speed, float mn, float mx) {
    uint32_t id = hash(label, p_.id);
    UIRect rc = alloc(WH);
    if (numEditId_ == id) {
        double nv;
        if (numericEdit(id, rc, false, &nv)) {
            *v = clampf((float)nv, mn, mx);
            return true;
        }
        return false;
    }
    bool hovered;
    bool clicked = behave(id, rc, &hovered);
    bool changed = false;
    float& moved = storage_[id + 21];
    if (activeId_ == id && in_.mouseDown) {
        static std::unordered_map<uint32_t, float> lastX;
        if (in_.mousePressed) { lastX[id] = in_.mouseX; moved = 0; }
        float dx = in_.mouseX - lastX[id];
        lastX[id] = in_.mouseX;
        moved += fabsf(dx);
        if (dx != 0) {
            *v = clampf(*v + dx * speed, mn, mx);
            changed = true;
        }
    }
    if (clicked && moved < 3) {
        // click without dragging: switch to keyboard editing
        numEditId_ = id;
        focusId_ = id;
        snprintf(numEditBuf_, sizeof(numEditBuf_), "%g", *v);
        beginTextSelection(id,numEditBuf_,textIndexAt(numEditBuf_,in_.mouseX-std::max(rc.x+6,rc.x+rc.w-r->textWidth(numEditBuf_)-8)));
        lastTextClickId_=id;lastTextClickFrame_=frame_;
    }
    Vec3 c = activeId_ == id ? C_WIDGET_ACT : (hovered ? C_WIDGET_HOT : C_WIDGET);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, c, 1);
    if (label[0] && label[0] != '#') {
        r->drawTextLine(rc.x + 6, textCenterY(rc), stripId(label), C_TEXT_DIM, 1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", *v);
    float tw = r->textWidth(buf);
    r->drawTextLine(rc.x + rc.w - tw - 8, textCenterY(rc), buf, C_TEXT, 1);
    return changed;
}

bool UI::numberFieldRect(const char* id, const UIRect& rc, float* v, float wheelStep,
                         const char* label, bool isInt, float mn, float mx, float textScale) {
    uint32_t wid = hash(id, p_.id);
    lastItemRect_ = rc;
    if (textScale <= 0) textScale = 1;
    if (numEditId_ == wid) {                 // keyboard editing in progress
        double nv;
        if (numericEdit(wid, rc, isInt, &nv, textScale)) { *v = clampf((float)nv, mn, mx); return true; }
        return false;
    }
    bool over = mouseIn(rc.x, rc.y, rc.w, rc.h) && !blocked_;
    bool changed = false;
    if (over && in_.wheel != 0) {
        *v = clampf(*v + in_.wheel * wheelStep, mn, mx);
        changed = true;
    }
    if (over && in_.mousePressed) {          // click → edit as text (select / copy / paste)
        numEditId_ = wid;
        focusId_ = wid;
        activeId_ = wid;
        snprintf(numEditBuf_, sizeof(numEditBuf_), isInt ? "%.0f" : "%g", *v);
        float editX = std::max(rc.x + 6 * textScale, rc.x + rc.w - r->textWidth(numEditBuf_, textScale) - 8 * textScale);
        beginTextSelection(wid, numEditBuf_, textIndexAt(numEditBuf_, in_.mouseX - editX, textScale));
        lastTextClickId_ = wid;
        lastTextClickFrame_ = frame_;
    }
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, over ? C_WIDGET_HOT : C_WIDGET, 1);
    float ty = fieldTextY(rc, textScale);
    if (label && label[0]) r->drawTextLine(rc.x + 6 * textScale, ty, label, C_TEXT_DIM, 1, textScale);
    char buf[32];
    snprintf(buf, sizeof(buf), isInt ? "%.0f" : "%g", *v);
    r->drawTextLine(rc.x + rc.w - r->textWidth(buf, textScale) - 8 * textScale, ty, buf, C_TEXT, 1, textScale);
    return changed;
}

bool UI::combo(const char* label, int* idx, const char* const* items, int count) {
    uint32_t id = hash(label, p_.id);
    float& open = storage_[id + 3];
    UIRect rc = alloc(WH);
    bool hovered;
    bool clicked = behave(id, rc, &hovered);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, hovered ? C_WIDGET_HOT : C_WIDGET, 1);
    std::string cur = (*idx >= 0 && *idx < count) ? items[*idx] : "-";
    if (label[0] != '#') {
        std::string cleanLabel = stripId(label);
        float labelMax = (std::max)(32.0f, rc.w * 0.42f);
        std::string shownLabel = ellipsize(cleanLabel, labelMax);
        r->drawTextLine(rc.x + 6, textCenterY(rc), shownLabel, C_TEXT_DIM, 1);
        float valueLeft = rc.x + 12 + (std::min)(r->textWidth(shownLabel), labelMax);
        float valueAvail = (std::max)(8.0f, rc.x + rc.w - 22 - valueLeft);
        std::string shown = ellipsize(cur, valueAvail);
        float tw = r->textWidth(shown);
        r->drawTextLine(rc.x + rc.w - tw - 22, textCenterY(rc), shown, C_TEXT, 1);
        hoverTip(cur, rc, valueAvail);
    } else {
        std::string shown = ellipsize(cur, rc.w - 28.0f);
        r->drawTextLine(rc.x + 6, textCenterY(rc), shown, C_TEXT, 1);
        hoverTip(cur, rc, rc.w - 28.0f);
    }
    r->drawTextLine(rc.x + rc.w - 14, textCenterY(rc), open > 0 ? "^" : "v", C_ACCENT, 1);
    if (clicked) open = open > 0 ? 0.0f : 1.0f;
    bool changed = false;
    if (open > 0) {
        // A combo can be the first widget of a multi-column row (for example
        // "Type / Remove" in function signatures).  Its expanded entries
        // must begin on the following line: otherwise the first item consumes
        // the next column and appears beside the combo itself.
        while (rowCols_ > 0) (void)alloc(WH);
        for (int i = 0; i < count; i++) {
            char iid[64];
            snprintf(iid, sizeof(iid), "%s##%d", label, i);
            uint32_t itemId = hash(iid, p_.id);
            UIRect irc = alloc(WH * 0.9f);
            bool ihov;
            bool iclick = behave(itemId, irc, &ihov);
            Vec3 bg = i == *idx ? C_SELECTED : (ihov ? C_WIDGET_HOT : C_PANEL_HEAD);
            r->drawRectPx(irc.x + 8, irc.y, irc.w - 8, irc.h, bg, 1);
            std::string shownItem = ellipsize(items[i], irc.w - 30.0f);
            r->drawTextLine(irc.x + 16, textCenterY(irc), shownItem, C_TEXT, 1);
            hoverTip(items[i], irc, irc.w - 30.0f);
            if (iclick) {
                *idx = i;
                open = 0;
                changed = true;
            }
        }
    }
    return changed;
}

bool UI::textInput(const char* id, char* buf, int cap) {
    UIRect rc = alloc(WH);
    return textInputRect(id, buf, cap, rc, false);
}

bool UI::textInputRect(const char* id, char* buf, int cap, const UIRect& rc, bool autoFocus) {
    uint32_t wid = hash(id, p_.id);
    lastItemRect_ = rc;
    bool focusedByAuto = false;
    if (autoFocus && focusId_ != wid) {
        focusId_ = wid;
        int len = (int)strlen(buf);
        textEditId_ = wid;
        textAnchor_ = 0;
        textCursor_ = len;
        textSelecting_ = false;
        focusedByAuto = true;
    }
    bool hovered;
    (void)behave(wid, rc, &hovered);
    bool over=mouseIn(rc.x,rc.y,rc.w,rc.h)&&!blocked_;
    if(over&&in_.mousePressed){
        focusId_=wid;activeId_=wid;int len=(int)strlen(buf);int cursor=textIndexAt(buf,in_.mouseX-(rc.x+6));
        bool dbl=lastTextClickId_==wid&&frame_-lastTextClickFrame_<=18;
        lastTextClickId_=wid;lastTextClickFrame_=frame_;
        if(textEditId_!=wid)beginTextSelection(wid,buf,cursor);
        if(dbl){textAnchor_=0;textCursor_=len;textSelecting_=false;}
        else{textCursor_=textAnchor_=cursor;textSelecting_=true;}
    }
    if (!focusedByAuto && focusId_ == wid && in_.mousePressed && !mouseIn(rc.x, rc.y, rc.w, rc.h)) focusId_ = 0;
    bool focused = focusId_ == wid;
    bool changed = false;
    if (focused) {
        int len = (int)strlen(buf);
        if(textEditId_!=wid)beginTextSelection(wid,buf,len);
        if(textSelecting_&&activeId_==wid&&(in_.mouseDown||in_.mouseReleased))textCursor_=textIndexAt(buf,in_.mouseX-(rc.x+6));
        if(in_.mouseReleased)textSelecting_=false;
        if(in_.keySelectAll){textAnchor_=0;textCursor_=len;}
        if(in_.keyLeft){if(!in_.keyShift&&textCursor_!=textAnchor_)textCursor_=std::min(textCursor_,textAnchor_);else textCursor_=std::max(0,textCursor_-1);if(!in_.keyShift)textAnchor_=textCursor_;}
        if(in_.keyRight){if(!in_.keyShift&&textCursor_!=textAnchor_)textCursor_=std::max(textCursor_,textAnchor_);else textCursor_=std::min(len,textCursor_+1);if(!in_.keyShift)textAnchor_=textCursor_;}
        if(in_.keyCopy&&textCursor_!=textAnchor_){int a=std::min(textCursor_,textAnchor_),b=std::max(textCursor_,textAnchor_);requestCopyText(std::string(buf+a,buf+b));}
        if(in_.keyPaste&&!pasteText_.empty()){eraseTextSelection(buf,len);for(char ch:pasteText_){if(ch<32||ch>=127||len>=cap-1)continue;memmove(buf+textCursor_+1,buf+textCursor_,(size_t)(len-textCursor_+1));buf[textCursor_++]=ch;len++;textAnchor_=textCursor_;changed=true;}}
        for (int i = 0; i < in_.typedCount; i++) {
            char ch = in_.typed[i];
            if (ch >= 32 && ch < 127 && len < cap - 1) {
                eraseTextSelection(buf,len);
                memmove(buf+textCursor_+1,buf+textCursor_,(size_t)(len-textCursor_+1));
                buf[textCursor_++]=ch;len++;textAnchor_=textCursor_;
                changed = true;
            }
        }
        if(in_.keyBackspace){if(eraseTextSelection(buf,len)){changed=true;}else if(textCursor_>0){memmove(buf+textCursor_-1,buf+textCursor_,(size_t)(len-textCursor_+1));textCursor_--;textAnchor_=textCursor_;changed=true;}}
        if(in_.keyDelete){if(eraseTextSelection(buf,len)){changed=true;}else if(textCursor_<len){memmove(buf+textCursor_,buf+textCursor_+1,(size_t)(len-textCursor_));changed=true;}}
        if (in_.keyEnter) focusId_ = 0;
    }
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, focused ? Vec3{ 0.08f, 0.09f, 0.11f } : C_WIDGET, 1);
    if (focused) {
        r->drawRectPx(rc.x, rc.y, rc.w, 1, C_ACCENT, 1);
        r->drawRectPx(rc.x, rc.y + rc.h - 1, rc.w, 1, C_ACCENT, 1);
    }
    if(focused&&textCursor_!=textAnchor_){int a=std::min(textCursor_,textAnchor_),b=std::max(textCursor_,textAnchor_);float x0=r->textWidth(std::string(buf,buf+a)),x1=r->textWidth(std::string(buf,buf+b));r->drawRectPx(rc.x+6+x0,rc.y+3,x1-x0,rc.h-6,C_SELECTED,1);}
    r->drawTextLine(rc.x + 6, textCenterY(rc), buf, C_TEXT, 1);
    if (focused && (frame_ / 30) % 2 == 0) {
        float tw = r->textWidth(std::string(buf,buf+textCursor_));
        r->drawRectPx(rc.x + 7 + tw, rc.y + 4, 2, rc.h - 8, C_ACCENT, 1);
    }
    return changed;
}

// draw a thin 1px frame around a swatch so pure black/white stays visible
void UI::swatchBorder(const UIRect& sw) {
    Vec3 b{ .05f, .05f, .06f };
    r->drawRectPx(sw.x, sw.y, sw.w, 1, b, 1);
    r->drawRectPx(sw.x, sw.y + sw.h - 1, sw.w, 1, b, 1);
    r->drawRectPx(sw.x, sw.y, 1, sw.h, b, 1);
    r->drawRectPx(sw.x + sw.w - 1, sw.y, 1, sw.h, b, 1);
}

// A colour value is now edited only through the shared colour picker: the widget
// is just a clickable swatch (no more raw R/G/B drag fields). Click it to open the
// picker; it reports `changed` on the frame(s) the picker mutates this colour.
bool UI::colorEdit(const char* label, Vec3* c) {
    this->label(label, C_TEXT_DIM);
    UIRect sw = alloc(WH);
    r->drawRectPx(sw.x, sw.y, sw.w, sw.h, *c, 1);
    swatchBorder(sw);
    uint32_t pickerId = hash(label, p_.id ^ 0xC010A11u);
    if (mouseOk() && mouseIn(sw.x, sw.y, sw.w, sw.h) && in_.mousePressed)
        openColorPicker(label, c, nullptr, sw.x, sw.y + sw.h + 4);
    if (colorPickerId_ == pickerId && colorPickerChanged_) { colorPickerChanged_ = false; return true; }
    return false;
}

bool UI::colorEditRGBA(const char* label, Vec3* rgb, float* alpha) {
    this->label(label, C_TEXT_DIM);
    UIRect sw = alloc(WH);
    // checkerboard behind the colour reveals its alpha
    Vec3 light{ .62f, .62f, .65f }, dark{ .28f, .29f, .32f };
    float cell = sw.h * 0.5f;
    for (float cx = sw.x; cx < sw.x + sw.w; cx += cell)
        for (int cy = 0; cy < 2; cy++) {
            float cw = (std::min)(cell, sw.x + sw.w - cx);
            int col = (int)((cx - sw.x) / cell);
            r->drawRectPx(cx, sw.y + cy * cell, cw, cell, ((col + cy) & 1) ? dark : light, 1);
        }
    r->drawRectPx(sw.x, sw.y, sw.w, sw.h, *rgb, clampf(*alpha, 0, 1));
    swatchBorder(sw);
    uint32_t pickerId = hash(label, p_.id ^ 0xC010A11u);
    if (mouseOk() && mouseIn(sw.x, sw.y, sw.w, sw.h) && in_.mousePressed)
        openColorPicker(label, rgb, alpha, sw.x, sw.y + sw.h + 4);
    if (colorPickerId_ == pickerId && colorPickerChanged_) { colorPickerChanged_ = false; return true; }
    return false;
}

static void toHSV(Vec3 c,float&h,float&s,float&v){float mx=(std::max)(c.x,(std::max)(c.y,c.z)),mn=(std::min)(c.x,(std::min)(c.y,c.z)),d=mx-mn;v=mx;s=mx?d/mx:0;if(d<.00001f)h=0;else if(mx==c.x)h=fmodf((c.y-c.z)/d,6)/6;else if(mx==c.y)h=((c.z-c.x)/d+2)/6;else h=((c.x-c.y)/d+4)/6;if(h<0)h+=1;}
static Vec3 fromHSV(float h,float s,float v){float z=h*6,f=z-floorf(z),p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f));switch(((int)z)%6){case 0:return{v,t,p};case 1:return{q,v,p};case 2:return{p,v,t};case 3:return{p,q,v};case 4:return{t,p,v};default:return{v,p,q};}}
static int nib(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return-1;}

void UI::openColorPicker(const char* id,Vec3* rgb,float* alpha,float x,float y){
    colorPickerId_=hash(id,p_.id^0xC010A11u);colorPickerRgb_=rgb;colorPickerAlpha_=alpha?alpha:&colorPickerOwnedAlpha_;
    if(!alpha)colorPickerOwnedAlpha_=1;colorPickerX_=x<0?in_.mouseX:x;colorPickerY_=y<0?in_.mouseY:y;colorPickerHexFocus_=false;
    // Callers open the picker from the very click that is still "pressed" this
    // frame; without this the click-outside test below would shut it instantly.
    colorPickerOpenFrame_=frame_;
}

bool UI::takeColorPick(const char* id) {
    uint32_t pickerId = hash(id, p_.id ^ 0xC010A11u);
    if (colorPickerId_ == pickerId && colorPickerChanged_) { colorPickerChanged_ = false; return true; }
    return false;
}

void UI::drawColorPicker(){
    if(!colorPickerId_||!colorPickerRgb_||!colorPickerAlpha_)return;
    const float W=310,H=354,PI=3.14159265f;float x=clampf(colorPickerX_,4,(std::max)(4.f,(float)r->width()-W-4)),y=clampf(colorPickerY_,4,(std::max)(4.f,(float)r->height()-H-4));
    UIRect pop{x,y,W,H};bool inside=mouseIn(x,y,W,H);
    bool justOpened=(frame_==colorPickerOpenFrame_);
    if(in_.keyEscape||(!justOpened&&in_.mousePressed&&!inside)){colorPickerId_=0;colorPickerRgb_=nullptr;return;}
    r->setUIScissor(0,0,0,0,false);r->drawRectPx(x+4,y+5,W,H,{0,0,0},.4f);r->drawRectPx(x,y,W,H,{.105f,.115f,.135f},1);r->drawRectPx(x,y,W,27,{.14f,.155f,.18f},1);r->drawTextLine(x+10,y+5,"Color picker",C_TEXT,1);
    float h,s,v;toHSV(*colorPickerRgb_,h,s,v);float cx=x+82,cy=y+107,ro=65,ri=47;
    for(int i=0;i<72;i++){float a=i/72.f*2*PI-PI/2,b=(i+1)/72.f*2*PI-PI/2;Vec3 c=fromHSV(i/72.f,1,1);r->drawTriPx(cx+cosf(a)*ri,cy+sinf(a)*ri,cx+cosf(a)*ro,cy+sinf(a)*ro,cx+cosf(b)*ro,cy+sinf(b)*ro,c,1);r->drawTriPx(cx+cosf(a)*ri,cy+sinf(a)*ri,cx+cosf(b)*ro,cy+sinf(b)*ro,cx+cosf(b)*ri,cy+sinf(b)*ri,c,1);}
    float dx=in_.mouseX-cx,dy=in_.mouseY-cy,d=sqrtf(dx*dx+dy*dy);if(in_.mousePressed&&d>ri-4&&d<ro+4)colorPickerDrag_=1;if(colorPickerDrag_==1&&in_.mouseDown){h=atan2f(dy,dx)/(2*PI)+.25f;if(h<0)h++;if(h>=1)h--;*colorPickerRgb_=fromHSV(h,s,v);colorPickerChanged_=true;}float a=h*2*PI-PI/2;r->drawRectPx(cx+cosf(a)*56-3,cy+sinf(a)*56-3,6,6,{1,1,1},1);
    float sx=x+160,sy=y+43,sw=132,sh=128;for(int iy=0;iy<32;iy++)for(int ix=0;ix<33;ix++)r->drawRectPx(sx+ix*4,sy+iy*4,4,4,fromHSV(h,ix/32.f,1-iy/31.f),1);
    if(in_.mousePressed&&mouseIn(sx,sy,sw,sh))colorPickerDrag_=2;if(colorPickerDrag_==2&&in_.mouseDown){s=clampf((in_.mouseX-sx)/sw,0,1);v=1-clampf((in_.mouseY-sy)/sh,0,1);*colorPickerRgb_=fromHSV(h,s,v);colorPickerChanged_=true;}r->drawRectPx(sx+s*sw-3,sy+(1-v)*sh-3,6,6,{1,1,1},1);
    const char* nm[4]={"R","G","B","A"};float* val[4]={&colorPickerRgb_->x,&colorPickerRgb_->y,&colorPickerRgb_->z,colorPickerAlpha_};for(int j=0;j<4;j++){float yy=y+188+j*28,bx=x+32,bw=205;r->drawTextLine(x+12,yy+4,nm[j],C_TEXT_DIM,1);for(int i=0;i<52;i++){Vec3 c=*colorPickerRgb_;float q=i/51.f;if(j==0)c.x=q;if(j==1)c.y=q;if(j==2)c.z=q;if(j==3)c={q,q,q};r->drawRectPx(bx+i*bw/52,yy,bw/52+1,20,c,1);}r->drawRectPx(bx+clampf(*val[j],0,1)*bw-2,yy-2,4,24,{1,1,1},1);char n[8];snprintf(n,8,"%d",(int)(clampf(*val[j],0,1)*255+.5f));r->drawTextLine(x+250,yy+4,n,C_TEXT,1);if(in_.mousePressed&&mouseIn(bx,yy,bw,20))colorPickerDrag_=3+j;if(colorPickerDrag_==3+j&&in_.mouseDown){*val[j]=clampf((in_.mouseX-bx)/bw,0,1);colorPickerChanged_=true;}}
    if(in_.mouseReleased)colorPickerDrag_=0;UIRect hr{x+58,y+309,150,25};r->drawTextLine(x+12,y+314,"HEX",C_TEXT_DIM,1);r->drawRectPx(hr.x,hr.y,hr.w,hr.h,colorPickerHexFocus_?Vec3{.08f,.09f,.11f}:C_WIDGET,1);
    if(!colorPickerHexFocus_){int R=(int)(clampf(colorPickerRgb_->x,0,1)*255+.5f),G=(int)(clampf(colorPickerRgb_->y,0,1)*255+.5f),B=(int)(clampf(colorPickerRgb_->z,0,1)*255+.5f),A=(int)(clampf(*colorPickerAlpha_,0,1)*255+.5f);snprintf(colorPickerHex_,10,"#%02X%02X%02X%02X",R,G,B,A);}r->drawTextLine(hr.x+7,hr.y+5,colorPickerHex_,C_TEXT,1);
    if(in_.mousePressed&&mouseIn(hr.x,hr.y,hr.w,hr.h))colorPickerHexFocus_=true;if(colorPickerHexFocus_){int len=(int)strlen(colorPickerHex_);if(in_.keyBackspace&&len>1)colorPickerHex_[--len]=0;for(int i=0;i<in_.typedCount&&len<9;i++)if(nib(in_.typed[i])>=0){colorPickerHex_[len++]=in_.typed[i];colorPickerHex_[len]=0;}if(len==9){int q[8];bool ok=true;for(int i=0;i<8;i++){q[i]=nib(colorPickerHex_[i+1]);ok&=q[i]>=0;}if(ok){colorPickerRgb_->x=(q[0]*16+q[1])/255.f;colorPickerRgb_->y=(q[2]*16+q[3])/255.f;colorPickerRgb_->z=(q[4]*16+q[5])/255.f;*colorPickerAlpha_=(q[6]*16+q[7])/255.f;colorPickerChanged_=true;}}if(in_.keyEnter)colorPickerHexFocus_=false;}r->drawTextLine(x+218,y+314,"#RRGGBBAA",C_TEXT_DIM,1);registerBlockingRect(pop);reclipPanel();
}

void UI::openEnumPicker(const char* id, int current, const char* const* items, int count,
                        float x, float y, float w) {
    enumPickerId_ = hash(id, p_.id ^ 0xE17E5u);
    enumPickerCurrent_ = current;
    enumPickerItems_.clear();
    for (int i = 0; i < count; i++) enumPickerItems_.push_back(items[i] ? items[i] : "");
    enumPickerX_ = x; enumPickerY_ = y; enumPickerW_ = w < 60 ? 60 : w;
    // opened by a click that is still "pressed" this frame — without this the
    // click-outside test would shut it instantly
    enumPickerOpenFrame_ = frame_;
}

bool UI::takeEnumPick(const char* id, int* out) {
    uint32_t wid = hash(id, p_.id ^ 0xE17E5u);
    if (!enumPickerResultId_ || enumPickerResultId_ != wid) return false;
    enumPickerResultId_ = 0;
    if (out) *out = enumPickerResult_;
    return true;
}

bool UI::popupCoversPointer() const {
    const UIRect& rc = enumPickerRect_;
    return enumPickerId_ != 0 && rc.w > 0 &&
           in_.mouseX >= rc.x && in_.mouseX < rc.x + rc.w &&
           in_.mouseY >= rc.y && in_.mouseY < rc.y + rc.h;
}

void UI::drawEnumPicker() {
    if (!enumPickerId_ || enumPickerItems_.empty()) { enumPickerRect_ = {}; return; }
    const float ROW = 20, PAD = 3;
    float w = enumPickerW_, h = enumPickerItems_.size() * ROW + PAD * 2;
    float x = clampf(enumPickerX_, 2, std::max(2.0f, (float)r->width() - w - 2));
    float y = clampf(enumPickerY_, 2, std::max(2.0f, (float)r->height() - h - 2));
    enumPickerRect_ = { x, y, w, h };   // consulted by popupCoversPointer()
    bool inside = mouseIn(x, y, w, h);
    bool justOpened = (frame_ == enumPickerOpenFrame_);
    if (in_.keyEscape || (!justOpened && in_.mousePressed && !inside)) {
        enumPickerId_ = 0; enumPickerRect_ = {};
        return;
    }
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(x + 3, y + 4, w, h, { 0, 0, 0 }, 0.35f);            // drop shadow
    r->drawRectPx(x, y, w, h, { 0.105f, 0.115f, 0.135f }, 1);
    r->drawRectPx(x, y, w, 1, C_ACCENT, 0.8f);
    for (int i = 0; i < (int)enumPickerItems_.size(); i++) {
        UIRect row = { x + PAD, y + PAD + i * ROW, w - PAD * 2, ROW };
        bool over = mouseIn(row.x, row.y, row.w, row.h);
        bool cur = i == enumPickerCurrent_;
        if (cur) r->drawRectPx(row.x, row.y, row.w, row.h, C_SELECTED, 1);
        else if (over) r->drawRectPx(row.x, row.y, row.w, row.h, C_WIDGET_HOT, 1);
        r->drawTextLine(row.x + 8, row.y + 3, ellipsize(enumPickerItems_[i], row.w - 14),
                        cur ? Vec3{ 0.85f, 0.93f, 1.0f } : C_TEXT, 1);
        if (over && in_.mousePressed && !justOpened) {
            // the owner collects this by id on its next frame
            enumPickerResultId_ = enumPickerId_;
            enumPickerResult_ = i;
            enumPickerId_ = 0;
            enumPickerRect_ = {};
            registerBlockingRect({ x, y, w, h });
            reclipPanel();
            return;
        }
    }
    registerBlockingRect({ x, y, w, h });
    reclipPanel();
}

bool UI::headerClosable(const char* text) {
    UIRect rc = alloc(WH * 0.9f);
    r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, C_PANEL_HEAD, 1);
    r->drawTextLine(rc.x + 2, rc.y + 2, text, C_ACCENT, 1);
    UIRect xr = { rc.x + rc.w - 18, rc.y + 2, 16, 16 };
    uint32_t wid = hash(text, p_.id ^ 0xC105E);
    bool hov;
    bool clicked = behave(wid, xr, &hov);
    r->drawTextLine(xr.x + 4, xr.y - 1, "x", hov ? Vec3{ 1, 0.5f, 0.5f } : C_TEXT_DIM, 1);
    return clicked;
}

int UI::componentBegin(const char* idText, const char* title, bool collapsed, bool removable,
                       bool dragging, bool dropHighlight, bool draggable) {
    componentCardId_ = hash(idText, p_.id ^ 0xC04DCA4D);
    componentCardStartY_ = p_.cy;
    componentCardActive_ = true;
    uint32_t heightId = componentCardId_ ^ 0x4A31B7u;
    float previousHeight = storage_[heightId];
    if (previousHeight < WH + 4) previousHeight = collapsed ? WH + 4 : 112;

    componentCardSavedX_ = p_.x;
    componentCardSavedW_ = p_.w;
    Vec3 body = dropHighlight ? Vec3{ .105f, .205f, .30f } : Vec3{ .115f, .128f, .155f };
    float cardX = p_.x - 2, cardW = p_.w + 4;
    r->drawRectPx(cardX + 4, componentCardStartY_ + 5, cardW, previousHeight, { 0,0,0 }, .28f);
    r->drawRectPx(cardX, componentCardStartY_, cardW, previousHeight, body, 1);

    UIRect rc = alloc(WH);
    lastComponentHeader_ = rc;
    Vec3 head = dragging ? C_WIDGET_ACT : (dropHighlight ? C_SELECTED : Vec3{ .155f, .17f, .20f });
    r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, head, 1);
    r->drawRectPx(rc.x - 4, rc.y, 3, rc.h, dropHighlight ? Vec3{ .35f, .75f, 1.0f } : C_ACCENT, 1);

    int result = 0;
    bool headerHover = mouseIn(rc.x - 4, rc.y, rc.w + 8, rc.h);
    // Right-click reports COMP_RESET even while blocked when the reset probe is on,
    // so that right-clicking a component while its menu is open moves/reopens it.
    if ((!blocked_ || componentResetProbe_) && headerHover && in_.rmbReleased) result |= COMP_RESET;
    UIRect arrow = { rc.x + 2, rc.y + 2, 18, rc.h - 4 };
    bool arrowHover = false;
    if (behave(componentCardId_ ^ 0xA22u, arrow, &arrowHover)) result |= COMP_TOGGLED;
    r->drawTextLine(arrow.x + 5, arrow.y + 1, collapsed ? ">" : "v", arrowHover ? Vec3{ .9f,.95f,1 } : C_ACCENT, 1);

    float right = removable ? 22.0f : 3.0f;
    UIRect dragRect = { rc.x + 22, rc.y, rc.w - 22 - right, rc.h };
    bool hover = false;
    if (draggable) {
        behave(componentCardId_, dragRect, &hover);
        if (hover) result |= COMP_HOVERED;
        if (hover && in_.mousePressed) result |= COMP_PRESSED;
        if (activeId_ == componentCardId_ && in_.mouseDown) result |= COMP_HELD;
    }
    r->drawTextLine(dragRect.x + 1, textCenterY(rc), title, hover ? Vec3{ .93f,.95f,1 } : C_TEXT, 1);
    if (draggable) r->drawTextLine(rc.x + rc.w - right - 13, rc.y + 3, "=", hover ? C_ACCENT : C_TEXT_DIM, 1);

    if (removable) {
        UIRect close = { rc.x + rc.w - 19, rc.y + 2, 17, rc.h - 4 };
        bool closeHover = false;
        if (behave(componentCardId_ ^ 0xC105Eu, close, &closeHover)) result |= COMP_REMOVE;
        r->drawTextLine(close.x + 5, close.y, "x", closeHover ? Vec3{ 1,.5f,.5f } : C_TEXT_DIM, 1);
    }
    // Every component owns a visibly padded body. Callers keep using the
    // regular layout API; componentEnd restores the panel bounds.
    p_.x = componentCardSavedX_ + componentCardPad_;
    p_.w = (std::max)(40.0f, componentCardSavedW_ - componentCardPad_ * 2);
    p_.cy += collapsed ? 5.0f : componentCardPad_;
    return result;
}

void UI::componentEnd() {
    if (!componentCardActive_) return;
    spacing(componentCardPad_);
    p_.x = componentCardSavedX_;
    p_.w = componentCardSavedW_;
    float height = p_.cy - componentCardStartY_;
    storage_[componentCardId_ ^ 0x4A31B7u] = height;
    float x = p_.x - 2, w = p_.w + 4;
    Vec3 border = { .255f,.29f,.35f };
    r->drawRectPx(x, componentCardStartY_, 1, height, border, 1);
    r->drawRectPx(x + w - 1, componentCardStartY_, 1, height, border, 1);
    r->drawRectPx(x, componentCardStartY_ + height - 1, w, 1, border, 1);
    componentCardActive_ = false;
    spacing(8);
}

int UI::iconTile(const char* id, const std::string& label, int icon, bool selected, float tileHeight, bool drawLabel, const Vec3* folderColor, const char* imageName) {
    uint32_t wid = hash(id, p_.id);
    tileHeight = clampf(tileHeight, 64.0f, 144.0f);
    UIRect rc = alloc(tileHeight);
    bool hov;
    bool clicked = behave(wid, rc, &hov);

    Vec3 bg = selected ? C_SELECTED : (hov ? C_WIDGET : C_PANEL_HEAD);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, bg, selected || hov ? 1.0f : 0.55f);
    if (selected) r->drawRectPx(rc.x, rc.y, rc.w, 2, C_ACCENT, 1);

    float cx = floorf(rc.x + rc.w / 2);
    float iconScale = clampf((std::min)(rc.w, rc.h) / 84.0f, 0.65f, 1.55f);
    float iy = rc.y + 5;
    auto imageIt=imageName?assetIcons_.find(imageName):assetIcons_.end();
    if(imageIt!=assetIcons_.end()){
        float side=(std::min)(rc.w-10.0f,rc.h-(drawLabel?34.0f:10.0f));
        Vec3 tint=(icon==0&&folderColor)?*folderColor:Vec3{1,1,1};
        r->drawImagePx(imageIt->second,floorf(cx-side*.5f),floorf(iy),side,side,tint,1);
    } else if (icon == 0) {
        // folder
        Vec3 base = folderColor ? *folderColor : Vec3{ 0.85f, 0.68f, 0.32f };
        Vec3 gold = hov ? base * 1.12f : base;
        float fw = 42 * iconScale, fh = 28 * iconScale;
        r->drawRectPx(floorf(cx - fw * .5f), floorf(iy + 5 * iconScale), floorf(18 * iconScale), floorf(8 * iconScale), gold * 0.85f, 1);
        r->drawRectPx(floorf(cx - fw * .5f), floorf(iy + 11 * iconScale), floorf(fw), floorf(fh), gold, 1);
        r->drawRectPx(floorf(cx - fw * .5f), floorf(iy + 11 * iconScale), floorf(fw), (std::max)(2.0f, floorf(4 * iconScale)), gold * 1.12f, 1);
    } else {
        // sheet with folded corner
        Vec3 paper = { 0.88f, 0.9f, 0.93f };
        float sw = floorf(30 * iconScale), sh = floorf(40 * iconScale), fold = floorf(10 * iconScale);
        float sx = floorf(cx - sw * .5f), sy = floorf(iy + 2 * iconScale);
        r->drawRectPx(sx, sy, sw - fold, sh, paper, 1);
        r->drawRectPx(sx, sy + fold, sw, sh - fold, paper, 1);
        r->drawTriPx(sx + sw - fold, sy, sx + sw, sy + fold, sx + sw - fold, sy + fold, paper * 0.72f, 1);
        // text lines
        for (int i = 0; i < 3; i++) {
            r->drawRectPx(floorf(sx + 5 * iconScale), floorf(sy + (16 + i * 6) * iconScale),
                          (std::max)(3.0f, floorf(sw - 10 * iconScale)), (std::max)(1.0f, floorf(2 * iconScale)),
                          { 0.55f, 0.58f, 0.64f }, 1);
        }
        // type badge
        Vec3 badge = icon == 2 ? Vec3{ 0.9f, 0.55f, 0.2f }     // prefab
                   : icon == 3 ? Vec3{ 0.35f, 0.75f, 0.45f }   // scene
                   : icon == 4 ? Vec3{ 0.3f, 0.62f, 0.99f }    // blueprint
                   : icon == 5 ? Vec3{ 0.68f, 0.35f, 0.92f }   // blueprint interface
                   : icon == 6 ? Vec3{ 0.2f, 0.82f, 0.82f }    // curve
                   : icon == 7 ? Vec3{ 0.95f, 0.58f, 0.22f }   // mesh 3D
                   : icon == 8 ? Vec3{ 0.35f, 0.82f, 0.38f }   // texture
                   : icon == 9 ? Vec3{ 0.92f, 0.32f, 0.62f }   // audio
                   : icon == 10 ? Vec3{ 0.28f, 0.65f, 0.95f }  // audio class
                   : icon == 11 ? Vec3{ 0.62f, 0.38f, 0.92f }  // attenuation
                   : icon == 12 ? Vec3{ 0.95f, 0.58f, 0.22f }  // concurrency
                   : icon == 13 ? Vec3{ 0.72f, 0.36f, 0.92f }  // enum
                   : icon == 14 ? Vec3{ 0.95f, 0.34f, 0.30f }  // animation clip
                   : icon == 15 ? Vec3{ 0.28f, 0.78f, 0.48f }  // animator controller
                   : Vec3{ 0.6f, 0.6f, 0.65f };
        if (icon >= 2) r->drawRectPx(floorf(sx + 3 * iconScale), floorf(sy + sh - 11 * iconScale),
                                     floorf(12 * iconScale), floorf(8 * iconScale), badge, 1);
    }

    if (!drawLabel) {
        int res = 0;
        if (clicked) {
            float last = storage_[wid + 11];
            res = (frame_ - last < 22) ? TILE_DBLCLICK : TILE_CLICK;
            storage_[wid + 11] = (float)frame_;
        }
        if (hov) {
            res |= TILE_HOVERED;
            if (in_.mousePressed) res |= TILE_PRESSED;
            if (in_.rmbReleased) res |= TILE_RCLICKED;
        }
        return res;
    }

    // Smaller crisp label, wrapped to two centered lines before ellipsizing.
    const float textScale = 0.86f;
    const float maxTextW = (std::max)(24.0f, rc.w - 8);
    auto fitPrefix = [&](const std::string& value) {
        int fit = 0;
        for (int i = 1; i <= (int)value.size(); i++) {
            if (r->textWidth(value.substr(0, i), textScale) > maxTextW) break;
            fit = i;
        }
        return fit;
    };
    auto ellipsizeScaled = [&](std::string value) {
        if (r->textWidth(value, textScale) <= maxTextW) return value;
        while (!value.empty() && r->textWidth(value + "...", textScale) > maxTextW) value.pop_back();
        return value + "...";
    };
    std::string first = label, second;
    if (r->textWidth(label, textScale) > maxTextW) {
        int split = fitPrefix(label);
        int natural = split;
        while (natural > split / 2 && label[natural - 1] != ' ' && label[natural - 1] != '_' &&
               label[natural - 1] != '-' && label[natural - 1] != '.') natural--;
        if (natural > split / 2) split = natural;
        first = label.substr(0, split);
        second = label.substr(split);
        while (!second.empty() && second.front() == ' ') second.erase(second.begin());
        second = ellipsizeScaled(second);
    }
    Vec3 labelColor = selected ? Vec3{ 0.8f, 0.9f, 1.0f } : Vec3{ .90f, .92f, .96f };
    if (second.empty()) {
        first = ellipsizeScaled(first);
        float tw = r->textWidth(first, textScale);
        r->drawTextLine(floorf(cx - tw / 2), floorf(rc.y + rc.h - 18), first, labelColor, 1, textScale);
    } else {
        float firstW = r->textWidth(first, textScale), secondW = r->textWidth(second, textScale);
        r->drawTextLine(floorf(cx - firstW / 2), floorf(rc.y + rc.h - 29), first, labelColor, 1, textScale);
        r->drawTextLine(floorf(cx - secondW / 2), floorf(rc.y + rc.h - 16), second, labelColor, 1, textScale);
    }
    hoverTip(label, rc, maxTextW * 2.0f / textScale);

    int res = 0;
    if (clicked) {
        float last = storage_[wid + 11];
        res = (frame_ - last < 22) ? TILE_DBLCLICK : TILE_CLICK;
        storage_[wid + 11] = (float)frame_;
    }
    if (hov) {
        res |= TILE_HOVERED;
        if (in_.mousePressed) res |= TILE_PRESSED;
        if (in_.rmbReleased) res |= TILE_RCLICKED;
    }
    return res;
}

int UI::treeItem(const char* id, const std::string& text, int depth, bool hasChildren,
                 bool expanded, bool selected, bool dropHighlight, bool prefabTint, int folderIcon, const Vec3* folderColor,
                 const char* iconImage) {
    uint32_t wid = hash(id, p_.id);
    UIRect rc = alloc(WH * 0.92f);
    int result = 0;
    bool hovered;
    bool clicked = behave(wid, rc, &hovered);
    if (hovered) result |= TREE_HOVERED;
    if (hovered && in_.mousePressed) result |= TREE_PRESSED;

    float indent = depth * 16.0f;
    UIRect tri = { rc.x + indent, rc.y, 16, rc.h };
    bool overTri = hasChildren && mouseOk() && !blocked_ && mouseIn(tri.x, tri.y, tri.w, tri.h);

    if (dropHighlight) r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, { 0.15f, 0.38f, 0.2f }, 1);
    else if (selected) r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, C_SELECTED, 1);
    else if (hovered) r->drawRectPx(rc.x - 4, rc.y, rc.w + 8, rc.h, C_WIDGET, 0.7f);

    if (hasChildren) {
        r->drawTextLine(tri.x + 3, rc.y + 3, expanded ? "v" : ">", C_ACCENT, 1);
    }
    float tx = rc.x + indent + 16;
    Vec3 rowText = selected ? Vec3{ 0.75f, 0.87f, 1.0f } : prefabTint ? Vec3{ .46f, .72f, 1.0f } : C_TEXT;
    if (folderIcon >= 0) {
        bool open = folderIcon > 0;
        float fx = tx + 1.0f, fy = rc.y + 5.0f;
        Vec3 base = folderColor ? *folderColor : Vec3{ 0.86f, 0.66f, 0.31f };
        Vec3 back = base * (open ? .80f : .86f);
        Vec3 front = open ? base * 1.08f : base;
        auto fit=assetIcons_.find(open?"folder_open":"folder");
        if(fit!=assetIcons_.end()){
            r->drawImagePx(fit->second,fx,rc.y+1,20,20,folderColor?*folderColor:Vec3{1,1,1},1);
        } else {
            r->drawRectPx(fx + 1, fy, 8, 4, back * 1.08f, 1);
            r->drawRectPx(fx, fy + 3, 17, 11, back, 1);
            if (open) {
                r->drawTriPx(fx + 1, fy + 7, fx + 18, fy + 7, fx + 3, fy + 15, front, 1);
                r->drawRectPx(fx + 3, fy + 7, 17, 8, front, 1);
            } else {
                r->drawRectPx(fx + 1, fy + 6, 18, 9, front, 1);
                r->drawRectPx(fx + 1, fy + 6, 18, 2, front * 1.12f, 1);
            }
        }
        tx += 23.0f;
    }
    if (iconImage && *iconImage) {   // per-type asset icon at the row start (e.g. outliner entity kind)
        auto iit = assetIcons_.find(iconImage);
        if (iit != assetIcons_.end()) r->drawImagePx(iit->second, tx, rc.y + 1, 18, 18, { 1, 1, 1 }, 1);
        tx += 21.0f;
    }
    float avail = rc.x + rc.w - tx - 2;
    r->drawTextLine(tx, textCenterY(rc), ellipsize(text, avail), rowText, 1);
    hoverTip(text, rc, avail);

    if (clicked) {
        if (overTri) result |= TREE_TOGGLED;
        else result |= TREE_CLICKED;
    }
    if (hovered && in_.rmbReleased) result |= TREE_RCLICKED;
    return result;
}

void UI::separator() {
    UIRect rc = alloc(6);
    r->drawRectPx(rc.x, rc.y + 3, rc.w, 1, C_BORDER, 1);
}

std::string UI::ellipsize(const std::string& s, float maxW) const {
    if (maxW <= 0 || r->textWidth(s) <= maxW) return s;
    float dots = r->textWidth("...");
    std::string out = s;
    while (!out.empty() && r->textWidth(out) + dots > maxW) out.pop_back();
    return out + "...";
}

void UI::hoverTip(const std::string& full, const UIRect& rc, float shownW) {
    if (mouseOk() && !blocked_ && mouseIn(rc.x, rc.y, rc.w, rc.h) && r->textWidth(full) > shownW) {
        tip_ = full;
        tipX_ = in_.mouseX;
        tipY_ = in_.mouseY;
    }
}

void UI::spacing(float h) {
    alloc(h);
}

bool UI::tabBar(const char* const* tabs, int count, int* active) {
    bool changed = false;
    row(count);
    for (int i = 0; i < count; i++) {
        uint32_t id = hash(tabs[i], p_.id ^ 0xABCD);
        UIRect rc = alloc(WH);
        bool hovered;
        bool clicked = behave(id, rc, &hovered);
        bool act = *active == i;
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, act ? C_SELECTED : (hovered ? C_WIDGET_HOT : C_PANEL_HEAD), 1);
        if (act) r->drawRectPx(rc.x, rc.y, rc.w, 2, C_ACCENT, 1);
        float tw = r->textWidth(tabs[i]);
        r->drawTextLine(rc.x + (rc.w - tw) / 2, textCenterY(rc), tabs[i], act ? C_TEXT : C_TEXT_DIM, 1);
        if (clicked) { *active = i; changed = true; }
    }
    return changed;
}

// ═══ menu bar ═══
void UI::menuBarBegin(float h) {
    menuBarH_ = h;
    menuX_ = 8;
    barRect_ = { 0, 0, (float)r->width(), h };
    panelRects_.push_back(barRect_);
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(0, 0, (float)r->width(), h, C_PANEL_HEAD, 1);
    r->drawRectPx(0, h - 1, (float)r->width(), 1, C_BORDER, 1);
}

bool UI::menuBegin(const char* label) {
    uint32_t id = hash(label, 0x51EA);
    float tw = r->textWidth(label);
    UIRect rc = { menuX_, 2, tw + 22, menuBarH_ - 4 };
    menuX_ += rc.w + 2;
    bool over = mouseIn(rc.x, rc.y, rc.w, rc.h);
    if (over && in_.mousePressed) {
        openMenuId_ = openMenuId_ == id ? 0 : id;
        menuClickedThisFrame_ = true;
    }
    if (over && openMenuId_ && openMenuId_ != id) openMenuId_ = id; // slide between menus
    bool open = openMenuId_ == id;
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, open ? C_SELECTED : (over ? C_WIDGET_HOT : C_PANEL_HEAD), 1);
    r->drawTextLine(rc.x + 11, textCenterY(rc), label, open ? Vec3{ 0.8f, 0.9f, 1.0f } : C_TEXT, 1);
    if (open) {
        menuScope_ = true;
        popX_ = rc.x;
        popY_ = menuBarH_;
        popW_ = 230;
        itemY_ = popY_ + 3;
        popupRect_ = { popX_, popY_, popW_, 4 };
        r->drawRectPx(popX_, popY_, popW_, 3, C_PANEL_HEAD, 0.98f);
    }
    return open;
}

bool UI::menuItem(const char* label) {
    float needed = r->textWidth(label) + 34.0f;
    if (needed > popW_) {
        float oldW = popW_;
        popW_ = (std::min)(needed, (float)r->width() - popX_ - 6.0f);
        // Il menu puo' scoprire una voce piu' lunga dopo avere gia' disegnato
        // le righe precedenti. Ridisegnare qui l'intero fondo copriva i testi
        // gia' emessi; estendiamo invece solo la nuova fascia laterale.
        if (popW_ > oldW)
            r->drawRectPx(popX_ + oldW, popY_, popW_ - oldW, popupRect_.h, C_PANEL_HEAD, 0.98f);
        popupRect_.w = popW_;
    }
    uint32_t id = hash(label, 0x77AA ^ openMenuId_);
    UIRect rc = { popX_, itemY_, popW_, WH };
    itemY_ += WH;
    popupRect_.h = itemY_ - popY_ + 3;
    bool over = mouseIn(rc.x, rc.y, rc.w, rc.h);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, over ? C_SELECTED : C_PANEL_HEAD, 0.98f);
    std::string shown = ellipsize(label, rc.w - 28.0f);
    r->drawTextLine(rc.x + 14, textCenterY(rc), shown, over ? Vec3{ 0.85f, 0.93f, 1.0f } : C_TEXT, 1);
    if (over && in_.mousePressed) {
        openMenuId_ = 0;
        menuClickedThisFrame_ = true;
        return true;
    }
    return false;
}

void UI::menuLabel(const char* label) {
    float needed = r->textWidth(label) + 28.0f;
    if (needed > popW_) {
        float oldW = popW_;
        popW_ = (std::min)(needed, (float)r->width() - popX_ - 6.0f);
        if (popW_ > oldW)
            r->drawRectPx(popX_ + oldW, popY_, popW_ - oldW, popupRect_.h, C_PANEL_HEAD, 0.98f);
        popupRect_.w = popW_;
    }
    UIRect rc = { popX_, itemY_, popW_, 19 };
    itemY_ += rc.h;
    popupRect_.h = itemY_ - popY_ + 3;
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { .105f, .115f, .135f }, .99f);
    r->drawTextLine(rc.x + 10, textCenterY(rc), ellipsize(label, rc.w - 20.0f), { .38f, .68f, 1.0f }, 1);
}

void UI::menuSeparator() {
    UIRect rc = { popX_, itemY_, popW_, 7 };
    itemY_ += 7;
    popupRect_.h = itemY_ - popY_ + 3;
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, C_PANEL_HEAD, 0.98f);
    r->drawRectPx(rc.x + 8, rc.y + 3, rc.w - 16, 1, C_BORDER, 1);
}

void UI::menuEnd() {
    r->drawRectPx(popX_, itemY_, popW_, 3, C_PANEL_HEAD, 0.98f);
    // border
    r->drawRectPx(popX_, popY_, 1, popupRect_.h, C_BORDER, 1);
    r->drawRectPx(popX_ + popW_ - 1, popY_, 1, popupRect_.h, C_BORDER, 1);
    r->drawRectPx(popX_, popY_ + popupRect_.h - 1, popW_, 1, C_BORDER, 1);
    menuScope_ = false;
}

void UI::menuBarEnd() {}

bool UI::barButton(const char* label, Vec3 bg, Vec3 fg) {
    uint32_t id = hash(label, 0xBB77);
    float tw = r->textWidth(label);
    UIRect rc = { menuX_, 3, tw + 20, menuBarH_ - 6 };
    menuX_ += rc.w + 4;
    bool over = mouseOk() && mouseIn(rc.x, rc.y, rc.w, rc.h);
    if (over && in_.mousePressed) activeId_ = id;
    bool clicked = over && in_.mouseReleased && activeId_ == id;
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, over ? bg * 1.3f : bg, 1);
    r->drawTextLine(rc.x + 10, textCenterY(rc), label, fg, 1);
    return clicked;
}

bool UI::barCheckbox(const char* label, bool* value) {
    uint32_t id = hash(label, 0xBC77);
    float tw = r->textWidth(label);
    UIRect rc = { menuX_, 3, tw + 35, menuBarH_ - 6 };
    menuX_ += rc.w + 4;
    bool over = mouseOk() && mouseIn(rc.x, rc.y, rc.w, rc.h);
    if (over && in_.mousePressed) activeId_ = id;
    bool clicked = over && in_.mouseReleased && activeId_ == id;
    Vec3 bg = *value ? Vec3{ 0.12f, 0.32f, 0.56f }
                     : (over ? Vec3{ 0.20f, 0.24f, 0.31f } : Vec3{ 0.115f, 0.13f, 0.17f });
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, bg, 1);
    float box = 14, by = rc.y + (rc.h - box) * 0.5f;
    r->drawRectPx(rc.x + 7, by, box, box, over ? C_WIDGET_HOT : C_WIDGET, 1);
    if (*value) r->drawRectPx(rc.x + 10, by + 3, box - 6, box - 6, C_ACCENT, 1);
    r->drawTextLine(rc.x + 27, textCenterY(rc), label,
                    *value ? Vec3{ 0.82f, 0.92f, 1.0f } : C_TEXT, 1);
    if (clicked) { *value = !*value; return true; }
    return false;
}

void UI::barLabel(const std::string& text, Vec3 color) {
    r->drawTextLine(menuX_ + 6, 8, text, color, 1);
    menuX_ += r->textWidth(text) + 16;
}
