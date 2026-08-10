// ─── Pulse Engine material system: Unreal-style node graph → surface parameters ───
#pragma once
#include "math.h"
#include "ui.h"
#include <string>
#include <vector>

// Node kinds. A material is a small graph whose Result node's inputs define the
// surface. The graph is constant-folded on the CPU into a few shader parameters
// (base colour + optional albedo texture, metallic, roughness, emissive); it is
// not a full per-pixel shader compiler (that is a later upgrade).
//
// The first seven ids are the original set and MUST keep their values: .mat files
// store the type as an int. Everything new is appended at the end.
enum MatNodeType {
    MAT_RESULT = 0,     // the surface output (pins listed in matPinName)
    MAT_CONST_COLOR,    // Constant3Vector
    MAT_CONST_FLOAT,    // Constant
    MAT_TEXTURE,        // texture asset (.png) sampled (triplanar) → rgb
    MAT_MULTIPLY,       // A * B
    MAT_ADD,            // A + B
    MAT_LERP,           // mix(A, B, Alpha)
    // ── appended (ids after this point never appear in a version-1 file) ──
    MAT_CONST2,         // Constant2Vector
    MAT_CONST4,         // Constant4Vector
    MAT_SCALAR_PARAM,   // named scalar constant
    MAT_VECTOR_PARAM,   // named rgba constant
    MAT_SUBTRACT,
    MAT_DIVIDE,
    MAT_ONE_MINUS,
    MAT_POWER,
    MAT_ABS,
    MAT_MIN,
    MAT_MAX,
    MAT_CLAMP,
    MAT_FRAC,
    MAT_FLOOR,
    MAT_CEIL,
    MAT_SINE,
    MAT_COSINE,
    MAT_DOT,
    MAT_CROSS,
    MAT_NORMALIZE,
    MAT_LENGTH,
    MAT_DISTANCE,
    MAT_MASK,           // component mask (RGBA switches)
    MAT_APPEND,         // append two values into a wider one
    MAT_BREAK,          // split a colour into R, G, B
    MAT_DESATURATION,   // desaturate by a fraction
    MAT_REROUTE,        // pass-through knot, drawn as a dot (tidies long wires)
    MAT_TEXTURE_PARAM,  // named Texture Sample
    MAT_IF,             // picks a branch by comparing A and B
    MAT_NODE_COUNT
};

// both sampler forms behave identically; only the named one is a parameter
inline bool matNodeIsTexture(int type) { return type == MAT_TEXTURE || type == MAT_TEXTURE_PARAM; }
inline bool matNodeIsParameter(int type) {
    return type == MAT_SCALAR_PARAM || type == MAT_VECTOR_PARAM || type == MAT_TEXTURE_PARAM;
}
// graph-space step nodes snap to while they are dragged
const float MAT_GRID = 16.0f;

// the knot node has no header and no pin labels: it is drawn as a small dot
inline bool matNodeIsReroute(int type) { return type == MAT_REROUTE; }
// nodes that "Convert to Parameter" can turn into a named parameter, and what
// they become (-1 = the node has no value to promote)
int matParameterFormOf(int type);

// ─── material settings (the Details panel when nothing / the Result is selected) ───
enum MatBlendMode { MBM_OPAQUE = 0, MBM_MASKED, MBM_TRANSLUCENT, MBM_ADDITIVE, MBM_COUNT };
enum MatShadingModel { MSM_LIT = 0, MSM_UNLIT, MSM_COUNT };
const char* matBlendModeName(int mode);
const char* matShadingModelName(int model);

// palette grouping, Unreal's material menu categories
enum MatCategory { MCAT_CONSTANTS = 0, MCAT_PARAMETERS, MCAT_TEXTURE, MCAT_MATH, MCAT_VECTOR, MCAT_UTILITY, MCAT_COUNT };
const char* matCategoryName(int category);

// the widest node in the set: pin arrays are fixed-size so a MatNode stays a POD
enum { MAT_MAX_IN = 8, MAT_MAX_OUT = 5 };

