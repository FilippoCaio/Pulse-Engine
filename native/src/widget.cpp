#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "widget.h"
#include "material.h"   // matLoadTexture
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

const char* widgetTypeName(int type) {
    switch (type) {
    case WT_CANVAS: return "Canvas";
    case WT_PANEL: return "Panel";
    case WT_VBOX: return "Vertical Box";
    case WT_HBOX: return "Horizontal Box";
    case WT_TEXT: return "Text";
    case WT_BUTTON: return "Button";
    case WT_IMAGE: return "Image";
    case WT_BORDER: return "Border";
    case WT_PROGRESSBAR: return "Progress Bar";
    case WT_CHECKBOX: return "Check Box";
    case WT_SLIDER: return "Slider";
    case WT_OVERLAY: return "Overlay";
    case WT_SIZEBOX: return "Size Box";
    case WT_SCALEBOX: return "Scale Box";
    case WT_GRIDPANEL: return "Grid Panel";
    case WT_UNIFORMGRID: return "Uniform Grid Panel";
    case WT_SCROLLBOX: return "Scroll Box";
    case WT_STACKBOX: return "Stack Box";
    case WT_WRAPBOX: return "Wrap Box";
    case WT_SAFEZONE: return "Safe Zone";
    case WT_WIDGETSWITCHER: return "Widget Switcher";
    case WT_RICHTEXT: return "Rich Text Block";
    case WT_EDITABLETEXT: return "Editable Text";
    case WT_MULTILINETEXT: return "Editable Text (Multi-Line)";
    case WT_SPINBOX: return "Spin Box";
    case WT_COMBOBOX: return "Combo Box";
    case WT_THROBBER: return "Throbber";
    case WT_SPACER: return "Spacer";
    }
    return "?";
}

const char* widgetCategoryName(int category) {
    switch (category) {
    case WC_PANEL: return "PANEL";
    case WC_COMMON: return "COMMON";
    case WC_INPUT: return "INPUT";
    case WC_PRIMITIVE: return "PRIMITIVE";
    case WC_SPECIAL: return "SPECIAL EFFECTS";
    }
    return "MISC";
}

int widgetTypeCategory(int type) {
    switch (type) {
    case WT_CANVAS: case WT_PANEL: case WT_VBOX: case WT_HBOX: case WT_OVERLAY:
    case WT_SIZEBOX: case WT_SCALEBOX: case WT_GRIDPANEL: case WT_UNIFORMGRID:
    case WT_SCROLLBOX: case WT_STACKBOX: case WT_WRAPBOX: case WT_SAFEZONE:
    case WT_WIDGETSWITCHER:
        return WC_PANEL;
    case WT_TEXT: case WT_BUTTON: case WT_IMAGE: case WT_CHECKBOX:
    case WT_PROGRESSBAR: case WT_SLIDER:
        return WC_COMMON;
    case WT_EDITABLETEXT: case WT_MULTILINETEXT: case WT_SPINBOX: case WT_COMBOBOX:
        return WC_INPUT;
    case WT_BORDER: case WT_RICHTEXT: case WT_SPACER:
        return WC_PRIMITIVE;
    case WT_THROBBER:
        return WC_SPECIAL;
    }
    return WC_PRIMITIVE;
}

// containers can hold children
bool widgetIsContainer(int type) {
    return type == WT_CANVAS || type == WT_PANEL || type == WT_VBOX || type == WT_HBOX ||
           type == WT_BUTTON ||   // a Button hosts its own content (label, icon, …)
           type == WT_BORDER || type == WT_OVERLAY || type == WT_SIZEBOX || type == WT_SCALEBOX ||
           type == WT_GRIDPANEL || type == WT_UNIFORMGRID || type == WT_SCROLLBOX ||
           type == WT_STACKBOX || type == WT_WRAPBOX || type == WT_SAFEZONE ||
           type == WT_WIDGETSWITCHER;
}
// a parent that positions its children by alignment (not free x/y)
bool widgetIsAligned(int parentType) { return widgetLaysOutChildren(parentType); }

const char* widgetHAlignName(int align) {
    static const char* names[4] = { "Left", "Center", "Right", "Fill" };
    return names[align & 3];
}
const char* widgetVAlignName(int align) {
    static const char* names[4] = { "Top", "Center", "Bottom", "Fill" };
    return names[align & 3];
}

// Which slot a child sits in is decided entirely by its parent's type.
int widgetSlotKind(int parentType) {
    if (parentType < 0 || parentType == WT_CANVAS) return WSLOT_CANVAS;   // < 0 = the screen
    if (widgetLaysOutChildren(parentType)) return WSLOT_ALIGNED;
    return WSLOT_FREE;
}
const char* widgetSlotName(int parentType) {
    if (parentType < 0) return "Screen Slot";
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s Slot", widgetTypeName(parentType));
    return buf;
}

// Every container except the Canvas arranges its own children, so those children
// expose H/V alignment. Canvas children keep free x/y plus an anchor instead.
bool widgetLaysOutChildren(int parentType) {
    switch (parentType) {
    case WT_VBOX: case WT_HBOX: case WT_OVERLAY: case WT_SIZEBOX: case WT_SCALEBOX:
    case WT_GRIDPANEL: case WT_UNIFORMGRID: case WT_SCROLLBOX: case WT_STACKBOX:
    case WT_WRAPBOX: case WT_SAFEZONE: case WT_WIDGETSWITCHER: case WT_BUTTON:
        return true;
    }
    return false;   // Canvas, Border and the screen leave children free
}

// Only these paint a fillable background; layout panels are invisible chrome.
bool widgetHasBackground(int type) {
    switch (type) {
    case WT_BORDER: case WT_BUTTON: case WT_PANEL:
    case WT_PROGRESSBAR: case WT_SLIDER: case WT_CHECKBOX:
    case WT_EDITABLETEXT: case WT_MULTILINETEXT: case WT_SPINBOX: case WT_COMBOBOX:
    case WT_THROBBER:
        return true;
    }
    return false;
}

void widgetAnchorRange(int anchor, float& minX, float& minY, float& maxX, float& maxY) {
    if (anchor < 0 || anchor >= WANCH_COUNT) anchor = WANCH_TOP_LEFT;
    const int col = anchor % 4, row = anchor / 4;
    // columns/rows 0..2 pin, 3 stretches across the whole axis
    const float lo[4] = { 0.0f, 0.5f, 1.0f, 0.0f };
    const float hi[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    minX = lo[col]; maxX = hi[col];
    minY = lo[row]; maxY = hi[row];
}

// Slot offsets → the box the widget actually occupies in its parent.
// Pinned axis: the offset is a position. Stretched axis: the two offsets are
// insets from the anchor edges (Unreal's Offset Left/Right).
void widgetSlotToBox(int anchor, float parentW, float parentH,
                     float x, float y, float w, float h,
                     float& bx, float& by, float& bw, float& bh) {
    float aMinX, aMinY, aMaxX, aMaxY;
    widgetAnchorRange(anchor, aMinX, aMinY, aMaxX, aMaxY);
    float x0 = parentW * aMinX, x1 = parentW * aMaxX;
    float y0 = parentH * aMinY, y1 = parentH * aMaxY;
    if (aMinX == aMaxX) { bx = x0 + x; bw = w; }
    else { bx = x0 + x; bw = (x1 - x0) - (x + w); }
    if (aMinY == aMaxY) { by = y0 + y; bh = h; }
    else { by = y0 + y; bh = (y1 - y0) - (y + h); }
}

void widgetBoxToSlot(int anchor, float parentW, float parentH,
                     float bx, float by, float bw, float bh,
                     float& x, float& y, float& w, float& h) {
    float aMinX, aMinY, aMaxX, aMaxY;
    widgetAnchorRange(anchor, aMinX, aMinY, aMaxX, aMaxY);
    float x0 = parentW * aMinX, x1 = parentW * aMaxX;
    float y0 = parentH * aMinY, y1 = parentH * aMaxY;
    x = bx - x0;
    y = by - y0;
    w = (aMinX == aMaxX) ? bw : (x1 - x0) - (bx - x0) - bw;
    h = (aMinY == aMaxY) ? bh : (y1 - y0) - (by - y0) - bh;
}

// ─── component properties reachable from a graph ───
// One table, because every entry maps to a real WidgetNode field: a Percent on
// a Text is harmless, it just writes a value nothing reads. The per-type list
// below is purely what the editor advertises.
static const WidgetProperty WIDGET_PROPS[] = {
    { "Percent",    WPK_NUM,   "bar value, inside its Min..Max range (0..1 by default)" },
    { "Min",        WPK_NUM,   "bottom of a Progress Bar / Slider range (empty bar)" },
    { "Max",        WPK_NUM,   "top of a Progress Bar / Slider range (full bar)" },
    { "Value",      WPK_NUM,   "raw value: spin box, switcher index, check state" },
    { "Opacity",    WPK_NUM,   "0..1 alpha of the background" },
    { "Font Size",  WPK_NUM,   "text scale" },
    { "X",          WPK_NUM,   "slot offset X, in reference px" },
    { "Y",          WPK_NUM,   "slot offset Y, in reference px" },
    { "Width",      WPK_NUM,   "width, in reference px" },
    { "Height",     WPK_NUM,   "height, in reference px" },
    // ── slot properties: what they do depends on the parent (see widgetSlotKind) ──
    { "H Align",    WPK_NUM,   "0 Left, 1 Center, 2 Right, 3 Fill (aligned slots)" },
    { "V Align",    WPK_NUM,   "0 Top, 1 Center, 2 Bottom, 3 Fill (aligned slots)" },
    { "Size Rule",  WPK_NUM,   "box slot: 0 Auto (own size), 1 Fill (share the room)" },
    { "Fill Weight",WPK_NUM,   "Fill slots split the free room by this weight" },
    { "Anchor",     WPK_NUM,   "anchor preset 0..15 (Canvas slots only)" },
    // Unreal calls the canvas pivot "Alignment"; the old names still resolve
    { "Alignment X", WPK_NUM,  "Canvas slot: 0..1 origin the X offset measures from" },
    { "Alignment Y", WPK_NUM,  "Canvas slot: 0..1 origin the Y offset measures from" },
    // ── Render Transform (visual only: layout does not move) ──
    { "Translation X", WPK_NUM, "render offset X, in reference px" },
    { "Translation Y", WPK_NUM, "render offset Y, in reference px" },
    { "Scale X",    WPK_NUM,   "render scale on X (1 = unscaled)" },
    { "Scale Y",    WPK_NUM,   "render scale on Y (1 = unscaled)" },
    { "Angle",      WPK_NUM,   "render rotation, in degrees" },
    { "Shear X",    WPK_NUM,   "slant on X, -1..1 == -60..60 degrees" },
    { "Shear Y",    WPK_NUM,   "slant on Y, -1..1 == -60..60 degrees" },
    // ── Rendering / Behaviour ──
    { "Render Opacity", WPK_NUM, "0..1 applied to this component and its children" },
    { "Justification", WPK_NUM, "text alignment: 0 Left, 1 Center, 2 Right" },
    { "Is Enabled", WPK_BOOL,  "a disabled component is greyed out and inert" },
    { "Auto Wrap",  WPK_BOOL,  "break long text at the component's width" },
    { "Tool Tip",   WPK_STR,   "text shown while the pointer rests on it" },
    { "Text",       WPK_STR,   "displayed text" },
    { "Image",      WPK_STR,   ".png asset, relative to the project" },
    { "Color",      WPK_COLOR, "background / fill colour (carries opacity)" },
    { "Text Color", WPK_COLOR, "colour of the text" },
    { "Visible",    WPK_BOOL,  "hides the component and everything under it" },
    { "Checked",    WPK_BOOL,  "check-box state (Value > 0.5)" },
};
static const int WIDGET_PROP_COUNT = (int)(sizeof(WIDGET_PROPS) / sizeof(WIDGET_PROPS[0]));

int widgetPropertyCount() { return WIDGET_PROP_COUNT; }
const WidgetProperty& widgetPropertyAt(int index) {
    if (index < 0 || index >= WIDGET_PROP_COUNT) index = 0;
    return WIDGET_PROPS[index];
}
const WidgetProperty* widgetFindProperty(const char* name) {
    if (!name || !name[0]) return nullptr;
    // the canvas pivot was called "Pivot X/Y" before it was renamed to Unreal's
    // "Alignment X/Y"; graphs saved with the old name must keep working
    if (_stricmp(name, "Pivot X") == 0) name = "Alignment X";
    else if (_stricmp(name, "Pivot Y") == 0) name = "Alignment Y";
    for (const WidgetProperty& p : WIDGET_PROPS) if (_stricmp(p.name, name) == 0) return &p;
    return nullptr;
}
const char* widgetPropKindName(int kind) {
    switch (kind) {
    case WPK_STR: return "String";
    case WPK_COLOR: return "Color";
    case WPK_BOOL: return "Bool";
    default: return "Number";
    }
}

// does this component show text the user can drive?
static bool widgetHasText(int type) {
    switch (type) {
    case WT_TEXT: case WT_RICHTEXT: case WT_BUTTON: case WT_EDITABLETEXT:
    case WT_MULTILINETEXT: case WT_COMBOBOX:
        return true;
    }
    return false;
}

std::vector<const WidgetProperty*> widgetPropertiesFor(int type) {
    auto add = [](std::vector<const WidgetProperty*>& out, const char* name) {
        if (const WidgetProperty* p = widgetFindProperty(name)) out.push_back(p);
    };
    std::vector<const WidgetProperty*> out;
    add(out, "Visible"); add(out, "Render Opacity"); add(out, "Is Enabled"); add(out, "Tool Tip");
    if (widgetHasText(type)) {
        add(out, "Text"); add(out, "Text Color"); add(out, "Font Size");
        add(out, "Justification"); add(out, "Auto Wrap");
    }
    if (type == WT_PROGRESSBAR || type == WT_SLIDER) { add(out, "Percent"); add(out, "Min"); add(out, "Max"); }
    if (type == WT_CHECKBOX) add(out, "Checked");
    if (type == WT_SPINBOX || type == WT_WIDGETSWITCHER) add(out, "Value");
    if (type == WT_IMAGE) add(out, "Image");
    if (widgetHasBackground(type) || type == WT_IMAGE) { add(out, "Color"); add(out, "Opacity"); }
    add(out, "X"); add(out, "Y"); add(out, "Width"); add(out, "Height");
    // slot properties: every component has them, the parent decides which bite
    add(out, "H Align"); add(out, "V Align"); add(out, "Size Rule"); add(out, "Fill Weight");
    add(out, "Anchor"); add(out, "Alignment X"); add(out, "Alignment Y");
    // render transform: visual only, so every component carries the whole set
    add(out, "Translation X"); add(out, "Translation Y");
    add(out, "Scale X"); add(out, "Scale Y");
    add(out, "Angle"); add(out, "Shear X"); add(out, "Shear Y");
    return out;
}

// `prop` is matched case-insensitively; an unknown or wrongly-typed name fails
// without touching the node.
static bool propIs(const char* prop, const char* name, int kind) {
    const WidgetProperty* p = widgetFindProperty(prop);
    return p && p->kind == kind && _stricmp(p->name, name) == 0;
}
static bool propKind(const char* prop, int kind) {
    const WidgetProperty* p = widgetFindProperty(prop);
    return p && p->kind == kind;
}

bool widgetGetNumber(const WidgetNode& n, const char* prop, float& out) {
    if (!propKind(prop, WPK_NUM)) return false;
    if (propIs(prop, "Percent", WPK_NUM) || propIs(prop, "Value", WPK_NUM)) out = n.value;
    else if (propIs(prop, "Min", WPK_NUM)) out = n.minValue;
    else if (propIs(prop, "Max", WPK_NUM)) out = n.maxValue;
    else if (propIs(prop, "Opacity", WPK_NUM)) out = n.alpha;
    else if (propIs(prop, "Font Size", WPK_NUM)) out = n.fontScale;
    else if (propIs(prop, "X", WPK_NUM)) out = n.x;
    else if (propIs(prop, "Y", WPK_NUM)) out = n.y;
    else if (propIs(prop, "Width", WPK_NUM)) out = n.w;
    else if (propIs(prop, "Height", WPK_NUM)) out = n.h;
    else if (propIs(prop, "H Align", WPK_NUM)) out = (float)n.hAlign;
    else if (propIs(prop, "V Align", WPK_NUM)) out = (float)n.vAlign;
    else if (propIs(prop, "Anchor", WPK_NUM)) out = (float)n.anchor;
    else if (propIs(prop, "Alignment X", WPK_NUM)) out = n.pivotX;
    else if (propIs(prop, "Alignment Y", WPK_NUM)) out = n.pivotY;
    else if (propIs(prop, "Size Rule", WPK_NUM)) out = (float)n.sizeRule;
    else if (propIs(prop, "Fill Weight", WPK_NUM)) out = n.fillWeight;
    else if (propIs(prop, "Translation X", WPK_NUM)) out = n.transX;
    else if (propIs(prop, "Translation Y", WPK_NUM)) out = n.transY;
    else if (propIs(prop, "Scale X", WPK_NUM)) out = n.scaleX;
    else if (propIs(prop, "Scale Y", WPK_NUM)) out = n.scaleY;
    else if (propIs(prop, "Angle", WPK_NUM)) out = n.angle;
    else if (propIs(prop, "Shear X", WPK_NUM)) out = n.shearX;
    else if (propIs(prop, "Shear Y", WPK_NUM)) out = n.shearY;
    else if (propIs(prop, "Render Opacity", WPK_NUM)) out = n.renderOpacity;
    else if (propIs(prop, "Justification", WPK_NUM)) out = (float)n.justify;
    else return false;
    return true;
}
bool widgetSetNumber(WidgetNode& n, const char* prop, float value) {
    if (!propKind(prop, WPK_NUM)) return false;
    // a bar value is held inside its own range, not hard-clamped to 0..1
    if (propIs(prop, "Percent", WPK_NUM)) n.value = clampf(value, std::min(n.minValue, n.maxValue),
                                                                  std::max(n.minValue, n.maxValue));
    else if (propIs(prop, "Min", WPK_NUM)) n.minValue = value;
    else if (propIs(prop, "Max", WPK_NUM)) n.maxValue = value;
    else if (propIs(prop, "Value", WPK_NUM)) n.value = value;
    else if (propIs(prop, "Opacity", WPK_NUM)) n.alpha = clampf(value, 0, 1);
    else if (propIs(prop, "Font Size", WPK_NUM)) n.fontScale = std::max(0.01f, value);
    else if (propIs(prop, "X", WPK_NUM)) n.x = value;
    else if (propIs(prop, "Y", WPK_NUM)) n.y = value;
    else if (propIs(prop, "Width", WPK_NUM)) n.w = value;
    else if (propIs(prop, "Height", WPK_NUM)) n.h = value;
    else if (propIs(prop, "H Align", WPK_NUM)) n.hAlign = (int)clampf(value, 0, 3);
    else if (propIs(prop, "V Align", WPK_NUM)) n.vAlign = (int)clampf(value, 0, 3);
    else if (propIs(prop, "Anchor", WPK_NUM)) n.anchor = (int)clampf(value, 0, WANCH_COUNT - 1);
    else if (propIs(prop, "Alignment X", WPK_NUM)) n.pivotX = clampf(value, 0, 1);
    else if (propIs(prop, "Alignment Y", WPK_NUM)) n.pivotY = clampf(value, 0, 1);
    else if (propIs(prop, "Size Rule", WPK_NUM)) n.sizeRule = (int)clampf(value, 0, 1);
    else if (propIs(prop, "Fill Weight", WPK_NUM)) n.fillWeight = std::max(0.0f, value);
    else if (propIs(prop, "Translation X", WPK_NUM)) n.transX = value;
    else if (propIs(prop, "Translation Y", WPK_NUM)) n.transY = value;
    else if (propIs(prop, "Scale X", WPK_NUM)) n.scaleX = value;
    else if (propIs(prop, "Scale Y", WPK_NUM)) n.scaleY = value;
    else if (propIs(prop, "Angle", WPK_NUM)) n.angle = value;
    else if (propIs(prop, "Shear X", WPK_NUM)) n.shearX = clampf(value, -1, 1);
    else if (propIs(prop, "Shear Y", WPK_NUM)) n.shearY = clampf(value, -1, 1);
    else if (propIs(prop, "Render Opacity", WPK_NUM)) n.renderOpacity = clampf(value, 0, 1);
    else if (propIs(prop, "Justification", WPK_NUM)) n.justify = (int)clampf(value, 0, 2);
    else return false;
    return true;
}
bool widgetGetString(const WidgetNode& n, const char* prop, std::string& out) {
    if (propIs(prop, "Text", WPK_STR)) { out = n.text; return true; }
    if (propIs(prop, "Image", WPK_STR)) { out = n.image; return true; }
    if (propIs(prop, "Tool Tip", WPK_STR)) { out = n.tooltip; return true; }
    return false;
}
bool widgetSetString(WidgetNode& n, const char* prop, const std::string& value) {
    if (propIs(prop, "Text", WPK_STR)) { snprintf(n.text, sizeof(n.text), "%s", value.c_str()); return true; }
    if (propIs(prop, "Image", WPK_STR)) { snprintf(n.image, sizeof(n.image), "%s", value.c_str()); return true; }
    if (propIs(prop, "Tool Tip", WPK_STR)) { snprintf(n.tooltip, sizeof(n.tooltip), "%s", value.c_str()); return true; }
    return false;
}
bool widgetGetColor(const WidgetNode& n, const char* prop, Vec3& rgb, float& alpha) {
    if (propIs(prop, "Color", WPK_COLOR)) { rgb = n.color; alpha = n.alpha; return true; }
    if (propIs(prop, "Text Color", WPK_COLOR)) { rgb = n.textColor; alpha = 1.0f; return true; }
    return false;
}
bool widgetSetColor(WidgetNode& n, const char* prop, const Vec3& rgb, float alpha) {
    if (propIs(prop, "Color", WPK_COLOR)) { n.color = rgb; n.alpha = clampf(alpha, 0, 1); return true; }
    if (propIs(prop, "Text Color", WPK_COLOR)) { n.textColor = rgb; return true; }
    return false;
}
bool widgetGetBool(const WidgetNode& n, const char* prop, bool& out) {
    if (propIs(prop, "Visible", WPK_BOOL)) { out = n.visible; return true; }
    if (propIs(prop, "Checked", WPK_BOOL)) { out = n.value > 0.5f; return true; }
    if (propIs(prop, "Is Enabled", WPK_BOOL)) { out = n.enabled; return true; }
    if (propIs(prop, "Auto Wrap", WPK_BOOL)) { out = n.wrap; return true; }
    return false;
}
bool widgetSetBool(WidgetNode& n, const char* prop, bool value) {
    if (propIs(prop, "Visible", WPK_BOOL)) { n.visible = value; return true; }
    if (propIs(prop, "Checked", WPK_BOOL)) { n.value = value ? 1.0f : 0.0f; return true; }
    if (propIs(prop, "Is Enabled", WPK_BOOL)) { n.enabled = value; return true; }
    if (propIs(prop, "Auto Wrap", WPK_BOOL)) { n.wrap = value; return true; }
    return false;
}

const char* widgetAnchorName(int anchor) {
    static const char* names[WANCH_COUNT] = {
        "Top Left", "Top Center", "Top Right", "Top Stretch",
        "Center Left", "Center", "Center Right", "Center Stretch",
        "Bottom Left", "Bottom Center", "Bottom Right", "Bottom Stretch",
        "Left Stretch", "Center Stretch V", "Right Stretch", "Fill",
    };
    return (anchor >= 0 && anchor < WANCH_COUNT) ? names[anchor] : names[0];
}

// A fresh widget starts empty: the Canvas is now an optional component the user
// adds explicitly. Top-level nodes carry parent 0 (attached to the screen).
void WidgetAsset::makeDefault() {
    nodes.clear();
    nextId = 1;
}

WidgetNode* WidgetAsset::find(int id) { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
const WidgetNode* WidgetAsset::find(int id) const { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
int WidgetAsset::rootId() const { for (const auto& n : nodes) if (n.type == WT_CANVAS) return n.id; return nodes.empty() ? -1 : nodes[0].id; }

int WidgetAsset::addNode(int type, int parent) {
    WidgetNode n;
    n.id = nextId++;
    n.parent = parent;
    n.type = type;
    snprintf(n.name, sizeof(n.name), "%s_%d", widgetTypeName(type), n.id);
    if (type == WT_TEXT) { n.w = 160; n.h = 30; n.color = { 0, 0, 0 }; n.alpha = 0; snprintf(n.text, sizeof(n.text), "New text"); }
    else if (type == WT_BUTTON) { n.w = 150; n.h = 44; n.color = { 0.22f, 0.42f, 0.66f }; n.text[0] = 0; }
    else if (type == WT_IMAGE) { n.w = 120; n.h = 120; n.color = { 1, 1, 1 }; }
    else if (type == WT_VBOX || type == WT_HBOX) { n.w = 240; n.h = 200; n.color = { 0.14f, 0.16f, 0.22f }; n.alpha = 0.6f; }
    else if (type == WT_BORDER) { n.w = 220; n.h = 140; n.color = { 0.14f, 0.16f, 0.22f }; n.alpha = 0.6f; }
    else if (type == WT_PROGRESSBAR) { n.w = 220; n.h = 22; n.color = { 0.30f, 0.62f, 0.99f }; n.value = 0.6f; }
    else if (type == WT_CHECKBOX) { n.w = 26; n.h = 26; n.color = { 0.30f, 0.62f, 0.99f }; n.value = 1.0f; }
    else if (type == WT_SLIDER) { n.w = 220; n.h = 22; n.color = { 0.30f, 0.62f, 0.99f }; n.value = 0.5f; }
    else if (type == WT_CANVAS) { n.w = 400; n.h = 300; n.color = { 0.10f, 0.12f, 0.16f }; n.alpha = 0; }
    else if (type == WT_OVERLAY) { n.w = 300; n.h = 220; n.color = { 0.16f, 0.18f, 0.24f }; n.alpha = 0; }
    else if (type == WT_SIZEBOX) { n.w = 200; n.h = 120; n.color = { 0.16f, 0.18f, 0.24f }; n.alpha = 0; }
    else if (type == WT_SCALEBOX) { n.w = 200; n.h = 120; n.color = { 0.16f, 0.18f, 0.24f }; n.alpha = 0; }
    else if (type == WT_GRIDPANEL || type == WT_UNIFORMGRID) { n.w = 300; n.h = 220; n.color = { 0.16f, 0.18f, 0.24f }; n.alpha = 0; n.value = 3; }
    else if (type == WT_SCROLLBOX) { n.w = 260; n.h = 240; n.color = { 0.13f, 0.15f, 0.20f }; n.alpha = 0.5f; }
    else if (type == WT_STACKBOX || type == WT_WRAPBOX) { n.w = 280; n.h = 200; n.color = { 0.16f, 0.18f, 0.24f }; n.alpha = 0; }
    else if (type == WT_SAFEZONE) { n.w = 360; n.h = 260; n.color = { 0.16f, 0.18f, 0.24f }; n.alpha = 0; n.value = 16; }
    else if (type == WT_WIDGETSWITCHER) { n.w = 280; n.h = 200; n.color = { 0.16f, 0.18f, 0.24f }; n.alpha = 0; n.value = 0; }
    else if (type == WT_RICHTEXT) { n.w = 220; n.h = 32; n.color = { 0, 0, 0 }; n.alpha = 0; snprintf(n.text, sizeof(n.text), "Rich text"); }
    else if (type == WT_EDITABLETEXT) { n.w = 200; n.h = 28; n.color = { 0.10f, 0.11f, 0.14f }; snprintf(n.text, sizeof(n.text), "Text..."); }
    else if (type == WT_MULTILINETEXT) { n.w = 220; n.h = 90; n.color = { 0.10f, 0.11f, 0.14f }; snprintf(n.text, sizeof(n.text), "Text..."); }
    else if (type == WT_SPINBOX) { n.w = 130; n.h = 26; n.color = { 0.10f, 0.11f, 0.14f }; n.value = 0; }
    else if (type == WT_COMBOBOX) { n.w = 170; n.h = 28; n.color = { 0.14f, 0.16f, 0.21f }; snprintf(n.text, sizeof(n.text), "Option"); }
    else if (type == WT_THROBBER) { n.w = 60; n.h = 20; n.color = { 0.70f, 0.76f, 0.86f }; }
    else if (type == WT_SPACER) { n.w = 60; n.h = 40; n.color = { 0.5f, 0.6f, 0.8f }; n.alpha = 0; }
    nodes.push_back(n);
    return n.id;
}

void WidgetAsset::removeNode(int id) {
    const WidgetNode* n = find(id);
    if (!n) return;
    // collect descendants
    std::vector<int> doomed = { id };
    for (size_t i = 0; i < doomed.size(); i++)
        for (const auto& c : nodes) if (c.parent == doomed[i]) doomed.push_back(c.id);
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const WidgetNode& c) { return std::find(doomed.begin(), doomed.end(), c.id) != doomed.end(); }),
                nodes.end());
}

