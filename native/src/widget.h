// ─── Pulse Engine UI widget system (UMG-style): a tree of widgets, a designer, HUD ───
#pragma once
#include "math.h"
#include "ui.h"
#include "blueprint.h"
#include <string>
#include <utility>
#include <vector>

enum WidgetType {
    WT_CANVAS = 0,   // free-positioning container (fills its slot; top-level canvas = screen)
    WT_PANEL,        // legacy filled rectangle (kept for serialization; not in the palette)
    WT_VBOX,         // container (visual panel; children positioned absolutely for now)
    WT_HBOX,
    WT_TEXT,
    WT_BUTTON,
    WT_IMAGE,
    // appended for serialization compatibility (types are stored as ints)
    WT_BORDER,       // filled rectangle with an outline
    WT_PROGRESSBAR,  // track + fill driven by `value` (0..1)
    WT_CHECKBOX,     // box, checked when `value` > 0.5
    WT_SLIDER,       // track + handle positioned by `value` (0..1)
    WT_OVERLAY,      // container: children placed by H/V alignment (no free move), layered
    WT_SIZEBOX,      // container: forces its child to the box size
    WT_SCALEBOX,     // container: scales its child to fit
    // appended (serialized as ints — always add before WT_TYPE_COUNT)
    WT_GRIDPANEL,    // container: free layout, drawn with grid guides
    WT_UNIFORMGRID,  // container: free layout, uniform cell guides
    WT_SCROLLBOX,    // container: clipped scrolling region
    WT_STACKBOX,     // container: stack (vertical/horizontal per `value`)
    WT_WRAPBOX,      // container: wrapping flow
    WT_SAFEZONE,     // container: inset safe area
    WT_WIDGETSWITCHER, // container: shows one child at a time (`value` = index)
    WT_RICHTEXT,     // text with markup-ish styling (rendered as text)
    WT_EDITABLETEXT, // single-line input field
    WT_MULTILINETEXT,// multi-line input field
    WT_SPINBOX,      // numeric stepper
    WT_COMBOBOX,     // drop-down
    WT_THROBBER,     // busy indicator
    WT_SPACER,       // invisible layout gap
    WT_TYPE_COUNT
};
const char* widgetTypeName(int type);
bool widgetIsContainer(int type);   // can hold children
bool widgetIsAligned(int parentType); // parent lays children out by alignment, not free x/y

// palette grouping (Unreal-style categories)
enum WidgetCategory { WC_PANEL = 0, WC_COMMON, WC_INPUT, WC_PRIMITIVE, WC_SPECIAL, WC_COUNT };
const char* widgetCategoryName(int category);
int widgetTypeCategory(int type);

enum WidgetAlign { WA_START = 0, WA_CENTER = 1, WA_END = 2, WA_FILL = 3 };

// Slot Size along a stacking box's axis (Unreal's Auto / Fill segmented control)
enum WidgetSizeRule { WSR_AUTO = 0, WSR_FILL = 1 };
const char* widgetSizeRuleName(int rule);
// The same four values read differently per axis, exactly like Unreal's
// EHorizontalAlignment / EVerticalAlignment. Both the designer drop-down and the
// graph's Set Alignment nodes label the enum through these.
const char* widgetHAlignName(int align);   // Left / Center / Right / Fill
const char* widgetVAlignName(int align);   // Top / Center / Bottom / Fill

// ─── slots (Unreal's model: the PARENT decides which layout properties a child
// has) ───
// A component dropped into a Canvas gets anchors, offsets and a pivot; the same
// component dropped into a Vertical Box gets alignment and padding instead. The
// properties belong to the slot between parent and child, never to the parent —
// so "the alignment of a Vertical Box" is really the alignment of its children.
enum WidgetSlotKind {
    WSLOT_CANVAS = 0,   // parent is a Canvas or the screen: anchor + offsets + pivot
    WSLOT_ALIGNED,      // parent lays its children out: H/V alignment
    WSLOT_FREE          // parent leaves children at their own x/y (Border, legacy Panel)
};
int widgetSlotKind(int parentType);          // parentType < 0 = the screen
const char* widgetSlotName(int parentType);  // "Canvas Slot", "Vertical Box Slot", …

