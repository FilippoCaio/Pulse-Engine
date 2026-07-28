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
enum MatNodeType {
    MAT_RESULT = 0,     // inputs: BaseColor(rgb), Metallic, Roughness, Emissive
    MAT_CONST_COLOR,    // rgb constant
    MAT_CONST_FLOAT,    // scalar constant
    MAT_TEXTURE,        // texture asset (.png) sampled (triplanar) → rgb
    MAT_MULTIPLY,       // A * B
    MAT_ADD,            // A + B
    MAT_LERP,           // mix(A, B, Alpha)
    MAT_NODE_COUNT
};

// number of input pins per node type
int matNodeInputCount(int type);
const char* matNodeName(int type);
const char* matPinName(int type, int pin);

struct MatNode {
    int id = 0;
    int type = MAT_CONST_COLOR;
    float x = 0, y = 0;                 // editor position (graph space)
    Vec3 color = { 0.8f, 0.8f, 0.8f };  // CONST_COLOR value
    float scalar = 0.5f;               // CONST_FLOAT value
    char texturePath[192] = "";         // TEXTURE asset (relative to project)
    int in[4] = { -1, -1, -1, -1 };     // source node id per input pin (source output is pin 0)
};

// folded value: effective = tint * (hasTex ? sample(tex) : 1)
struct MatValue {
    Vec3 tint = { 1, 1, 1 };
    bool hasTex = false;
    std::string tex;
};

struct MaterialEval {
    Vec3 baseColor = { 0.8f, 0.8f, 0.8f };  // uColor (tint when a texture is present)
    std::string baseColorTex;               // albedo texture (empty = none)
    float metallic = 0.0f;
    float roughness = 0.6f;
    float emissive = 0.0f;
};

// map a folded material evaluation onto the fixed lit-shader parameters
void applyMaterialEval(const MaterialEval& e, Vec3& color, float& specular, float& shininess, float& emissive);
// load (cached) a project-relative .png as a GL texture; 0 if missing
GLuint matLoadTexture(Renderer* r, const std::string& projectDir, const std::string& rel);

struct MaterialAsset {
    std::vector<MatNode> nodes;
    int nextId = 1;
    // editor view state (persisted for convenience)
    float viewX = 0, viewY = 0;

    MaterialAsset() { makeDefault(); }
    void makeDefault();                 // a Result + a base ConstColor wired to BaseColor
    MatNode* find(int id);
    const MatNode* find(int id) const;
    int resultId() const;
    int addNode(int type, float x, float y);
    void removeNode(int id);

    MaterialEval evaluate() const;      // constant-fold the graph
    std::string serialize() const;
    bool deserialize(const std::string& text);

private:
    MatValue evalNode(int id, int depth) const;
};

// ─── document editor (opens as its own tab, like CurveEditor) ───
class MaterialEditor {
public:
    MaterialAsset material;
    std::string curPath;      // relative to projectDir
    std::string projectDir;
    bool dirty = false;
    void (*logFn)(int, const char*, ...) = nullptr;

    bool loadFrom(const std::string& absPath, const std::string& rel);
    bool save();
    void draw(UI& ui);        // node canvas + palette + details + 3D preview

private:
    int selected_ = -1;
    bool dragNode_ = false;
    bool panning_ = false;
    float panMouseX_ = 0, panMouseY_ = 0;
    float dragOffX_ = 0, dragOffY_ = 0;
    // pending connection drag: from (node,outPin) to a hovered input pin
    int linkFromNode_ = -1;
    bool viewInit_ = false;
    float previewYaw_ = -0.7f, previewPitch_ = 0.5f;
    bool previewOrbit_ = false;
    float previewMouseX_ = 0, previewMouseY_ = 0;

    void drawNode(UI& ui, MatNode& n, const UIRect& canvas);
};