bool WidgetAsset::moveSibling(int id, int delta) {
    const WidgetNode* self = find(id);
    if (!self || delta == 0) return false;
    const int parent = self->parent;
    std::vector<int> sibs;
    for (const WidgetNode& s : nodes) if (s.parent == parent) sibs.push_back(s.id);
    int at = -1;
    for (int i = 0; i < (int)sibs.size(); i++) if (sibs[i] == id) at = i;
    if (at < 0) return false;
    const int to = at + delta;
    if (to < 0 || to >= (int)sibs.size()) return false;

    // a component's children follow it, so whole subtrees are what move
    auto subtree = [&](int root) {
        std::vector<int> out = { root };
        for (size_t i = 0; i < out.size(); i++)
            for (const WidgetNode& c : nodes) if (c.parent == out[i]) out.push_back(c.id);
        return out;
    };
    const std::vector<int> block = subtree(id);
    auto inBlock = [&](int nid) { return std::find(block.begin(), block.end(), nid) != block.end(); };

    std::vector<WidgetNode> lifted;                 // keep their relative order
    for (const WidgetNode& s : nodes) if (inBlock(s.id)) lifted.push_back(s);
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const WidgetNode& s) { return inBlock(s.id); }), nodes.end());

    const int targetId = sibs[to];
    size_t insertAt = nodes.size();
    if (delta < 0) {                                // land in front of the target
        for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].id == targetId) { insertAt = i; break; }
    } else {                                        // land past the target's own subtree
        const std::vector<int> tsub = subtree(targetId);
        for (size_t i = 0; i < nodes.size(); i++)
            if (std::find(tsub.begin(), tsub.end(), nodes[i].id) != tsub.end()) insertAt = i + 1;
    }
    nodes.insert(nodes.begin() + (long)insertAt, lifted.begin(), lifted.end());
    return true;
}

std::string WidgetAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSOWIDGET 1\n";
    o << "ref " << refW << " " << refH << "\n";
    for (const auto& n : nodes) {
        o << "node " << n.id << " " << n.parent << " " << n.type << " "
          << n.x << " " << n.y << " " << n.w << " " << n.h << " "
          << n.color.x << " " << n.color.y << " " << n.color.z << " " << n.alpha << " "
          << n.textColor.x << " " << n.textColor.y << " " << n.textColor.z << " " << n.fontScale << " "
          << n.value << " " << n.hAlign << " " << n.vAlign << " "
          << (n.isVariable ? 1 : 0) << " " << n.padL << " " << n.padT << " " << n.padR << " " << n.padB
          << " " << n.anchor << " " << n.pivotX << " " << n.pivotY
          << " " << (n.visible ? 1 : 0) << "\n";
        o << "wname " << n.id << " " << n.name << "\n";
        if (n.text[0]) o << "wtext " << n.id << " " << n.text << "\n";
        if (n.image[0]) o << "wimg " << n.id << " " << n.image << "\n";
        // Everything added after v1 rides on its own optional line instead of
        // growing the fixed `node` record: an older file simply has none of them
        // and every field keeps its default.
        if (n.slotL || n.slotT || n.slotR || n.slotB)
            o << "wslot " << n.id << " " << n.slotL << " " << n.slotT << " " << n.slotR << " " << n.slotB << "\n";
        if (n.sizeRule != WSR_AUTO || n.fillWeight != 1.0f)
            o << "wfill " << n.id << " " << n.sizeRule << " " << n.fillWeight << "\n";
        if (n.transX || n.transY || n.scaleX != 1 || n.scaleY != 1 || n.angle || n.shearX || n.shearY)
            o << "wxf " << n.id << " " << n.transX << " " << n.transY << " " << n.scaleX << " " << n.scaleY
              << " " << n.angle << " " << n.shearX << " " << n.shearY << "\n";
        if (n.renderOpacity != 1.0f || !n.enabled)
            o << "wrend " << n.id << " " << n.renderOpacity << " " << (n.enabled ? 1 : 0) << "\n";
        if (n.justify || n.wrap)
            o << "wtxt2 " << n.id << " " << n.justify << " " << (n.wrap ? 1 : 0) << "\n";
        if (n.sizeFlags)
            o << "wsize " << n.id << " " << n.sizeFlags << " " << n.minW << " " << n.minH
              << " " << n.maxW << " " << n.maxH << "\n";
        if (n.minValue != 0.0f || n.maxValue != 1.0f)
            o << "wrange " << n.id << " " << n.minValue << " " << n.maxValue << "\n";
        if (n.tooltip[0]) o << "wtip " << n.id << " " << n.tooltip << "\n";
    }
    return o.str();
}

bool WidgetAsset::deserialize(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("IMPULSOWIDGET", 0) != 0) return false;
    nodes.clear();
    nextId = 1;
    while (std::getline(in, line)) {
        if (line.rfind("ref ", 0) == 0) sscanf(line.c_str(), "ref %f %f", &refW, &refH);
        else if (line.rfind("node ", 0) == 0) {
            WidgetNode n;
            int isVar = 0, isVisible = 1;
            int rd = sscanf(line.c_str(), "node %d %d %d %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d %f %f %f %f %d %f %f %d",
                            &n.id, &n.parent, &n.type, &n.x, &n.y, &n.w, &n.h,
                            &n.color.x, &n.color.y, &n.color.z, &n.alpha,
                            &n.textColor.x, &n.textColor.y, &n.textColor.z, &n.fontScale, &n.value,
                            &n.hAlign, &n.vAlign,
                            &isVar, &n.padL, &n.padT, &n.padR, &n.padB, &n.anchor, &n.pivotX, &n.pivotY,
                            &isVisible);
            if (rd >= 19) n.isVariable = isVar != 0;   // fields appended after v1
            if (rd >= 27) n.visible = isVisible != 0;  // older files: everything visible
            if (rd >= 7) { nodes.push_back(n); if (n.id >= nextId) nextId = n.id + 1; }
        } else if (line.rfind("wname ", 0) == 0) {
            int id = 0; char b[256] = ""; if (sscanf(line.c_str(), "wname %d %255[^\n]", &id, b) == 2) { WidgetNode* n = find(id); if (n) { strncpy(n->name, b, sizeof(n->name) - 1); n->name[sizeof(n->name) - 1] = 0; } }
        } else if (line.rfind("wtext ", 0) == 0) {
            int id = 0; char b[256] = ""; if (sscanf(line.c_str(), "wtext %d %255[^\n]", &id, b) == 2) { WidgetNode* n = find(id); if (n) { strncpy(n->text, b, sizeof(n->text) - 1); n->text[sizeof(n->text) - 1] = 0; } }
        } else if (line.rfind("wimg ", 0) == 0) {
            int id = 0; char b[256] = ""; if (sscanf(line.c_str(), "wimg %d %255[^\n]", &id, b) == 2) { WidgetNode* n = find(id); if (n) { strncpy(n->image, b, sizeof(n->image) - 1); n->image[sizeof(n->image) - 1] = 0; } }
        } else if (line.rfind("wslot ", 0) == 0) {
            int id = 0; float l = 0, t = 0, rr = 0, b = 0;
            if (sscanf(line.c_str(), "wslot %d %f %f %f %f", &id, &l, &t, &rr, &b) == 5)
                if (WidgetNode* n = find(id)) { n->slotL = l; n->slotT = t; n->slotR = rr; n->slotB = b; }
        } else if (line.rfind("wfill ", 0) == 0) {
            int id = 0, rule = 0; float weight = 1;
            if (sscanf(line.c_str(), "wfill %d %d %f", &id, &rule, &weight) == 3)
                if (WidgetNode* n = find(id)) { n->sizeRule = rule; n->fillWeight = weight; }
        } else if (line.rfind("wxf ", 0) == 0) {
            int id = 0; float tx = 0, ty = 0, sxv = 1, syv = 1, ang = 0, shx = 0, shy = 0;
            if (sscanf(line.c_str(), "wxf %d %f %f %f %f %f %f %f", &id, &tx, &ty, &sxv, &syv, &ang, &shx, &shy) == 8)
                if (WidgetNode* n = find(id)) {
                    n->transX = tx; n->transY = ty; n->scaleX = sxv; n->scaleY = syv;
                    n->angle = ang; n->shearX = shx; n->shearY = shy;
                }
        } else if (line.rfind("wrend ", 0) == 0) {
            int id = 0, en = 1; float op = 1;
            if (sscanf(line.c_str(), "wrend %d %f %d", &id, &op, &en) == 3)
                if (WidgetNode* n = find(id)) { n->renderOpacity = op; n->enabled = en != 0; }
        } else if (line.rfind("wtxt2 ", 0) == 0) {
            int id = 0, just = 0, wr = 0;
            if (sscanf(line.c_str(), "wtxt2 %d %d %d", &id, &just, &wr) == 3)
                if (WidgetNode* n = find(id)) { n->justify = just; n->wrap = wr != 0; }
        } else if (line.rfind("wsize ", 0) == 0) {
            int id = 0; unsigned fl = 0; float mnw = 0, mnh = 0, mxw = 0, mxh = 0;
            if (sscanf(line.c_str(), "wsize %d %u %f %f %f %f", &id, &fl, &mnw, &mnh, &mxw, &mxh) == 6)
                if (WidgetNode* n = find(id)) { n->sizeFlags = fl; n->minW = mnw; n->minH = mnh; n->maxW = mxw; n->maxH = mxh; }
        } else if (line.rfind("wrange ", 0) == 0) {
            int id = 0; float mn = 0, mx = 1;
            if (sscanf(line.c_str(), "wrange %d %f %f", &id, &mn, &mx) == 3)
                if (WidgetNode* n = find(id)) { n->minValue = mn; n->maxValue = mx; }
        } else if (line.rfind("wtip ", 0) == 0) {
            int id = 0; char b[256] = "";
            if (sscanf(line.c_str(), "wtip %d %255[^\n]", &id, b) == 2)
                if (WidgetNode* n = find(id)) { strncpy(n->tooltip, b, sizeof(n->tooltip) - 1); n->tooltip[sizeof(n->tooltip) - 1] = 0; }
        }
    }
    return true;   // an empty widget (no nodes) is valid now
}

