// ─── Pulse Engine UI: immediate-mode GUI from scratch (no external libs) ───
#pragma once
#include "math.h"
#include "render.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct UIInput {
    float mouseX = 0, mouseY = 0;
    bool mouseDown = false;
    bool mousePressed = false;
    bool mouseReleased = false;
    bool rmbDown = false, rmbPressed = false, rmbReleased = false;
    bool mmbDown = false, mmbPressed = false, mmbReleased = false;
    float wheel = 0;              // +1 per notch up
    char typed[32] = {};
    int typedCount = 0;
    bool keyBackspace = false, keyEnter = false, keyEscape = false, keyDelete = false;
    bool keyLeft = false, keyRight = false, keySelectAll = false;
    bool keyCtrl = false, keyAlt = false, keyShift = false; // modifier state this frame
    bool keyCopy = false, keyPaste = false;       // Ctrl+C / Ctrl+V routed to a canvas
    bool keyDuplicate = false;                    // Ctrl+D routed to a canvas
    int keyPressedVK = 0;                         // raw VK pressed this frame (key binding)
};

struct UIRect { float x = 0, y = 0, w = 0, h = 0; };

class UI {
public:
    void begin(Renderer* renderer, const UIInput& input);
    void end();
    bool wantMouse() const;
    bool wantKeyboard() const { return focusId_ != 0; }

    // ── menu bar (call LAST each frame so popups draw on top) ──
    void menuBarBegin(float h = 30);
    bool menuBegin(const char* label);
    bool menuItem(const char* label);
    void menuLabel(const char* label);
    void menuSeparator();
    void menuEnd();
    void menuBarEnd();
    bool barButton(const char* label, Vec3 bg, Vec3 fg);
    bool barCheckbox(const char* label, bool* value);
    void barLabel(const std::string& text, Vec3 color);
    void barSpace(float w) { menuX_ += w; }

    // ── panels ──
    void panelBegin(const char* id, float x, float y, float w, float h, const char* title);
    void panelEnd();
    // re-apply the current panel's scissor after a temporary setUIScissor
    void reclipPanel() { if (inPanel_) r->setUIScissor(p_.sx, p_.sy, p_.sw, p_.sh, true); }