// Canvas slot anchors, laid out as Unreal's 4x4 preset grid. Columns 0..2 pin
// left/centre/right, column 3 stretches horizontally; rows do the same
// vertically, so index 15 (bottom-right cell) is "fill the whole canvas".
enum WidgetAnchor {
    WANCH_TOP_LEFT = 0, WANCH_TOP_CENTER, WANCH_TOP_RIGHT, WANCH_TOP_STRETCH,
    WANCH_MID_LEFT,     WANCH_MID_CENTER, WANCH_MID_RIGHT, WANCH_MID_STRETCH,
    WANCH_BOT_LEFT,     WANCH_BOT_CENTER, WANCH_BOT_RIGHT, WANCH_BOT_STRETCH,
    WANCH_VSTRETCH_LEFT, WANCH_VSTRETCH_CENTER, WANCH_VSTRETCH_RIGHT, WANCH_FILL,
    WANCH_COUNT
};
// normalised anchor span inside the parent (min == max means a point anchor)
void widgetAnchorRange(int anchor, float& minX, float& minY, float& maxX, float& maxY);
// Canvas slot <-> parent-relative box, both in reference-resolution units.
// Re-anchoring converts through these so the widget does not visually move.
void widgetSlotToBox(int anchor, float parentW, float parentH,
                     float x, float y, float w, float h,
                     float& bx, float& by, float& bw, float& bh);
void widgetBoxToSlot(int anchor, float parentW, float parentH,
                     float bx, float by, float bw, float bh,
                     float& x, float& y, float& w, float& h);
const char* widgetAnchorName(int anchor);
// only a Border/Button-style widget paints a background the user can colour;
// pure layout containers are invisible chrome
bool widgetHasBackground(int type);
// the parent positions its children (so they expose H/V alignment instead of x/y)
bool widgetLaysOutChildren(int parentType);

struct WidgetNode {
    int id = 0;
    int parent = 0;               // parent node id (0 = canvas root, whose parent is -1)
    int type = WT_PANEL;
    char name[48] = "Widget";
    float x = 40, y = 40, w = 200, h = 60;   // relative to parent, in reference-resolution px
    Vec3 color = { 0.20f, 0.24f, 0.32f };
    float alpha = 1.0f;
    Vec3 textColor = { 0.95f, 0.97f, 1.0f };
    char text[128] = "Text";
    float fontScale = 1.2f;
    char image[192] = "";         // .png asset (relative to project)
    float value = 0.5f;           // progress bar / slider fraction, check-box state (>0.5)
    int hAlign = WA_FILL;         // used when the parent is an alignment container (Overlay)
    int vAlign = WA_FILL;
    bool isVariable = false;      // Unreal-style: expose this widget to the graph by name
    float padL = 0, padT = 0, padR = 0, padB = 0;   // inner padding applied to children
    int anchor = WANCH_TOP_LEFT;  // canvas-slot anchor (used when the parent is a Canvas/screen)
    float pivotX = 0, pivotY = 0; // 0..1 origin inside the widget that x/y refer to
    bool visible = true;          // hiding a node hides its whole subtree

    // ── slot padding: the margin around this component inside an aligned
    // parent. Belongs to the slot, so it is the child's field, not the box's
    // (padL..padB above is the container's own inner padding).
    float slotL = 0, slotT = 0, slotR = 0, slotB = 0;
    // Slot Size along a box's stacking axis. Auto keeps the component's own
    // width/height; Fill greedily shares the room the Auto siblings leave, split
    // between the Fill siblings in proportion to `fillWeight`. Alignment then
    // places the component inside whatever it ended up with.
    int sizeRule = WSR_AUTO;
    float fillWeight = 1.0f;

    // ── Render Transform: purely visual, applied about the component's centre
    // and inherited by its children. Layout and hit-testing ignore it, exactly
    // like Unreal's.
    float transX = 0, transY = 0;   // translation, in reference px
    float scaleX = 1, scaleY = 1;
    float angle = 0;                // degrees around the one visible axis
    float shearX = 0, shearY = 0;   // -1..1 == -60..60 degrees of slant