// ─── rendering (shared by the editor preview and the in-Play HUD) ───
// Editor-only chrome for a layout panel. A panel paints no fill of its own, so
// it is marked by a dashed outline with its name sitting just *above* the
// top-left corner — outside the box, where it can never cover the components
// inside it. None of this is drawn in Play.
static void drawPanelChrome(Renderer* r, const WidgetNode& n, const UIRect& rc, const Vec3& c) {
    const float dash = 7, gap = 5;
    for (float x = rc.x; x < rc.x + rc.w; x += dash + gap) {
        float w = std::min(dash, rc.x + rc.w - x);
        r->drawRectPx(x, rc.y, w, 1, c, 0.8f);
        r->drawRectPx(x, rc.y + rc.h - 1, w, 1, c, 0.8f);
    }
    for (float y = rc.y; y < rc.y + rc.h; y += dash + gap) {
        float h = std::min(dash, rc.y + rc.h - y);
        r->drawRectPx(rc.x, y, 1, h, c, 0.8f);
        r->drawRectPx(rc.x + rc.w - 1, y, 1, h, c, 0.8f);
    }
    r->drawTextLine(rc.x, rc.y - 14, n.name[0] ? n.name : widgetTypeName(n.type),
                    { 0.62f, 0.74f, 0.90f }, 1, 0.85f);
}

// `zoom` scales the font with the design surface, so text shrinks when the user
// zooms out instead of staying at a fixed pixel size.
// Lay the component's text out inside `rc`: honours Justification and, when
// Auto Wrap is on, breaks at word boundaries so nothing spills past the width.
// `emit` receives one line at a time, already positioned.
static void widgetLayOutText(Renderer* r, const WidgetNode& n, const UIRect& rc, float fs,
                             const std::function<void(float, float, const std::string&)>& emit) {
    const float pad = 4, avail = std::max(1.0f, rc.w - pad * 2);
    std::vector<std::string> lines;
    std::string text = n.text;
    if (!n.wrap) {
        lines.push_back(text);
    } else {
        std::string line;
        size_t i = 0;
        while (i <= text.size()) {
            size_t sp = text.find(' ', i);
            std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && r->textWidth(candidate, fs) > avail) { lines.push_back(line); line = word; }
            else line = candidate;
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(line);
    }
    const float lineH = r->fontHeight() * fs;
    float y = rc.y + pad;
    for (const std::string& l : lines) {
        float x = rc.x + pad;
        float lw = r->textWidth(l, fs);
        if (n.justify == WJ_CENTER) x = rc.x + (rc.w - lw) * 0.5f;
        else if (n.justify == WJ_RIGHT) x = rc.x + rc.w - lw - pad;
        emit(x, y, l);
        y += lineH;
    }
}

static void drawOneWidget(UI& ui, const WidgetNode& n, const UIRect& rc, Renderer* r,
                          const std::string& projectDir, bool selected, bool editor, float zoom,
                          float opacity) {
    const float fs = n.fontScale * (zoom > 0.01f ? zoom : 0.01f);
    // Render Opacity scales everything this component paints; a disabled
    // component is drawn washed out, the way Unreal greys one out.
    const float op = clampf(opacity, 0.0f, 1.0f) * (n.enabled ? 1.0f : 0.45f);
    auto A = [&](float a) { return a * op; };          // alpha through the opacity
    switch (n.type) {
    case WT_CANVAS:
        if (editor) drawPanelChrome(r, n, rc, { 0.30f, 0.45f, 0.62f });
        break;
    case WT_VBOX:
    case WT_HBOX:
    case WT_OVERLAY:
    case WT_SIZEBOX:
    case WT_SCALEBOX:
    case WT_GRIDPANEL:
    case WT_UNIFORMGRID:
    case WT_SCROLLBOX:
    case WT_STACKBOX:
    case WT_WRAPBOX:
    case WT_SAFEZONE:
    case WT_WIDGETSWITCHER:
        // layout panels never paint a fill of their own — only the editor outline
        if (editor) {
            Vec3 c = { 0.36f, 0.52f, 0.70f };
            drawPanelChrome(r, n, rc, c);
            // cell guides so grid containers read as grids in the designer
            if (n.type == WT_GRIDPANEL || n.type == WT_UNIFORMGRID) {
                int cells = (int)(n.value < 1 ? 1 : (n.value > 16 ? 16 : n.value));
                for (int i = 1; i < cells; i++) {
                    r->drawRectPx(rc.x + rc.w * i / cells, rc.y, 1, rc.h, c, 0.28f);
                    r->drawRectPx(rc.x, rc.y + rc.h * i / cells, rc.w, 1, c, 0.28f);
                }
            }
            if (n.type == WT_SAFEZONE) {
                float m = n.value < 0 ? 0 : n.value;
                r->drawRectPx(rc.x + m, rc.y + m, rc.w - 2 * m, 1, c, 0.35f);
                r->drawRectPx(rc.x + m, rc.y + rc.h - m, rc.w - 2 * m, 1, c, 0.35f);
                r->drawRectPx(rc.x + m, rc.y + m, 1, rc.h - 2 * m, c, 0.35f);
                r->drawRectPx(rc.x + rc.w - m, rc.y + m, 1, rc.h - 2 * m, c, 0.35f);
            }
        }
        break;
    case WT_TEXT:
    case WT_RICHTEXT: {
        if (n.alpha > 0.001f) r->drawRectPx(rc.x, rc.y, rc.w, rc.h, n.color, A(n.alpha));
        widgetLayOutText(r, n, rc, fs, [&](float tx, float ty, const std::string& line) {
            r->drawTextLine(tx, ty, line, n.textColor, A(1), fs);
        });
        break;
    }
    case WT_BUTTON: {
        // A Button is a container: it paints its background only and whatever the
        // user drops inside (a Text, an Image, …) is drawn as its child.
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, n.color, A(n.alpha));
        r->drawRectPx(rc.x, rc.y, rc.w, 2, n.color * 1.3f, A(n.alpha));
        break;
    }
    case WT_IMAGE: {
        GLuint tex = n.image[0] ? matLoadTexture(r, projectDir, n.image) : 0;
        if (tex) r->drawImagePx(tex, rc.x, rc.y, rc.w, rc.h, { 1, 1, 1 }, A(n.alpha));
        // no texture: a placeholder box, but only in the designer — in Play an
        // unset image must draw nothing rather than leaking editor chrome
        else if (editor) { r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.3f, 0.32f, 0.38f }, A(0.7f)); r->drawTextLine(rc.x + 6, rc.y + 6, "[img]", { 0.8f, 0.84f, 0.9f }, A(1)); }
        break;
    }
    case WT_BORDER: {
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, n.color, A(n.alpha));
        Vec3 line = n.color * 1.6f + Vec3{ 0.15f, 0.15f, 0.18f };
        r->drawRectPx(rc.x, rc.y, rc.w, 2, line, A(1)); r->drawRectPx(rc.x, rc.y + rc.h - 2, rc.w, 2, line, A(1));
        r->drawRectPx(rc.x, rc.y, 2, rc.h, line, A(1)); r->drawRectPx(rc.x + rc.w - 2, rc.y, 2, rc.h, line, A(1));
        break;
    }
    case WT_PROGRESSBAR: {
        float f = widgetFillFraction(n);
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.10f, 0.11f, 0.14f }, A(n.alpha));
        r->drawRectPx(rc.x, rc.y, rc.w * f, rc.h, n.color, A(1));
        break;
    }
    case WT_CHECKBOX: {
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.10f, 0.11f, 0.14f }, A(1));
        Vec3 edge = { 0.45f, 0.5f, 0.58f };
        r->drawRectPx(rc.x, rc.y, rc.w, 2, edge, A(1)); r->drawRectPx(rc.x, rc.y + rc.h - 2, rc.w, 2, edge, A(1));
        r->drawRectPx(rc.x, rc.y, 2, rc.h, edge, A(1)); r->drawRectPx(rc.x + rc.w - 2, rc.y, 2, rc.h, edge, A(1));
        if (n.value > 0.5f) {
            float m = rc.w * 0.22f;
            r->drawRectPx(rc.x + m, rc.y + m, rc.w - 2 * m, rc.h - 2 * m, n.color, A(1));
        }
        break;
    }
    case WT_SLIDER: {
        float f = widgetFillFraction(n);
        float track = rc.h * 0.28f;
        r->drawRectPx(rc.x, rc.y + (rc.h - track) * 0.5f, rc.w, track, { 0.12f, 0.13f, 0.16f }, A(1));
        r->drawRectPx(rc.x, rc.y + (rc.h - track) * 0.5f, rc.w * f, track, n.color * 0.9f, A(1));
        float hx = rc.x + rc.w * f;
        r->drawRectPx(hx - 5, rc.y, 10, rc.h, n.color, A(1));
        break;
    }
    case WT_EDITABLETEXT:
    case WT_MULTILINETEXT: {
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, n.color, A(n.alpha));
        Vec3 edge = { 0.34f, 0.38f, 0.46f };
        r->drawRectPx(rc.x, rc.y, rc.w, 1, edge, A(1)); r->drawRectPx(rc.x, rc.y + rc.h - 1, rc.w, 1, edge, A(1));
        r->drawRectPx(rc.x, rc.y, 1, rc.h, edge, A(1)); r->drawRectPx(rc.x + rc.w - 1, rc.y, 1, rc.h, edge, A(1));
        widgetLayOutText(r, n, rc, fs, [&](float tx, float ty, const std::string& line) {
            r->drawTextLine(tx + 2, ty + 1, line, n.textColor, A(0.75f), fs);
        });
        break;
    }
    case WT_SPINBOX: {
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, n.color, A(n.alpha));
        char b[32]; snprintf(b, sizeof(b), "%g", n.value);
        r->drawTextLine(rc.x + 6, rc.y + 4, b, n.textColor, A(1), fs);
        float bw = rc.h * 0.8f;   // stepper affordance on the right
        r->drawRectPx(rc.x + rc.w - bw, rc.y + 2, bw - 2, rc.h * 0.5f - 3, { 0.28f, 0.31f, 0.38f }, A(1));
        r->drawRectPx(rc.x + rc.w - bw, rc.y + rc.h * 0.5f + 1, bw - 2, rc.h * 0.5f - 3, { 0.28f, 0.31f, 0.38f }, A(1));
        break;
    }
    case WT_COMBOBOX: {
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, n.color, A(n.alpha));
        widgetLayOutText(r, n, rc, fs, [&](float tx, float ty, const std::string& line) {
            r->drawTextLine(tx + 4, ty + 1, line, n.textColor, A(1), fs);
        });
        float cx = rc.x + rc.w - 14, cy = rc.y + rc.h * 0.5f;   // ▼
        r->drawTriPx(cx - 5, cy - 2, cx + 5, cy - 2, cx, cy + 4, n.textColor, A(1));
        break;
    }
    case WT_THROBBER: {
        int dots = 3;
        float d = rc.h * 0.6f, gap = (rc.w - dots * d) / (dots + 1);
        for (int i = 0; i < dots; i++)
            r->drawRectPx(rc.x + gap + i * (d + gap), rc.y + (rc.h - d) * 0.5f, d, d, n.color, A(1.0f - i * 0.22f));
        break;
    }
    case WT_SPACER:
        if (editor) {   // invisible in game: editor-only hatch so it can be grabbed
            Vec3 c = { 0.45f, 0.55f, 0.75f };
            r->drawRectPx(rc.x, rc.y, rc.w, 1, c, 0.4f); r->drawRectPx(rc.x, rc.y + rc.h - 1, rc.w, 1, c, 0.4f);
            r->drawRectPx(rc.x, rc.y, 1, rc.h, c, 0.4f); r->drawRectPx(rc.x + rc.w - 1, rc.y, 1, rc.h, c, 0.4f);
        }
        break;
    default:   // legacy PANEL: the only container that still owns a fill
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, n.color, A(n.alpha));
        break;
    }
    if (selected) {
        Vec3 a = { 0.98f, 0.72f, 0.2f };
        r->drawRectPx(rc.x, rc.y, rc.w, 2, a, 1); r->drawRectPx(rc.x, rc.y + rc.h - 2, rc.w, 2, a, 1);
        r->drawRectPx(rc.x, rc.y, 2, rc.h, a, 1); r->drawRectPx(rc.x + rc.w - 2, rc.y, 2, rc.h, a, 1);
    }
}

// Containers that give each child its own strip or cell, so two children can
// never cover each other. Only the Overlay and the Canvas layer their children.
bool widgetStacksChildren(int parentType) {
    switch (parentType) {
    case WT_VBOX: case WT_HBOX: case WT_STACKBOX: case WT_WRAPBOX:
    case WT_SCROLLBOX: case WT_GRIDPANEL: case WT_UNIFORMGRID:
        return true;
    }
    return false;
}

// A Size Box constrains the box it hands to its child: each override only
// counts while its own switch is on, otherwise the natural size stands.
static void widgetApplySizeBox(const WidgetNode& box, float sx, float sy, float& w, float& h) {
    if (box.sizeFlags & WSF_W) w = box.w * sx;
    if (box.sizeFlags & WSF_H) h = box.h * sy;
    if (box.sizeFlags & WSF_MIN_W) w = std::max(w, box.minW * sx);
    if (box.sizeFlags & WSF_MIN_H) h = std::max(h, box.minH * sy);
    if (box.sizeFlags & WSF_MAX_W) w = std::min(w, box.maxW * sx);
    if (box.sizeFlags & WSF_MAX_H) h = std::min(h, box.maxH * sy);
    w = std::max(0.0f, w); h = std::max(0.0f, h);
}

const char* widgetSizeRuleName(int rule) { return rule == WSR_FILL ? "Fill" : "Auto"; }

// Does this slot claim a share of the box's free room along the stacking axis?
// Only the Size rule decides, never the alignment: hAlign/vAlign default to Fill
// on every node, so reading a Fill *alignment* as a Fill *size* would quietly
// make every child of every box stretch. Size owns the stacking axis, alignment
// owns the cross axis — the same split Unreal draws.
bool widgetSlotFills(const WidgetNode& n, bool verticalAxis) {
    (void)verticalAxis;
    return n.sizeRule == WSR_FILL;
}

// place a box of w x h inside `area` by the child's H/V alignment (Fill stretches)
static UIRect widgetAlignIn(const UIRect& area, const WidgetNode& n, float w, float h) {
    float x = area.x, y = area.y;
    switch (n.hAlign) { case WA_CENTER: x = area.x + (area.w - w) * 0.5f; break;
                        case WA_END: x = area.x + area.w - w; break;
                        case WA_FILL: x = area.x; w = area.w; break; default: x = area.x; }
    switch (n.vAlign) { case WA_CENTER: y = area.y + (area.h - h) * 0.5f; break;
                        case WA_END: y = area.y + area.h - h; break;
                        case WA_FILL: y = area.y; h = area.h; break; default: y = area.y; }
    return { x, y, w, h };
}

// The strip a stacked child owns inside its parent's content box: every earlier
// sibling has already taken its own, so the strips tile without overlapping.
// The child is then aligned on the cross axis inside its strip.
static UIRect widgetStackSlot(const WidgetAsset& a, const WidgetNode& n, const WidgetNode& parent,
                              const UIRect& content, float sx, float sy) {
    const int pt = parent.type;
    const bool vertical = pt == WT_VBOX || pt == WT_SCROLLBOX ||
                          (pt == WT_STACKBOX && parent.value < 0.5f);
    // outer size = the child plus its slot padding, which is what tiles
    const float mw = (n.slotL + n.slotR) * sx, mh = (n.slotT + n.slotB) * sy;

    if (pt == WT_GRIDPANEL || pt == WT_UNIFORMGRID) {
        int cols = (int)(parent.value < 1 ? 1 : (parent.value > 16 ? 16 : parent.value));
        int index = 0;
        for (const WidgetNode& s : a.nodes) { if (s.parent != n.parent) continue; if (s.id == n.id) break; index++; }
        int total = 0;
        for (const WidgetNode& s : a.nodes) if (s.parent == n.parent) total++;
        int rows = (total + cols - 1) / cols; if (rows < 1) rows = 1;
        float cw = content.w / cols, ch = content.h / rows;
        UIRect cell = { content.x + (index % cols) * cw, content.y + (index / cols) * ch, cw, ch };
        cell.x += n.slotL * sx; cell.y += n.slotT * sy;
        cell.w = std::max(0.0f, cell.w - mw); cell.h = std::max(0.0f, cell.h - mh);
        return widgetAlignIn(cell, n, n.w * sx, n.h * sy);
    }

    if (pt == WT_WRAPBOX) {   // flow left to right, drop to a new line when full
        float penX = 0, penY = 0, lineH = 0;
        for (const WidgetNode& s : a.nodes) {
            if (s.parent != n.parent) continue;
            float ow = s.w * sx + (s.slotL + s.slotR) * sx, oh = s.h * sy + (s.slotT + s.slotB) * sy;
            if (penX > 0 && penX + ow > content.w) { penX = 0; penY += lineH; lineH = 0; }
            if (s.id == n.id) {
                UIRect strip = { content.x + penX + s.slotL * sx, content.y + penY + s.slotT * sy,
                                 s.w * sx, s.h * sy };
                return strip;
            }
            penX += ow;
            lineH = std::max(lineH, oh);
        }
        return { content.x, content.y, n.w * sx, n.h * sy };
    }

    // Vertical / Horizontal / Stack Box: one strip per child, in order
    float offset = 0;
    for (const WidgetNode& s : a.nodes) {
        if (s.parent != n.parent) continue;
        if (s.id == n.id) break;
        offset += vertical ? s.h * sy + (s.slotT + s.slotB) * sy
                           : s.w * sx + (s.slotL + s.slotR) * sx;
    }
    // Slot Size: an Auto child keeps its own extent along the stacking axis, a
    // Fill child takes a share of whatever the Auto ones leave over — that is
    // what makes "Fill" actually stretch a row of buttons across the box.
    const float axisTotal = vertical ? content.h : content.w;
    float takenByAuto = 0, totalWeight = 0;
    for (const WidgetNode& s : a.nodes) {
        if (s.parent != n.parent) continue;
        takenByAuto += vertical ? (s.slotT + s.slotB) * sy : (s.slotL + s.slotR) * sx;
        if (widgetSlotFills(s, vertical)) totalWeight += std::max(0.0f, s.fillWeight);
        else takenByAuto += vertical ? s.h * sy : s.w * sx;
    }
    const float freeRoom = std::max(0.0f, axisTotal - takenByAuto);
    auto axisExtent = [&](const WidgetNode& s) {
        if (!widgetSlotFills(s, vertical)) return vertical ? s.h * sy : s.w * sx;
        float weight = std::max(0.0f, s.fillWeight);
        if (totalWeight <= 0.0001f) return 0.0f;
        return freeRoom * weight / totalWeight;
    };

    // re-walk the earlier siblings, now that a Fill child's extent is known
    offset = 0;
    for (const WidgetNode& s : a.nodes) {
        if (s.parent != n.parent) continue;
        if (s.id == n.id) break;
        offset += axisExtent(s) + (vertical ? (s.slotT + s.slotB) * sy : (s.slotL + s.slotR) * sx);
    }
    const float extent = axisExtent(n);

    // The strip is this child's alone, which is what stops the overlap; the
    // child is then aligned inside it on both axes.
    if (vertical) {
        UIRect strip = { content.x + n.slotL * sx, content.y + offset + n.slotT * sy,
                         std::max(0.0f, content.w - mw), extent };
        // Auto: the strip is exactly the child's height, so only H alignment has
        // room. Fill: the strip is bigger, so V alignment bites as well.
        float ch = widgetSlotFills(n, true) ? n.h * sy : extent;
        return widgetAlignIn(strip, n, n.w * sx, ch);
    }
    UIRect strip = { content.x + offset + n.slotL * sx, content.y + n.slotT * sy,
                     extent, std::max(0.0f, content.h - mh) };
    float cw = widgetSlotFills(n, false) ? n.w * sx : extent;
    return widgetAlignIn(strip, n, cw, n.h * sy);
}