    // ── widgets ──
    void label(const std::string& text, Vec3 color = { 0.85f, 0.88f, 0.93f });
    void labelWrapped(const std::string& text, Vec3 color);
    void header(const char* text);
    bool button(const char* label);
    bool buttonColored(const char* label, Vec3 bg, Vec3 fg);
    // Compact centered icon toolbar. Active toggles stay blue until clicked
    // again; icon: 0 save, 1 save-as, 2 assign, 3 panels, 4 settings.
    void beginCenteredToolRow(int count, float squareSize = 28.0f, float gap = 8.0f);
    void endCenteredToolRow();
    // `dirty` renders the button green (unsaved changes) — used by every Save control
    bool toolIconButton(const char* id, int icon, bool active = false, const char* tooltip = nullptr,
                        bool dirty = false);
    // same control at an explicit rect, for bespoke tool bars (the widget editor
    // puts its graph toggles next to the Save button rather than on a row of its own)
    bool toolIconButtonRect(const char* id, const UIRect& rc, int icon, bool active = false,
                            const char* tooltip = nullptr, bool dirty = false);
    bool selectable(const char* id, const std::string& text, bool selected);
    bool checkbox(const char* label, bool* v);
    void disabledField(const char* label, const std::string& value = "Collegato");
    bool dragFloat(const char* label, float* v, float speed, float mn, float mx);
    bool combo(const char* label, int* idx, const char* const* items, int count);
    bool textInput(const char* id, char* buf, int cap);
    bool textInputRect(const char* id, char* buf, int cap, const UIRect& rc, bool autoFocus = false);
    // Numeric field at an explicit rect (for bespoke panels such as the widget
    // designer). Wheel nudges by `wheelStep`; clicking switches to full keyboard
    // editing with selection, Ctrl+A/C/V — the same behaviour as blueprint values.
    bool numberFieldRect(const char* id, const UIRect& rc, float* v, float wheelStep,
                         const char* label = nullptr, bool isInt = false,
                         float mn = -1e9f, float mx = 1e9f);
    bool colorEdit(const char* label, Vec3* c);
    bool colorEditRGBA(const char* label, Vec3* rgb, float* alpha);
    void openColorPicker(const char* id, Vec3* rgb, float* alpha = nullptr, float x = -1, float y = -1);
    // Drop-down list anchored at (x, y), for panels that lay themselves out at
    // explicit rects (the widget designer). Same deferred pattern as the colour
    // picker: it stays open across frames and is drawn on top in end(). The
    // choice is collected by id rather than through a pointer, so nothing has to
    // outlive the frame that opened it.
    void openEnumPicker(const char* id, int current, const char* const* items, int count,
                        float x, float y, float w);
    // the picker opened with `id` produced a choice: write it out (once)
    bool takeEnumPick(const char* id, int* out);
    // An open drop-down sits under the cursor. The picker is drawn in end(), so
    // the panel beneath it has already hit-tested by then — it must consult this
    // and block itself, or the click that picks an item also hits the row below.
    bool popupCoversPointer() const;
    void separator();
    void spacing(float h = 5);
    // truncate `s` to fit `maxW` px, appending "..."; empty maxW returns s
    std::string ellipsize(const std::string& s, float maxW) const;
    // if the mouse is over `rc` and `full` doesn't fit in `rc.w`, queue a tooltip
    void hoverTip(const std::string& full, const UIRect& rc, float shownW);
    // queue a tooltip at the cursor unconditionally (widget Tool Tip Text)
    void showTip(const std::string& text) { if (!text.empty()) { tip_ = text; tipX_ = in_.mouseX; tipY_ = in_.mouseY; } }
    bool tabBar(const char* const* tabs, int count, int* active);
    void row(int cols);           // the next `cols` widgets share one line
    // reserve a layout row of custom height (scroll-aware, screen coords) so callers
    // can draw bespoke widgets (big asset fields, previews) inside the panel flow
    UIRect allocRow(float h) { return alloc(h); }
    void scrollToBottom(const char* panelId) { storage_[hash(panelId, 0)] = 1e9f; }

    // ── scrolling sub-region ──
    // Clips the widgets between begin/end to `rc`, scrolls them with the wheel and
    // draws a scrollbar whenever they overflow. Panels only scroll as a whole, so
    // any editor with side columns needs this to scroll them independently.
    void beginScrollRegion(const char* id, const UIRect& rc);
    void endScrollRegion();
    // Scrollbar for a region that lays itself out (explicit rects rather than the
    // panel flow). Draws nothing when the content fits.
    void drawScrollbar(const UIRect& rc, float scroll, float contentHeight);

    // vertical column layout inside the current panel: left fixed, middle flexible,
    // optional right fixed column (rightWidthPx > 0)
    void beginColumns(float leftWidthPx, float rightWidthPx = 0);
    void nextColumn();
    void endColumns();
    bool dragInt(const char* label, int* v, float speed, int mn, int mx);

    // tree row with indent, expand triangle and drag support (bitmask result)
    enum { TREE_CLICKED = 1, TREE_TOGGLED = 2, TREE_HOVERED = 4, TREE_PRESSED = 8, TREE_RCLICKED = 16 };
    int treeItem(const char* id, const std::string& text, int depth, bool hasChildren,
                 bool expanded, bool selected, bool dropHighlight, bool prefabTint = false, int folderIcon = -1,
                 const Vec3* folderColor = nullptr, const char* iconImage = nullptr);