    // ── Rendering ──
    float renderOpacity = 1.0f;     // multiplies this component and its subtree

    // ── Behaviour ──
    bool enabled = true;            // greyed out and inert when off
    char tooltip[128] = "";         // shown while the pointer rests on it

    // ── Text ──
    int justify = 0;                // 0 Left, 1 Center, 2 Right (inside the box)
    bool wrap = false;              // break long lines at the component's width

    // ── Size Box: every override has its own switch; unchecked = ignored ──
    unsigned sizeFlags = 0;         // see WidgetSizeFlag
    float minW = 0, minH = 0, maxW = 0, maxH = 0;

    // ── Progress Bar / Slider range ──
    // `value` is a raw number inside [minValue, maxValue]; the fill is where it
    // sits in that span. The 0..1 default keeps "Percent" meaning a percentage.
    float minValue = 0, maxValue = 1;
};

// 0..1 fill of a Progress Bar / Slider: where `value` sits between min and max.
// An empty or inverted span reads as empty rather than dividing by zero.
float widgetFillFraction(const WidgetNode& n);

// Size Box overrides. Each bit turns its value on; with the bit clear the box
// keeps whatever size its own slot gives it.
enum WidgetSizeFlag {
    WSF_W = 1, WSF_H = 2, WSF_MIN_W = 4, WSF_MIN_H = 8, WSF_MAX_W = 16, WSF_MAX_H = 32
};

enum WidgetJustify { WJ_LEFT = 0, WJ_CENTER = 1, WJ_RIGHT = 2 };
const char* widgetJustifyName(int justify);

// The shear field is a friendly -1..1; this is the angle it stands for.
inline float widgetShearDegrees(float shear) { return shear * 60.0f; }

// A container that stacks its children so they cannot overlap. Only the Overlay
// and the Canvas let children share the same space.
bool widgetStacksChildren(int parentType);
// true when this slot takes a share of the box's free room along the stacking
// axis: Size = Fill, or the alignment on that same axis is Fill
bool widgetSlotFills(const WidgetNode& n, bool verticalAxis);

// ─── component properties reachable from a graph ───
// Every property maps to a WidgetNode field, so the accessors below work on any
// component; the per-type list is what the editor advertises as meaningful.
enum WidgetPropKind { WPK_NUM = 0, WPK_STR, WPK_COLOR, WPK_BOOL, WPK_KIND_COUNT };

struct WidgetProperty {
    const char* name;    // as typed on the node's Property pin (case-insensitive)
    int kind;            // WidgetPropKind
    const char* hint;    // one line for the node Details
};

int widgetPropertyCount();
const WidgetProperty& widgetPropertyAt(int index);
const WidgetProperty* widgetFindProperty(const char* name);
// properties worth showing for a component type (Text has Font Size, a
// ProgressBar has Percent, ...). Every property still works on every type.
std::vector<const WidgetProperty*> widgetPropertiesFor(int type);
const char* widgetPropKindName(int kind);

// Read / write by property name. Return false when the name is unknown or is
// not of that kind, leaving the node untouched.
bool widgetGetNumber(const WidgetNode& n, const char* prop, float& out);
bool widgetSetNumber(WidgetNode& n, const char* prop, float value);
bool widgetGetString(const WidgetNode& n, const char* prop, std::string& out);
bool widgetSetString(WidgetNode& n, const char* prop, const std::string& value);
bool widgetGetColor(const WidgetNode& n, const char* prop, Vec3& rgb, float& alpha);
bool widgetSetColor(WidgetNode& n, const char* prop, const Vec3& rgb, float alpha);
bool widgetGetBool(const WidgetNode& n, const char* prop, bool& out);
bool widgetSetBool(WidgetNode& n, const char* prop, bool value);

struct WidgetAsset {
    std::vector<WidgetNode> nodes;   // nodes[0] = canvas root
    int nextId = 1;
    float refW = 1280, refH = 720;   // reference resolution the layout is authored at