int matNodeInputCount(int type);
int matNodeOutputCount(int type);
const char* matNodeName(int type);
const char* matPinName(int type, int pin);
const char* matOutPinName(int type, int pin);
int matNodeCategory(int type);
// one line of help, shown in the Details panel under the node's name
const char* matNodeHint(int type);
// Result pins that the current settings actually consume. A greyed-out pin still
// accepts a wire (exactly like Unreal's) but is ignored while it stays disabled.
bool matResultPinEnabled(int pin, int blendMode, int shadingModel);

struct MatNode {
    int id = 0;
    int type = MAT_CONST_COLOR;
    float x = 0, y = 0;                 // editor position (graph space)
    Vec3 color = { 0.8f, 0.8f, 0.8f };  // rgb of the constant / parameter nodes
    float alpha = 1.0f;                 // 4th component (Constant4Vector, VectorParameter)
    float scalar = 0.5f;                // Constant / ScalarParameter value
    unsigned mask = 0x7;                // MAT_MASK: bit per component (R G B A)
    char texturePath[192] = "";         // TEXTURE asset (relative to project)
    char paramName[64] = "Param";       // parameter nodes: the name shown on the node
    int in[MAT_MAX_IN] = { -1, -1, -1, -1, -1, -1, -1, -1 };   // source node id per input pin
    signed char inPin[MAT_MAX_IN] = { 0, 0, 0, 0, 0, 0, 0, 0 }; // which output of that node
};

// folded value: up to four components, plus the albedo texture it flowed through
// (the one genuinely per-pixel part of the graph)
struct MatValue {
    float c[4] = { 1, 1, 1, 1 };
    int comps = 1;                      // 1..4 — how many of c[] are meaningful
    bool hasTex = false;
    std::string tex;

    float x() const { return c[0]; }
    Vec3 rgb() const { return { c[0], comps > 1 ? c[1] : c[0], comps > 2 ? c[2] : c[0] }; }
    static MatValue F(float v) { MatValue m; m.c[0] = m.c[1] = m.c[2] = m.c[3] = v; m.comps = 1; return m; }
    static MatValue V3(const Vec3& v) { MatValue m; m.c[0] = v.x; m.c[1] = v.y; m.c[2] = v.z; m.c[3] = 1; m.comps = 3; return m; }
};

struct MaterialEval {
    Vec3 baseColor = { 0.8f, 0.8f, 0.8f };  // uColor (tint when a texture is present)
    std::string baseColorTex;               // albedo texture (empty = none)
    float metallic = 0.0f;
    float specular = 0.5f;                  // Unreal's 0..1 dielectric specular (0.5 = 4%)
    float roughness = 0.6f;
    Vec3 emissiveColor = { 0, 0, 0 };       // Emissive Color pin, already scaled
    float emissive = 0.0f;                  // its intensity (0 = no emission)
    float opacity = 1.0f;                   // Translucent / Additive
    float opacityMask = 1.0f;               // Masked
    // settings, copied out of the asset so a caller only needs this struct
    int blendMode = MBM_OPAQUE;
    int shadingModel = MSM_LIT;
    bool twoSided = false;
    float maskClip = 0.3333f;
    bool castShadow = true;
    // the whole surface is clipped away (Masked, and the mask folded below the clip)
    bool fullyMasked() const { return blendMode == MBM_MASKED && opacityMask < maskClip; }
};

// map a folded material evaluation onto a draw item's surface parameters
void applyMaterialEval(const MaterialEval& e, DrawItem& item);
// load (cached) a project-relative .png as a GL texture; 0 if missing
GLuint matLoadTexture(Renderer* r, const std::string& projectDir, const std::string& rel);

struct MaterialAsset {
    std::vector<MatNode> nodes;
    int nextId = 1;
    // ── settings (Unreal's Material details) ──
    int blendMode = MBM_OPAQUE;
    int shadingModel = MSM_LIT;
    bool twoSided = false;
    float maskClip = 0.3333f;            // Opacity Mask Clip Value
    bool castShadow = true;
    // Value each Result pin uses while nothing is wired into it — the surface is
    // fully authorable without building a graph, and a wire simply overrides it.
    Vec3 resultDef[MAT_MAX_IN];
    void resetResultDefaults();          // the stock Base Color / Metallic / ... values
    // Result pins whose default is a colour rather than a single scalar
    static bool resultPinIsColor(int pin) { return pin == 0 || pin == 4; }
    // editor view state (persisted for convenience)
    float viewX = 0, viewY = 0;