    // header with a close/remove "x"; returns true when the x is clicked
    bool headerClosable(const char* text);
    // Inspector component card. The arrow toggles its body, the title is a
    // drag handle, and the optional x removes it.
    enum { COMP_TOGGLED = 1, COMP_REMOVE = 2, COMP_PRESSED = 4, COMP_HELD = 8, COMP_HOVERED = 16, COMP_RESET = 32 };
    int componentBegin(const char* id, const char* title, bool collapsed, bool removable,
                       bool dragging = false, bool dropHighlight = false, bool draggable = true);
    void componentEnd();
    UIRect lastComponentHeader() const { return lastComponentHeader_; }
    // grid tile with icon (0 folder, 1 file, 2 prefab, 3 scene, 4 blueprint)
    // bitmask: 1 click, 2 double click, 4 pressed, 8 hovered, 16 right-clicked
    enum { TILE_CLICK = 1, TILE_DBLCLICK = 2, TILE_PRESSED = 4, TILE_HOVERED = 8, TILE_RCLICKED = 16 };
    int iconTile(const char* id, const std::string& label, int icon, bool selected, float tileHeight = 84.0f,
                 bool drawLabel = true, const Vec3* folderColor = nullptr, const char* imageName = nullptr);
    void setAssetIcon(const std::string& name, GLuint texture) { assetIcons_[name] = texture; }
    UIRect lastItemRect() const { return lastItemRect_; }
    // true when the given textInput currently has keyboard focus
    bool inputFocused(const char* id) const { return focusId_ == hash(id, p_.id); }
    uint32_t pushId(const char* id) { uint32_t previous = p_.id; p_.id = hash(id, p_.id); return previous; }
    void popId(uint32_t previous) { p_.id = previous; }

    // ── integration hooks for the dock system / custom canvases ──
    // Custom canvases read through this accessor. While another panel is above
    // them they receive a neutral input frame, so an already-started drag/pan
    // cannot continue through the overlay.
    const UIInput& input() const { return blocked_ ? blockedInput_ : in_; }
    // real pointer position, ignoring the interaction block (for popups that must
    // anchor to the cursor even while they block the panel behind them)
    float rawMouseX() const { return in_.mouseX; }
    float rawMouseY() const { return in_.mouseY; }
    void consumeWheel() { in_.wheel = 0; }
    void setInteractionBlocked(bool b, bool cancelCapture = true);
    bool interactionBlocked() const { return blocked_; }
    // Allow componentBegin to still report COMP_RESET (right-click) even while the
    // panel is interaction-blocked by its own reset popup, so right-clicking another
    // component moves/reopens the menu. Auto-clears every frame.
    void setComponentResetProbe(bool b) { componentResetProbe_ = b; }
    void registerBlockingRect(const UIRect& rc) { panelRects_.push_back(rc); }
    void setExternalCapture(bool b) { externalCapture_ = b; }
    uint32_t makeId(const char* s, uint32_t seed) const { return hash(s, seed); }
    UIRect panelInner() const { return { p_.x, p_.y, p_.w, p_.h }; }
    UIRect panelClip() const { return { p_.sx, p_.sy, p_.sw, p_.sh }; }
    float panelCursorY() const { return p_.cy; }
    void extendContent(float bottomY) { if (bottomY > p_.contentBottom) p_.contentBottom = bottomY; }

    // text clipboard bridge — the app layer moves these to/from the OS clipboard.
    // Fields read pasteText() on Ctrl+V and call requestCopyText() on Ctrl+C.
    void setPasteText(const std::string& s) { pasteText_ = s; }
    const std::string& pasteText() const { return pasteText_; }
    void requestCopyText(const std::string& s) { copyText_ = s; wantCopy_ = true; }
    bool takeCopyText(std::string& out) { if (!wantCopy_) return false; out = copyText_; wantCopy_ = false; return true; }

    Renderer* r = nullptr;

private:
    std::string pasteText_, copyText_;
    bool wantCopy_ = false;
    UIInput in_;
    UIInput blockedInput_;
    uint32_t hotId_ = 0, activeId_ = 0, focusId_ = 0, openMenuId_ = 0;
    std::unordered_map<uint32_t, float> storage_;
    float dragAccum_ = 0;
    int frame_ = 0;
    bool menuClickedThisFrame_ = false;
    bool blocked_ = false;
    bool componentResetProbe_ = false;
    bool externalCapture_ = false;