// a node's absolute screen rect, honouring each parent's layout rule.
UIRect widgetNodeRect(const WidgetAsset& a, const WidgetNode& n, const UIRect& screen, float sx, float sy) {
    const WidgetNode* p = a.find(n.parent);               // parent 0 / -1 / missing → screen
    UIRect pr = p ? widgetNodeRect(a, *p, screen, sx, sy) : screen;
    int ptype = p ? p->type : -1;                         // -1 = screen (free)
    // Canvas fills its slot: top-level canvas = whole screen; nested canvas = free box.
    if (n.type == WT_CANVAS && ptype < 0) return screen;
    if (p) {                                              // the parent's padding shrinks the content box
        pr.x += p->padL * sx; pr.y += p->padT * sy;
        pr.w = std::max(0.0f, pr.w - (p->padL + p->padR) * sx);
        pr.h = std::max(0.0f, pr.h - (p->padT + p->padB) * sy);
    }

    float w = n.w * sx, h = n.h * sy;
    // a Size Box constrains its own box before anything is laid out inside it
    if (n.type == WT_SIZEBOX) widgetApplySizeBox(n, sx, sy, w, h);
    if (ptype == WT_OVERLAY)                              // layered: alignment only
        return widgetAlignIn(pr, n, w, h);
    if (ptype == WT_SIZEBOX) {
        // the box hands its child the overridden area; the child places itself
        // in it with its own H/V alignment
        UIRect area = pr;
        area.x += n.slotL * sx; area.y += n.slotT * sy;
        area.w = std::max(0.0f, area.w - (n.slotL + n.slotR) * sx);
        area.h = std::max(0.0f, area.h - (n.slotT + n.slotB) * sy);
        return widgetAlignIn(area, n, w, h);
    }
    if (ptype == WT_SCALEBOX) {                           // scale child to fit, centred
        float s = std::min(pr.w / std::max(1.0f, w), pr.h / std::max(1.0f, h));
        float rw = w * s, rh = h * s;
        return { pr.x + (pr.w - rw) * 0.5f, pr.y + (pr.h - rh) * 0.5f, rw, rh };
    }
    if (p && widgetStacksChildren(ptype))                 // one strip or cell each
        return widgetStackSlot(a, n, *p, pr, sx, sy);
    if (widgetLaysOutChildren(ptype)) {                   // single-slot: Button, Safe Zone, Switcher
        UIRect area = pr;
        area.x += n.slotL * sx; area.y += n.slotT * sy;
        area.w = std::max(0.0f, area.w - (n.slotL + n.slotR) * sx);
        area.h = std::max(0.0f, area.h - (n.slotT + n.slotB) * sy);
        return widgetAlignIn(area, n, w, h);
    }
    // Canvas (and the screen root): anchor the child to a side/corner/stretch.
    // On a pinned axis x/y are offsets from the anchor point; on a stretched axis
    // they are insets from the two anchor edges (Unreal's Offset Left/Right).
    if (ptype == WT_CANVAS || ptype < 0) {
        // work in reference units, then scale — same maths the editor uses when
        // re-anchoring, so a slot round-trips exactly
        float bx, by, bw, bh;
        widgetSlotToBox(n.anchor, pr.w / sx, pr.h / sy, n.x, n.y, n.w, n.h, bx, by, bw, bh);
        bw = std::max(0.0f, bw); bh = std::max(0.0f, bh);
        // the pivot says which point of the widget the offsets refer to
        return { pr.x + (bx - bw * n.pivotX) * sx, pr.y + (by - bh * n.pivotY) * sy, bw * sx, bh * sy };
    }
    // free (border / panel): child at its own x/y
    return { pr.x + n.x * sx, pr.y + n.y * sy, w, h };
}

// The box a child is actually laid out in: the parent's rect minus its padding
// (the screen for a top-level node). Every slot <-> box conversion must measure
// against this — the parent's own w/h field is not the same thing (a top-level
// Canvas fills the screen, a nested one is sized by its own slot), and using it
// makes a widget jump when it is re-anchored or resized.
UIRect widgetParentContentRect(const WidgetAsset& a, const WidgetNode& n,
                               const UIRect& screen, float sx, float sy) {
    const WidgetNode* p = a.find(n.parent);
    if (!p) return screen;
    UIRect pr = widgetNodeRect(a, *p, screen, sx, sy);
    pr.x += p->padL * sx; pr.y += p->padT * sy;
    pr.w = std::max(0.0f, pr.w - (p->padL + p->padR) * sx);
    pr.h = std::max(0.0f, pr.h - (p->padT + p->padB) * sy);
    return pr;
}

float widgetFillFraction(const WidgetNode& n) {
    const float span = n.maxValue - n.minValue;
    if (span <= 0.0f) return 0.0f;
    return clampf((n.value - n.minValue) / span, 0.0f, 1.0f);
}

const char* widgetJustifyName(int justify) {
    static const char* names[3] = { "Left", "Center", "Right" };
    return names[justify < 0 || justify > 2 ? 0 : justify];
}

// ── Render Transform ──
// Purely visual: the layout above has already run, so this only bends what is
// drawn. A node inherits its ancestors' transforms, which is why the chain is
// composed rather than applied one node at a time.
static void widgetMulAffine(const float a[6], const float b[6], float out[6]) {
    float r[6];   // out = a ∘ b  (b applied first)
    r[0] = a[0] * b[0] + a[2] * b[1];
    r[1] = a[1] * b[0] + a[3] * b[1];
    r[2] = a[0] * b[2] + a[2] * b[3];
    r[3] = a[1] * b[2] + a[3] * b[3];
    r[4] = a[0] * b[4] + a[2] * b[5] + a[4];
    r[5] = a[1] * b[4] + a[3] * b[5] + a[5];
    for (int i = 0; i < 6; i++) out[i] = r[i];
}

// one node's own transform, about the centre of its screen rect
static bool widgetLocalMatrix(const WidgetAsset& a, const WidgetNode& n, const UIRect& screen,
                              float sx, float sy, float out[6]) {
    const bool identity = n.transX == 0 && n.transY == 0 && n.scaleX == 1 && n.scaleY == 1 &&
                          n.angle == 0 && n.shearX == 0 && n.shearY == 0;
    out[0] = 1; out[1] = 0; out[2] = 0; out[3] = 1; out[4] = 0; out[5] = 0;
    if (identity) return false;
    UIRect rc = widgetNodeRect(a, n, screen, sx, sy);
    const float cx = rc.x + rc.w * 0.5f, cy = rc.y + rc.h * 0.5f;
    const float rad = n.angle * 3.14159265f / 180.0f;
    const float c = cosf(rad), s = sinf(rad);
    // shear is authored as -1..1 and stands for -60..60 degrees
    const float shx = tanf(widgetShearDegrees(n.shearX) * 3.14159265f / 180.0f);
    const float shy = tanf(widgetShearDegrees(n.shearY) * 3.14159265f / 180.0f);
    // scale, then shear, then rotate — the order Unreal uses
    float m00 = n.scaleX, m01 = 0.0f, m10 = 0.0f, m11 = n.scaleY;
    float s00 = 1, s01 = shy, s10 = shx, s11 = 1;
    float t00 = s00 * m00 + s10 * m01, t01 = s01 * m00 + s11 * m01;
    float t10 = s00 * m10 + s10 * m11, t11 = s01 * m10 + s11 * m11;
    out[0] = c * t00 - s * t01; out[1] = s * t00 + c * t01;
    out[2] = c * t10 - s * t11; out[3] = s * t10 + c * t11;
    // keep the centre put, then translate
    out[4] = cx - (out[0] * cx + out[2] * cy) + n.transX * sx;
    out[5] = cy - (out[1] * cx + out[3] * cy) + n.transY * sy;
    return true;
}

void widgetRenderMatrix(const WidgetAsset& a, const WidgetNode& n, const UIRect& screen,
                        float sx, float sy, float out[6]) {
    out[0] = 1; out[1] = 0; out[2] = 0; out[3] = 1; out[4] = 0; out[5] = 0;
    // ancestors first: the outermost transform wraps everything below it
    std::vector<const WidgetNode*> chain;
    for (const WidgetNode* c = &n; c; c = a.find(c->parent)) {
        chain.push_back(c);
        if (chain.size() > 256) break;
    }
    for (size_t i = chain.size(); i-- > 0; ) {
        float local[6];
        if (widgetLocalMatrix(a, *chain[i], screen, sx, sy, local)) widgetMulAffine(out, local, out);
    }
}

float widgetEffectiveOpacity(const WidgetAsset& a, const WidgetNode& n) {
    float o = n.renderOpacity;
    const WidgetNode* p = a.find(n.parent);
    for (int guard = 0; p && guard < 256; guard++) { o *= p->renderOpacity; p = a.find(p->parent); }
    return clampf(o, 0.0f, 1.0f);
}

bool widgetNodeVisible(const WidgetAsset& a, const WidgetNode& n) {
    if (!n.visible) return false;
    const WidgetNode* p = a.find(n.parent);
    for (int guard = 0; p && guard < 256; guard++) {
        if (!p->visible) return false;
        p = a.find(p->parent);
    }
    return true;
}

void widgetRenderTree(UI& ui, const WidgetAsset& asset, const UIRect& screen,
                      Renderer* r, const std::string& projectDir, int selectedId, bool editor) {
    float sx = screen.w / asset.refW, sy = screen.h / asset.refH;
    // draw in declaration order (parents before children keeps a natural z-order)
    for (const auto& n : asset.nodes) {
        // hidden subtrees vanish in game; the designer keeps drawing them so
        // they stay selectable (the hierarchy row marks them "hidden")
        if (!editor && !widgetNodeVisible(asset, n)) continue;
        UIRect rc = widgetNodeRect(asset, n, screen, sx, sy);
        // Render Transform: layout already ran, so this only bends the drawing.
        // Set once per node and everything it emits — fills, text, images —
        // goes through the same affine.
        float m[6];
        widgetRenderMatrix(asset, n, screen, sx, sy, m);
        r->setUITransform(m[0], m[1], m[2], m[3], m[4], m[5]);
        drawOneWidget(ui, n, rc, r, projectDir, n.id == selectedId, editor, sy,
                      widgetEffectiveOpacity(asset, n));
        r->clearUITransform();
    }
}

// The event graph lives in the .wgt after a marker line, so older files (no
// marker) still load and simply come back with an empty graph.
const char* const WIDGET_GRAPH_MARKER = "\nWIDGETGRAPH\n";

// components flagged "Is Variable", walked in hierarchy order
static std::vector<std::pair<std::string, std::string>> widgetVariableMembers(const WidgetAsset& a) {
    std::vector<std::pair<std::string, std::string>> out;
    std::function<void(int)> walk = [&](int parent) {
        for (const WidgetNode& n : a.nodes) {
            if (n.parent != parent) continue;
            if (n.isVariable && n.name[0]) out.push_back({ n.name, widgetTypeName(n.type) });
            walk(n.id);
        }
    };
    for (const WidgetNode& n : a.nodes)                      // top-level roots first
        if (!a.find(n.parent)) {
            if (n.isVariable && n.name[0]) out.push_back({ n.name, widgetTypeName(n.type) });
            walk(n.id);
        }
    return out;
}

bool widgetParseAsset(const std::string& fileText, WidgetAsset& asset, BPGraph& graph) {
    const std::string marker = WIDGET_GRAPH_MARKER;
    size_t split = fileText.find(marker);
    if (!asset.deserialize(split == std::string::npos ? fileText : fileText.substr(0, split))) return false;
    if (split != std::string::npos) {   // no marker: leave the caller's starter graph alone
        graph.clear();
        graph.deserialize(fileText.substr(split + marker.size()));
    }
    graph.syncWidgetVariables(widgetVariableMembers(asset));
    return true;
}

const WidgetNode* widgetNodeAtPoint(const WidgetAsset& asset, const UIRect& screen, float x, float y) {
    float sx = screen.w / asset.refW, sy = screen.h / asset.refH;
    const WidgetNode* hit = nullptr;
    for (const WidgetNode& n : asset.nodes) {               // declaration order = draw order
        if (n.type == WT_SPACER) continue;                  // invisible in game
        if (widgetIsContainer(n.type) && !widgetHasBackground(n.type)) continue;   // pure layout
        if (!widgetNodeVisible(asset, n)) continue;         // hidden: no pointer either
        UIRect rc = widgetNodeRect(asset, n, screen, sx, sy);
        if (x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h) hit = &n;
    }
    return hit;
}

// ─── document editor ───
bool WidgetEditor::loadFrom(const std::string& absPath, const std::string& rel) {
    std::ifstream f(absPath, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    graph.widgetMode = true;
    graph.projectDir = projectDir;
    graph.newGraph();
    if (!widgetParseAsset(data, widget, graph.graph)) return false;
    graph.dirty = false;
    curPath = rel;
    selected_ = -1;
    dirty = false;
    return true;
}
bool WidgetEditor::save() {
    if (curPath.empty() || projectDir.empty()) return false;
    std::ofstream f(projectDir + "\\" + curPath, std::ios::binary);
    if (!f) return false;
    std::string data = widget.serialize() + WIDGET_GRAPH_MARKER + graph.graph.serialize();
    f.write(data.data(), (std::streamsize)data.size());
    dirty = !f.good();
    if (!dirty) graph.dirty = false;
    if (!dirty && logFn) logFn(1, "Widget saved: %s", curPath.c_str());
    return !dirty;
}

// components flagged "Is Variable", in hierarchy order — the graph mirrors these
// as read-only Get-only variables
std::vector<std::pair<std::string, std::string>> WidgetEditor::variableMembers() const {
    return widgetVariableMembers(widget);
}

std::string WidgetEditor::widgetAssetName() const {
    if (curPath.empty()) return "Widget";
    size_t slash = curPath.find_last_of("\\/");
    std::string name = slash == std::string::npos ? curPath : curPath.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name.empty() ? "Widget" : name;
}

bool WidgetEditor::isSelected(int id) const {
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}
void WidgetEditor::selectOnly(int id) {
    selection_.clear();
    if (id >= 0) selection_.push_back(id);
    selected_ = id;
    selAnchor_ = id;
}
// Ctrl click: add the node to the selection, or drop it if already there
void WidgetEditor::toggleSelected(int id) {
    auto it = std::find(selection_.begin(), selection_.end(), id);
    if (it != selection_.end()) {
        selection_.erase(it);
        if (selected_ == id) selected_ = selection_.empty() ? -1 : selection_.back();
    } else {
        selection_.push_back(id);
        selected_ = id;
    }
    selAnchor_ = id;
}

// Shift+click: take everything between the anchor and `id` in the order the
// hierarchy rows are laid out on screen. Ctrl+Shift keeps what was already
// selected — the same rules as the level Outliner.
void WidgetEditor::selectRangeTo(int id, bool additive) {
    int from = -1, to = -1;
    for (int i = 0; i < (int)rowOrder_.size(); i++) {
        if (rowOrder_[i] == selAnchor_) from = i;
        if (rowOrder_[i] == id) to = i;
    }
    if (from < 0 || to < 0) { selectOnly(id); return; }
    if (from > to) std::swap(from, to);
    if (!additive) selection_.clear();
    for (int i = from; i <= to; i++)
        if (!isSelected(rowOrder_[i])) selection_.push_back(rowOrder_[i]);
    selected_ = id;   // the anchor stays put, so dragging the range keeps working
}

// ─── component clipboard ───
// Depth-first so a parent always lands in `out` before its children: paste can
// then walk the vector once and map each entry's parent index to a new node id.
void WidgetEditor::collectSubtree(int id, std::vector<ClipNode>& out, int parentIndex) const {
    const WidgetNode* n = widget.find(id);
    if (!n) return;
    int here = (int)out.size();
    out.push_back({ *n, parentIndex });
    for (const WidgetNode& c : widget.nodes) if (c.parent == id) collectSubtree(c.id, out, here);
}

void WidgetEditor::makeUniqueName(char* name, int cap) const {
    auto taken = [&](const char* candidate) {
        for (const WidgetNode& n : widget.nodes) if (_stricmp(n.name, candidate) == 0) return true;
        return false;
    };
    if (!taken(name)) return;
    // strip a trailing _<digits> so Button_1 becomes Button_2, not Button_1_1
    std::string base = name;
    size_t us = base.find_last_of('_');
    if (us != std::string::npos && us + 1 < base.size() &&
        base.find_first_not_of("0123456789", us + 1) == std::string::npos)
        base = base.substr(0, us);
    for (int i = 1; i < 10000; i++) {
        char b[64];
        snprintf(b, sizeof(b), "%.40s_%d", base.c_str(), i);
        if (!taken(b)) { snprintf(name, cap, "%s", b); return; }
    }
}

void WidgetEditor::copySelection() {
    clipboard_.clear();
    // an ancestor already brings its children along, so skip nested selections
    for (int id : selection_) {
        bool nested = false;
        for (int other : selection_) if (other != id && isAncestorOf(other, id)) { nested = true; break; }
        if (!nested) collectSubtree(id, clipboard_, -1);
    }
}

// `parent` < -1 keeps every root where it was (Ctrl+D); otherwise everything is
// re-parented under `parent` (0 / a container id).
void WidgetEditor::pasteClipboard(int parent, bool offset) {
    if (clipboard_.empty()) return;
    std::vector<int> newIds((int)clipboard_.size(), -1);
    std::vector<int> roots;
    for (int i = 0; i < (int)clipboard_.size(); i++) {
        const ClipNode& c = clipboard_[i];
        int parentId = c.parent >= 0 ? newIds[c.parent]
                     : (parent < -1 ? c.node.parent : parent);
        int id = widget.addNode(c.node.type, parentId);
        WidgetNode* nn = widget.find(id);
        if (!nn) continue;
        int keepId = nn->id, keepParent = nn->parent;
        *nn = c.node;                       // copy every property…
        nn->id = keepId; nn->parent = keepParent;   // …but keep its new identity
        makeUniqueName(nn->name, sizeof(nn->name));
        if (c.parent < 0 && offset) { nn->x += 16; nn->y += 16; }
        newIds[i] = id;
        if (c.parent < 0) roots.push_back(id);
    }
    selection_ = roots;
    selected_ = roots.empty() ? -1 : roots.front();
    selAnchor_ = selected_;
    dirty = true;
}

void WidgetEditor::duplicateSelection() {
    if (selection_.empty()) return;
    std::vector<ClipNode> keep = clipboard_;   // Ctrl+D must not clobber the clipboard
    copySelection();
    pasteClipboard(-2, true);
    clipboard_ = keep;
}

// true when `node` sits above `maybeDescendant` in the parent chain — used to
// refuse a re-parent that would build a cycle.
bool WidgetEditor::isAncestorOf(int node, int maybeDescendant) const {
    const WidgetNode* c = widget.find(maybeDescendant);
    for (int guard = 0; c && guard < 256; guard++) {
        if (c->parent == node) return true;
        c = widget.find(c->parent);
    }
    return false;
}

void WidgetEditor::drawHierarchyRow(UI& ui, int id, int depth, float& y, const UIRect& panel) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    const WidgetNode* n = widget.find(id);
    if (!n) return;
    UIRect row = { panel.x + 4, y, panel.w - 8, 20 };
    bool visible = row.y + row.h > panel.y && row.y < panel.y + panel.h;
    bool over = visible && !ui.interactionBlocked() &&
                in.mouseX >= row.x && in.mouseX < row.x + row.w &&
                in.mouseY >= row.y && in.mouseY < row.y + row.h;
    // while something is being dragged, this row is a candidate parent
    bool dragging = dragNewActive_ || hierDragActive_;
    bool dropOk = over && dragging && widgetIsContainer(n->type) &&
                  !(hierDragActive_ && (id == hierDragNode_ || isAncestorOf(hierDragNode_, id)));
    if (dropOk) hierDropTarget_ = id;
    if (visible) {
        // primary selection is brighter than the rest of a multi-selection
        if (dropOk) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.12f, 0.34f, 0.24f }, 1);
        else if (id == selected_) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.20f, 0.34f, 0.52f }, 1);
        else if (isSelected(id)) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.16f, 0.25f, 0.38f }, 1);
        else if (over) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.18f, 0.20f, 0.25f }, 1);
        if (dropOk) r->drawRectPx(row.x, row.y, 3, row.h, { 0.35f, 0.85f, 0.5f }, 1);
        std::string label = std::string(widgetTypeName(n->type)) + "  " + n->name;
        if (!n->visible) label += "   (hidden)";
        r->drawTextLine(row.x + 8 + depth * 12, row.y + 3, ui.ellipsize(label, row.w - 16 - depth * 12),
                        n->visible ? Vec3{ 0.85f, 0.89f, 0.95f } : Vec3{ 0.55f, 0.58f, 0.64f }, 1);
    }
    rowOrder_.push_back(id);            // visit order, for Shift range selection
    if (over && in.mousePressed) {
        // Outliner rules: Shift extends from the anchor, Ctrl toggles one row,
        // Ctrl+Shift extends without clearing. The row order is only complete
        // once the whole tree is walked, so a range is resolved after the loop.
        if (in.keyShift && selAnchor_ >= 0) { pendingRange_ = id; pendingRangeAdd_ = in.keyCtrl; }
        else if (in.keyCtrl) toggleSelected(id);                // add / remove
        else {
            selectOnly(id);
            hierDragNode_ = id;                 // may become a re-parent drag
            hierDragX_ = in.mouseX; hierDragY_ = in.mouseY;
        }
    }
    y += 20;
    for (const auto& c : widget.nodes) if (c.parent == id) drawHierarchyRow(ui, c.id, depth + 1, y, panel);
}