    MaterialAsset() { resetResultDefaults(); makeDefault(); }
    void makeDefault();                 // a Result + a base ConstColor wired to BaseColor
    MatNode* find(int id);
    const MatNode* find(int id) const;
    int resultId() const;
    int addNode(int type, float x, float y);
    void removeNode(int id);
    void connect(int fromNode, int fromPin, int toNode, int toPin);
    void disconnect(int toNode, int toPin);
    // false when wiring `fromNode` into that input would close a cycle
    bool wouldLoop(int fromNode, int toNode) const;

    // pull apart nodes whose boxes sit on top of each other (legacy layouts)
    void spreadOverlaps();
    // "Base" -> "Base_1" until no other parameter node answers to it. Parameters
    // are addressed by name, so two of them sharing one is always a mistake.
    std::string uniqueParamName(const std::string& wanted, int exceptId) const;
    // turn a constant into its parameter form (and give it a free name); false
    // when this node has no value to promote
    bool convertToParameter(int id);

    MaterialEval evaluate() const;      // constant-fold the graph
    MatValue evalInput(int nodeId, int pin) const;   // folded value feeding one input pin
    std::string serialize() const;
    bool deserialize(const std::string& text);

private:
    MatValue evalNode(int id, int outPin, int depth) const;
};

// ─── material instance ───
// The Unreal model: no logic of its own, just a parent material plus a value for
// the parameters it chooses to override. Anything left unchecked falls through
// to whatever the parent's own parameter node holds.
enum MatParamKind { MPK_SCALAR = 0, MPK_VECTOR, MPK_TEXTURE };

struct MatParamInfo {
    std::string name;
    int kind = MPK_SCALAR;
    float scalar = 0;                 // the parent's value, shown while not overridden
    Vec3 color = { 1, 1, 1 };
    float alpha = 1;
    std::string texture;
};

struct MatParamOverride {
    char name[64] = "";
    int kind = MPK_SCALAR;
    bool enabled = false;
    float scalar = 0;
    Vec3 color = { 1, 1, 1 };
    float alpha = 1;
    char texturePath[192] = "";
};

struct MaterialInstance {
    char parent[192] = "";            // project-relative .mat
    std::vector<MatParamOverride> overrides;

    MatParamOverride* find(const char* name);
    const MatParamOverride* find(const char* name) const;
    // returns the entry for `name`, creating a disabled one seeded from the
    // parent's current value when it does not exist yet
    MatParamOverride& ensure(const MatParamInfo& info);
    // drop entries whose parameter no longer exists in the parent
    void pruneAgainst(const std::vector<MatParamInfo>& params);

    std::string serialize() const;
    bool deserialize(const std::string& text);
};

// every parameter node the graph exposes, in graph order
std::vector<MatParamInfo> matCollectParameters(const MaterialAsset& asset);
// write the enabled overrides onto a copy of the parent graph
void matApplyInstance(MaterialAsset& asset, const MaterialInstance& instance);
// true when the text is a material instance rather than a material graph
bool matTextIsInstance(const std::string& fileText);
bool matPathIsInstance(const std::string& path);

// ─── document editor (opens as its own tab, like CurveEditor) ───
class MaterialEditor {
public:
    // For a plain .mat this is the graph being edited. For an instance it is the
    // resolved parent (parent graph + overrides), rebuilt every frame, so the
    // preview and every consumer read one thing either way.
    MaterialAsset material;
    std::string curPath;      // relative to projectDir
    std::string projectDir;
    bool dirty = false;
    void (*logFn)(int, const char*, ...) = nullptr;
    // Project asset lists, owned by the app layer and kept current by its browser
    // scan, so the Texture Sample node and the instance's parent field can offer
    // real pickers. Null = no list, and the field falls back to a typed path.
    const std::vector<std::string>* textureAssets = nullptr;
    const std::vector<std::string>* materialAssets = nullptr;

    // ── material instance mode ──
    bool isInstance = false;
    MaterialInstance instance;
    MaterialAsset parentGraph;     // the parent as loaded, before overrides