    struct Panel {
        float x, y, w, h, cx, cy, contentBottom;
        uint32_t id;
        float scroll;
        float sx, sy, sw, sh;      // scissor applied at panelBegin
    } p_ = {};
    bool inPanel_ = false;
    uint32_t numEditId_ = 0;       // dragFloat/dragInt being edited via keyboard
    char numEditBuf_[32] = {};
    uint32_t textEditId_ = 0, lastTextClickId_ = 0;
    int textCursor_ = 0, textAnchor_ = 0, lastTextClickFrame_ = -1000;
    bool textSelecting_ = false;
    std::string tip_;              // tooltip queued this frame (drawn in end())
    float tipX_ = 0, tipY_ = 0;

    int rowCols_ = 0, rowIdx_ = 0;
    float rowY_ = 0;

    // scroll-region stack (regions nest inside a panel, never inside each other
    // in practice, but the saved state makes that safe anyway)
    struct ScrollRegion {
        uint32_t id;
        UIRect rc;
        float savedX, savedW, savedCy, savedContentBottom;
        float savedSx, savedSy, savedSw, savedSh;
        float top;
    };
    std::vector<ScrollRegion> scrollRegions_;

    bool colActive_ = false;
    int colIndex_ = 0, colCount_ = 2;
    float colSavedX_ = 0, colSavedW_ = 0, colStartY_ = 0;
    float colX_[3] = {}, colW_[3] = {}, colEndY_[3] = {};

    float menuBarH_ = 30, menuX_ = 0;
    bool menuScope_ = false;
    float popX_ = 0, popY_ = 0, popW_ = 0, itemY_ = 0;
    UIRect popupRect_;
    UIRect barRect_;
    std::vector<UIRect> panelRects_;      // this frame
    std::vector<UIRect> prevPanelRects_;  // last frame (for wantMouse)

    bool componentCardActive_ = false;
    float componentCardStartY_ = 0;
    uint32_t componentCardId_ = 0;
    float componentCardSavedX_ = 0, componentCardSavedW_ = 0;
    float componentCardPad_ = 10;
    UIRect lastComponentHeader_;
    UIRect lastItemRect_;

    bool centeredToolRow_ = false;
    int centeredToolCount_ = 0, centeredToolIndex_ = 0;
    float centeredToolX_ = 0, centeredToolY_ = 0, centeredToolSize_ = 28, centeredToolGap_ = 8;
    uint32_t colorPickerId_ = 0;
    Vec3* colorPickerRgb_ = nullptr;
    float* colorPickerAlpha_ = nullptr;
    float colorPickerOwnedAlpha_ = 1.0f, colorPickerX_ = 0, colorPickerY_ = 0;
    int colorPickerDrag_ = 0;
    int colorPickerOpenFrame_ = -1;   // frame the picker was opened on
    char colorPickerHex_[10] = "#FFFFFFFF";
    bool colorPickerHexFocus_ = false, colorPickerChanged_ = false;
    uint32_t enumPickerId_ = 0, enumPickerResultId_ = 0;
    int enumPickerCurrent_ = 0, enumPickerResult_ = 0;
    std::vector<std::string> enumPickerItems_;
    float enumPickerX_ = 0, enumPickerY_ = 0, enumPickerW_ = 160;
    int enumPickerOpenFrame_ = -1;
    UIRect enumPickerRect_{};      // resolved (clamped) rect of the open list
    std::unordered_map<std::string, GLuint> assetIcons_;

    uint32_t hash(const char* s, uint32_t seed) const;
    bool mouseIn(float x, float y, float w, float h) const;
    bool numericEdit(uint32_t id, const UIRect& rc, bool isInt, double* out);
    int textIndexAt(const char* text, float localX) const;
    void beginTextSelection(uint32_t id, const char* text, int cursor);
    bool eraseTextSelection(char* text, int& len);
    bool mouseOk() const { return menuScope_ || openMenuId_ == 0; }
    UIRect alloc(float h);
    bool behave(uint32_t id, const UIRect& rc, bool* hovered = nullptr);
    void swatchBorder(const UIRect& rc);
    void drawColorPicker();
    void drawEnumPicker();
};