    WidgetAsset() { makeDefault(); }
    void makeDefault();
    WidgetNode* find(int id);
    const WidgetNode* find(int id) const;
    int rootId() const;
    int addNode(int type, int parent);
    void removeNode(int id);         // and its descendants
    // Swap a component with the sibling before (-1) or after (+1) it. In a
    // stacking box the hierarchy order IS the layout order, so this is how a
    // child is moved along the row/column. The whole subtree travels with it.
    // False when there is no sibling on that side.
    bool moveSibling(int id, int delta);

    std::string serialize() const;
    bool deserialize(const std::string& text);
};

// absolute screen rect of a node, honouring the parent chain and each parent's
// layout rules (free / alignment / size / scale). Scales from the reference res.
UIRect widgetNodeRect(const WidgetAsset& a, const WidgetNode& n, const UIRect& screen, float sx, float sy);
// false when the node or any of its ancestors is hidden ("Visible" property)
bool widgetNodeVisible(const WidgetAsset& a, const WidgetNode& n);
// 2D affine (m00 m01 m10 m11 tx ty) of a node's Render Transform chain, in
// screen px. Identity when nothing in the chain transforms.
void widgetRenderMatrix(const WidgetAsset& a, const WidgetNode& n, const UIRect& screen,
                        float sx, float sy, float out[6]);
// this node's Render Opacity multiplied by every ancestor's
float widgetEffectiveOpacity(const WidgetAsset& a, const WidgetNode& n);
// the box `n` is laid out inside: its parent's rect minus the parent's padding
// (the screen for a top-level node). Slot <-> box conversions measure against this.
UIRect widgetParentContentRect(const WidgetAsset& a, const WidgetNode& n, const UIRect& screen, float sx, float sy);

// A .wgt is the designer tree, this marker, then the event graph. Exported so
// anything that wants only the graph half (the Bind Dispatcher picker listing a
// widget's dispatchers) can split the file without duplicating the literal.
extern const char* const WIDGET_GRAPH_MARKER;

// Split a .wgt file into its two halves. The designer tree is required; the
// graph part is optional (older files have none) and comes back empty then.
// Widget component variables are synced into `graph`, so Get nodes resolve.
bool widgetParseAsset(const std::string& fileText, WidgetAsset& asset, BPGraph& graph);

// Topmost element under a point, for runtime pointer events. Layout containers
// that paint nothing (and Spacers) are transparent, so a click reaches the
// button under them. Returns nullptr when the point hits no element.
const WidgetNode* widgetNodeAtPoint(const WidgetAsset& asset, const UIRect& screen, float x, float y);

// draw the widget tree into `screen` (px), scaling from the reference resolution.
// Images load via matLoadTexture(r, projectDir, rel). `selectedId` (>=0) outlines
// that node; `editor` adds faint container outlines. Pure rendering — no input.
void widgetRenderTree(UI& ui, const WidgetAsset& asset, const UIRect& screen,
                      class Renderer* r, const std::string& projectDir, int selectedId = -1, bool editor = false);

// Shared Save control (implemented in app.cpp): grey when clean, green when the
// document has unsaved changes. Returns true when clicked.
bool drawSaveButton(UI& ui, const UIRect& rc, bool dirty, const char* tooltip = nullptr);

// ─── document editor ───
class WidgetEditor {
public:
    WidgetAsset widget;
    std::string curPath;
    std::string projectDir;
    bool dirty = false;
    void (*logFn)(int, const char*, ...) = nullptr;
    // Designer | Graph, exactly like a Widget Blueprint in Unreal. The graph is
    // a full Blueprint editor restricted to the UMG event set.
    BPEditor graph;
    bool graphMode = false;
    // One .wgt holds the designer tree and the graph, so the document is dirty
    // when either half is: there is no separate Blueprint asset to save.
    bool isDirty() const { return dirty || graph.dirty; }
    bool wantsTextInput() const { return graphMode && graph.wantsTextInput(); }
    bool listeningKey() const { return graphMode && graph.listeningKey(); }