    bool loadFrom(const std::string& absPath, const std::string& rel);
    bool save();
    void draw(UI& ui);        // preview + details column, node canvas, right-click palette
    // (re)reads instance.parent off disk into parentGraph; false when it is missing
    bool reloadParent();
    // the palette's search box swallows typing while it is open
    bool wantsTextInput() const { return paletteOpen_; }

private:
    // ── selection / canvas interaction ──
    int selected_ = -1;
    bool dragNode_ = false;
    bool panning_ = false;
    float panMouseX_ = 0, panMouseY_ = 0;
    float dragOffX_ = 0, dragOffY_ = 0;
    // pending connection drag, from an output pin or back from an input pin
    int linkFromNode_ = -1, linkFromPin_ = 0;
    // The auto-frame has to wait for the panel to settle: on the very first frame
    // the layout has not measured itself yet and the canvas is the wrong size.
    bool framePending_ = true;
    float lastCanvasW_ = -1, lastCanvasH_ = -1;
    float zoom_ = 1.0f;
    float rmbPressX_ = 0, rmbPressY_ = 0;   // a right-click that did not pan opens the palette
    // double click on a wire drops a knot on it (same gesture as the Blueprint graph)
    int frame_ = 0, lastClickFrame_ = -100;
    float lastClickX_ = 0, lastClickY_ = 0;
    // inserts a Reroute into the wire under (mx, my); false when no wire is close
    bool insertRerouteAt(float mx, float my, const UIRect& canvas);

    // ── preview viewport ──
    float previewYaw_ = -0.7f, previewPitch_ = 0.35f;
    bool previewOrbit_ = false;
    float previewMouseX_ = 0, previewMouseY_ = 0;
    int previewMesh_ = 1;             // MeshType: sphere by default, like Unreal
    bool previewGrid_ = true;

    // ── left column layout (resizable, like the widget editor's splitters) ──
    float leftW_ = 300, previewH_ = 250;
    int dragSplit_ = 0;               // 1 = column width, 2 = preview height
    float detScroll_ = 0;

    // ── right-click node menu ──
    bool ctxOpen_ = false;
    int ctxNode_ = -1;
    float ctxX_ = 0, ctxY_ = 0;

    // ── right-click palette ──
    bool paletteOpen_ = false;
    float palX_ = 0, palY_ = 0, palWX_ = 0, palWY_ = 0, palScroll_ = 0;
    char palSearch_[64] = "";
    unsigned palCatOpen_ = 0;
    // palette opened by dropping a wire on empty canvas: the new node auto-wires
    bool palLinkMode_ = false;
    int palLinkNode_ = -1, palLinkPin_ = 0;
    bool palLinkOut_ = false;

    UIRect canvasRect_{};    // last graph rect, so the Details can act on the view
    // centre every node in the canvas (also runs on open when the saved view,
    // authored before the preview column existed, would leave the graph off screen)
    void frameGraph(const UIRect& canvas);
    void drawNode(UI& ui, MatNode& n, const UIRect& canvas);
    void drawDetails(UI& ui, const UIRect& rc);
    void drawMaterialSettings(UI& ui, const UIRect& rc, float& y);
    void drawNodeDetails(UI& ui, const UIRect& rc, MatNode& n, float& y);
    void drawPalette(UI& ui, const UIRect& canvas);
    // the instance's parameter list, drawn where the graph canvas would be
    void drawInstanceParameters(UI& ui, const UIRect& rc);
    float instScroll_ = 0;
    // preview + Details + splitters, shared by both modes
    void drawPreviewAndDetails(UI& ui, const UIRect& preview, const UIRect& details,
                               const UIRect& canvasAll, const MaterialEval& ev);
    void drawNodeMenu(UI& ui, const UIRect& canvas);
    // inline editors drawn on the node itself (values, Result pin defaults)
    void drawNodeValue(UI& ui, MatNode& n, const UIRect& rc);
    void drawResultDefaults(UI& ui, MatNode& n, const UIRect& canvas);
    // node interaction is inert while a menu, the palette or a picker is up
    bool inputBlocked(UI& ui) const;
    void openPalette(float screenX, float screenY, float worldX, float worldY);
    // screen rect of a node, given the canvas origin and the current view
    UIRect nodeRect(const MatNode& n, const UIRect& canvas) const;
    void inPinPos(const MatNode& n, int pin, const UIRect& canvas, float& px, float& py) const;
    void outPinPos(const MatNode& n, int pin, const UIRect& canvas, float& px, float& py) const;
};