// topmost node (last drawn) whose rect contains the point; -1 if none
int WidgetEditor::nodeAtPoint(float mx, float my, const UIRect& screen, float scale) const {
    int hit = -1;
    for (const auto& n : widget.nodes) {
        UIRect rc = widgetNodeRect(widget, n, screen, scale, scale);
        if (mx >= rc.x && mx < rc.x + rc.w && my >= rc.y && my < rc.y + rc.h) hit = n.id;
    }
    return hit;
}
// deepest (most-nested) container under the point; 0 = the screen root
int WidgetEditor::deepestContainerAt(float mx, float my, const UIRect& screen, float scale) const {
    int best = 0, bestDepth = -1;
    for (const auto& n : widget.nodes) {
        if (!widgetIsContainer(n.type)) continue;
        UIRect rc = widgetNodeRect(widget, n, screen, scale, scale);
        if (mx < rc.x || mx >= rc.x + rc.w || my < rc.y || my >= rc.y + rc.h) continue;
        int depth = 0;
        for (const WidgetNode* p = widget.find(n.parent); p; p = widget.find(p->parent)) depth++;
        if (depth >= bestDepth) { bestDepth = depth; best = n.id; }
    }
    return best;
}

void WidgetEditor::draw(UI& ui) {
    // The alignment/anchor drop-down is drawn last (in UI::end), so this panel
    // has already hit-tested by the time the user clicks an item. Block the
    // whole editor while the cursor is inside the open list, otherwise the click
    // that picks a value also lands on the row underneath it.
    const bool wasBlocked = ui.interactionBlocked();
    if (ui.popupCoversPointer()) ui.setInteractionBlocked(true, false);
    struct Unblock {   // restores on every exit path, including the Graph one
        UI& ui; bool was;
        ~Unblock() { ui.setInteractionBlocked(was, false); }
    } unblock{ ui, wasBlocked };
    const UIInput& in = ui.input();
    Renderer* r = ui.r;

    // ── top-left tool bar (same language as the Blueprint editor) ──
    {
        UIRect bar = ui.allocRow(38);
        r->drawRectPx(bar.x, bar.y, bar.w, bar.h, { 0.105f, 0.115f, 0.14f }, 1);
        r->drawRectPx(bar.x, bar.y + bar.h - 1, bar.w, 1, { 0.05f, 0.055f, 0.065f }, 1);
        const float BW = 34, BH = bar.h - 10;
        float bx = bar.x + 8;
        if (drawSaveButton(ui, { bx, bar.y + 5, BW, BH }, isDirty(),
                           "Save the Widget (designer + graph)")) save();
        bx += BW + 8;
        // The graph has no tool row of its own: its two toggles sit here, same
        // size and same row as Save, so the whole editor has one tool bar.
        if (graphMode) {
            if (ui.toolIconButtonRect("wgt_tool_panels", { bx, bar.y + 5, BW, BH }, 3,
                                      graph.panelsVisible(), "Shows or hides the Blueprint panels"))
                graph.togglePanels();
            bx += BW + 8;
            if (ui.toolIconButtonRect("wgt_tool_settings", { bx, bar.y + 5, BW, BH }, 4,
                                      graph.settingsVisible(), "Opens or closes the Blueprint Settings"))
                graph.toggleSettings();
        }

        // ── Designer | Graph switch, right-aligned like Unreal's ──
        const char* modes[2] = { "Designer", "Graph" };
        float mw = 0;
        for (int i = 0; i < 2; i++) mw = std::max(mw, r->textWidth(modes[i]) + 26);
        for (int i = 0; i < 2; i++) {
            UIRect rc = { bar.x + bar.w - (2 - i) * (mw + 4) - 8, bar.y + 5, mw, bar.h - 10 };
            bool on = (i == 1) == graphMode;
            bool over = !ui.interactionBlocked() && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
                        in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
            r->drawRectPx(rc.x, rc.y, rc.w, rc.h, on ? Vec3{ 0.12f, 0.32f, 0.56f }
                                                : over ? Vec3{ 0.20f, 0.23f, 0.28f } : Vec3{ 0.145f, 0.155f, 0.19f }, 1);
            if (on) r->drawRectPx(rc.x, rc.y + rc.h - 2, rc.w, 2, { 0.30f, 0.62f, 0.99f }, 1);
            r->drawTextLine(rc.x + (rc.w - r->textWidth(modes[i])) * 0.5f, rc.y + (rc.h - 12) * 0.5f, modes[i],
                            on ? Vec3{ 0.85f, 0.93f, 1.0f } : Vec3{ 0.68f, 0.73f, 0.81f }, 1);
            if (over && in.mousePressed) graphMode = (i == 1);
        }
    }

    // ── Graph mode: hand the whole canvas to the embedded Blueprint editor ──
    if (graphMode) {
        graph.projectDir = projectDir;
        graph.logFn = logFn;
        graph.widgetMode = true;               // UMG events, no Begin Play/Tick/Hit
        graph.graph.syncWidgetVariables(variableMembers());
        graph.draw(ui);
        return;
    }

    UIRect pin = ui.panelInner();
    float top = ui.panelCursorY() + 6;
    UIRect canvas = { pin.x, top, pin.w, pin.y + pin.h - top };
    ui.spacing(canvas.h);
    r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);

    // resizable splitters clamp the panel sizes each frame
    leftW_ = std::min(std::max(leftW_, 150.0f), canvas.w * 0.4f);
    rightW_ = std::min(std::max(rightW_, 170.0f), canvas.w * 0.4f);
    paletteFrac_ = std::min(std::max(paletteFrac_, 0.2f), 0.8f);
    float HW = leftW_, DW = rightW_;
    float palH = (canvas.h - 6) * paletteFrac_;
    UIRect palette = { canvas.x, canvas.y, HW, palH };
    UIRect hier = { canvas.x, canvas.y + palH + 6, HW, canvas.h - palH - 6 };
    UIRect det = { canvas.x + canvas.w - DW, canvas.y, DW, canvas.h };
    // the design area reserves a ruler strip on top (X) and on the right (Y)
    const float RULER_T = 18, RULER_R = 34;
    UIRect designOuter = { canvas.x + HW + 10, canvas.y + 10, canvas.w - HW - DW - 20, canvas.h - 20 };
    UIRect design = { designOuter.x, designOuter.y + RULER_T,
                      std::max(40.0f, designOuter.w - RULER_R), std::max(40.0f, designOuter.h - RULER_T - 22) };

    // ── free navigation (Unreal-style): wheel zooms at the cursor, MMB/RMB pans ──
    bool overDesignArea = !ui.interactionBlocked() &&
                          in.mouseX >= design.x && in.mouseX < design.x + design.w &&
                          in.mouseY >= design.y && in.mouseY < design.y + design.h;
    float fitScale = std::min(design.w / widget.refW, design.h / widget.refH);
    if (fitScale <= 0) fitScale = 0.01f;
    if (overDesignArea && in.wheel != 0) {
        // keep the point under the cursor fixed while zooming
        float before = fitScale * viewZoom_;
        float ox = (in.mouseX - (design.x + (design.w - widget.refW * before) * 0.5f + viewPanX_)) / before;
        float oy = (in.mouseY - (design.y + (design.h - widget.refH * before) * 0.5f + viewPanY_)) / before;
        viewZoom_ = clampf(viewZoom_ * (in.wheel > 0 ? 1.12f : 1.0f / 1.12f), 0.15f, 8.0f);
        float after = fitScale * viewZoom_;
        viewPanX_ += (in.mouseX - (design.x + (design.w - widget.refW * after) * 0.5f + viewPanX_)) - ox * after;
        viewPanY_ += (in.mouseY - (design.y + (design.h - widget.refH * after) * 0.5f + viewPanY_)) - oy * after;
        ui.consumeWheel();   // the enclosing panel must not also scroll
    }
    if (overDesignArea && (in.mmbPressed || in.rmbPressed)) {
        viewPanning_ = true;
        panStartX_ = in.mouseX; panStartY_ = in.mouseY;
        panOrigX_ = viewPanX_; panOrigY_ = viewPanY_;
    }
    if (viewPanning_) {
        if (in.mmbDown || in.rmbDown) {
            viewPanX_ = panOrigX_ + (in.mouseX - panStartX_);
            viewPanY_ = panOrigY_ + (in.mouseY - panStartY_);
        } else viewPanning_ = false;
    }

    // design surface = the reference screen, letterboxed then pan/zoomed
    float scale = fitScale * viewZoom_;
    UIRect screen = { design.x + (design.w - widget.refW * scale) * 0.5f + viewPanX_,
                      design.y + (design.h - widget.refH * scale) * 0.5f + viewPanY_,
                      widget.refW * scale, widget.refH * scale };
    designRect_ = screen; designScaleX_ = designScaleY_ = scale;

    r->drawRectPx(canvas.x, canvas.y, canvas.w, canvas.h, { 0.09f, 0.095f, 0.11f }, 1);
    // ── designer grid: covers the whole design area, not just the widget rect ──
    r->setUIScissor(design.x, design.y, design.w, design.h, true);
    if (showGrid_ && gridStep_ > 1) {
        float stepPx = gridStep_ * scale;
        if (stepPx >= 4) {   // skip when the zoom makes the grid unreadable
            // first grid line at or before the left/top edge of the design area
            float firstX = screen.x - std::ceil((screen.x - design.x) / stepPx) * stepPx;
            float firstY = screen.y - std::ceil((screen.y - design.y) / stepPx) * stepPx;
            for (float gx = firstX; gx < design.x + design.w; gx += stepPx) {
                int idx = (int)std::lround((gx - screen.x) / stepPx);
                bool major = (idx % 5) == 0;
                r->drawRectPx(gx, design.y, 1, design.h, { 0.35f, 0.42f, 0.55f }, major ? 0.26f : 0.12f);
            }
            for (float gy = firstY; gy < design.y + design.h; gy += stepPx) {
                int idx = (int)std::lround((gy - screen.y) / stepPx);
                bool major = (idx % 5) == 0;
                r->drawRectPx(design.x, gy, design.w, 1, { 0.35f, 0.42f, 0.55f }, major ? 0.26f : 0.12f);
            }
        }
    }
    // No fill behind the widget: the reference resolution is marked by a dashed
    // outline alone, so the grid stays visible right through it.
    {
        const Vec3 c = { 0.42f, 0.50f, 0.62f };
        const float dash = 9, gap = 6;
        for (float x = screen.x; x < screen.x + screen.w; x += dash + gap) {
            float w = std::min(dash, screen.x + screen.w - x);
            r->drawRectPx(x, screen.y, w, 1, c, 0.75f);
            r->drawRectPx(x, screen.y + screen.h - 1, w, 1, c, 0.75f);
        }
        for (float y = screen.y; y < screen.y + screen.h; y += dash + gap) {
            float h = std::min(dash, screen.y + screen.h - y);
            r->drawRectPx(screen.x, y, 1, h, c, 0.75f);
            r->drawRectPx(screen.x + screen.w - 1, y, 1, h, c, 0.75f);
        }
    }
    widgetRenderTree(ui, widget, screen, r, projectDir, selected_, /*editor=*/true);
    // secondary members of a multi-selection get a dimmer outline than the primary
    for (int id : selection_) {
        if (id == selected_) continue;
        const WidgetNode* mn = widget.find(id);
        if (!mn) continue;
        UIRect mr = widgetNodeRect(widget, *mn, screen, scale, scale);
        Vec3 c = { 0.98f, 0.72f, 0.2f };
        r->drawRectPx(mr.x, mr.y, mr.w, 1, c, 0.55f); r->drawRectPx(mr.x, mr.y + mr.h - 1, mr.w, 1, c, 0.55f);
        r->drawRectPx(mr.x, mr.y, 1, mr.h, c, 0.55f); r->drawRectPx(mr.x + mr.w - 1, mr.y, 1, mr.h, c, 0.55f);
    }
    if (widget.nodes.empty()) {
        const char* msg = "Empty widget: drag a component from the palette.";
        r->drawTextLine(screen.x + (screen.w - r->textWidth(msg)) * 0.5f, screen.y + screen.h * 0.5f - 8,
                        msg, { 0.5f, 0.55f, 0.62f }, 1);
    }

    r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);   // leave the design clip

    auto nodeRect = [&](const WidgetNode& n) { return widgetNodeRect(widget, n, screen, scale, scale); };
    auto parentBox = [&](const WidgetNode& n) { return widgetParentContentRect(widget, n, screen, scale, scale); };
    auto parentType = [&](const WidgetNode& n) { const WidgetNode* p = widget.find(n.parent); return p ? p->type : -1; };

    // ── rulers: X across the top, Y down the right, in reference-resolution px ──
    {
        const Vec3 rbg = { 0.125f, 0.135f, 0.165f }, tick = { 0.45f, 0.52f, 0.62f }, txt = { 0.66f, 0.72f, 0.82f };
        UIRect topR = { design.x, designOuter.y, design.w, RULER_T };
        UIRect rightR = { design.x + design.w, design.y, RULER_R, design.h };
        r->drawRectPx(topR.x, topR.y, topR.w, topR.h, rbg, 1);
        r->drawRectPx(rightR.x, rightR.y, rightR.w, rightR.h, rbg, 1);
        // highlight the selected node's span on both rulers (its current size)
        const WidgetNode* rn = selected_ >= 0 ? widget.find(selected_) : nullptr;
        if (rn) {
            UIRect rr = nodeRect(*rn);
            r->drawRectPx(std::max(rr.x, topR.x), topR.y,
                          std::min(rr.w, topR.x + topR.w - rr.x), topR.h, { 0.30f, 0.62f, 0.99f }, 0.30f);
            r->drawRectPx(rightR.x, std::max(rr.y, rightR.y), rightR.w,
                          std::min(rr.h, rightR.y + rightR.h - rr.y), { 0.30f, 0.62f, 0.99f }, 0.30f);
        }
        // labelled step that stays readable at the current zoom
        static const float steps[] = { 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000 };
        float step = steps[9];
        for (float s : steps) if (s * scale >= 56) { step = s; break; }
        for (int i = 0; i * step <= widget.refW + 0.5f; i++) {
            float x = screen.x + i * step * scale;
            if (x < topR.x - 1 || x > topR.x + topR.w + 1) continue;
            r->drawRectPx(x, topR.y + RULER_T - 6, 1, 6, tick, 0.9f);
            char b[16]; snprintf(b, sizeof(b), "%g", i * step);
            r->drawTextLine(x + 3, topR.y + 2, b, txt, 1, 0.8f);
        }
        for (int i = 0; i * step <= widget.refH + 0.5f; i++) {
            float y = screen.y + i * step * scale;
            if (y < rightR.y - 1 || y > rightR.y + rightR.h + 1) continue;
            r->drawRectPx(rightR.x, y, 6, 1, tick, 0.9f);
            char b[16]; snprintf(b, sizeof(b), "%g", i * step);
            r->drawTextLine(rightR.x + 8, y + 1, b, txt, 1, 0.8f);
        }
        // readout strip under the design: reference resolution + current selection size
        char info[128];
        if (rn) snprintf(info, sizeof(info), "Resolution %g x %g   |   %s:  %g x %g  @ (%g, %g)",
                         widget.refW, widget.refH, rn->name, rn->w, rn->h, rn->x, rn->y);
        else snprintf(info, sizeof(info), "Resolution %g x %g   |   zoom %.0f%%   |   grid %s (%g px)   |   wheel: zoom, MMB/RMB: pan",
                      widget.refW, widget.refH, viewZoom_ * 100.0f, showGrid_ ? "on" : "off", gridStep_);
        r->drawTextLine(design.x + 2, design.y + design.h + 5, ui.ellipsize(info, design.w + RULER_R - 4),
                        { 0.55f, 0.61f, 0.70f }, 1, 0.9f);
    }
    // Tool Tip Text, previewed right here: hovering a component in the designer
    // shows the same tip the player will get.
    if (overDesignArea && !viewPanning_ && !dragMove_ && !dragResize_) {
        int hover = nodeAtPoint(in.mouseX, in.mouseY, screen, scale);
        if (const WidgetNode* hn = hover >= 0 ? widget.find(hover) : nullptr) ui.showTip(hn->tooltip);
    }

    // The design viewport is the only gate: once panned/zoomed, `screen` can
    // extend under the side panels, so a click in the Details must not fall
    // through here. A widget dragged past the widget rect stays fully editable —
    // it is drawn (and grabbed) anywhere inside the viewport.
    bool overDesign = overDesignArea;

    // ── design-surface interaction: only free-positioned nodes move/resize ──
    // clipped to the viewport so handles never spill over the side panels
    r->setUIScissor(design.x, design.y, design.w, design.h, true);
    bool handleHot = false;
    WidgetNode* selNode = selected_ >= 0 ? widget.find(selected_) : nullptr;
    bool selFree = selNode && selNode->type != WT_CANVAS && !widgetIsAligned(parentType(*selNode));

    // ── reorder arrows, on the two opposite bounds of the stacking axis ──
    // In a Vertical/Horizontal Box the hierarchy order IS the layout order, so
    // the only way to move a component along the row is to move it among its
    // siblings. The arrows sit just outside the component, pointing the way it
    // will travel, and the one with nothing to swap with is drawn inert.
    int reorderDelta = 0;
    if (selNode && widgetStacksChildren(parentType(*selNode))) {
        const WidgetNode* par = widget.find(selNode->parent);
        int siblings = 0, index = 0;
        for (const WidgetNode& s : widget.nodes) {
            if (s.parent != selNode->parent) continue;
            if (s.id == selNode->id) index = siblings;
            siblings++;
        }
        if (siblings > 1) {
            const int pt = parentType(*selNode);
            const bool vertical = pt == WT_VBOX || pt == WT_SCROLLBOX ||
                                  (pt == WT_STACKBOX && par && par->value < 0.5f);
            UIRect rc = nodeRect(*selNode);
            const float BS = 20, GAPX = 3;
            // "earlier" is left/up, "later" is right/down — the direction the
            // component actually moves in the box
            UIRect back = vertical ? UIRect{ rc.x + (rc.w - BS) * 0.5f, rc.y - BS - GAPX, BS, BS }
                                   : UIRect{ rc.x - BS - GAPX, rc.y + (rc.h - BS) * 0.5f, BS, BS };
            UIRect fwd  = vertical ? UIRect{ rc.x + (rc.w - BS) * 0.5f, rc.y + rc.h + GAPX, BS, BS }
                                   : UIRect{ rc.x + rc.w + GAPX, rc.y + (rc.h - BS) * 0.5f, BS, BS };
            struct Arrow { UIRect rc; int delta; bool live; };
            const Arrow arrows[2] = { { back, -1, index > 0 }, { fwd, +1, index < siblings - 1 } };
            for (const Arrow& a : arrows) {
                bool over = a.live && overDesignArea &&
                            in.mouseX >= a.rc.x && in.mouseX < a.rc.x + a.rc.w &&
                            in.mouseY >= a.rc.y && in.mouseY < a.rc.y + a.rc.h;
                Vec3 bg = !a.live ? Vec3{ 0.16f, 0.17f, 0.20f }
                        : over    ? Vec3{ 0.30f, 0.62f, 0.99f } : Vec3{ 0.22f, 0.28f, 0.38f };
                r->drawRectPx(a.rc.x, a.rc.y, a.rc.w, a.rc.h, bg, a.live ? 0.95f : 0.55f);
                Vec3 fg = a.live ? Vec3{ 0.92f, 0.96f, 1.0f } : Vec3{ 0.42f, 0.45f, 0.50f };
                float cx = a.rc.x + a.rc.w * 0.5f, cy = a.rc.y + a.rc.h * 0.5f;
                const float t = 5;
                if (vertical) {
                    if (a.delta < 0) r->drawTriPx(cx, cy - t, cx - t, cy + t, cx + t, cy + t, fg, 1);
                    else             r->drawTriPx(cx, cy + t, cx + t, cy - t, cx - t, cy - t, fg, 1);
                } else {
                    if (a.delta < 0) r->drawTriPx(cx - t, cy, cx + t, cy - t, cx + t, cy + t, fg, 1);
                    else             r->drawTriPx(cx + t, cy, cx - t, cy + t, cx - t, cy - t, fg, 1);
                }
                if (over && in.mousePressed) { reorderDelta = a.delta; handleHot = true; }
            }
        }
    }
    if (reorderDelta && widget.moveSibling(selected_, reorderDelta)) {
        dirty = true;
        selNode = widget.find(selected_);   // the move rebuilt the node vector
        selFree = selNode && selNode->type != WT_CANVAS && !widgetIsAligned(parentType(*selNode));
    }

    if (selNode && selFree) {
        UIRect rc = nodeRect(*selNode);
        // ── anchor marker: where this slot is pinned, plus a tie line ──
        int pt = parentType(*selNode);
        if (pt == WT_CANVAS || pt < 0) {
            UIRect prc = parentBox(*selNode);
            float aMinX, aMinY, aMaxX, aMaxY;
            widgetAnchorRange(selNode->anchor, aMinX, aMinY, aMaxX, aMaxY);
            const Vec3 AC = { 0.98f, 0.72f, 0.2f };
            float ax0 = prc.x + prc.w * aMinX, ax1 = prc.x + prc.w * aMaxX;
            float ay0 = prc.y + prc.h * aMinY, ay1 = prc.y + prc.h * aMaxY;
            auto marker = [&](float mx, float my) {   // little four-arm cross
                r->drawRectPx(mx - 7, my - 1, 14, 2, AC, 0.95f);
                r->drawRectPx(mx - 1, my - 7, 2, 14, AC, 0.95f);
            };
            if (aMinX == aMaxX && aMinY == aMaxY) marker(ax0, ay0);
            else {   // stretched: mark the whole anchored span
                if (aMinX != aMaxX) { r->drawRectPx(ax0, ay0 - 1, ax1 - ax0, 2, AC, 0.75f); marker(ax0, ay0); marker(ax1, ay0); }
                if (aMinY != aMaxY) { r->drawRectPx(ax0 - 1, ay0, 2, ay1 - ay0, AC, 0.75f); marker(ax0, ay0); marker(ax0, ay1); }
            }
            // Dashed tie from the anchor to the centre of the widget. The line
            // aims at the centre so it reads as pointing at the component, but
            // every dash that falls inside the component's bounds is skipped:
            // nothing is ever drawn on top of what the user is designing.
            float ccx = rc.x + rc.w * 0.5f, ccy = rc.y + rc.h * 0.5f;
            float dx = ccx - ax0, dy = ccy - ay0, len = sqrtf(dx * dx + dy * dy);
            for (float t = 0; len > 0.5f && t < len; t += 10) {
                float dxp = ax0 + dx * (t / len), dyp = ay0 + dy * (t / len);
                if (dxp >= rc.x && dxp < rc.x + rc.w && dyp >= rc.y && dyp < rc.y + rc.h) continue;
                r->drawRectPx(dxp - 1, dyp - 1, 2, 2, AC, 0.5f);
            }
            // pivot dot: the point the slot offsets are measured from
            float px = rc.x + rc.w * selNode->pivotX, py = rc.y + rc.h * selNode->pivotY;
            r->drawRectPx(px - 3, py - 3, 6, 6, AC, 0.9f);
        }
        // ── resize handles on all four sides and corners ──
        const float HS = 9;
        struct Grip { float x, y; int edges; };   // 1 L, 2 R, 4 T, 8 B
        Grip grips[8] = {
            { rc.x,            rc.y,            1 | 4 }, { rc.x + rc.w * 0.5f, rc.y,            4 },
            { rc.x + rc.w,     rc.y,            2 | 4 }, { rc.x + rc.w,        rc.y + rc.h*0.5f, 2 },
            { rc.x + rc.w,     rc.y + rc.h,     2 | 8 }, { rc.x + rc.w * 0.5f, rc.y + rc.h,     8 },
            { rc.x,            rc.y + rc.h,     1 | 8 }, { rc.x,               rc.y + rc.h*0.5f, 1 },
        };
        for (const Grip& g : grips) {
            UIRect h = { g.x - HS * 0.5f, g.y - HS * 0.5f, HS, HS };
            bool hot = overDesign && in.mouseX >= h.x && in.mouseX < h.x + h.w &&
                       in.mouseY >= h.y && in.mouseY < h.y + h.h;
            r->drawRectPx(h.x, h.y, h.w, h.h, hot ? Vec3{ 1.0f, 0.85f, 0.45f } : Vec3{ 0.98f, 0.72f, 0.2f }, 1);
            if (hot && in.mousePressed) { dragResize_ = true; resizeEdges_ = g.edges; handleHot = true; }
        }
    }
    r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);
    if (overDesign && in.mousePressed && !handleHot && dragNewType_ < 0 && dragSplit_ == 0) {
        int hit = nodeAtPoint(in.mouseX, in.mouseY, screen, scale);
        // a top-level Canvas covers the whole screen: treat it as empty space, or
        // no marquee could ever start (it stays selectable from the hierarchy)
        if (const WidgetNode* hn = hit >= 0 ? widget.find(hit) : nullptr)
            if (hn->type == WT_CANVAS && !widget.find(hn->parent)) hit = -1;
        if (hit < 0) {
            // empty space: start a marquee instead of just clearing the selection
            marquee_ = true;
            marqueeX0_ = in.mouseX; marqueeY0_ = in.mouseY;
            bool additive = in.keyCtrl || in.keyShift;   // keep what is already selected
            marqueeBase_ = additive ? selection_ : std::vector<int>{};
            if (!additive) selectOnly(-1);
        } else if (in.keyCtrl || in.keyAlt) {
            toggleSelected(hit);
        } else if (!isSelected(hit)) {
            selectOnly(hit);       // clicking outside the group starts a new one
        } else {
            selected_ = hit;       // clicking a member keeps the group and drags it
        }
        // grab every free-positioned node in the selection, so a multi-selection
        // moves as one block
        if (hit >= 0 && !in.keyCtrl && !in.keyAlt) {
            dragStart_.clear();
            for (int id : selection_) {
                const WidgetNode* dn = widget.find(id);
                if (!dn || dn->type == WT_CANVAS || widgetIsAligned(parentType(*dn))) continue;
                // a selected ancestor already carries this node along
                bool carried = false;
                for (int other : selection_) if (other != id && isAncestorOf(other, id)) { carried = true; break; }
                if (carried) continue;
                UIRect rc = nodeRect(*dn);
                dragStart_.push_back({ id, { in.mouseX - rc.x, in.mouseY - rc.y } });
            }
            if (!dragStart_.empty()) dragMove_ = true;
        }
        selNode = selected_ >= 0 ? widget.find(selected_) : nullptr;
        selFree = selNode && selNode->type != WT_CANVAS && !widgetIsAligned(parentType(*selNode));
    }
    if (marquee_) {
        float x0 = std::min(marqueeX0_, in.mouseX), x1 = std::max(marqueeX0_, in.mouseX);
        float y0 = std::min(marqueeY0_, in.mouseY), y1 = std::max(marqueeY0_, in.mouseY);
        selection_ = marqueeBase_;
        // any node whose rect meets the band joins the selection (Canvas roots
        // excluded: they fill the screen and would swallow every marquee)
        for (const auto& n : widget.nodes) {
            if (n.type == WT_CANVAS && !widget.find(n.parent)) continue;
            UIRect rc = nodeRect(n);
            if (rc.x + rc.w < x0 || rc.x > x1 || rc.y + rc.h < y0 || rc.y > y1) continue;
            if (!isSelected(n.id)) selection_.push_back(n.id);
        }
        selected_ = selection_.empty() ? -1 : selection_.back();
        selAnchor_ = selected_;
        r->setUIScissor(design.x, design.y, design.w, design.h, true);
        const Vec3 mc = { 0.30f, 0.62f, 0.99f };
        r->drawRectPx(x0, y0, x1 - x0, y1 - y0, mc, 0.12f);
        r->drawRectPx(x0, y0, x1 - x0, 1, mc, 0.9f); r->drawRectPx(x0, y1 - 1, x1 - x0, 1, mc, 0.9f);
        r->drawRectPx(x0, y0, 1, y1 - y0, mc, 0.9f); r->drawRectPx(x1 - 1, y0, 1, y1 - y0, mc, 0.9f);
        r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);
        if (!in.mouseDown) marquee_ = false;
        selNode = selected_ >= 0 ? widget.find(selected_) : nullptr;
        selFree = selNode && selNode->type != WT_CANVAS && !widgetIsAligned(parentType(*selNode));
    }
    if (dragMove_) {
        if (in.mouseDown) {
            for (const auto& d : dragStart_) {
                WidgetNode* mn = widget.find(d.first);
                if (!mn) continue;
                UIRect self = nodeRect(*mn);
                float dx = (in.mouseX - d.second.first - self.x) / scale;
                float dy = (in.mouseY - d.second.second - self.y) / scale;
                int pt = parentType(*mn);
                if (pt == WT_CANVAS || pt < 0) {
                    // slide the whole box: on a stretched axis both offsets have to
                    // move, otherwise dragging would resize the widget instead
                    UIRect prc = parentBox(*mn);
                    float pw = prc.w / scale, ph = prc.h / scale;
                    float bx, by, bw, bh;
                    widgetSlotToBox(mn->anchor, pw, ph, mn->x, mn->y, mn->w, mn->h, bx, by, bw, bh);
                    widgetBoxToSlot(mn->anchor, pw, ph, bx + dx, by + dy, bw, bh, mn->x, mn->y, mn->w, mn->h);
                } else {
                    mn->x += dx; mn->y += dy;
                }
                dirty = true;
            }
        }
        if (!in.mouseDown) { dragMove_ = false; dragStart_.clear(); }
    }
    if (dragResize_) {
        WidgetNode* rn = selected_ >= 0 ? widget.find(selected_) : nullptr;
        if (rn && in.mouseDown) {
            // Resize in parent-relative reference units so that stretched anchors
            // and the pivot keep working; then convert back to slot offsets.
            UIRect prc = parentBox(*rn);
            float pw = prc.w / scale, ph = prc.h / scale;
            int pt = parentType(*rn);
            bool canvasSlot = (pt == WT_CANVAS || pt < 0);
            UIRect rc = nodeRect(*rn);
            // current box in parent-relative reference units (pivot removed)
            float bx = (rc.x - prc.x) / scale, by = (rc.y - prc.y) / scale;
            float bw = rc.w / scale, bh = rc.h / scale;
            float mx = (in.mouseX - prc.x) / scale, my = (in.mouseY - prc.y) / scale;
            const float MIN = 8;
            if (resizeEdges_ & 1) { float right = bx + bw; bx = std::min(mx, right - MIN); bw = right - bx; }
            if (resizeEdges_ & 2) { bw = std::max(MIN, mx - bx); }
            if (resizeEdges_ & 4) { float bottom = by + bh; by = std::min(my, bottom - MIN); bh = bottom - by; }
            if (resizeEdges_ & 8) { bh = std::max(MIN, my - by); }
            if (canvasSlot) {
                // put the pivot back before converting to slot offsets
                widgetBoxToSlot(rn->anchor, pw, ph, bx + bw * rn->pivotX, by + bh * rn->pivotY,
                                bw, bh, rn->x, rn->y, rn->w, rn->h);
            } else {
                rn->x = bx; rn->y = by; rn->w = bw; rn->h = bh;
            }
            dirty = true;
        }
        if (!in.mouseDown) dragResize_ = false;
    }
    // Canc removes every selected node — but not while a field owns the keyboard
    if (!selection_.empty() && in.keyDelete && !ui.wantKeyboard()) {
        std::vector<int> doomed = selection_;
        for (int id : doomed) widget.removeNode(id);
        selectOnly(-1);
        dirty = true;
    }
    // ── Ctrl+C / Ctrl+V / Ctrl+D on the whole selection ──
    // A paste lands inside the selected container, else beside the selected
    // component, else at the top level — the same rule Unreal uses.
    if (!ui.wantKeyboard()) {
        if (in.keyCopy && !selection_.empty()) copySelection();
        if (in.keyPaste && !clipboard_.empty()) {
            const WidgetNode* target = selected_ >= 0 ? widget.find(selected_) : nullptr;
            int parent = !target ? 0 : widgetIsContainer(target->type) ? target->id : target->parent;
            pasteClipboard(parent, true);
        }
        if (in.keyDuplicate) duplicateSelection();
    }

    r->setUIScissor(0, 0, 0, 0, false);

    // ── palette panel (top-left): drag a component onto the design to insert it ──
    r->drawRectPx(palette.x, palette.y, palette.w, palette.h, { 0.11f, 0.12f, 0.145f }, 0.98f);
    r->drawRectPx(palette.x, palette.y, palette.w, 1, { 0.30f, 0.62f, 0.99f }, 0.7f);
    r->drawTextLine(palette.x + 10, palette.y + 8, "COMPONENTS", { 0.55f, 0.62f, 0.72f }, 1);
    UIRect palBody = { palette.x + 1, palette.y + 24, palette.w - 2, palette.h - 26 };
    bool overPalette = !ui.interactionBlocked() && in.mouseX >= palBody.x && in.mouseX < palBody.x + palBody.w &&
                       in.mouseY >= palBody.y && in.mouseY < palBody.y + palBody.h;
    if (overPalette && in.wheel != 0) palScroll_ -= in.wheel * 34;
    r->setUIScissor(palBody.x, palBody.y, palBody.w, palBody.h, true);
    float py = palBody.y + 4 - palScroll_;
    const float startY = py;
    for (int cat = 0; cat < WC_COUNT; cat++) {
        // category header: click to collapse/expand
        UIRect hrow = { palette.x + 2, py, palette.w - 4, 20 };
        bool overH = overPalette && in.mouseX >= hrow.x && in.mouseX < hrow.x + hrow.w &&
                     in.mouseY >= hrow.y && in.mouseY < hrow.y + hrow.h;
        r->drawRectPx(hrow.x, hrow.y, hrow.w, hrow.h, overH ? Vec3{ 0.20f, 0.22f, 0.27f } : Vec3{ 0.155f, 0.165f, 0.20f }, 1);
        float tx = hrow.x + 8, ty = hrow.y + hrow.h * 0.5f;
        if (palOpen_[cat]) r->drawTriPx(tx - 4, ty - 3, tx + 4, ty - 3, tx, ty + 3, { 0.62f, 0.68f, 0.78f }, 1);
        else               r->drawTriPx(tx - 3, ty - 4, tx + 3, ty, tx - 3, ty + 4, { 0.62f, 0.68f, 0.78f }, 1);
        r->drawTextLine(hrow.x + 20, hrow.y + 3, widgetCategoryName(cat), { 0.62f, 0.68f, 0.78f }, 1);
        if (overH && in.mousePressed) palOpen_[cat] = !palOpen_[cat];
        py += 22;
        if (!palOpen_[cat]) continue;
        for (int type = 0; type < WT_TYPE_COUNT; type++) {
            if (type == WT_PANEL) continue;                 // legacy: not offered in the palette
            if (widgetTypeCategory(type) != cat) continue;
            UIRect row = { palette.x + 4, py, palette.w - 8, 20 };
            bool over = overPalette && in.mouseX >= row.x && in.mouseX < row.x + row.w &&
                        in.mouseY >= row.y && in.mouseY < row.y + row.h;
            if (over) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.18f, 0.22f, 0.30f }, 1);
            r->drawTextLine(row.x + 18, row.y + 3, ui.ellipsize(widgetTypeName(type), row.w - 26), { 0.85f, 0.89f, 0.95f }, 1);
            if (over && in.mousePressed) { dragNewType_ = type; dragNewX_ = in.mouseX; dragNewY_ = in.mouseY; dragNewActive_ = false; }
            py += 20;
        }
    }
    r->setUIScissor(0, 0, 0, 0, false);
    float palContent = py - startY;
    palScroll_ = std::min(std::max(0.0f, palScroll_), std::max(0.0f, palContent - palBody.h + 8));
    ui.drawScrollbar(palBody, palScroll_, palContent);

    // arm the palette drag; the drop itself is resolved after the hierarchy has
    // had a chance to publish its drop target for this frame
    if (dragNewType_ >= 0 && in.mouseDown && !dragNewActive_ &&
        (fabsf(in.mouseX - dragNewX_) > 6 || fabsf(in.mouseY - dragNewY_) > 6))
        dragNewActive_ = true;

    // ── hierarchy panel (bottom-left): components already inserted ──
    hierDropTarget_ = -2;   // recomputed by the rows below while a drag is live
    r->drawRectPx(hier.x, hier.y, hier.w, hier.h, { 0.11f, 0.12f, 0.145f }, 0.98f);
    r->drawRectPx(hier.x, hier.y, hier.w, 1, { 0.30f, 0.62f, 0.99f }, 0.7f);
    r->drawTextLine(hier.x + 10, hier.y + 8, "HIERARCHY", { 0.55f, 0.62f, 0.72f }, 1);
    UIRect hierBody = { hier.x + 1, hier.y + 24, hier.w - 2, hier.h - 26 };
    bool overHier = !ui.interactionBlocked() && in.mouseX >= hierBody.x && in.mouseX < hierBody.x + hierBody.w &&
                    in.mouseY >= hierBody.y && in.mouseY < hierBody.y + hierBody.h;
    if (overHier && in.wheel != 0) hierScroll_ -= in.wheel * 34;
    r->setUIScissor(hierBody.x, hierBody.y, hierBody.w, hierBody.h, true);
    float hy = hierBody.y + 4 - hierScroll_;
    const float hStart = hy;
    rowOrder_.clear();
    {   // root row = the widget asset itself; everything the user adds hangs under it
        UIRect row = { hier.x + 4, hy, hier.w - 8, 20 };
        bool over = overHier && in.mouseX >= row.x && in.mouseX < row.x + row.w &&
                    in.mouseY >= row.y && in.mouseY < row.y + row.h;
        bool rootSel = selection_.empty() && selected_ < 0;
        if (rootSel) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.20f, 0.34f, 0.52f }, 1);
        else if (over) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.18f, 0.20f, 0.25f }, 1);
        r->drawTextLine(row.x + 8, row.y + 3, ui.ellipsize("[ " + widgetAssetName() + " ]", row.w - 16),
                        { 0.72f, 0.84f, 0.98f }, 1);
        // the root row is a valid drop target too: it re-parents to the screen
        if (over && (dragNewType_ >= 0 || hierDragActive_)) {
            hierDropTarget_ = 0;
            r->drawRectPx(row.x, row.y, 3, row.h, { 0.35f, 0.85f, 0.5f }, 1);
        }
        if (over && in.mousePressed) selectOnly(-1);
        hy += 20;
    }
    for (const auto& n : widget.nodes)                 // top-level = parent is not a real node
        if (!widget.find(n.parent)) drawHierarchyRow(ui, n.id, 1, hy, hierBody);
    r->setUIScissor(0, 0, 0, 0, false);
    if (pendingRange_ >= 0) { selectRangeTo(pendingRange_, pendingRangeAdd_); pendingRange_ = -1; }
    hierScroll_ = std::min(std::max(0.0f, hierScroll_), std::max(0.0f, (hy - hStart) - hierBody.h + 8));
    ui.drawScrollbar(hierBody, hierScroll_, hy - hStart);

    // ── hierarchy drag & drop ──
    // promote a pressed row to a re-parent drag once the cursor actually moves
    if (hierDragNode_ >= 0 && !hierDragActive_ && in.mouseDown &&
        (fabsf(in.mouseX - hierDragX_) > 5 || fabsf(in.mouseY - hierDragY_) > 5))
        hierDragActive_ = true;
    if (hierDragActive_) {
        // ghost label following the cursor
        const WidgetNode* dn = widget.find(hierDragNode_);
        if (dn) {
            r->setUIScissor(0, 0, 0, 0, false);
            std::string lbl = std::string(widgetTypeName(dn->type)) + "  " + dn->name;
            float tw = r->textWidth(lbl) + 18;
            bool ok = hierDropTarget_ > -2;
            r->drawRectPx(in.mouseX + 12, in.mouseY + 8, tw, 20, ok ? Vec3{ 0.12f, 0.34f, 0.24f } : Vec3{ 0.1f, 0.11f, 0.13f }, 0.95f);
            r->drawTextLine(in.mouseX + 20, in.mouseY + 10, lbl, { 0.85f, 0.95f, 0.9f }, 1);
        }
    }
    if (in.mouseReleased) {
        if (hierDragActive_ && hierDropTarget_ > -2 && hierDragNode_ >= 0) {
            WidgetNode* moved = widget.find(hierDragNode_);
            if (moved && hierDropTarget_ != hierDragNode_ && !isAncestorOf(hierDragNode_, hierDropTarget_)) {
                moved->parent = hierDropTarget_;
                dirty = true;
            }
        }
        hierDragNode_ = -1;
        hierDragActive_ = false;
    }

    // ── resolve a palette drop (hierarchy target wins over the design surface) ──
    if (dragNewType_ >= 0 && in.mouseReleased) {
        if (dragNewActive_ && hierDropTarget_ > -2) {
            // dropped on a hierarchy row: the user picked the exact parent
            int parentId = hierDropTarget_;
            const WidgetNode* pnode = widget.find(parentId);
            int newId = widget.addNode(dragNewType_, parentId);
            if (WidgetNode* nn = widget.find(newId)) {
                if (pnode && widgetIsAligned(pnode->type)) { nn->hAlign = WA_CENTER; nn->vAlign = WA_CENTER; }
                else { nn->x = 0; nn->y = 0; }
            }
            selectOnly(newId);
            dirty = true;
        } else if (dragNewActive_ && overDesign) {
            int parentId = deepestContainerAt(in.mouseX, in.mouseY, screen, scale);
            const WidgetNode* pnode = widget.find(parentId);
            int newId = widget.addNode(dragNewType_, parentId);
            if (WidgetNode* nn = widget.find(newId)) {
                if (pnode && widgetIsAligned(pnode->type)) { nn->hAlign = WA_CENTER; nn->vAlign = WA_CENTER; }
                else {
                    // drop where the cursor is, even outside the widget rect
                    UIRect prc = parentBox(*nn);
                    nn->x = (in.mouseX - prc.x) / scale - nn->w * 0.5f;
                    nn->y = (in.mouseY - prc.y) / scale - nn->h * 0.5f;
                }
            }
            selectOnly(newId);
            dirty = true;
        }
        dragNewType_ = -1; dragNewActive_ = false;
    }

    // ── splitters (drag the borders to resize the panels) ──
    {
        float x = canvas.x + HW;
        bool over = !ui.interactionBlocked() && in.mouseX >= x - 3 && in.mouseX <= x + 6 &&
                    in.mouseY >= canvas.y && in.mouseY < canvas.y + canvas.h;
        if (over || dragSplit_ == 1) r->drawRectPx(x + 2, canvas.y, 2, canvas.h, { 0.30f, 0.62f, 0.99f }, 0.7f);
        if (over && in.mousePressed) dragSplit_ = 1;
    }
    {
        float x = det.x;
        bool over = !ui.interactionBlocked() && in.mouseX >= x - 6 && in.mouseX <= x + 3 &&
                    in.mouseY >= canvas.y && in.mouseY < canvas.y + canvas.h;
        if (over || dragSplit_ == 2) r->drawRectPx(x - 3, canvas.y, 2, canvas.h, { 0.30f, 0.62f, 0.99f }, 0.7f);
        if (over && in.mousePressed) dragSplit_ = 2;
    }
    {
        float y = canvas.y + palH;
        bool over = !ui.interactionBlocked() && in.mouseX >= canvas.x && in.mouseX < canvas.x + HW &&
                    in.mouseY >= y && in.mouseY <= y + 6;
        if (over || dragSplit_ == 3) r->drawRectPx(canvas.x, y + 2, HW, 2, { 0.30f, 0.62f, 0.99f }, 0.7f);
        if (over && in.mousePressed) dragSplit_ = 3;
    }
    if (dragSplit_ == 1) { leftW_ = in.mouseX - canvas.x; if (!in.mouseDown) dragSplit_ = 0; }
    if (dragSplit_ == 2) { rightW_ = canvas.x + canvas.w - in.mouseX; if (!in.mouseDown) dragSplit_ = 0; }
    if (dragSplit_ == 3) { paletteFrac_ = (in.mouseY - canvas.y) / std::max(1.0f, canvas.h - 6); if (!in.mouseDown) dragSplit_ = 0; }

    // ── details panel ──
    r->drawRectPx(det.x, det.y, det.w, det.h, { 0.11f, 0.12f, 0.145f }, 0.98f);
    r->drawRectPx(det.x, det.y, det.w, 1, { 0.30f, 0.62f, 0.99f }, 0.7f);
    r->drawTextLine(det.x + 10, det.y + 8, "DETAILS", { 0.55f, 0.62f, 0.72f }, 1);
    WidgetNode* sn = selected_ >= 0 ? widget.find(selected_) : nullptr;
    // The Details have grown past a screenful, so the body scrolls; the Delete
    // button stays pinned below it.
    UIRect detBody = { det.x + 1, det.y + 28, det.w - 2, det.h - 28 - (sn ? 40.0f : 4.0f) };
    bool overDet = !ui.interactionBlocked() && in.mouseX >= detBody.x && in.mouseX < detBody.x + detBody.w &&
                   in.mouseY >= detBody.y && in.mouseY < detBody.y + detBody.h;
    if (overDet && in.wheel != 0) { detScroll_ -= in.wheel * 34; ui.consumeWheel(); }
    r->setUIScissor(detBody.x, detBody.y, detBody.w, detBody.h, true);
    float dy = detBody.y + 4 - detScroll_;
    const float detTop = dy;
    // the editor's standard check box (UI::checkbox), drawn at an explicit rect
    // because this panel lays itself out
    auto checkBox = [&](const char* lbl, bool value, const UIRect& rc) {
        bool over = !ui.interactionBlocked() && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
                    in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
        const float box = 15;
        r->drawRectPx(rc.x, rc.y + 3, box, box, over ? Vec3{ 0.22f, 0.24f, 0.28f } : Vec3{ 0.17f, 0.185f, 0.215f }, 1);
        if (value) r->drawRectPx(rc.x + 3, rc.y + 6, box - 6, box - 6, { 0.30f, 0.62f, 0.99f }, 1);
        r->drawTextLine(rc.x + box + 8, rc.y + 3, lbl, { 0.85f, 0.88f, 0.93f }, 1);
        return over && in.mousePressed;
    };
    if (!sn) {
        // nothing selected = the widget asset itself: resolution and designer grid
        r->drawTextLine(det.x + 12, dy, widgetAssetName().c_str(), { 0.9f, 0.92f, 0.97f }, 1); dy += 24;
        r->drawTextLine(det.x + 12, dy, "REFERENCE RESOLUTION", { 0.5f, 0.55f, 0.62f }, 1); dy += 18;
        if (ui.numberFieldRect("wrefw", { det.x + 12, dy, det.w - 24, 22 }, &widget.refW, 10, "Width", true, 64, 8192)) dirty = true;
        dy += 26;
        if (ui.numberFieldRect("wrefh", { det.x + 12, dy, det.w - 24, 22 }, &widget.refH, 10, "Height", true, 64, 8192)) dirty = true;
        dy += 32;
        r->drawTextLine(det.x + 12, dy, "GRID", { 0.5f, 0.55f, 0.62f }, 1); dy += 18;
        if (checkBox("Show grid", showGrid_, { det.x + 12, dy, det.w - 24, 22 })) showGrid_ = !showGrid_;
        dy += 26;
        ui.numberFieldRect("wgrid", { det.x + 12, dy, det.w - 24, 22 }, &gridStep_, 2, "Grid step", true, 2, 512);
        dy += 32;
        r->drawTextLine(det.x + 12, dy, "Select a component to", { 0.5f, 0.55f, 0.62f }, 1); dy += 16;
        r->drawTextLine(det.x + 12, dy, "edit its properties.", { 0.5f, 0.55f, 0.62f }, 1);
    }
    else {
        // ── uniform metrics: every row is ROW_H tall and every row advances by
        // ROW_H + GAP, so captions can never overlap the row above ──
        const float ROW_H = 22, GAP = 6, LBL_H = 16, SEC_GAP = 10;
        const float RX = det.x + 12, RW = det.w - 24;
        bool aligned = widgetIsAligned(parentType(*sn));

        auto section = [&](const char* title) {
            dy += SEC_GAP;
            r->drawTextLine(RX, dy, title, { 0.5f, 0.55f, 0.62f }, 1);
            dy += LBL_H + 2;
        };
        auto numRow = [&](const char* lbl, const char* fid, float* v, float step) {
            if (ui.numberFieldRect(fid, { RX, dy, RW, ROW_H }, v, step, lbl)) dirty = true;
            dy += ROW_H + GAP;
        };
        auto num2Row = [&](const char* la, const char* ia, float* va,
                           const char* lb, const char* ib, float* vb, float step) {
            float half = (RW - GAP) * 0.5f;
            if (ui.numberFieldRect(ia, { RX, dy, half, ROW_H }, va, step, la)) dirty = true;
            if (ui.numberFieldRect(ib, { RX + half + GAP, dy, half, ROW_H }, vb, step, lb)) dirty = true;
            dy += ROW_H + GAP;
        };
        auto toggleRow = [&](const char* lbl, bool value, const char* onText, const char* offText) {
            UIRect rc = { RX, dy, RW, ROW_H };
            bool over = !ui.interactionBlocked() && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
                        in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
            r->drawRectPx(rc.x, rc.y, rc.w, rc.h, over ? Vec3{ 0.20f, 0.24f, 0.30f } : Vec3{ 0.15f, 0.16f, 0.20f }, 1);
            // check box on the left, caption after it
            Vec3 boxCol = value ? Vec3{ 0.30f, 0.62f, 0.99f } : Vec3{ 0.28f, 0.31f, 0.37f };
            r->drawRectPx(rc.x + 5, rc.y + 5, 12, 12, boxCol, 1);
            if (value) r->drawRectPx(rc.x + 8, rc.y + 8, 6, 6, { 0.06f, 0.09f, 0.13f }, 1);
            r->drawTextLine(rc.x + 24, rc.y + 4, lbl, { 0.85f, 0.89f, 0.95f }, 1);
            const char* state = value ? onText : offText;
            r->drawTextLine(rc.x + rc.w - r->textWidth(state) - 8, rc.y + 4, state, { 0.55f, 0.60f, 0.68f }, 1);
            dy += ROW_H + GAP;
            return over && in.mousePressed;
        };
        // Enum field: shows the current name and opens a drop-down, the same
        // control (and the same names) the graph's Set Alignment / Set Anchors
        // nodes offer.
        // Returns the newly picked index, or -1 when nothing changed. The choice
        // arrives on a later frame (the list stays open), so it is collected by
        // id — nothing has to outlive the frame that opened the drop-down.
        auto enumRow = [&](const char* lbl, const char* fid, int value,
                           const char* const* names, int count) {
            UIRect rc = { RX, dy, RW, ROW_H };
            bool over = !ui.interactionBlocked() && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
                        in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
            r->drawRectPx(rc.x, rc.y, rc.w, rc.h, over ? Vec3{ 0.20f, 0.24f, 0.30f } : Vec3{ 0.15f, 0.16f, 0.20f }, 1);
            r->drawTextLine(rc.x + 6, rc.y + 4, lbl, { 0.85f, 0.89f, 0.95f }, 1);
            const char* v = names[value < 0 || value >= count ? 0 : value];
            float ax = rc.x + rc.w - 12, ay = rc.y + rc.h * 0.5f;         // ▼
            r->drawTextLine(ax - 10 - r->textWidth(v), rc.y + 4, v, { 0.85f, 0.89f, 0.95f }, 1);
            r->drawTriPx(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3, { 0.62f, 0.68f, 0.78f }, 1);
            if (over && in.mousePressed) ui.openEnumPicker(fid, value, names, count, rc.x, rc.y + ROW_H + 2, rc.w);
            dy += ROW_H + GAP;
            int picked = -1;
            if (ui.takeEnumPick(fid, &picked) && picked != value) return picked;
            return -1;
        };
        auto alignRow = [&](const char* lbl, const char* fid, int* a, bool horizontal) {
            const char* names[4];
            for (int i = 0; i < 4; i++) names[i] = horizontal ? widgetHAlignName(i) : widgetVAlignName(i);
            int picked = enumRow(lbl, fid, *a, names, 4);
            if (picked >= 0) { *a = picked; dirty = true; }
        };
        auto textRow = [&](const char* label, const char* fid, char* buf, int cap) {
            r->drawTextLine(RX, dy, label, { 0.5f, 0.55f, 0.62f }, 1);
            dy += LBL_H;
            if (ui.textInputRect(fid, buf, cap, { RX, dy, RW, ROW_H + 2 })) dirty = true;
            dy += ROW_H + 2 + GAP;
        };
        auto colorRow = [&](const char* label, const char* pid, Vec3* rgb, float* alpha) {
            UIRect rc = { RX, dy, RW, ROW_H };
            r->drawRectPx(rc.x, rc.y, 38, rc.h, *rgb, 1);
            r->drawRectPx(rc.x, rc.y, 38, 1, { 0.35f, 0.4f, 0.48f }, 0.8f);
            bool over = !ui.interactionBlocked() && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
                        in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
            r->drawTextLine(rc.x + 46, rc.y + 4, ui.ellipsize(label, rc.w - 54),
                            over ? Vec3{ 0.95f, 0.97f, 1.0f } : Vec3{ 0.8f, 0.84f, 0.9f }, 1);
            if (over && in.mousePressed) ui.openColorPicker(pid, rgb, alpha, rc.x, rc.y + ROW_H + 4);
            dy += ROW_H + GAP;
        };
        // Size Box style row: a switch decides whether the value counts at all.
        // Unchecked leaves the field greyed out and the box keeps its own size.
        auto overrideRow = [&](const char* lbl, const char* fid, unsigned bit, float* v, float step) {
            UIRect box = { RX, dy, 18, ROW_H };
            bool on = (sn->sizeFlags & bit) != 0;
            bool over = !ui.interactionBlocked() && in.mouseX >= box.x && in.mouseX < box.x + box.w &&
                        in.mouseY >= box.y && in.mouseY < box.y + box.h;
            r->drawRectPx(box.x, box.y + 4, 14, 14, over ? Vec3{ 0.22f, 0.24f, 0.28f } : Vec3{ 0.17f, 0.185f, 0.215f }, 1);
            if (on) r->drawRectPx(box.x + 3, box.y + 7, 8, 8, { 0.30f, 0.62f, 0.99f }, 1);
            if (over && in.mousePressed) { sn->sizeFlags ^= bit; dirty = true; }
            UIRect field = { RX + 24, dy, RW - 24, ROW_H };
            if (on) { if (ui.numberFieldRect(fid, field, v, step, lbl)) dirty = true; }
            else {   // inert preview of the value it would use
                r->drawRectPx(field.x, field.y, field.w, field.h, { 0.115f, 0.12f, 0.135f }, 1);
                r->drawTextLine(field.x + 6, field.y + 4, lbl, { 0.38f, 0.41f, 0.46f }, 1);
                char b[32]; snprintf(b, sizeof(b), "%g", *v);
                r->drawTextLine(field.x + field.w - r->textWidth(b) - 8, field.y + 4, b, { 0.38f, 0.41f, 0.46f }, 1);
            }
            dy += ROW_H + GAP;
        };

        // ── identity first (Unreal puts the name and Is Variable at the top) ──
        r->drawTextLine(RX, dy, widgetTypeName(sn->type), { 0.9f, 0.92f, 0.97f }, 1);
        if (selection_.size() > 1) {
            char extra[32]; snprintf(extra, sizeof(extra), "+%d selected", (int)selection_.size() - 1);
            r->drawTextLine(RX + RW - r->textWidth(extra), dy, extra, { 0.55f, 0.72f, 0.95f }, 1);
        }
        dy += ROW_H;
        textRow("Name", "wname", sn->name, sizeof(sn->name));
        if (toggleRow("Is Variable", sn->isVariable, "exposed", "")) { sn->isVariable = !sn->isVariable; dirty = true; }

        // ── the slot: which layout properties exist is the PARENT's call ──
        // A component in a Canvas gets anchors, offsets and a pivot; the same
        // component in a Vertical Box gets alignment instead. The section is
        // named after the slot so the rule is visible, not folklore.
        int ptype = parentType(*sn);
        int slot = widgetSlotKind(ptype);
        bool canvasChild = slot == WSLOT_CANVAS && sn->type != WT_CANVAS;
        if (sn->type != WT_CANVAS) {
            // Unreal's wording: "Slot (Size Box Slot)" — the properties belong to
            // the slot the parent hands out, so its name is the header.
            std::string slotTitle = "SLOT (" + std::string(widgetSlotName(ptype)) + ")";
            for (char& c : slotTitle) c = (char)toupper((unsigned char)c);
            section(slotTitle.c_str());
            if (canvasChild) {
                // Canvas slot: a 4x4 anchor grid decides which side/corner the
                // widget is pinned to (or which axis it stretches along)
                float aMinX, aMinY, aMaxX, aMaxY;
                widgetAnchorRange(sn->anchor, aMinX, aMinY, aMaxX, aMaxY);
                bool stretchX = aMinX != aMaxX, stretchY = aMinY != aMaxY;
                // preset drop-down over the grid — same 16 names as Set Anchors
                {
                    const char* anchorNames[WANCH_COUNT];
                    for (int i = 0; i < WANCH_COUNT; i++) anchorNames[i] = widgetAnchorName(i);
                    int picked = enumRow("Anchors", "wanch", sn->anchor, anchorNames, WANCH_COUNT);
                    if (picked >= 0) {            // re-anchor without moving (see below)
                        UIRect prc = parentBox(*sn);
                        float pw = prc.w / scale, ph = prc.h / scale, bx, by, bw, bh;
                        widgetSlotToBox(sn->anchor, pw, ph, sn->x, sn->y, sn->w, sn->h, bx, by, bw, bh);
                        sn->anchor = picked;
                        widgetBoxToSlot(picked, pw, ph, bx, by, bw, bh, sn->x, sn->y, sn->w, sn->h);
                        dirty = true;
                    }
                }
                // the grid itself: 4x4 cells, each drawing the region it pins to
                const float cell = std::min(26.0f, (RW - 3 * 4) / 4.0f);
                for (int i = 0; i < WANCH_COUNT; i++) {
                    float cx = RX + (i % 4) * (cell + 4), cy = dy + (i / 4) * (cell + 4);
                    UIRect c = { cx, cy, cell, cell };
                    bool over = !ui.interactionBlocked() && in.mouseX >= c.x && in.mouseX < c.x + c.w &&
                                in.mouseY >= c.y && in.mouseY < c.y + c.h;
                    bool cur = sn->anchor == i;
                    r->drawRectPx(c.x, c.y, c.w, c.h, cur ? Vec3{ 0.18f, 0.30f, 0.46f }
                                                  : over ? Vec3{ 0.20f, 0.23f, 0.28f } : Vec3{ 0.13f, 0.14f, 0.17f }, 1);
                    if (cur) { Vec3 b = { 0.30f, 0.62f, 0.99f };
                        r->drawRectPx(c.x, c.y, c.w, 1, b, 1); r->drawRectPx(c.x, c.y + c.h - 1, c.w, 1, b, 1);
                        r->drawRectPx(c.x, c.y, 1, c.h, b, 1); r->drawRectPx(c.x + c.w - 1, c.y, 1, c.h, b, 1); }
                    float nx, ny, xx, xy;
                    widgetAnchorRange(i, nx, ny, xx, xy);
                    const float pad = 4, inner = cell - pad * 2;
                    float mw = (nx == xx) ? inner * 0.34f : inner;
                    float mh = (ny == xy) ? inner * 0.34f : inner;
                    float mx = c.x + pad + (inner - mw) * nx;
                    float my = c.y + pad + (inner - mh) * ny;
                    r->drawRectPx(mx, my, mw, mh, cur ? Vec3{ 0.55f, 0.80f, 1.0f } : Vec3{ 0.45f, 0.50f, 0.60f }, 1);
                    if (over && in.mousePressed && sn->anchor != i) {
                        // Re-anchor without moving: convert the current slot to a
                        // parent-relative box, then back through the new anchor.
                        // Measured against the parent's laid-out content box —
                        // the same one widgetNodeRect uses, so nothing shifts.
                        UIRect prc = parentBox(*sn);
                        float pw = prc.w / scale, ph = prc.h / scale;
                        float bx, by, bw, bh;
                        widgetSlotToBox(sn->anchor, pw, ph, sn->x, sn->y, sn->w, sn->h, bx, by, bw, bh);
                        sn->anchor = i;
                        widgetBoxToSlot(i, pw, ph, bx, by, bw, bh, sn->x, sn->y, sn->w, sn->h);
                        dirty = true;
                    }
                }
                dy += 4 * (cell + 4) + GAP;
                // offsets: position on a pinned axis, insets on a stretched one
                num2Row(stretchX ? "Off L" : "X", "wx", &sn->x,
                        stretchY ? "Off T" : "Y", "wy", &sn->y, 5);
                num2Row(stretchX ? "Off R" : "W", "ww", &sn->w,
                        stretchY ? "Off B" : "H", "wh", &sn->h, 5);
                // In a Canvas slot the pivot IS Unreal's "Alignment": the 0..1
                // origin inside the widget that the offsets are measured from.
                if (ui.numberFieldRect("wpvx", { RX, dy, (RW - GAP) * 0.5f, ROW_H }, &sn->pivotX, 0.1f, "Align X", false, 0, 1)) dirty = true;
                if (ui.numberFieldRect("wpvy", { RX + (RW - GAP) * 0.5f + GAP, dy, (RW - GAP) * 0.5f, ROW_H }, &sn->pivotY, 0.1f, "Align Y", false, 0, 1)) dirty = true;
                dy += ROW_H + GAP;
                r->drawTextLine(RX, dy, "0..1: the point X/Y measure from",
                                { 0.45f, 0.49f, 0.56f }, 1, 0.9f);
                dy += LBL_H;
            } else if (aligned) {
                num2Row("W", "ww", &sn->w, "H", "wh", &sn->h, 5);
                // Slot Size, Unreal's segmented Auto | Fill + weight. Only a
                // stacking box has an axis to share, so only it shows the row.
                if (widgetStacksChildren(ptype)) {
                    bool vertAxis = ptype == WT_VBOX || ptype == WT_SCROLLBOX ||
                                    (ptype == WT_STACKBOX && widget.find(sn->parent) &&
                                     widget.find(sn->parent)->value < 0.5f);
                    UIRect rc = { RX, dy, RW, ROW_H };
                    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.15f, 0.16f, 0.20f }, 1);
                    r->drawTextLine(rc.x + 6, rc.y + 4, "Size", { 0.85f, 0.89f, 0.95f }, 1);
                    bool fills = sn->sizeRule == WSR_FILL;
                    const float segW = 42, wgtW = 46;
                    float sx0 = rc.x + rc.w - wgtW - GAP - segW * 2 - 2;
                    for (int i = 0; i < 2; i++) {
                        UIRect seg = { sx0 + i * (segW + 2), rc.y + 2, segW, rc.h - 4 };
                        bool on = (i == 1) == fills;
                        bool over = !ui.interactionBlocked() && in.mouseX >= seg.x && in.mouseX < seg.x + seg.w &&
                                    in.mouseY >= seg.y && in.mouseY < seg.y + seg.h;
                        r->drawRectPx(seg.x, seg.y, seg.w, seg.h, on ? Vec3{ 0.12f, 0.32f, 0.56f }
                                                          : over ? Vec3{ 0.22f, 0.25f, 0.31f } : Vec3{ 0.115f, 0.125f, 0.15f }, 1);
                        const char* lbl = widgetSizeRuleName(i);
                        r->drawTextLine(seg.x + (seg.w - r->textWidth(lbl)) * 0.5f, seg.y + 3, lbl,
                                        on ? Vec3{ 0.85f, 0.93f, 1.0f } : Vec3{ 0.68f, 0.73f, 0.81f }, 1);
                        if (over && in.mousePressed && sn->sizeRule != i) { sn->sizeRule = i; dirty = true; }
                    }
                    UIRect wgt = { rc.x + rc.w - wgtW, rc.y, wgtW, rc.h };
                    if (fills) { if (ui.numberFieldRect("wfw", wgt, &sn->fillWeight, 0.1f, nullptr, false, 0, 100)) dirty = true; }
                    dy += ROW_H + GAP;
                    // spell out which axis Size owns, so the alignment rows below
                    // are not mistaken for a way to stretch along it
                    r->drawTextLine(RX, dy, fills ? "Fill: shares the room by weight"
                                                  : (vertAxis ? "Auto: keeps its own height"
                                                              : "Auto: keeps its own width"),
                                    { 0.45f, 0.49f, 0.56f }, 1, 0.9f);
                    dy += LBL_H;
                }
                alignRow("H Align", "whal", &sn->hAlign, true);
                alignRow("V Align", "wval", &sn->vAlign, false);
                // slot padding: the margin the parent leaves around this child.
                // In a stacking box it is also what spaces the children apart.
                num2Row("Pad L", "wsl", &sn->slotL, "Pad T", "wst", &sn->slotT, 2);
                num2Row("Pad R", "wsr", &sn->slotR, "Pad B", "wsb", &sn->slotB, 2);
            } else {
                num2Row("X", "wx", &sn->x, "Y", "wy", &sn->y, 5);
                num2Row("W", "ww", &sn->w, "H", "wh", &sn->h, 5);
            }
        }

        // ── what this component does with the children it holds ──
        if (sn->type == WT_SIZEBOX) {
            section("CHILD LAYOUT");
            overrideRow("Width Override", "wsbw", WSF_W, &sn->w, 5);
            overrideRow("Height Override", "wsbh", WSF_H, &sn->h, 5);
            overrideRow("Min Desired Width", "wsbmnw", WSF_MIN_W, &sn->minW, 5);
            overrideRow("Min Desired Height", "wsbmnh", WSF_MIN_H, &sn->minH, 5);
            overrideRow("Max Desired Width", "wsbmxw", WSF_MAX_W, &sn->maxW, 5);
            overrideRow("Max Desired Height", "wsbmxh", WSF_MAX_H, &sn->maxH, 5);
        }
        if (sn->type == WT_STACKBOX) {
            section("CHILD LAYOUT");
            static const char* ORIENT[2] = { "Vertical", "Horizontal" };
            int picked = enumRow("Orientation", "wsbo", sn->value < 0.5f ? 0 : 1, ORIENT, 2);
            if (picked >= 0) { sn->value = (float)picked; dirty = true; }
        }
        if (widgetIsContainer(sn->type)) {
            section("CONTENT PADDING");   // the container's own inset, not a slot
            num2Row("Left", "wpl", &sn->padL, "Top", "wpt", &sn->padT, 2);
            num2Row("Right", "wpr", &sn->padR, "Bottom", "wpb", &sn->padB, 2);
        }

        // ── per-type appearance ──
        section("APPEARANCE");
        if (sn->type == WT_TEXT || sn->type == WT_RICHTEXT || sn->type == WT_EDITABLETEXT ||
            sn->type == WT_MULTILINETEXT || sn->type == WT_COMBOBOX) {
            textRow("Text", "wtext", sn->text, sizeof(sn->text));
            colorRow("Text color", "wtcol", &sn->textColor, nullptr);
            numRow("Font scale", "wfs", &sn->fontScale, 0.05f);
            // the text's own alignment inside its box, independent of the slot
            static const char* JUST[3] = { "Left", "Center", "Right" };
            int picked = enumRow("Justification", "wjust", sn->justify, JUST, 3);
            if (picked >= 0) { sn->justify = picked; dirty = true; }
            if (toggleRow("Auto Wrap", sn->wrap, "wraps", "one line")) { sn->wrap = !sn->wrap; dirty = true; }
        }
        if (sn->type == WT_IMAGE) {
            textRow("Image (.png rel.)", "wimg", sn->image, sizeof(sn->image));
            colorRow("Tint", "wfill", &sn->color, &sn->alpha);   // tints the texture too
        }
        if (sn->type == WT_PROGRESSBAR || sn->type == WT_SLIDER) {
            // The bar holds a raw value inside a range the user chooses; the fill
            // is where that value sits between Min and Max. Min 0 / Max 1 keeps
            // the value reading as a plain 0..1 percentage.
            numRow("Value", "wval", &sn->value, 0.05f);
            num2Row("Min", "wvmin", &sn->minValue, "Max", "wvmax", &sn->maxValue, 0.5f);
            char fill[48];
            snprintf(fill, sizeof(fill), "fill %.0f%%", widgetFillFraction(*sn) * 100.0f);
            r->drawTextLine(RX, dy, fill, { 0.45f, 0.49f, 0.56f }, 1, 0.9f);
            dy += LBL_H;
            if (sn->maxValue <= sn->minValue) {
                r->drawTextLine(RX, dy, "Max must be above Min", { 0.92f, 0.58f, 0.28f }, 1, 0.9f);
                dy += LBL_H;
            }
            colorRow("Fill color", "wfill", &sn->color, &sn->alpha);
        }
        if (sn->type == WT_CHECKBOX) {
            if (toggleRow("Checked", sn->value > 0.5f, "on", "off")) {
                sn->value = sn->value > 0.5f ? 0.0f : 1.0f;
                dirty = true;
            }
            colorRow("Check color", "wfill", &sn->color, &sn->alpha);
        }
        if (sn->type == WT_SPINBOX) numRow("Value", "wval", &sn->value, 1);
        if (sn->type == WT_GRIDPANEL || sn->type == WT_UNIFORMGRID) numRow("Cells", "wcells", &sn->value, 1);
        if (sn->type == WT_SAFEZONE) numRow("Inset", "wval", &sn->value, 2);
        if (sn->type == WT_WIDGETSWITCHER) numRow("Active index", "wval", &sn->value, 1);
        // only Border/Button-style widgets own a background; layout panels do not
        if (widgetHasBackground(sn->type) && sn->type != WT_PROGRESSBAR && sn->type != WT_SLIDER &&
            sn->type != WT_CHECKBOX && sn->type != WT_EDITABLETEXT && sn->type != WT_MULTILINETEXT &&
            sn->type != WT_COMBOBOX)
            colorRow("Background", "wfill", &sn->color, &sn->alpha);

        // ── Render Transform: visual only, inherited by the children ──
        section("RENDER TRANSFORM");
        num2Row("Trans X", "wtrx", &sn->transX, "Trans Y", "wtry", &sn->transY, 1);
        num2Row("Scale X", "wscx", &sn->scaleX, "Scale Y", "wscy", &sn->scaleY, 0.05f);
        numRow("Angle (deg)", "wang", &sn->angle, 1);
        if (ui.numberFieldRect("wshx", { RX, dy, (RW - GAP) * 0.5f, ROW_H }, &sn->shearX, 0.05f, "Shear X", false, -1, 1)) dirty = true;
        if (ui.numberFieldRect("wshy", { RX + (RW - GAP) * 0.5f + GAP, dy, (RW - GAP) * 0.5f, ROW_H }, &sn->shearY, 0.05f, "Shear Y", false, -1, 1)) dirty = true;
        dy += ROW_H + GAP;
        r->drawTextLine(RX, dy, "shear -1..1 = -60..60 deg", { 0.45f, 0.49f, 0.56f }, 1, 0.9f);
        dy += LBL_H;

        // ── Rendering ──
        section("RENDERING");
        if (ui.numberFieldRect("wrop", { RX, dy, RW, ROW_H }, &sn->renderOpacity, 0.05f, "Render Opacity", false, 0, 1)) dirty = true;
        dy += ROW_H + GAP;
        // hidden components still show in the designer, but not in game
        if (toggleRow("Visibility", sn->visible, "visible", "hidden")) { sn->visible = !sn->visible; dirty = true; }

        // ── Behaviour ──
        section("BEHAVIOUR");
        if (toggleRow("Is Enabled", sn->enabled, "enabled", "disabled")) { sn->enabled = !sn->enabled; dirty = true; }
        textRow("Tool Tip Text", "wtip", sn->tooltip, sizeof(sn->tooltip));
    }
    // leave the scrolled body; the Delete button is pinned under it
    r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);
    detScroll_ = std::min(std::max(0.0f, detScroll_), std::max(0.0f, (dy - detTop) - detBody.h + 12));
    ui.drawScrollbar(detBody, detScroll_, dy - detTop);
    if (sn) {
        UIRect del = { det.x + 12, det.y + det.h - 34, det.w - 24, 24 };
        bool overDel = !ui.interactionBlocked() && in.mouseX >= del.x && in.mouseX < del.x + del.w &&
                       in.mouseY >= del.y && in.mouseY < del.y + del.h;
        r->drawRectPx(del.x, del.y, del.w, del.h, overDel ? Vec3{ 0.48f, 0.18f, 0.18f } : Vec3{ 0.35f, 0.14f, 0.14f }, 1);
        char dl[48];
        if (selection_.size() > 1) snprintf(dl, sizeof(dl), "Delete %d components", (int)selection_.size());
        else snprintf(dl, sizeof(dl), "Delete component");
        r->drawTextLine(del.x + del.w * 0.5f - r->textWidth(dl) * 0.5f, del.y + 5, dl, { 0.95f, 0.8f, 0.8f }, 1);
        if (in.mousePressed && overDel) {
            std::vector<int> doomed = selection_.empty() ? std::vector<int>{ selected_ } : selection_;
            for (int id : doomed) widget.removeNode(id);
            selectOnly(-1);
            dirty = true;
        }
    }

    // ── drag ghost for a palette drag-and-drop (drawn last, unclipped) ──
    if (dragNewActive_ && dragNewType_ >= 0) {
        r->setUIScissor(0, 0, 0, 0, false);
        const char* lbl = widgetTypeName(dragNewType_);
        float tw = r->textWidth(lbl);
        bool ok = in.mouseX >= screen.x && in.mouseX < screen.x + screen.w && in.mouseY >= screen.y && in.mouseY < screen.y + screen.h;
        r->drawRectPx(in.mouseX + 12, in.mouseY + 8, tw + 16, 20, ok ? Vec3{ 0.13f, 0.30f, 0.50f } : Vec3{ 0.1f, 0.11f, 0.13f }, 0.95f);
        r->drawTextLine(in.mouseX + 20, in.mouseY + 10, lbl, { 0.85f, 0.9f, 1.0f }, 1);
    }
}