    bool loadFrom(const std::string& absPath, const std::string& rel);
    bool save();
    void draw(UI& ui);
    // file name (no folders, no extension) shown as the hierarchy root
    std::string widgetAssetName() const;
    // (name, type name) of every component flagged "Is Variable", hierarchy order
    std::vector<std::pair<std::string, std::string>> variableMembers() const;

private:
    int selected_ = -1;                  // primary selection (drives the Details panel)
    std::vector<int> selection_;         // full multi-selection (hierarchy, Ctrl/Alt)
    bool isSelected(int id) const;
    void selectOnly(int id);
    void toggleSelected(int id);         // Ctrl/Alt click: add or remove
    void selectRangeTo(int id, bool additive);   // Shift click: select the visible span
    std::vector<int> rowOrder_;          // hierarchy rows in display order (this frame)
    int pendingRange_ = -1;              // Shift-click target, resolved after the list is built
    bool pendingRangeAdd_ = false;       // Ctrl+Shift: extend without clearing
    int selAnchor_ = -1;                 // Shift range starts here (Outliner semantics)
    // ── component clipboard (Ctrl+C / Ctrl+V / Ctrl+D) ──
    // A copied entry is a whole subtree: `parent` is an index into the vector
    // (-1 = the root of the copied selection), so it re-parents anywhere.
    struct ClipNode { WidgetNode node; int parent; };
    std::vector<ClipNode> clipboard_;
    void copySelection();
    // paste under `parent` (-1 = keep each root where it was); selects the result
    void pasteClipboard(int parent, bool offset);
    void duplicateSelection();
    void collectSubtree(int id, std::vector<ClipNode>& out, int parentIndex) const;
    // "Button" -> "Button_1": no two components share a name (graph Get nodes use it)
    void makeUniqueName(char* name, int cap) const;
    // ── marquee selection on the design surface ──
    bool marquee_ = false;
    float marqueeX0_ = 0, marqueeY0_ = 0;
    std::vector<int> marqueeBase_;       // selection the marquee extends (Ctrl/Shift)
    // per-node drag origin, so a whole multi-selection moves together
    std::vector<std::pair<int, std::pair<float, float>>> dragStart_;
    // hierarchy drag & drop: choose the exact parent for a new or existing widget
    int hierDropTarget_ = -2;            // node under the cursor while dragging (0 = root, -2 = none)
    int hierDragNode_ = -1;              // hierarchy row grabbed for re-parenting
    bool hierDragActive_ = false;
    float hierDragX_ = 0, hierDragY_ = 0;
    bool isAncestorOf(int node, int maybeDescendant) const;
    bool dragMove_ = false, dragResize_ = false;
    int resizeEdges_ = 0;                // 1 left, 2 right, 4 top, 8 bottom
    float dragOffX_ = 0, dragOffY_ = 0;
    bool palOpen_[WC_COUNT] = { true, true, true, true, true };
    float palScroll_ = 0, hierScroll_ = 0, detScroll_ = 0;
    bool showGrid_ = true;               // designer grid + rulers
    float gridStep_ = 32;                // reference-resolution px between grid lines
    // free navigation (blueprint-style): wheel zooms at the cursor, MMB/RMB pans
    float viewZoom_ = 1.0f, viewPanX_ = 0, viewPanY_ = 0;
    bool viewPanning_ = false;
    float panStartX_ = 0, panStartY_ = 0, panOrigX_ = 0, panOrigY_ = 0;
    UIRect designRect_{};    // last screen rect of the design surface (for hit-testing)
    float designScaleX_ = 1, designScaleY_ = 1;
    // resizable splitters (persisted per session)
    float leftW_ = 210, rightW_ = 250, paletteFrac_ = 0.5f;
    int dragSplit_ = 0;      // 1 = left col, 2 = right col, 3 = palette/hierarchy
    // palette → design/hierarchy drag-and-drop
    int dragNewType_ = -1;   // component type being dragged from the palette (-1 = none)
    float dragNewX_ = 0, dragNewY_ = 0;
    bool dragNewActive_ = false;
    int deepestContainerAt(float mx, float my, const UIRect& screen, float scale) const;
    int nodeAtPoint(float mx, float my, const UIRect& screen, float scale) const;
    void drawHierarchyRow(UI& ui, int id, int depth, float& y, const UIRect& panel);
};
