#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "material.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Shared Save control (implemented in app.cpp). Declared without a default
// argument: widget.h declares the same function with one, and only the first
// declaration in a translation unit may carry it.
bool drawSaveButton(UI& ui, const UIRect& rc, bool dirty, const char* tooltip);

// ─── node metadata ───
namespace {
struct MatDef {
    const char* name;
    int category;
    int nIns;
    const char* ins[MAT_MAX_IN];
    int nOuts;
    const char* outs[MAT_MAX_OUT];
    const char* hint;
};

// indexed by MatNodeType — keep in the enum's order
const MatDef DEFS[MAT_NODE_COUNT] = {
    { "Result", MCAT_UTILITY, 8,
      { "Base Color", "Metallic", "Specular", "Roughness", "Emissive Color", "Opacity", "Opacity Mask", "Ambient Occlusion" },
      0, {}, "The surface itself. Its settings live in this panel." },
    { "Constant3Vector", MCAT_CONSTANTS, 0, {}, 1, { "RGB" }, "A fixed colour." },
    { "Constant", MCAT_CONSTANTS, 0, {}, 1, { "" }, "A fixed scalar." },
    { "Texture Sample", MCAT_TEXTURE, 1, { "UVs" }, 5, { "RGBA", "R", "G", "B", "A" },
      "Samples a .png. The engine samples object-space triplanar, so the UVs pin is "
      "accepted but not read yet." },
    { "Multiply", MCAT_MATH, 2, { "A", "B" }, 1, { "" }, "A * B, component by component." },
    { "Add", MCAT_MATH, 2, { "A", "B" }, 1, { "" }, "A + B, component by component." },
    { "Lerp", MCAT_MATH, 3, { "A", "B", "Alpha" }, 1, { "" }, "Blends A and B by Alpha." },
    { "Constant2Vector", MCAT_CONSTANTS, 0, {}, 1, { "RG" }, "A fixed pair of scalars." },
    { "Constant4Vector", MCAT_CONSTANTS, 0, {}, 1, { "RGBA" }, "A fixed colour with alpha." },
    { "ScalarParameter", MCAT_PARAMETERS, 0, {}, 1, { "" }, "A named scalar." },
    { "VectorParameter", MCAT_PARAMETERS, 0, {}, 1, { "RGBA" }, "A named colour." },
    { "Subtract", MCAT_MATH, 2, { "A", "B" }, 1, { "" }, "A - B." },
    { "Divide", MCAT_MATH, 2, { "A", "B" }, 1, { "" }, "A / B (a zero divisor gives 0)." },
    { "OneMinus", MCAT_MATH, 1, { "" }, 1, { "" }, "1 - the input." },
    { "Power", MCAT_MATH, 2, { "Base", "Exp" }, 1, { "" }, "Base raised to Exp." },
    { "Abs", MCAT_MATH, 1, { "" }, 1, { "" }, "Absolute value." },
    { "Min", MCAT_MATH, 2, { "A", "B" }, 1, { "" }, "The smaller of the two." },
    { "Max", MCAT_MATH, 2, { "A", "B" }, 1, { "" }, "The larger of the two." },
    { "Clamp", MCAT_MATH, 3, { "In", "Min", "Max" }, 1, { "" }, "Keeps the input inside a range." },
    { "Frac", MCAT_MATH, 1, { "" }, 1, { "" }, "The fractional part." },
    { "Floor", MCAT_MATH, 1, { "" }, 1, { "" }, "Rounds down." },
    { "Ceil", MCAT_MATH, 1, { "" }, 1, { "" }, "Rounds up." },
    { "Sine", MCAT_MATH, 1, { "" }, 1, { "" }, "Sine of the input, in radians." },
    { "Cosine", MCAT_MATH, 1, { "" }, 1, { "" }, "Cosine of the input, in radians." },
    { "Dot", MCAT_VECTOR, 2, { "A", "B" }, 1, { "" }, "Dot product; the result is a scalar." },
    { "Cross", MCAT_VECTOR, 2, { "A", "B" }, 1, { "" }, "Cross product of two 3-component values." },
    { "Normalize", MCAT_VECTOR, 1, { "" }, 1, { "" }, "Scales the input to unit length." },
    { "Length", MCAT_VECTOR, 1, { "" }, 1, { "" }, "Length of the input; the result is a scalar." },
    { "Distance", MCAT_VECTOR, 2, { "A", "B" }, 1, { "" }, "Distance between A and B." },
    { "Component Mask", MCAT_UTILITY, 1, { "" }, 1, { "" }, "Keeps only the ticked channels." },
    { "Append Vector", MCAT_UTILITY, 2, { "A", "B" }, 1, { "" }, "Joins two values into a wider one." },
    { "Break Out Float3", MCAT_UTILITY, 1, { "" }, 3, { "R", "G", "B" }, "Splits a colour into its channels." },
    { "Desaturation", MCAT_UTILITY, 2, { "In", "Fraction" }, 1, { "" }, "Fades the input towards grey." },
    { "Reroute", MCAT_UTILITY, 1, { "" }, 1, { "" },
      "A knot that passes its input through, to route long wires. Alt+click it to "
      "remove it and rejoin the wire." },
    { "TextureParameter", MCAT_PARAMETERS, 1, { "UVs" }, 5, { "RGBA", "R", "G", "B", "A" },
      "A named Texture Sample. Same sampling, addressable by name." },
    { "If", MCAT_UTILITY, 5, { "A", "B", "A > B", "A == B", "A < B" }, 1, { "" },
      "Compares A with B and passes through the matching branch." },
};

const char* const CAT_NAMES[MCAT_COUNT] = { "Constants", "Parameters", "Texture", "Math", "Vector Ops", "Utility" };
const Vec3 CAT_COLORS[MCAT_COUNT] = {
    { 0.24f, 0.34f, 0.50f },   // constants  blue
    { 0.42f, 0.30f, 0.55f },   // parameters violet
    { 0.20f, 0.44f, 0.40f },   // texture    teal
    { 0.30f, 0.32f, 0.36f },   // math       grey (Unreal's plain nodes)
    { 0.50f, 0.38f, 0.18f },   // vector ops amber
    { 0.34f, 0.30f, 0.30f },   // utility    warm grey
};
const Vec3 RESULT_COLOR = { 0.36f, 0.26f, 0.44f };
const Vec3 WIRE_COLOR = { 0.78f, 0.81f, 0.86f };
const Vec3 ACCENT = { 0.30f, 0.62f, 0.99f };

bool validType(int t) { return t >= 0 && t < MAT_NODE_COUNT; }
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
// node box in graph units — defined with the rest of the geometry further down
float matNodeWidth(const MatNode& n);
float matNodeHeight(const MatNode& n);
}  // namespace

int matNodeInputCount(int type) { return validType(type) ? DEFS[type].nIns : 0; }
int matNodeOutputCount(int type) { return validType(type) ? DEFS[type].nOuts : 0; }
const char* matNodeName(int type) { return validType(type) ? DEFS[type].name : "?"; }
const char* matNodeHint(int type) { return validType(type) ? DEFS[type].hint : ""; }
int matNodeCategory(int type) { return validType(type) ? DEFS[type].category : MCAT_UTILITY; }
const char* matPinName(int type, int pin) {
    if (!validType(type) || pin < 0 || pin >= DEFS[type].nIns) return "";
    return DEFS[type].ins[pin];
}
const char* matOutPinName(int type, int pin) {
    if (!validType(type) || pin < 0 || pin >= DEFS[type].nOuts) return "";
    return DEFS[type].outs[pin];
}
const char* matCategoryName(int category) {
    return category >= 0 && category < MCAT_COUNT ? CAT_NAMES[category] : "";
}
const char* matBlendModeName(int mode) {
    static const char* n[MBM_COUNT] = { "Opaque", "Masked", "Translucent", "Additive" };
    return mode >= 0 && mode < MBM_COUNT ? n[mode] : "Opaque";
}
const char* matShadingModelName(int model) {
    static const char* n[MSM_COUNT] = { "Default Lit", "Unlit" };
    return model >= 0 && model < MSM_COUNT ? n[model] : "Default Lit";
}
int matParameterFormOf(int type) {
    switch (type) {
    case MAT_CONST_FLOAT: return MAT_SCALAR_PARAM;
    case MAT_CONST_COLOR: case MAT_CONST2: case MAT_CONST4: return MAT_VECTOR_PARAM;
    case MAT_TEXTURE: return MAT_TEXTURE_PARAM;
    default: return -1;   // nothing to promote (a Multiply has no value of its own)
    }
}
bool matResultPinEnabled(int pin, int blendMode, int shadingModel) {
    bool lit = shadingModel == MSM_LIT;
    switch (pin) {
    case 0: return true;                                    // Base Color
    case 1: case 2: case 3: return lit;                     // Metallic / Specular / Roughness
    case 4: return true;                                    // Emissive Color
    case 5: return blendMode == MBM_TRANSLUCENT || blendMode == MBM_ADDITIVE;
    case 6: return blendMode == MBM_MASKED;
    case 7: return lit;                                     // Ambient Occlusion
    }
    return false;
}

// ─── MaterialAsset ───
void MaterialAsset::resetResultDefaults() {
    resultDef[0] = { 0.75f, 0.75f, 0.78f };   // Base Color
    resultDef[1] = { 0, 0, 0 };               // Metallic
    resultDef[2] = { 0.5f, 0.5f, 0.5f };      // Specular
    resultDef[3] = { 0.6f, 0.6f, 0.6f };      // Roughness
    resultDef[4] = { 0, 0, 0 };               // Emissive Color
    resultDef[5] = { 1, 1, 1 };               // Opacity
    resultDef[6] = { 1, 1, 1 };               // Opacity Mask
    resultDef[7] = { 1, 1, 1 };               // Ambient Occlusion
}

void MaterialAsset::makeDefault() {
    nodes.clear();
    nextId = 1;
    int result = addNode(MAT_RESULT, 470, 120);
    int col = addNode(MAT_CONST_COLOR, 140, 130);
    find(col)->color = { 0.75f, 0.75f, 0.78f };
    find(result)->in[0] = col;   // BaseColor <- Constant3Vector
}

MatNode* MaterialAsset::find(int id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
const MatNode* MaterialAsset::find(int id) const {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
int MaterialAsset::resultId() const {
    for (const auto& n : nodes) if (n.type == MAT_RESULT) return n.id;
    return -1;
}
int MaterialAsset::addNode(int type, float x, float y) {
    MatNode n;
    n.id = nextId++;
    n.type = type;
    n.x = floorf(x / MAT_GRID + 0.5f) * MAT_GRID;   // land on the grid like a drag does
    n.y = floorf(y / MAT_GRID + 0.5f) * MAT_GRID;
    switch (type) {
    case MAT_CONST_FLOAT: case MAT_SCALAR_PARAM: n.scalar = 1.0f; break;
    case MAT_LERP: n.scalar = 0.5f; break;
    case MAT_MASK: n.mask = 0x7; break;
    default: break;
    }
    if (type == MAT_SCALAR_PARAM) snprintf(n.paramName, sizeof(n.paramName), "Scalar");
    if (type == MAT_VECTOR_PARAM) snprintf(n.paramName, sizeof(n.paramName), "Color");
    if (type == MAT_TEXTURE_PARAM) snprintf(n.paramName, sizeof(n.paramName), "Texture");
    nodes.push_back(n);
    return n.id;
}
void MaterialAsset::removeNode(int id) {
    const MatNode* n = find(id);
    if (!n || n->type == MAT_RESULT) return;   // never remove the Result node
    // Removing a knot is transparent: whatever fed it is rejoined to everything
    // it fed, so tidying wires can never break the graph. Values are captured
    // before the erase — `n` points into the vector we are about to shrink.
    const bool bypass = matNodeIsReroute(n->type);
    const int srcNode = bypass ? n->in[0] : -1;
    const int srcPin = bypass ? (int)n->inPin[0] : 0;
    std::vector<std::pair<int, int>> downstream;   // (node id, input pin) it was feeding
    if (bypass)
        for (const auto& o : nodes)
            for (int i = 0; i < matNodeInputCount(o.type); i++)
                if (o.in[i] == id) downstream.push_back({ o.id, i });

    for (auto& o : nodes)
        for (int i = 0; i < MAT_MAX_IN; i++) if (o.in[i] == id) { o.in[i] = -1; o.inPin[i] = 0; }
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const MatNode& o) { return o.id == id; }), nodes.end());

    if (bypass && srcNode >= 0)
        for (const auto& target : downstream) connect(srcNode, srcPin, target.first, target.second);
}
void MaterialAsset::disconnect(int toNode, int toPin) {
    MatNode* n = find(toNode);
    if (n && toPin >= 0 && toPin < MAT_MAX_IN) { n->in[toPin] = -1; n->inPin[toPin] = 0; }
}
// depth-first walk upstream from `fromNode`: a wire that reaches `toNode` again
// would make the fold recurse forever
bool MaterialAsset::wouldLoop(int fromNode, int toNode) const {
    if (fromNode == toNode) return true;
    const MatNode* n = find(fromNode);
    if (!n) return false;
    for (int i = 0; i < matNodeInputCount(n->type); i++)
        if (n->in[i] >= 0 && wouldLoop(n->in[i], toNode)) return true;
    return false;
}
std::string MaterialAsset::uniqueParamName(const std::string& wanted, int exceptId) const {
    std::string base = wanted.empty() ? std::string("Param") : wanted;
    auto taken = [&](const std::string& name) {
        for (const auto& n : nodes) {
            if (n.id == exceptId) continue;
            if (!matNodeIsParameter(n.type)) continue;
            if (_stricmp(n.paramName, name.c_str()) == 0) return true;
        }
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 1; i < 1000; i++) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
    return base;
}

bool MaterialAsset::convertToParameter(int id) {
    MatNode* n = find(id);
    if (!n) return false;
    int form = matParameterFormOf(n->type);
    if (form < 0) return false;
    if (form != MAT_TEXTURE_PARAM) {
        // a Constant2Vector carries only RG; widen it so the parameter reads sanely
        if (n->type == MAT_CONST2) n->color.z = 0;
        if (n->type != MAT_CONST4) n->alpha = 1.0f;
        if (n->type == MAT_CONST_FLOAT) n->color = { n->scalar, n->scalar, n->scalar };
    }
    n->type = form;
    const char* wanted = form == MAT_SCALAR_PARAM ? "Scalar"
                       : form == MAT_VECTOR_PARAM ? "Color" : "Texture";
    snprintf(n->paramName, sizeof(n->paramName), "%s", uniqueParamName(wanted, id).c_str());
    return true;
}

void MaterialAsset::connect(int fromNode, int fromPin, int toNode, int toPin) {
    MatNode* to = find(toNode);
    const MatNode* from = find(fromNode);
    if (!to || !from || toPin < 0 || toPin >= matNodeInputCount(to->type)) return;
    if (fromPin < 0 || fromPin >= matNodeOutputCount(from->type)) return;
    if (wouldLoop(fromNode, toNode)) return;
    to->in[toPin] = fromNode;
    to->inPin[toPin] = (signed char)fromPin;
}

// ─── folding ───
namespace {
MatValue matBin(const MatValue& a, const MatValue& b, float (*f)(float, float)) {
    MatValue v;
    v.comps = a.comps > b.comps ? a.comps : b.comps;
    for (int i = 0; i < 4; i++) {
        float av = a.comps == 1 ? a.c[0] : a.c[i];
        float bv = b.comps == 1 ? b.c[0] : b.c[i];
        v.c[i] = f(av, bv);
    }
    v.hasTex = a.hasTex || b.hasTex;
    v.tex = a.hasTex ? a.tex : b.tex;
    return v;
}
MatValue matUnary(const MatValue& a, float (*f)(float)) {
    MatValue v = a;
    for (int i = 0; i < 4; i++) v.c[i] = f(a.c[i]);
    return v;
}
float opAdd(float a, float b) { return a + b; }
float opSub(float a, float b) { return a - b; }
float opMul(float a, float b) { return a * b; }
float opDiv(float a, float b) { return b == 0 ? 0 : a / b; }
float opMin(float a, float b) { return a < b ? a : b; }
float opMax(float a, float b) { return a > b ? a : b; }
float opPow(float a, float b) { return powf(a < 0 ? 0 : a, b); }
float opAbs(float a) { return fabsf(a); }
float opFrac(float a) { return a - floorf(a); }
float opFloor(float a) { return floorf(a); }
float opCeil(float a) { return ceilf(a); }
float opSin(float a) { return sinf(a); }
float opCos(float a) { return cosf(a); }
float opOneMinus(float a) { return 1.0f - a; }
}  // namespace

MatValue MaterialAsset::evalNode(int id, int outPin, int depth) const {
    const MatNode* n = find(id);
    if (!n || depth > 64) return MatValue::F(0);
    auto input = [&](int pin, float fallback) -> MatValue {
        if (pin < 0 || pin >= MAT_MAX_IN || n->in[pin] < 0) return MatValue::F(fallback);
        return evalNode(n->in[pin], n->inPin[pin], depth + 1);
    };
    switch (n->type) {
    case MAT_CONST_COLOR: return MatValue::V3(n->color);
    case MAT_CONST_FLOAT: case MAT_SCALAR_PARAM: return MatValue::F(n->scalar);
    case MAT_CONST2: { MatValue v; v.c[0] = n->color.x; v.c[1] = n->color.y; v.c[2] = v.c[3] = 0; v.comps = 2; return v; }
    case MAT_CONST4: case MAT_VECTOR_PARAM: {
        MatValue v; v.c[0] = n->color.x; v.c[1] = n->color.y; v.c[2] = n->color.z; v.c[3] = n->alpha; v.comps = 4; return v;
    }
    case MAT_TEXTURE: case MAT_TEXTURE_PARAM: {
        // The sample is per-pixel, so the fold can only carry the texture itself
        // and a white tint; the channel pins fold to a single white component.
        MatValue v; v.c[0] = v.c[1] = v.c[2] = v.c[3] = 1; v.comps = 4;
        v.hasTex = n->texturePath[0] != 0;
        v.tex = n->texturePath;
        if (outPin >= 1 && outPin <= 4) { v.comps = 1; v.c[0] = 1; }
        return v;
    }
    case MAT_IF: {
        // A single scalar comparison decides the branch, exactly as the fold sees it
        float a = input(0, 0).x(), b = input(1, 0).x();
        return a > b ? input(2, 0) : (a < b ? input(4, 0) : input(3, 0));
    }
    case MAT_MULTIPLY: return matBin(input(0, 1), input(1, 1), opMul);
    case MAT_ADD: return matBin(input(0, 0), input(1, 0), opAdd);
    case MAT_SUBTRACT: return matBin(input(0, 0), input(1, 0), opSub);
    case MAT_DIVIDE: return matBin(input(0, 1), input(1, 1), opDiv);
    case MAT_MIN: return matBin(input(0, 0), input(1, 0), opMin);
    case MAT_MAX: return matBin(input(0, 0), input(1, 0), opMax);
    case MAT_POWER: return matBin(input(0, 1), input(1, 1), opPow);
    case MAT_ONE_MINUS: return matUnary(input(0, 0), opOneMinus);
    case MAT_ABS: return matUnary(input(0, 0), opAbs);
    case MAT_FRAC: return matUnary(input(0, 0), opFrac);
    case MAT_FLOOR: return matUnary(input(0, 0), opFloor);
    case MAT_CEIL: return matUnary(input(0, 0), opCeil);
    case MAT_SINE: return matUnary(input(0, 0), opSin);
    case MAT_COSINE: return matUnary(input(0, 0), opCos);
    case MAT_LERP: {
        MatValue a = input(0, 0), b = input(1, 1), t = input(2, n->scalar);
        MatValue v;
        v.comps = a.comps > b.comps ? a.comps : b.comps;
        for (int i = 0; i < 4; i++) {
            float av = a.comps == 1 ? a.c[0] : a.c[i];
            float bv = b.comps == 1 ? b.c[0] : b.c[i];
            float tv = t.comps == 1 ? t.c[0] : t.c[i];
            v.c[i] = av * (1 - tv) + bv * tv;
        }
        // the texture follows whichever side dominates the blend
        bool takeB = t.c[0] >= 0.5f;
        v.hasTex = takeB ? b.hasTex : a.hasTex;
        v.tex = takeB ? b.tex : a.tex;
        if (!v.hasTex && (a.hasTex || b.hasTex)) { v.hasTex = true; v.tex = a.hasTex ? a.tex : b.tex; }
        return v;
    }
    case MAT_CLAMP: {
        MatValue in = input(0, 0), lo = input(1, 0), hi = input(2, 1);
        MatValue v = in;
        for (int i = 0; i < 4; i++) {
            float l = lo.comps == 1 ? lo.c[0] : lo.c[i], h = hi.comps == 1 ? hi.c[0] : hi.c[i];
            v.c[i] = in.c[i] < l ? l : (in.c[i] > h ? h : in.c[i]);
        }
        return v;
    }
    case MAT_DOT: {
        MatValue a = input(0, 0), b = input(1, 0);
        int comps = a.comps > b.comps ? a.comps : b.comps;
        float sum = 0;
        for (int i = 0; i < comps; i++) sum += (a.comps == 1 ? a.c[0] : a.c[i]) * (b.comps == 1 ? b.c[0] : b.c[i]);
        return MatValue::F(sum);
    }
    case MAT_CROSS: {
        MatValue a = input(0, 0), b = input(1, 0);
        Vec3 av = a.rgb(), bv = b.rgb();
        return MatValue::V3({ av.y * bv.z - av.z * bv.y, av.z * bv.x - av.x * bv.z, av.x * bv.y - av.y * bv.x });
    }
    case MAT_NORMALIZE: {
        MatValue a = input(0, 0);
        float len = 0;
        for (int i = 0; i < a.comps; i++) len += a.c[i] * a.c[i];
        len = sqrtf(len);
        MatValue v = a;
        if (len > 1e-6f) for (int i = 0; i < 4; i++) v.c[i] = a.c[i] / len;
        return v;
    }
    case MAT_LENGTH: {
        MatValue a = input(0, 0);
        float len = 0;
        for (int i = 0; i < a.comps; i++) len += a.c[i] * a.c[i];
        return MatValue::F(sqrtf(len));
    }
    case MAT_DISTANCE: {
        MatValue a = input(0, 0), b = input(1, 0);
        int comps = a.comps > b.comps ? a.comps : b.comps;
        float sum = 0;
        for (int i = 0; i < comps; i++) {
            float d = (a.comps == 1 ? a.c[0] : a.c[i]) - (b.comps == 1 ? b.c[0] : b.c[i]);
            sum += d * d;
        }
        return MatValue::F(sqrtf(sum));
    }
    case MAT_MASK: {
        MatValue a = input(0, 0);
        MatValue v;
        v.comps = 0;
        for (int i = 0; i < 4; i++) if (n->mask & (1u << i)) v.c[v.comps++] = a.comps == 1 ? a.c[0] : a.c[i];
        if (v.comps == 0) { v.comps = 1; v.c[0] = 0; }
        for (int i = v.comps; i < 4; i++) v.c[i] = v.c[v.comps - 1];
        v.hasTex = a.hasTex; v.tex = a.tex;
        return v;
    }
    case MAT_APPEND: {
        MatValue a = input(0, 0), b = input(1, 0);
        MatValue v;
        v.comps = 0;
        for (int i = 0; i < a.comps && v.comps < 4; i++) v.c[v.comps++] = a.c[i];
        for (int i = 0; i < b.comps && v.comps < 4; i++) v.c[v.comps++] = b.c[i];
        if (v.comps == 0) { v.comps = 1; v.c[0] = 0; }
        for (int i = v.comps; i < 4; i++) v.c[i] = 0;
        v.hasTex = a.hasTex || b.hasTex;
        v.tex = a.hasTex ? a.tex : b.tex;
        return v;
    }
    case MAT_BREAK: {
        MatValue a = input(0, 0);
        int c = outPin >= 0 && outPin < 3 ? outPin : 0;
        return MatValue::F(a.comps == 1 ? a.c[0] : a.c[c]);
    }
    case MAT_REROUTE: return input(0, 0);
    case MAT_DESATURATION: {
        MatValue a = input(0, 0), f = input(1, 1);
        float grey = a.rgb().x * 0.3f + a.rgb().y * 0.59f + a.rgb().z * 0.11f;
        float amount = clamp01(f.c[0]);
        MatValue v = a;
        for (int i = 0; i < 3; i++) v.c[i] = a.c[i] * (1 - amount) + grey * amount;
        return v;
    }
    default: break;
    }
    return MatValue::F(0);
}

MatValue MaterialAsset::evalInput(int nodeId, int pin) const {
    const MatNode* n = find(nodeId);
    if (!n || pin < 0 || pin >= MAT_MAX_IN || n->in[pin] < 0) return MatValue::F(0);
    return evalNode(n->in[pin], n->inPin[pin], 0);
}

MaterialEval MaterialAsset::evaluate() const {
    MaterialEval e;
    e.blendMode = blendMode;
    e.shadingModel = shadingModel;
    e.twoSided = twoSided;
    e.maskClip = maskClip;
    e.castShadow = castShadow;
    const MatNode* rn = find(resultId());
    if (!rn) return e;
    // Every pin reads either its wire or the value typed on the node, so the
    // surface is fully described even with an empty graph.
    auto value = [&](int pin) -> MatValue {
        if (!matResultPinEnabled(pin, blendMode, shadingModel)) return MatValue::V3(resultDef[pin]);
        if (rn->in[pin] < 0) return MatValue::V3(resultDef[pin]);
        return evalNode(rn->in[pin], rn->inPin[pin], 0);
    };
    {
        MatValue v = value(0);
        e.baseColor = v.rgb();
        if (v.hasTex) e.baseColorTex = v.tex;
    }
    e.metallic = clamp01(value(1).x());
    e.specular = clamp01(value(2).x());
    e.roughness = clamp01(value(3).x());
    {
        MatValue v = value(4);
        e.emissiveColor = v.rgb();
        e.emissive = std::max(std::max(e.emissiveColor.x, e.emissiveColor.y), std::max(e.emissiveColor.z, 0.0f));
    }
    e.opacity = clamp01(value(5).x());
    e.opacityMask = value(6).x();
    // the shader has no ambient-occlusion term, so a constant AO simply dims the
    // base colour — the closest a constant-folded graph can get
    e.baseColor = e.baseColor * clamp01(value(7).x());
    return e;
}

std::string MaterialAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSOMAT 2\n";
    o << "view " << viewX << " " << viewY << "\n";
    o << "mat " << blendMode << " " << shadingModel << " " << (twoSided ? 1 : 0) << " "
      << maskClip << " " << (castShadow ? 1 : 0) << "\n";
    for (int i = 0; i < MAT_MAX_IN; i++)
        o << "rdef " << i << " " << resultDef[i].x << " " << resultDef[i].y << " " << resultDef[i].z << "\n";
    for (const auto& n : nodes) {
        o << "mnode " << n.id << " " << n.type << " " << n.x << " " << n.y << " "
          << n.color.x << " " << n.color.y << " " << n.color.z << " " << n.alpha << " "
          << n.scalar << " " << n.mask << "\n";
        for (int i = 0; i < MAT_MAX_IN; i++)
            if (n.in[i] >= 0) o << "min " << n.id << " " << i << " " << n.in[i] << " " << (int)n.inPin[i] << "\n";
        if (n.texturePath[0]) o << "tex " << n.id << " " << n.texturePath << "\n";
        // only a parameter node has a name worth keeping
        if (matNodeIsParameter(n.type) && n.paramName[0])
            o << "pname " << n.id << " " << n.paramName << "\n";
    }
    return o.str();
}

bool MaterialAsset::deserialize(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("IMPULSOMAT", 0) != 0) return false;
    int version = 1;
    sscanf(line.c_str(), "IMPULSOMAT %d", &version);
    nodes.clear();
    nextId = 1;
    blendMode = MBM_OPAQUE;
    shadingModel = MSM_LIT;
    twoSided = false;
    maskClip = 0.3333f;
    castShadow = true;
    resetResultDefaults();   // a file without rdef lines (every version 1) uses the stock values
    while (std::getline(in, line)) {
        if (line.rfind("view ", 0) == 0) {
            sscanf(line.c_str(), "view %f %f", &viewX, &viewY);
        } else if (line.rfind("mat ", 0) == 0) {
            int two = 0, shadow = 1;
            sscanf(line.c_str(), "mat %d %d %d %f %d", &blendMode, &shadingModel, &two, &maskClip, &shadow);
            twoSided = two != 0;
            castShadow = shadow != 0;
        } else if (line.rfind("rdef ", 0) == 0) {
            int pin = -1; Vec3 v;
            if (sscanf(line.c_str(), "rdef %d %f %f %f", &pin, &v.x, &v.y, &v.z) == 4 &&
                pin >= 0 && pin < MAT_MAX_IN)
                resultDef[pin] = v;
        } else if (line.rfind("mnode ", 0) == 0) {
            MatNode n;
            unsigned mask = 0x7;
            int rd = sscanf(line.c_str(), "mnode %d %d %f %f %f %f %f %f %f %u",
                            &n.id, &n.type, &n.x, &n.y, &n.color.x, &n.color.y, &n.color.z,
                            &n.alpha, &n.scalar, &mask);
            n.mask = mask;
            if (rd >= 9 && validType(n.type)) { nodes.push_back(n); if (n.id >= nextId) nextId = n.id + 1; }
        } else if (line.rfind("min ", 0) == 0) {
            int id = 0, slot = 0, src = -1, srcPin = 0;
            if (sscanf(line.c_str(), "min %d %d %d %d", &id, &slot, &src, &srcPin) >= 3) {
                MatNode* n = find(id);
                if (n && slot >= 0 && slot < MAT_MAX_IN) { n->in[slot] = src; n->inPin[slot] = (signed char)srcPin; }
            }
        } else if (line.rfind("node ", 0) == 0) {
            // ── version 1: a fixed four-input node line, Result pins in the old order ──
            MatNode n;
            int old[4] = { -1, -1, -1, -1 };
            int rd = sscanf(line.c_str(), "node %d %d %f %f %f %f %f %f %d %d %d %d",
                            &n.id, &n.type, &n.x, &n.y, &n.color.x, &n.color.y, &n.color.z, &n.scalar,
                            &old[0], &old[1], &old[2], &old[3]);
            if (rd < 8 || !validType(n.type)) continue;
            if (n.type == MAT_RESULT) {
                // v1 Result was Base Color / Metallic / Roughness / Emissive
                static const int remap[4] = { 0, 1, 3, 4 };
                for (int i = 0; i < 4; i++) n.in[remap[i]] = old[i];
            } else {
                for (int i = 0; i < 4; i++) n.in[i] = old[i];
            }
            nodes.push_back(n);
            if (n.id >= nextId) nextId = n.id + 1;
        } else if (line.rfind("tex ", 0) == 0) {
            int id = 0; char path[256] = "";
            if (sscanf(line.c_str(), "tex %d %255[^\n]", &id, path) == 2) {
                MatNode* n = find(id);
                if (n) { strncpy(n->texturePath, path, sizeof(n->texturePath) - 1); n->texturePath[sizeof(n->texturePath) - 1] = 0; }
            }
        } else if (line.rfind("pname ", 0) == 0) {
            int id = 0; char name[128] = "";
            if (sscanf(line.c_str(), "pname %d %127[^\n]", &id, name) == 2) {
                MatNode* n = find(id);
                if (n) { strncpy(n->paramName, name, sizeof(n->paramName) - 1); n->paramName[sizeof(n->paramName) - 1] = 0; }
            }
        }
    }
    if (resultId() < 0) makeDefault();
    // Version-1 nodes were about half as tall (no value strip, fewer Result pins),
    // so a layout authored back then can collide once it is drawn at the current
    // size. Push the overlapping ones apart once, on load only.
    if (version < 2) spreadOverlaps();
    return true;
}

void MaterialAsset::spreadOverlaps() {
    auto boxW = [](const MatNode& n) { return matNodeWidth(n); };
    auto boxH = [](const MatNode& n) { return matNodeHeight(n); };
    for (int pass = 0; pass < 4; pass++) {
        bool moved = false;
        for (size_t i = 0; i < nodes.size(); i++)
            for (size_t j = 0; j < nodes.size(); j++) {
                if (i == j) continue;
                MatNode& a = nodes[i];
                MatNode& b = nodes[j];
                if (b.y < a.y || (b.y == a.y && j < i)) continue;   // only push the lower one down
                bool overlap = a.x < b.x + boxW(b) && b.x < a.x + boxW(a) &&
                               a.y < b.y + boxH(b) && b.y < a.y + boxH(a);
                if (!overlap) continue;
                b.y = a.y + boxH(a) + 14;
                moved = true;
            }
        if (!moved) break;
    }
}

// ─── material instance ───
static const char* const MATERIAL_INSTANCE_MAGIC = "IMPULSOMATINST";

bool matTextIsInstance(const std::string& fileText) {
    return fileText.rfind(MATERIAL_INSTANCE_MAGIC, 0) == 0;
}
bool matPathIsInstance(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".matinst";
}

std::vector<MatParamInfo> matCollectParameters(const MaterialAsset& asset) {
    std::vector<MatParamInfo> out;
    for (const MatNode& n : asset.nodes) {
        if (!matNodeIsParameter(n.type) || !n.paramName[0]) continue;
        MatParamInfo info;
        info.name = n.paramName;
        info.kind = n.type == MAT_SCALAR_PARAM ? MPK_SCALAR
                  : n.type == MAT_VECTOR_PARAM ? MPK_VECTOR : MPK_TEXTURE;
        info.scalar = n.scalar;
        info.color = n.color;
        info.alpha = n.alpha;
        info.texture = n.texturePath;
        out.push_back(std::move(info));
    }
    return out;
}

void matApplyInstance(MaterialAsset& asset, const MaterialInstance& instance) {
    for (MatNode& n : asset.nodes) {
        if (!matNodeIsParameter(n.type) || !n.paramName[0]) continue;
        const MatParamOverride* o = instance.find(n.paramName);
        if (!o || !o->enabled) continue;
        switch (n.type) {
        case MAT_SCALAR_PARAM: n.scalar = o->scalar; break;
        case MAT_VECTOR_PARAM: n.color = o->color; n.alpha = o->alpha; break;
        case MAT_TEXTURE_PARAM:
            snprintf(n.texturePath, sizeof(n.texturePath), "%s", o->texturePath);
            break;
        default: break;
        }
    }
}

MatParamOverride* MaterialInstance::find(const char* name) {
    if (!name || !name[0]) return nullptr;
    for (auto& o : overrides) if (_stricmp(o.name, name) == 0) return &o;
    return nullptr;
}
const MatParamOverride* MaterialInstance::find(const char* name) const {
    return const_cast<MaterialInstance*>(this)->find(name);
}

MatParamOverride& MaterialInstance::ensure(const MatParamInfo& info) {
    if (MatParamOverride* existing = find(info.name.c_str())) {
        existing->kind = info.kind;   // the parent may have changed the type
        return *existing;
    }
    MatParamOverride o;
    snprintf(o.name, sizeof(o.name), "%s", info.name.c_str());
    o.kind = info.kind;
    o.enabled = false;
    o.scalar = info.scalar;
    o.color = info.color;
    o.alpha = info.alpha;
    snprintf(o.texturePath, sizeof(o.texturePath), "%s", info.texture.c_str());
    overrides.push_back(o);
    return overrides.back();
}

void MaterialInstance::pruneAgainst(const std::vector<MatParamInfo>& params) {
    overrides.erase(std::remove_if(overrides.begin(), overrides.end(), [&](const MatParamOverride& o) {
        for (const MatParamInfo& p : params) if (_stricmp(p.name.c_str(), o.name) == 0) return false;
        return true;
    }), overrides.end());
}

std::string MaterialInstance::serialize() const {
    std::ostringstream o;
    o << MATERIAL_INSTANCE_MAGIC << " 1\n";
    o << "parent " << (parent[0] ? parent : "-") << "\n";
    for (const MatParamOverride& p : overrides) {
        if (!p.enabled) continue;      // only what the instance actually overrides
        if (p.kind == MPK_SCALAR) o << "scalar " << p.scalar << " " << p.name << "\n";
        else if (p.kind == MPK_VECTOR)
            o << "vector " << p.color.x << " " << p.color.y << " " << p.color.z << " " << p.alpha
              << " " << p.name << "\n";
        else o << "texture " << (p.texturePath[0] ? p.texturePath : "-") << " | " << p.name << "\n";
    }
    return o.str();
}

bool MaterialInstance::deserialize(const std::string& text) {
    if (!matTextIsInstance(text)) return false;
    std::istringstream in(text);
    std::string line;
    std::getline(in, line);   // magic
    parent[0] = 0;
    overrides.clear();
    while (std::getline(in, line)) {
        if (line.rfind("parent ", 0) == 0) {
            std::string path = line.substr(7);
            snprintf(parent, sizeof(parent), "%s", path == "-" ? "" : path.c_str());
        } else if (line.rfind("scalar ", 0) == 0) {
            MatParamOverride o; o.kind = MPK_SCALAR; o.enabled = true;
            char name[64] = "";
            if (sscanf(line.c_str(), "scalar %f %63[^\n]", &o.scalar, name) == 2) {
                snprintf(o.name, sizeof(o.name), "%s", name);
                overrides.push_back(o);
            }
        } else if (line.rfind("vector ", 0) == 0) {
            MatParamOverride o; o.kind = MPK_VECTOR; o.enabled = true;
            char name[64] = "";
            if (sscanf(line.c_str(), "vector %f %f %f %f %63[^\n]", &o.color.x, &o.color.y, &o.color.z,
                       &o.alpha, name) == 5) {
                snprintf(o.name, sizeof(o.name), "%s", name);
                overrides.push_back(o);
            }
        } else if (line.rfind("texture ", 0) == 0) {
            // "texture <path> | <name>": a path may contain spaces, the name may not
            std::string rest = line.substr(8);
            size_t bar = rest.rfind(" | ");
            if (bar == std::string::npos) continue;
            MatParamOverride o; o.kind = MPK_TEXTURE; o.enabled = true;
            std::string path = rest.substr(0, bar);
            snprintf(o.texturePath, sizeof(o.texturePath), "%s", path == "-" ? "" : path.c_str());
            snprintf(o.name, sizeof(o.name), "%s", rest.substr(bar + 3).c_str());
            overrides.push_back(o);
        }
    }
    return true;
}

// ─── shared helpers ───
void applyMaterialEval(const MaterialEval& e, DrawItem& item) {
    item.color = e.baseColor;
    // Unreal's Specular is a 0..1 dielectric reflectance where 0.5 means the 4%
    // default, so the engine's old formula is exactly the value at 0.5.
    item.specular = (0.04f + e.metallic * 0.85f) * (e.specular / 0.5f);
    item.shininess = 6.0f + (1.0f - e.roughness) * 134.0f;
    item.emissive = e.emissive;
    item.emissiveTinted = e.emissive > 0;
    item.emissiveColor = e.emissive > 0 ? e.emissiveColor * (1.0f / e.emissive) : Vec3{ 1, 1, 1 };
    item.doubleSided = e.twoSided;
    item.unlit = e.shadingModel == MSM_UNLIT;
    bool blended = e.blendMode == MBM_TRANSLUCENT || e.blendMode == MBM_ADDITIVE;
    item.opacity = blended ? e.opacity : 1.0f;
    item.additive = e.blendMode == MBM_ADDITIVE;
    item.castShadow = e.castShadow && !blended;
}

GLuint matLoadTexture(Renderer* r, const std::string& projectDir, const std::string& rel) {
    if (!r || rel.empty()) return 0;
    static std::unordered_map<std::string, GLuint> cache;
    auto it = cache.find(rel);
    if (it != cache.end()) return it->second;
    GLuint tex = r->loadPngTexture(projectDir + "\\" + rel);
    cache[rel] = tex;
    return tex;
}

// ─── document editor ───
bool MaterialEditor::reloadParent() {
    parentGraph = MaterialAsset();
    if (!instance.parent[0] || projectDir.empty()) return false;
    std::ifstream f(projectDir + "\\" + instance.parent, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // a parent that is itself an instance is not supported: only real graphs
    if (matTextIsInstance(data) || !parentGraph.deserialize(data)) return false;
    return true;
}

bool MaterialEditor::loadFrom(const std::string& absPath, const std::string& rel) {
    std::ifstream f(absPath, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    isInstance = matTextIsInstance(data) || matPathIsInstance(rel);
    if (isInstance) {
        if (!instance.deserialize(data)) {
            // a freshly created file: keep whatever the caller seeded and carry on
            instance = MaterialInstance();
        }
        curPath = rel;
        reloadParent();
        material = parentGraph;
        matApplyInstance(material, instance);
    } else {
        if (!material.deserialize(data)) return false;
        curPath = rel;
    }
    selected_ = -1;
    dirty = false;
    return true;
}
bool MaterialEditor::save() {
    extern bool gEditorProjectWritesAllowed;
    if (!gEditorProjectWritesAllowed) return false;
    if (curPath.empty() || projectDir.empty()) return false;
    std::ofstream f(projectDir + "\\" + curPath, std::ios::binary);
    if (!f) return false;
    std::string data = isInstance ? instance.serialize() : material.serialize();
    f.write(data.data(), (std::streamsize)data.size());
    dirty = !f.good();
    if (!dirty && logFn)
        logFn(1, isInstance ? "Material Instance saved: %s" : "Material saved: %s", curPath.c_str());
    return !dirty;
}

// ─── node geometry (graph units; the view scales them by zoom_) ───
namespace {
const float MAT_HDR_H = 24;      // title bar
const float MAT_PIN_STEP = 26;   // one pin row: an inline field (22) plus breathing room
const float MAT_PIN_TOP = 13;    // first pin's centre below the header/value strip
const float MAT_FIELD_H = 22;    // inline value box — must fit the 17 px font
const float MAT_REROUTE_W = 26, MAT_REROUTE_H = 20;

float matNodeWidth(const MatNode& n) {
    switch (n.type) {
    case MAT_REROUTE: return MAT_REROUTE_W;
    case MAT_RESULT: return 268;   // pin name + its inline default value
    case MAT_CONST_COLOR: case MAT_CONST2: case MAT_CONST4: return 182;   // "Constant3Vector" fits
    case MAT_TEXTURE: case MAT_TEXTURE_PARAM: return 172;
    case MAT_CLAMP: case MAT_DESATURATION: case MAT_SCALAR_PARAM: case MAT_VECTOR_PARAM: return 168;
    default: return 152;
    }
}
// height of the inline value strip drawn under the header (0 = none). Every
// editable strip is one MAT_FIELD_H row plus 4 px of padding, so the text always
// sits inside its box.
float matNodeValueH(const MatNode& n) {
    switch (n.type) {
    case MAT_CONST_COLOR: case MAT_CONST4: case MAT_VECTOR_PARAM:
    case MAT_CONST_FLOAT: case MAT_CONST2: case MAT_SCALAR_PARAM: case MAT_MASK:
        return MAT_FIELD_H + 6;
    case MAT_TEXTURE: case MAT_TEXTURE_PARAM: return 78;
    default: return 0;
    }
}
float matNodeHeight(const MatNode& n) {
    if (matNodeIsReroute(n.type)) return MAT_REROUTE_H;
    int rows = matNodeInputCount(n.type) > matNodeOutputCount(n.type)
             ? matNodeInputCount(n.type) : matNodeOutputCount(n.type);
    if (rows < 1) rows = 1;
    return MAT_HDR_H + matNodeValueH(n) + rows * MAT_PIN_STEP + 8;
}
// centres a line of text vertically inside a box of height h (font is 17 px)
float matTextY(float boxY, float boxH, float zoom) { return boxY + (boxH - 17.0f * zoom) * 0.5f; }
// nodes land on the graph grid, so columns of them line up by themselves
float matSnap(float v) { return floorf(v / MAT_GRID + 0.5f) * MAT_GRID; }
}  // namespace

void MaterialEditor::frameGraph(const UIRect& canvas) {
    if (material.nodes.empty() || canvas.w < 60 || canvas.h < 60) return;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& n : material.nodes) {
        minX = std::min(minX, n.x);
        minY = std::min(minY, n.y);
        maxX = std::max(maxX, n.x + matNodeWidth(n));
        maxY = std::max(maxY, n.y + matNodeHeight(n));
    }
    float gw = std::max(1.0f, maxX - minX), gh = std::max(1.0f, maxY - minY);
    zoom_ = std::min(std::min((canvas.w - 48) / gw, (canvas.h - 48) / gh), 1.0f);
    zoom_ = std::max(zoom_, 0.35f);
    material.viewX = (canvas.w - gw * zoom_) * 0.5f - minX * zoom_;
    material.viewY = (canvas.h - gh * zoom_) * 0.5f - minY * zoom_;
}

UIRect MaterialEditor::nodeRect(const MatNode& n, const UIRect& canvas) const {
    return { canvas.x + material.viewX + n.x * zoom_, canvas.y + material.viewY + n.y * zoom_,
             matNodeWidth(n) * zoom_, matNodeHeight(n) * zoom_ };
}
void MaterialEditor::inPinPos(const MatNode& n, int pin, const UIRect& canvas, float& px, float& py) const {
    UIRect rc = nodeRect(n, canvas);
    px = rc.x;
    py = matNodeIsReroute(n.type) ? rc.y + rc.h * 0.5f
       : rc.y + (MAT_HDR_H + matNodeValueH(n) + MAT_PIN_TOP + pin * MAT_PIN_STEP) * zoom_;
}
void MaterialEditor::outPinPos(const MatNode& n, int pin, const UIRect& canvas, float& px, float& py) const {
    UIRect rc = nodeRect(n, canvas);
    px = rc.x + rc.w;
    py = matNodeIsReroute(n.type) ? rc.y + rc.h * 0.5f
       : rc.y + (MAT_HDR_H + matNodeValueH(n) + MAT_PIN_TOP + pin * MAT_PIN_STEP) * zoom_;
}

// While a menu, the palette or a deferred picker is up the graph is inert: the
// click that dismisses them must not also land on a node underneath.
bool MaterialEditor::inputBlocked(UI& ui) const {
    return ui.interactionBlocked() || paletteOpen_ || ctxOpen_ || ui.popupOpen();
}

// Walks the same bezier the wire is drawn with and measures the distance from
// the cursor to each segment; the first wire within a few pixels gets the knot.
bool MaterialEditor::insertRerouteAt(float mx, float my, const UIRect& canvas) {
    for (auto& target : material.nodes) {
        for (int pin = 0; pin < matNodeInputCount(target.type); pin++) {
            if (target.in[pin] < 0) continue;
            const MatNode* src = material.find(target.in[pin]);
            if (!src) continue;
            float x1, y1, x2, y2;
            outPinPos(*src, target.inPin[pin], canvas, x1, y1);
            inPinPos(target, pin, canvas, x2, y2);
            float coff = std::min(std::max(fabsf(x2 - x1) * 0.5f + fabsf(y2 - y1) * 0.15f, 18.0f * zoom_), 220.0f * zoom_);
            float px = x1, py = y1;
            for (int s = 1; s <= 12; s++) {
                float t = s / 12.0f, mt = 1 - t;
                float bx = mt * mt * mt * x1 + 3 * mt * mt * t * (x1 + coff) + 3 * mt * t * t * (x2 - coff) + t * t * t * x2;
                float by = mt * mt * mt * y1 + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t * y2;
                float dx = bx - px, dy = by - py;
                float len2 = dx * dx + dy * dy;
                float tt = len2 > 0 ? ((mx - px) * dx + (my - py) * dy) / len2 : 0;
                tt = tt < 0 ? 0 : (tt > 1 ? 1 : tt);
                float qx = px + dx * tt - mx, qy = py + dy * tt - my;
                if (qx * qx + qy * qy < 49) {
                    int srcId = src->id, srcPin = target.inPin[pin], targetId = target.id;
                    int knot = material.addNode(MAT_REROUTE,
                                               (mx - canvas.x - material.viewX) / zoom_ - MAT_REROUTE_W * 0.5f,
                                               (my - canvas.y - material.viewY) / zoom_ - MAT_REROUTE_H * 0.5f);
                    material.disconnect(targetId, pin);
                    material.connect(srcId, srcPin, knot, 0);
                    material.connect(knot, 0, targetId, pin);
                    selected_ = knot;
                    dirty = true;
                    return true;
                }
                px = bx; py = by;
            }
        }
    }
    return false;
}

void MaterialEditor::openPalette(float screenX, float screenY, float worldX, float worldY) {
    paletteOpen_ = true;
    palScroll_ = 0;
    palSearch_[0] = 0;
    palCatOpen_ = 0;
    palX_ = screenX;
    palY_ = screenY;
    palWX_ = worldX;
    palWY_ = worldY;
}

void MaterialEditor::draw(UI& ui) {
    // The colour picker and the drop-downs are drawn last (in UI::end), so this
    // panel has already hit-tested by the time the user clicks inside them. While
    // one is open the whole editor is inert — otherwise the click that picks a
    // colour also lands on the node or canvas underneath.
    const bool wasBlocked = ui.interactionBlocked();
    if (ui.popupOpen()) ui.setInteractionBlocked(true, false);
    struct Unblock {
        UI& ui; bool was;
        ~Unblock() { ui.setInteractionBlocked(was, false); }
    } unblock{ ui, wasBlocked };
    const UIInput& in = ui.input();
    Renderer* r = ui.r;
    frame_++;

    // ── top-left tool bar (same language as the Widget / Blueprint editors) ──
    {
        UIRect bar = ui.allocRow(38);
        r->drawRectPx(bar.x, bar.y, bar.w, bar.h, { 0.105f, 0.115f, 0.14f }, 1);
        r->drawRectPx(bar.x, bar.y + bar.h - 1, bar.w, 1, { 0.05f, 0.055f, 0.065f }, 1);
        const float BW = 34, BH = bar.h - 10;
        float bx = bar.x + 8;
        if (drawSaveButton(ui, { bx, bar.y + 5, BW, BH }, dirty,
                           isInstance ? "Save the Material Instance" : "Save the Material")) save();
        bx += BW + 8;
        std::string title = curPath.empty() ? std::string("Material") : curPath;
        if (dirty) title += " *";
        r->drawTextLine(bx, ui.textCenterY(bar), ui.ellipsize(title, bar.w - (bx - bar.x) - 260),
                        isInstance ? Vec3{ 0.72f, 0.86f, 0.95f } : Vec3{ 0.82f, 0.72f, 0.95f }, 1);
        const char* hintText = isInstance ? "Instance: only the parent's parameters"
                                          : "Right-click the graph to add a node";
        r->drawTextLine(bar.x + bar.w - r->textWidth(hintText) - 10, ui.textCenterY(bar), hintText,
                        { 0.48f, 0.53f, 0.61f }, 1);
    }

    UIRect pin = ui.panelInner();
    float top = ui.panelCursorY() + 6;
    UIRect canvasAll = { pin.x, top, pin.w, pin.y + pin.h - top };
    ui.spacing(canvasAll.h);
    r->setUIScissor(canvasAll.x, canvasAll.y, canvasAll.w, canvasAll.h, true);

    // ── layout: preview + details on the left, graph on the right ──
    leftW_ = std::min(std::max(leftW_, 210.0f), std::max(220.0f, canvasAll.w * 0.5f));
    previewH_ = std::min(std::max(previewH_, 120.0f), std::max(130.0f, canvasAll.h - 160.0f));
    UIRect preview = { canvasAll.x, canvasAll.y, leftW_, previewH_ };
    UIRect details = { canvasAll.x, canvasAll.y + previewH_ + 6, leftW_, canvasAll.h - previewH_ - 6 };
    UIRect canvas = { canvasAll.x + leftW_ + 8, canvasAll.y, canvasAll.w - leftW_ - 8, canvasAll.h };

    // An instance carries no graph: rebuild the resolved material from the parent
    // every frame so the preview and the Details follow the overrides live.
    if (isInstance) {
        material = parentGraph;
        matApplyInstance(material, instance);
    }
    MaterialEval ev = material.evaluate();

    if (isInstance) {
        drawInstanceParameters(ui, canvas);
        drawPreviewAndDetails(ui, preview, details, canvasAll, ev);
        r->setUIScissor(0, 0, 0, 0, false);
        return;
    }

    // ── graph canvas ──
    r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);
    r->drawRectPx(canvas.x, canvas.y, canvas.w, canvas.h, { 0.075f, 0.08f, 0.095f }, 1);
    canvasRect_ = canvas;
    bool canvasStable = fabsf(canvas.w - lastCanvasW_) < 0.5f && fabsf(canvas.h - lastCanvasH_) < 0.5f;
    lastCanvasW_ = canvas.w;
    lastCanvasH_ = canvas.h;
    if (framePending_ && canvasStable) {
        framePending_ = false;
        const MatNode* rn = material.find(material.resultId());
        bool resultVisible = false;
        if (rn) {
            UIRect rr = nodeRect(*rn, canvas);
            resultVisible = rr.x >= canvas.x + 8 && rr.x + rr.w <= canvas.x + canvas.w - 8 &&
                            rr.y >= canvas.y + 8 && rr.y + rr.h <= canvas.y + canvas.h - 8;
        }
        if (!resultVisible) frameGraph(canvas);
    }
    float grid = 32 * zoom_;
    for (float gx = fmodf(material.viewX, grid); gx < canvas.w; gx += grid)
        r->drawRectPx(canvas.x + gx, canvas.y, 1, canvas.h, { 0.105f, 0.115f, 0.135f }, 1);
    for (float gy = fmodf(material.viewY, grid); gy < canvas.h; gy += grid)
        r->drawRectPx(canvas.x, canvas.y + gy, canvas.w, 1, { 0.105f, 0.115f, 0.135f }, 1);

    const bool blocked = inputBlocked(ui);
    bool overCanvas = !blocked &&
                      in.mouseX >= canvas.x && in.mouseX < canvas.x + canvas.w &&
                      in.mouseY >= canvas.y && in.mouseY < canvas.y + canvas.h;

    // ── zoom at the cursor, pan with MMB / RMB ──
    if (overCanvas && in.wheel != 0) {
        float wx = (in.mouseX - canvas.x - material.viewX) / zoom_;
        float wy = (in.mouseY - canvas.y - material.viewY) / zoom_;
        zoom_ = std::min(std::max(zoom_ * (in.wheel > 0 ? 1.1f : 1 / 1.1f), 0.35f), 2.5f);
        material.viewX = in.mouseX - canvas.x - wx * zoom_;
        material.viewY = in.mouseY - canvas.y - wy * zoom_;
        ui.consumeWheel();
    }
    if (overCanvas && in.rmbPressed) { rmbPressX_ = in.mouseX; rmbPressY_ = in.mouseY; }
    if (overCanvas && (in.mmbPressed || in.rmbPressed)) { panning_ = true; panMouseX_ = in.mouseX; panMouseY_ = in.mouseY; }
    if (panning_) {
        if (in.mmbDown || in.rmbDown) {
            material.viewX += in.mouseX - panMouseX_;
            material.viewY += in.mouseY - panMouseY_;
            panMouseX_ = in.mouseX; panMouseY_ = in.mouseY;
        } else panning_ = false;
    }

    // ── wires (behind the nodes) ──
    for (const auto& n : material.nodes) {
        int nin = matNodeInputCount(n.type);
        for (int i = 0; i < nin; i++) {
            if (n.in[i] < 0) continue;
            const MatNode* s = material.find(n.in[i]);
            if (!s) continue;
            bool live = n.type != MAT_RESULT ||
                        matResultPinEnabled(i, material.blendMode, material.shadingModel);
            float x1, y1, x2, y2;
            outPinPos(*s, n.inPin[i], canvas, x1, y1);
            inPinPos(n, i, canvas, x2, y2);
            Vec3 col = live ? WIRE_COLOR : Vec3{ 0.34f, 0.36f, 0.40f };
            float coff = std::min(std::max(fabsf(x2 - x1) * 0.5f + fabsf(y2 - y1) * 0.15f, 18.0f * zoom_), 220.0f * zoom_);
            float px = x1, py = y1;
            for (int s2 = 1; s2 <= 12; s2++) {
                float t = s2 / 12.0f, mt = 1 - t;
                float bx = mt * mt * mt * x1 + 3 * mt * mt * t * (x1 + coff) + 3 * mt * t * t * (x2 - coff) + t * t * t * x2;
                float by = mt * mt * mt * y1 + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t * y2;
                r->drawLinePx(px, py, bx, by, 2.0f * zoom_, col, live ? 0.92f : 0.55f);
                px = bx; py = by;
            }
        }
    }

    // ── nodes ──
    bool anyHeaderHit = false;
    int altRemove = -1;   // resolved after the loop: removing mutates the vector
    for (auto& n : material.nodes) {
        UIRect rc = nodeRect(n, canvas);
        if (rc.x + rc.w < canvas.x || rc.x > canvas.x + canvas.w ||
            rc.y + rc.h < canvas.y || rc.y > canvas.y + canvas.h) continue;
        drawNode(ui, n, canvas);

        if (!overCanvas) continue;
        // Header drag / selection. A knot has no header, so its whole box drags.
        UIRect hd = matNodeIsReroute(n.type) ? rc : UIRect{ rc.x, rc.y, rc.w, MAT_HDR_H * zoom_ };
        bool overHdr = in.mouseX >= hd.x && in.mouseX < hd.x + hd.w && in.mouseY >= hd.y && in.mouseY < hd.y + hd.h;
        // Alt+click a knot: drop it and rejoin the wire it was sitting on
        if (matNodeIsReroute(n.type) && in.keyAlt && in.mousePressed && overHdr) {
            altRemove = n.id;
            anyHeaderHit = true;
            continue;
        }
        if (overHdr && in.mousePressed && linkFromNode_ < 0) {
            selected_ = n.id; dragNode_ = true; anyHeaderHit = true;
            dragOffX_ = in.mouseX - rc.x; dragOffY_ = in.mouseY - rc.y;
        }
        // output pin press → start a wire
        for (int p = 0; p < matNodeOutputCount(n.type); p++) {
            float px, py; outPinPos(n, p, canvas, px, py);
            if (in.mousePressed && fabsf(in.mouseX - px) < 9 && fabsf(in.mouseY - py) < 9) {
                linkFromNode_ = n.id; linkFromPin_ = p; anyHeaderHit = true;
            }
        }
        // input pin press → grab the existing wire back, or start one from here
        for (int i = 0; i < matNodeInputCount(n.type); i++) {
            float px, py; inPinPos(n, i, canvas, px, py);
            if (in.mousePressed && fabsf(in.mouseX - px) < 9 && fabsf(in.mouseY - py) < 9 && linkFromNode_ < 0) {
                anyHeaderHit = true;
                if (n.in[i] >= 0) {
                    // pull the wire off this pin and keep dragging its source end
                    linkFromNode_ = n.in[i];
                    linkFromPin_ = n.inPin[i];
                    material.disconnect(n.id, i);
                    dirty = true;
                } else {
                    linkFromNode_ = -2 - n.id;   // negative marker: dragging from an input
                    linkFromPin_ = i;
                }
            }
        }
        // clicking anywhere else on the box (an inline field, the value strip)
        // still selects the node, it just does not drag it
        bool overBody = in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
                        in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
        if (overBody && in.mousePressed && linkFromNode_ < 0) { selected_ = n.id; anyHeaderHit = true; }
    }
    if (altRemove >= 0) {
        material.removeNode(altRemove);   // reconnects the two ends by itself
        if (selected_ == altRemove) selected_ = -1;
        dragNode_ = false;
        dirty = true;
    }

    // ── node drag ──
    if (dragNode_) {
        if (in.mouseDown) {
            MatNode* n = material.find(selected_);
            if (n) {
                n->x = matSnap((in.mouseX - canvas.x - material.viewX - dragOffX_) / zoom_);
                n->y = matSnap((in.mouseY - canvas.y - material.viewY - dragOffY_) / zoom_);
                dirty = true;
            }
        } else dragNode_ = false;
    }

    // ── wire drag ──
    if (linkFromNode_ != -1) {
        bool fromOutput = linkFromNode_ >= 0;
        int srcId = fromOutput ? linkFromNode_ : -linkFromNode_ - 2;
        const MatNode* s = material.find(srcId);
        if (s && in.mouseDown) {
            float ax, ay;
            if (fromOutput) outPinPos(*s, linkFromPin_, canvas, ax, ay);
            else inPinPos(*s, linkFromPin_, canvas, ax, ay);
            r->drawLinePx(ax, ay, in.mouseX, in.mouseY, 2.0f, { 0.85f, 0.9f, 1.0f }, 0.9f);
        }
        if (!in.mouseDown) {
            bool dropped = false;
            for (auto& n : material.nodes) {
                if (fromOutput) {
                    for (int i = 0; i < matNodeInputCount(n.type); i++) {
                        float px, py; inPinPos(n, i, canvas, px, py);
                        if (fabsf(in.mouseX - px) < 10 && fabsf(in.mouseY - py) < 10 && n.id != srcId) {
                            material.connect(srcId, linkFromPin_, n.id, i);
                            dropped = true; dirty = true;
                        }
                    }
                } else {
                    for (int p = 0; p < matNodeOutputCount(n.type); p++) {
                        float px, py; outPinPos(n, p, canvas, px, py);
                        if (fabsf(in.mouseX - px) < 10 && fabsf(in.mouseY - py) < 10 && n.id != srcId) {
                            material.connect(n.id, p, srcId, linkFromPin_);
                            dropped = true; dirty = true;
                        }
                    }
                }
            }
            // dropped on empty canvas: open the palette and auto-wire the new node
            if (!dropped && overCanvas && s) {
                openPalette(in.mouseX, in.mouseY,
                            (in.mouseX - canvas.x - material.viewX) / zoom_,
                            (in.mouseY - canvas.y - material.viewY) / zoom_);
                palLinkMode_ = true;
                palLinkNode_ = srcId;
                palLinkPin_ = linkFromPin_;
                palLinkOut_ = fromOutput;
            }
            linkFromNode_ = -1;
        }
    }

    // click empty canvas → select the graph itself (the Details show the material),
    // or drop a knot when it is the second click on the same wire
    if (overCanvas && in.mousePressed && !anyHeaderHit && linkFromNode_ == -1 && !panning_) {
        bool onNode = false;
        for (const auto& n : material.nodes) {
            UIRect rc = nodeRect(n, canvas);
            if (in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h) onNode = true;
        }
        bool doubleClick = (frame_ - lastClickFrame_) < 22 &&
                           fabsf(in.mouseX - lastClickX_) < 8 && fabsf(in.mouseY - lastClickY_) < 8;
        bool placedKnot = !onNode && doubleClick && insertRerouteAt(in.mouseX, in.mouseY, canvas);
        if (!onNode && !placedKnot) selected_ = -1;
        lastClickFrame_ = placedKnot ? -100 : frame_;   // a third click must not chain
        lastClickX_ = in.mouseX;
        lastClickY_ = in.mouseY;
    }
    // right click (without a pan drag): on a node → its menu, else the palette
    if (overCanvas && in.rmbReleased &&
        fabsf(in.mouseX - rmbPressX_) < 6 && fabsf(in.mouseY - rmbPressY_) < 6) {
        int onNode = -1;
        for (const auto& n : material.nodes) {
            UIRect rc = nodeRect(n, canvas);
            if (in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h)
                onNode = n.id;
        }
        if (onNode >= 0) {
            selected_ = onNode;
            ctxOpen_ = true; ctxNode_ = onNode; ctxX_ = in.mouseX; ctxY_ = in.mouseY;
        } else {
            palLinkMode_ = false;
            openPalette(in.mouseX, in.mouseY,
                        (in.mouseX - canvas.x - material.viewX) / zoom_,
                        (in.mouseY - canvas.y - material.viewY) / zoom_);
        }
    }
    // Delete removes the selected node
    if (selected_ >= 0 && in.keyDelete && !paletteOpen_ && !ctxOpen_ && !ui.wantKeyboard()) {
        material.removeNode(selected_);
        selected_ = -1;
        dirty = true;
    }
    if (in.keyEscape) { paletteOpen_ = false; palLinkMode_ = false; ctxOpen_ = false; }

    drawPalette(ui, canvas);
    drawNodeMenu(ui, canvas);
    drawPreviewAndDetails(ui, preview, details, canvasAll, ev);
    r->setUIScissor(0, 0, 0, 0, false);
}

// ─── left column: 3D preview on top, contextual Details below, and the two
// splitters. Shared by the graph editor and the instance editor. ───
void MaterialEditor::drawPreviewAndDetails(UI& ui, const UIRect& preview, const UIRect& details,
                                           const UIRect& canvasAll, const MaterialEval& ev) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    r->setUIScissor(canvasAll.x, canvasAll.y, canvasAll.w, canvasAll.h, true);

    // ── preview viewport (top-left, exactly like Unreal's) ──
    r->drawRectPx(preview.x, preview.y, preview.w, preview.h, { 0.09f, 0.095f, 0.115f }, 1);
    r->drawRectPx(preview.x, preview.y, preview.w, 1, ACCENT, 0.7f);
    r->drawTextLine(preview.x + 10, preview.y + 7, "VIEWPORT", { 0.55f, 0.62f, 0.72f }, 1);
    UIRect pv = { preview.x + 1, preview.y + 26, preview.w - 2, preview.h - 26 - 26 };
    if (pv.w > 20 && pv.h > 20) {
        // The panel chrome above is still sitting in the UI batch; flush it now or
        // it would be drawn *after* the 3D pass and paint straight over the sphere.
        r->flushUI();
        OrbitCamera cam;
        cam.target = { 0, 0, 0 };
        cam.distance = 3.1f;
        cam.yaw = previewYaw_;
        cam.pitch = previewPitch_;
        cam.update(pv.w / pv.h);
        Frame f;
        f.showGrid = false;
        f.shadowCenter = { 0, 0, 0 };
        f.env.fogDensity = 0;
        f.env.horizon = { 0.14f, 0.155f, 0.19f };
        f.env.zenith = { 0.07f, 0.08f, 0.10f };
        DrawItem it;
        it.mesh = (MeshType)std::min(std::max(previewMesh_, 0), (int)MESH_COUNT - 1);
        it.model = Mat4::identity();
        applyMaterialEval(ev, it);
        it.castShadow = false;
        it.albedoTex = ev.baseColorTex.empty() ? 0 : matLoadTexture(r, projectDir, ev.baseColorTex);
        if (!ev.fullyMasked()) f.items.push_back(it);
        r->render(f, cam, (int)pv.x, (int)pv.y, (int)pv.w, (int)pv.h, false);
        r->setUIScissor(canvasAll.x, canvasAll.y, canvasAll.w, canvasAll.h, true);
        if (ev.fullyMasked())
            r->drawTextLine(pv.x + 12, pv.y + pv.h * 0.5f - 6,
                            "Fully clipped by the Opacity Mask", { 0.72f, 0.6f, 0.4f }, 1);
    }
    // orbit the preview
    bool overPv = !inputBlocked(ui) &&
                  in.mouseX >= pv.x && in.mouseX < pv.x + pv.w && in.mouseY >= pv.y && in.mouseY < pv.y + pv.h;
    if (overPv && in.mousePressed) { previewOrbit_ = true; previewMouseX_ = in.mouseX; previewMouseY_ = in.mouseY; }
    if (previewOrbit_) {
        if (in.mouseDown) {
            previewYaw_ += (in.mouseX - previewMouseX_) * 0.01f;
            previewPitch_ = std::min(std::max(previewPitch_ + (in.mouseY - previewMouseY_) * 0.01f, -1.5f), 1.5f);
            previewMouseX_ = in.mouseX; previewMouseY_ = in.mouseY;
        } else previewOrbit_ = false;
    }
    // shape strip along the bottom of the viewport
    {
        static const char* shapes[MESH_COUNT] = { "Cube", "Sphere", "Cyl", "Cone", "Caps" };
        float bw = (preview.w - 16) / MESH_COUNT;
        for (int i = 0; i < MESH_COUNT; i++) {
            UIRect rc = { preview.x + 8 + i * bw, preview.y + preview.h - 24, bw - 3, 20 };
            bool on = previewMesh_ == i;
            bool over = !inputBlocked(ui) &&
                        in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
            r->drawRectPx(rc.x, rc.y, rc.w, rc.h, on ? Vec3{ 0.12f, 0.32f, 0.56f }
                                              : over ? Vec3{ 0.20f, 0.23f, 0.28f } : Vec3{ 0.135f, 0.145f, 0.175f }, 1);
            r->drawTextLine(rc.x + (rc.w - r->textWidth(shapes[i])) * 0.5f, ui.textCenterY(rc), shapes[i],
                            on ? Vec3{ 0.85f, 0.93f, 1.0f } : Vec3{ 0.66f, 0.71f, 0.79f }, 1);
            if (over && in.mousePressed) previewMesh_ = i;
        }
    }

    // ── details panel ──
    drawDetails(ui, details);

    // ── splitters ──
    {
        float x = canvasAll.x + leftW_;
        bool over = !inputBlocked(ui) && in.mouseX >= x && in.mouseX <= x + 8 &&
                    in.mouseY >= canvasAll.y && in.mouseY < canvasAll.y + canvasAll.h;
        if (over || dragSplit_ == 1) r->drawRectPx(x + 3, canvasAll.y, 2, canvasAll.h, ACCENT, 0.7f);
        if (over && in.mousePressed) dragSplit_ = 1;
    }
    {
        float y = canvasAll.y + previewH_;
        bool over = !inputBlocked(ui) && in.mouseX >= canvasAll.x && in.mouseX < canvasAll.x + leftW_ &&
                    in.mouseY >= y && in.mouseY <= y + 6;
        if (over || dragSplit_ == 2) r->drawRectPx(canvasAll.x, y + 2, leftW_, 2, ACCENT, 0.7f);
        if (over && in.mousePressed) dragSplit_ = 2;
    }
    if (dragSplit_ == 1) { leftW_ = in.mouseX - canvasAll.x; if (!in.mouseDown) dragSplit_ = 0; }
    if (dragSplit_ == 2) { previewH_ = in.mouseY - canvasAll.y; if (!in.mouseDown) dragSplit_ = 0; }
}

// ─── one node ───
void MaterialEditor::drawNode(UI& ui, MatNode& n, const UIRect& canvas) {
    Renderer* r = ui.r;
    const float Z = zoom_;
    UIRect rc = nodeRect(n, canvas);
    bool sel = n.id == selected_;
    Vec3 hdr = n.type == MAT_RESULT ? RESULT_COLOR : CAT_COLORS[matNodeCategory(n.type)];

    // ── knot: no header, no labels, just a dot on the wire ──
    if (matNodeIsReroute(n.type)) {
        r->drawRectPx(rc.x + 2, rc.y + 3, rc.w, rc.h, { 0, 0, 0 }, 0.3f);
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.145f, 0.16f, 0.19f }, 1);
        Vec3 border = sel ? Vec3{ 1, 0.8f, 0.3f } : Vec3{ 0.34f, 0.37f, 0.43f };
        r->drawRectPx(rc.x, rc.y, rc.w, 1, border, 1);
        r->drawRectPx(rc.x, rc.y + rc.h - 1, rc.w, 1, border, 1);
        r->drawRectPx(rc.x, rc.y, 1, rc.h, border, 1);
        r->drawRectPx(rc.x + rc.w - 1, rc.y, 1, rc.h, border, 1);
        r->drawRectPx(rc.x + rc.w * 0.5f - 3 * Z, rc.y + rc.h * 0.5f - 3 * Z, 6 * Z, 6 * Z, WIRE_COLOR, 1);
        float px, py;
        inPinPos(n, 0, canvas, px, py);
        r->drawRectPx(px - 4 * Z, py - 4 * Z, 8 * Z, 8 * Z, WIRE_COLOR, 1);
        outPinPos(n, 0, canvas, px, py);
        r->drawRectPx(px - 4 * Z, py - 4 * Z, 8 * Z, 8 * Z, WIRE_COLOR, 1);
        return;
    }

    r->drawRectPx(rc.x + 2, rc.y + 3, rc.w, rc.h, { 0, 0, 0 }, 0.32f);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.145f, 0.16f, 0.19f }, 1);
    if (sel) {
        r->drawRectPx(rc.x - 1, rc.y - 1, rc.w + 2, 1, { 1, 0.8f, 0.3f }, 1);
        r->drawRectPx(rc.x - 1, rc.y + rc.h, rc.w + 2, 1, { 1, 0.8f, 0.3f }, 1);
        r->drawRectPx(rc.x - 1, rc.y, 1, rc.h, { 1, 0.8f, 0.3f }, 1);
        r->drawRectPx(rc.x + rc.w, rc.y, 1, rc.h, { 1, 0.8f, 0.3f }, 1);
    }
    r->drawRectPx(rc.x, rc.y, rc.w, MAT_HDR_H * Z, hdr, 1);
    std::string title = matNodeName(n.type);
    if (matNodeIsParameter(n.type) && n.paramName[0]) title = n.paramName;
    r->drawTextLine(rc.x + 8 * Z, matTextY(rc.y, MAT_HDR_H * Z, Z), ui.ellipsize(title, rc.w / Z - 16),
                    { 0.94f, 0.96f, 1.0f }, 1, Z);

    drawNodeValue(ui, n, rc);

    // ── pins ──
    int nin = matNodeInputCount(n.type);
    for (int i = 0; i < nin; i++) {
        float px, py; inPinPos(n, i, canvas, px, py);
        bool conn = n.in[i] >= 0;
        bool live = n.type != MAT_RESULT || matResultPinEnabled(i, material.blendMode, material.shadingModel);
        Vec3 pc = !live ? Vec3{ 0.32f, 0.34f, 0.38f } : conn ? Vec3{ 0.88f, 0.91f, 0.96f } : Vec3{ 0.52f, 0.56f, 0.62f };
        float ps = 4.5f * Z;
        r->drawRectPx(px - ps, py - ps, 2 * ps, 2 * ps, pc, 1);
        if (!conn)
            r->drawRectPx(px - ps + 1.5f * Z, py - ps + 1.5f * Z, 2 * ps - 3 * Z, 2 * ps - 3 * Z, { 0.145f, 0.16f, 0.19f }, 1);
        r->drawTextLine(px + 9 * Z, matTextY(py - MAT_PIN_STEP * 0.5f * Z, MAT_PIN_STEP * Z, Z), matPinName(n.type, i),
                        live ? Vec3{ 0.76f, 0.81f, 0.88f } : Vec3{ 0.42f, 0.45f, 0.50f }, 1, Z);
    }
    int nout = matNodeOutputCount(n.type);
    for (int p = 0; p < nout; p++) {
        float px, py; outPinPos(n, p, canvas, px, py);
        float ps = 4.5f * Z;
        r->drawRectPx(px - ps, py - ps, 2 * ps, 2 * ps, { 0.88f, 0.91f, 0.96f }, 1);
        const char* nm = matOutPinName(n.type, p);
        if (nm && nm[0])
            r->drawTextLine(px - 9 * Z - r->textWidth(nm, Z), matTextY(py - MAT_PIN_STEP * 0.5f * Z, MAT_PIN_STEP * Z, Z),
                            nm, { 0.76f, 0.81f, 0.88f }, 1, Z);
    }
    if (n.type == MAT_RESULT) drawResultDefaults(ui, n, canvas);
}

// ─── the value strip under a node's header ───
// The fields scale their text with the node, and they are drawn the same way
// whether or not input happens to be blocked — UI::numberFieldRect already
// ignores the mouse while a popup is up, so nothing has to change on screen.
void MaterialEditor::drawNodeValue(UI& ui, MatNode& n, const UIRect& rc) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    const float Z = zoom_;
    float vh = matNodeValueH(n) * Z;
    if (vh <= 0) return;
    UIRect vr = { rc.x + 7 * Z, rc.y + MAT_HDR_H * Z + 3 * Z, rc.w - 14 * Z, vh - 6 * Z };
    const bool interactive = !inputBlocked(ui);
    char id[40];

    switch (n.type) {
    case MAT_CONST_COLOR: case MAT_CONST4: case MAT_VECTOR_PARAM: {
        r->drawRectPx(vr.x, vr.y, vr.w, vr.h, n.color, 1);
        r->drawRectPx(vr.x, vr.y, vr.w, 1, { 0.05f, 0.06f, 0.08f }, 1);
        bool over = interactive && in.mouseX >= vr.x && in.mouseX < vr.x + vr.w &&
                    in.mouseY >= vr.y && in.mouseY < vr.y + vr.h;
        snprintf(id, sizeof(id), "matn%d_col", n.id);
        if (over && in.mousePressed)
            ui.openColorPicker(id, &n.color, n.type == MAT_CONST_COLOR ? nullptr : &n.alpha, vr.x, vr.y + vr.h + 4);
        if (ui.takeColorPick(id)) dirty = true;
        break;
    }
    case MAT_CONST_FLOAT: case MAT_SCALAR_PARAM: {
        snprintf(id, sizeof(id), "matn%d_val", n.id);
        if (ui.numberFieldRect(id, vr, &n.scalar, 0.05f, nullptr, false, -1e9f, 1e9f, Z)) dirty = true;
        break;
    }
    case MAT_CONST2: {
        float half = (vr.w - 4 * Z) * 0.5f;
        snprintf(id, sizeof(id), "matn%d_r", n.id);
        if (ui.numberFieldRect(id, { vr.x, vr.y, half, vr.h }, &n.color.x, 0.05f, nullptr, false, -1e9f, 1e9f, Z)) dirty = true;
        snprintf(id, sizeof(id), "matn%d_g", n.id);
        if (ui.numberFieldRect(id, { vr.x + half + 4 * Z, vr.y, half, vr.h }, &n.color.y, 0.05f, nullptr, false, -1e9f, 1e9f, Z)) dirty = true;
        break;
    }
    case MAT_MASK: {
        static const char* chan[4] = { "R", "G", "B", "A" };
        float bw = (vr.w - 3 * 3 * Z) * 0.25f;
        for (int i = 0; i < 4; i++) {
            UIRect br = { vr.x + i * (bw + 3 * Z), vr.y, bw, vr.h };
            bool on = (n.mask & (1u << i)) != 0;
            bool over = interactive && in.mouseX >= br.x && in.mouseX < br.x + br.w &&
                        in.mouseY >= br.y && in.mouseY < br.y + br.h;
            r->drawRectPx(br.x, br.y, br.w, br.h, on ? Vec3{ 0.12f, 0.32f, 0.56f }
                                          : over ? Vec3{ 0.20f, 0.23f, 0.28f } : Vec3{ 0.10f, 0.11f, 0.13f }, 1);
            r->drawTextLine(br.x + (br.w - r->textWidth(chan[i], Z)) * 0.5f, matTextY(br.y, br.h, Z), chan[i],
                            on ? Vec3{ 0.88f, 0.94f, 1.0f } : Vec3{ 0.6f, 0.64f, 0.7f }, 1, Z);
            if (over && in.mousePressed) { n.mask ^= (1u << i); dirty = true; }
        }
        break;
    }
    case MAT_TEXTURE: case MAT_TEXTURE_PARAM: {
        r->drawRectPx(vr.x, vr.y, vr.w, vr.h, { 0.07f, 0.075f, 0.09f }, 1);
        GLuint tex = n.texturePath[0] ? matLoadTexture(r, projectDir, n.texturePath) : 0;
        if (tex) {
            float side = std::min(vr.w, vr.h);
            r->drawImagePx(tex, vr.x + (vr.w - side) * 0.5f, vr.y, side, side);
        } else {
            const char* none = n.texturePath[0] ? "missing" : "no texture";
            r->drawTextLine(vr.x + (vr.w - r->textWidth(none, Z)) * 0.5f, matTextY(vr.y, vr.h, Z), none,
                            { 0.55f, 0.58f, 0.64f }, 1, Z);
        }
        break;
    }
    default: break;
    }
}

// ─── the Result node's per-pin values ───
// Every enabled pin shows what it will use: the wire when one is attached, else
// an editable default, so a material needs no graph at all to be authored.
void MaterialEditor::drawResultDefaults(UI& ui, MatNode& n, const UIRect& canvas) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    const float Z = zoom_;
    UIRect rc = nodeRect(n, canvas);
    const float FIELD_W = 74 * Z;
    const bool interactive = !inputBlocked(ui);
    char id[40];
    for (int pin = 0; pin < matNodeInputCount(n.type); pin++) {
        if (!matResultPinEnabled(pin, material.blendMode, material.shadingModel)) continue;
        float px, py;
        inPinPos(n, pin, canvas, px, py);
        UIRect fr = { rc.x + rc.w - FIELD_W - 8 * Z, py - MAT_FIELD_H * 0.5f * Z, FIELD_W, MAT_FIELD_H * Z };
        if (n.in[pin] >= 0) {   // driven by the graph: show that, do not offer a value
            r->drawRectPx(fr.x, fr.y, fr.w, fr.h, { 0.11f, 0.13f, 0.16f }, 1);
            r->drawTextLine(fr.x + 6 * Z, matTextY(fr.y, fr.h, Z), "wired", { 0.46f, 0.62f, 0.80f }, 1, Z);
            continue;
        }
        if (MaterialAsset::resultPinIsColor(pin)) {
            r->drawRectPx(fr.x, fr.y, fr.w, fr.h, material.resultDef[pin], 1);
            r->drawRectPx(fr.x, fr.y, fr.w, 1, { 0.05f, 0.06f, 0.08f }, 1);
            bool over = interactive && in.mouseX >= fr.x && in.mouseX < fr.x + fr.w &&
                        in.mouseY >= fr.y && in.mouseY < fr.y + fr.h;
            snprintf(id, sizeof(id), "matres_col%d", pin);
            if (over && in.mousePressed)
                ui.openColorPicker(id, &material.resultDef[pin], nullptr, fr.x - 240, fr.y + fr.h + 4);
            if (ui.takeColorPick(id)) dirty = true;
        } else {
            snprintf(id, sizeof(id), "matres_num%d", pin);
            if (ui.numberFieldRect(id, fr, &material.resultDef[pin].x, 0.02f, nullptr, false, -1e9f, 1e9f, Z)) {
                material.resultDef[pin].y = material.resultDef[pin].z = material.resultDef[pin].x;
                dirty = true;
            }
        }
    }
}

// ─── details column ───
void MaterialEditor::drawDetails(UI& ui, const UIRect& rc) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.11f, 0.12f, 0.145f }, 0.98f);
    r->drawRectPx(rc.x, rc.y, rc.w, 1, ACCENT, 0.7f);
    r->drawTextLine(rc.x + 10, rc.y + 8, "DETAILS", { 0.55f, 0.62f, 0.72f }, 1);

    MatNode* sn = (!isInstance && selected_ >= 0) ? material.find(selected_) : nullptr;
    // The Result node is the material itself, exactly like Unreal: selecting it
    // (or nothing at all) shows the material settings rather than node properties.
    bool showSettings = !sn || sn->type == MAT_RESULT;

    UIRect body = { rc.x + 1, rc.y + 26, rc.w - 2, rc.h - 27 };
    bool overBody = !inputBlocked(ui) &&
                    in.mouseX >= body.x && in.mouseX < body.x + body.w &&
                    in.mouseY >= body.y && in.mouseY < body.y + body.h;
    if (overBody && in.wheel != 0) { detScroll_ -= in.wheel * 34; ui.consumeWheel(); }
    r->setUIScissor(body.x, body.y, body.w, body.h, true);
    float y = body.y + 6 - detScroll_;
    const float top = y;
    if (showSettings) drawMaterialSettings(ui, body, y);
    else drawNodeDetails(ui, body, *sn, y);
    r->setUIScissor(0, 0, 0, 0, false);
    detScroll_ = std::min(std::max(0.0f, detScroll_), std::max(0.0f, (y - top) - body.h + 14));
    ui.drawScrollbar(body, detScroll_, y - top);
}

// helpers shared by both Details modes: rows laid out at explicit rects
namespace {
struct DetailRows {
    UI& ui;
    Renderer* r;
    const UIInput& in;
    float x, w;
    float& y;
    bool blocked;

    // One line of text is 17 px tall, so nothing may advance by less than LINE —
    // a 16 px step made captions bite into the field under them.
    static const int LINE = 20;
    void section(const char* title) {
        y += 12;
        r->drawTextLine(x, y, title, { 0.5f, 0.55f, 0.62f }, 1);
        y += LINE + 2;
    }
    void help(const char* text) {
        r->drawTextLine(x, y, ui.ellipsize(text, w), { 0.48f, 0.52f, 0.58f }, 1);
        y += LINE - 2;
    }
    void caption(const char* text) {   // label sitting above its own field
        r->drawTextLine(x, y, ui.ellipsize(text, w), { 0.5f, 0.55f, 0.62f }, 1);
        y += LINE;
    }
    bool over(const UIRect& rc) const {
        return !blocked && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
               in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
    }
    bool toggle(const char* label, bool value) {
        UIRect rc = { x, y, w, 22 };
        bool ov = over(rc);
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, ov ? Vec3{ 0.20f, 0.24f, 0.30f } : Vec3{ 0.15f, 0.16f, 0.20f }, 1);
        Vec3 box = value ? ACCENT : Vec3{ 0.28f, 0.31f, 0.37f };
        r->drawRectPx(rc.x + 5, rc.y + 5, 12, 12, box, 1);
        if (value) r->drawRectPx(rc.x + 8, rc.y + 8, 6, 6, { 0.06f, 0.09f, 0.13f }, 1);
        r->drawTextLine(rc.x + 24, ui.textCenterY(rc), label, { 0.85f, 0.89f, 0.95f }, 1);
        y += 28;
        return ov && in.mousePressed;
    }
    // returns the newly picked index, or -1
    int enumRow(const char* label, const char* id, int value, const char* const* names, int count) {
        UIRect rc = { x, y, w, 22 };
        bool ov = over(rc);
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, ov ? Vec3{ 0.20f, 0.24f, 0.30f } : Vec3{ 0.15f, 0.16f, 0.20f }, 1);
        r->drawTextLine(rc.x + 6, ui.textCenterY(rc), label, { 0.85f, 0.89f, 0.95f }, 1);
        const char* v = names[value < 0 || value >= count ? 0 : value];
        float ax = rc.x + rc.w - 12, ay = rc.y + rc.h * 0.5f;
        r->drawTextLine(ax - 10 - r->textWidth(v), ui.textCenterY(rc), v, { 0.85f, 0.89f, 0.95f }, 1);
        r->drawTriPx(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3, { 0.62f, 0.68f, 0.78f }, 1);
        if (ov && in.mousePressed) ui.openEnumPicker(id, value, names, count, rc.x, rc.y + 24, rc.w);
        y += 28;
        int picked = -1;
        if (ui.takeEnumPick(id, &picked) && picked != value) return picked;
        return -1;
    }
    bool number(const char* label, const char* id, float* v, float step, float mn = -1e9f, float mx = 1e9f) {
        bool ch = ui.numberFieldRect(id, { x, y, w, 22 }, v, step, label, false, mn, mx);
        y += 28;
        return ch;
    }
    bool text(const char* label, const char* id, char* buf, int cap) {
        caption(label);
        bool ch = ui.textInputRect(id, buf, cap, { x, y, w, 24 });
        y += 30;
        return ch;
    }
    bool color(const char* label, const char* id, Vec3* rgb, float* alpha) {
        UIRect rc = { x, y, w, 24 };
        r->drawRectPx(rc.x, rc.y, 40, rc.h, *rgb, 1);
        r->drawRectPx(rc.x, rc.y, 40, 1, { 0.35f, 0.4f, 0.48f }, 0.8f);
        bool ov = over(rc);
        r->drawTextLine(rc.x + 48, ui.textCenterY(rc), ui.ellipsize(label, rc.w - 56),
                        ov ? Vec3{ 0.95f, 0.97f, 1.0f } : Vec3{ 0.8f, 0.84f, 0.9f }, 1);
        if (ov && in.mousePressed) ui.openColorPicker(id, rgb, alpha, rc.x, rc.y + 28);
        y += 30;
        // the picker writes straight through the pointer, so ask whether it moved
        return ui.takeColorPick(id);
    }
    // a row that only reports what the graph is feeding a pin
    void wired(const char* label) {
        UIRect rc = { x, y, w, 22 };
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.115f, 0.13f, 0.16f }, 1);
        r->drawTextLine(rc.x + 6, ui.textCenterY(rc), label, { 0.62f, 0.66f, 0.72f }, 1);
        const char* tag = "wired";
        r->drawTextLine(rc.x + rc.w - r->textWidth(tag) - 8, ui.textCenterY(rc), tag, { 0.46f, 0.62f, 0.80f }, 1);
        y += 28;
    }
    bool button(const char* label, Vec3 bg, Vec3 fg) {
        UIRect rc = { x, y, w, 24 };
        bool ov = over(rc);
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, ov ? bg * 1.35f : bg, 1);
        r->drawTextLine(rc.x + (rc.w - r->textWidth(label)) * 0.5f, ui.textCenterY(rc), label, fg, 1);
        y += 30;
        return ov && in.mousePressed;
    }
};
}  // namespace

void MaterialEditor::drawMaterialSettings(UI& ui, const UIRect& rc, float& y) {
    Renderer* r = ui.r;
    DetailRows row{ ui, r, ui.input(), rc.x + 12, rc.w - 24, y, inputBlocked(ui) };

    r->drawTextLine(row.x, y, isInstance ? "Material Instance" : "Material", { 0.9f, 0.92f, 0.97f }, 1);
    y += DetailRows::LINE;
    r->drawTextLine(row.x, y, ui.ellipsize(curPath.empty() ? "(unsaved)" : curPath, row.w), { 0.5f, 0.55f, 0.62f }, 1);
    y += DetailRows::LINE;

    // ── an instance owns no settings: it shows (and picks) its parent instead ──
    if (isInstance) {
        row.section("PARENT");
        if (materialAssets) {
            std::vector<UIAssetOption> options;
            UIAssetOption none; none.label = "None"; options.push_back(none);
            int current = 0;
            for (const std::string& rel : *materialAssets) {
                if (matPathIsInstance(rel)) continue;   // an instance cannot parent an instance
                UIAssetOption option;
                option.label = rel;
                option.iconImage = "material";
                if (_stricmp(rel.c_str(), instance.parent) == 0) current = (int)options.size();
                options.push_back(option);
            }
            float used = 0;
            int picked = ui.assetFieldRect("mi_parent", { row.x, y, row.w, 46 }, current, options, &used);
            y += used + 10;
            if (picked >= 0) {
                snprintf(instance.parent, sizeof(instance.parent), "%s",
                         picked == 0 ? "" : options[picked].label.c_str());
                reloadParent();
                dirty = true;
            }
        } else if (row.text("Parent (.mat)", "mi_parentpath", instance.parent, sizeof(instance.parent))) {
            reloadParent();
            dirty = true;
        }
        if (instance.parent[0] && parentGraph.nodes.empty())
            row.help("Parent could not be read.");

        row.section("INHERITED");
        static const char* blendNames[MBM_COUNT] = { "Opaque", "Masked", "Translucent", "Additive" };
        static const char* modelNames[MSM_COUNT] = { "Default Lit", "Unlit" };
        char line[128];
        snprintf(line, sizeof(line), "Blend: %s",
                 blendNames[parentGraph.blendMode < MBM_COUNT ? parentGraph.blendMode : 0]);
        row.help(line);
        snprintf(line, sizeof(line), "Shading: %s",
                 modelNames[parentGraph.shadingModel < MSM_COUNT ? parentGraph.shadingModel : 0]);
        row.help(line);
        snprintf(line, sizeof(line), "Two Sided: %s", parentGraph.twoSided ? "yes" : "no");
        row.help(line);
        int overridden = 0;
        for (const MatParamOverride& o : instance.overrides) if (o.enabled) overridden++;
        snprintf(line, sizeof(line), "%d parameter(s) overridden", overridden);
        row.section("INSTANCE");
        row.help(line);
        row.help("Settings and logic come from");
        row.help("the parent material.");
        y += 8;
        if (row.button("Reset all overrides", { 0.30f, 0.16f, 0.16f }, { 0.95f, 0.85f, 0.85f })) {
            for (MatParamOverride& o : instance.overrides) o.enabled = false;
            dirty = true;
        }
        return;
    }

    row.section("MATERIAL");
    {
        static const char* blends[MBM_COUNT] = { "Opaque", "Masked", "Translucent", "Additive" };
        int picked = row.enumRow("Blend Mode", "mat_blend", material.blendMode, blends, MBM_COUNT);
        if (picked >= 0) { material.blendMode = picked; dirty = true; }
    }
    {
        static const char* models[MSM_COUNT] = { "Default Lit", "Unlit" };
        int picked = row.enumRow("Shading Model", "mat_shading", material.shadingModel, models, MSM_COUNT);
        if (picked >= 0) { material.shadingModel = picked; dirty = true; }
    }
    if (row.toggle("Two Sided", material.twoSided)) { material.twoSided = !material.twoSided; dirty = true; }
    if (row.toggle("Cast Shadow", material.castShadow)) { material.castShadow = !material.castShadow; dirty = true; }

    if (material.blendMode == MBM_MASKED) {
        row.section("TRANSLUCENCY");
        if (row.number("Opacity Mask Clip Value", "mat_clip", &material.maskClip, 0.02f, 0.0f, 1.0f)) dirty = true;
    }

    // ── the Result pins, editable here as well as on the node ──
    row.section("SURFACE");
    char buf[160];
    {
        const MatNode* rn = material.find(material.resultId());
        char id[32];
        for (int pinIndex = 0; pinIndex < MAT_MAX_IN; pinIndex++) {
            if (!matResultPinEnabled(pinIndex, material.blendMode, material.shadingModel)) continue;
            const char* label = matPinName(MAT_RESULT, pinIndex);
            if (rn && rn->in[pinIndex] >= 0) { row.wired(label); continue; }
            if (MaterialAsset::resultPinIsColor(pinIndex)) {
                snprintf(id, sizeof(id), "matres_d%d", pinIndex);
                if (row.color(label, id, &material.resultDef[pinIndex], nullptr)) dirty = true;
            } else {
                snprintf(id, sizeof(id), "matres_n%d", pinIndex);
                if (row.number(label, id, &material.resultDef[pinIndex].x, 0.02f)) {
                    material.resultDef[pinIndex].y = material.resultDef[pinIndex].z = material.resultDef[pinIndex].x;
                    dirty = true;
                }
            }
        }
        MaterialEval ev = material.evaluate();
        if (!ev.baseColorTex.empty()) {
            snprintf(buf, sizeof(buf), "Texture: %s", ev.baseColorTex.c_str());
            row.help(buf);
        }
    }


    row.section("GRAPH");
    snprintf(buf, sizeof(buf), "%d nodes", (int)material.nodes.size());
    row.help(buf);
    row.help("Folded on the CPU: only the");
    row.help("Base Color texture is live.");
    y += 6;
    if (row.button("Frame the graph", { 0.18f, 0.22f, 0.30f }, { 0.85f, 0.9f, 0.97f }))
        frameGraph(canvasRect_);
}

void MaterialEditor::drawNodeDetails(UI& ui, const UIRect& rc, MatNode& n, float& y) {
    Renderer* r = ui.r;
    DetailRows row{ ui, r, ui.input(), rc.x + 12, rc.w - 24, y, inputBlocked(ui) };

    r->drawTextLine(row.x, y, matNodeName(n.type), { 0.9f, 0.92f, 0.97f }, 1);
    y += DetailRows::LINE + 2;
    r->drawRectPx(row.x, y + 4, 10, 10, CAT_COLORS[matNodeCategory(n.type)], 1);
    r->drawTextLine(row.x + 16, y, matCategoryName(matNodeCategory(n.type)), { 0.55f, 0.60f, 0.68f }, 1);
    y += DetailRows::LINE + 2;
    {
        // the hint wraps by hand: the panel is narrow and the font is fixed-height
        std::string hint = matNodeHint(n.type);
        size_t start = 0;
        while (start < hint.size()) {
            size_t take = hint.size() - start, best = take;
            while (best > 0 && r->textWidth(hint.substr(start, best)) > row.w) {
                size_t space = hint.rfind(' ', start + best - 1);
                best = space == std::string::npos || space <= start ? best - 1 : space - start;
            }
            if (best == 0) best = 1;
            r->drawTextLine(row.x, y, hint.substr(start, best), { 0.48f, 0.52f, 0.58f }, 1);
            y += DetailRows::LINE - 2;
            start += best;
            while (start < hint.size() && hint[start] == ' ') start++;
        }
    }

    // Parameters are addressed by name, so a duplicate is always a mistake: the
    // field types freely and the name is made unique when it loses focus.
    auto paramNameRow = [&](const char* fieldId) {
        bool changed = row.text("Parameter Name", fieldId, n.paramName, sizeof(n.paramName));
        if (changed) dirty = true;
        if (!ui.inputFocused(fieldId)) {
            std::string unique = material.uniqueParamName(n.paramName, n.id);
            if (unique != n.paramName) {
                snprintf(n.paramName, sizeof(n.paramName), "%s", unique.c_str());
                dirty = true;
            }
        } else {
            row.help("Names must be unique.");
        }
    };

    row.section("VALUE");
    switch (n.type) {
    case MAT_CONST_FLOAT: case MAT_SCALAR_PARAM:
        if (n.type == MAT_SCALAR_PARAM) paramNameRow("mat_pname");
        if (row.number("Value", "mat_scalar", &n.scalar, 0.05f)) dirty = true;
        break;
    case MAT_CONST_COLOR:
        if (row.color("Constant", "mat_col", &n.color, nullptr)) dirty = true;
        if (row.number("R", "mat_cr", &n.color.x, 0.02f)) dirty = true;
        if (row.number("G", "mat_cg", &n.color.y, 0.02f)) dirty = true;
        if (row.number("B", "mat_cb", &n.color.z, 0.02f)) dirty = true;
        break;
    case MAT_CONST2:
        if (row.number("R", "mat_c2r", &n.color.x, 0.02f)) dirty = true;
        if (row.number("G", "mat_c2g", &n.color.y, 0.02f)) dirty = true;
        break;
    case MAT_CONST4: case MAT_VECTOR_PARAM:
        if (n.type == MAT_VECTOR_PARAM) paramNameRow("mat_vpname");
        if (row.color("Constant", "mat_col4", &n.color, &n.alpha)) dirty = true;
        if (row.number("R", "mat_c4r", &n.color.x, 0.02f)) dirty = true;
        if (row.number("G", "mat_c4g", &n.color.y, 0.02f)) dirty = true;
        if (row.number("B", "mat_c4b", &n.color.z, 0.02f)) dirty = true;
        if (row.number("A", "mat_c4a", &n.alpha, 0.02f)) dirty = true;
        break;
    case MAT_TEXTURE: case MAT_TEXTURE_PARAM: {
        if (n.type == MAT_TEXTURE_PARAM) paramNameRow("mat_tpname");
        // Big Unreal-style picker over every .png in the project, with the image
        // itself as the thumbnail. Without a list (no project scan) fall back to
        // a typed path so the node is never uneditable.
        if (textureAssets) {
            std::vector<UIAssetOption> options;
            UIAssetOption none; none.label = "None"; options.push_back(none);
            int current = 0;
            for (const std::string& rel : *textureAssets) {
                UIAssetOption option;
                option.label = rel;
                option.tex = matLoadTexture(r, projectDir, rel);
                if (_stricmp(rel.c_str(), n.texturePath) == 0) current = (int)options.size();
                options.push_back(option);
            }
            row.caption("Texture");
            float used = 0;
            int picked = ui.assetFieldRect("mat_texfield", { row.x, y, row.w, 46 }, current, options, &used);
            y += used + 10;
            if (picked >= 0) {
                if (picked == 0) n.texturePath[0] = 0;
                else snprintf(n.texturePath, sizeof(n.texturePath), "%s", options[picked].label.c_str());
                dirty = true;
            }
        } else if (row.text("Texture (.png, project-relative)", "mat_tex", n.texturePath, sizeof(n.texturePath))) {
            dirty = true;
        }
        row.help("Sampled in object space.");
        break;
    }
    case MAT_LERP:
        if (row.number("Alpha (when unconnected)", "mat_lerp_a", &n.scalar, 0.02f, 0.0f, 1.0f)) dirty = true;
        break;
    case MAT_MASK: {
        static const char* chan[4] = { "R", "G", "B", "A" };
        for (int i = 0; i < 4; i++)
            if (row.toggle(chan[i], (n.mask & (1u << i)) != 0)) { n.mask ^= (1u << i); dirty = true; }
        break;
    }
    default:
        row.help("This node has no settings.");
        break;
    }

    // ── what each input is folding to right now ──
    int nin = matNodeInputCount(n.type);
    if (nin > 0) {
        row.section("INPUTS");
        for (int i = 0; i < nin; i++) {
            char line[160];
            if (n.in[i] < 0) {
                snprintf(line, sizeof(line), "%s: unconnected", matPinName(n.type, i));
            } else {
                MatValue v = material.evalInput(n.id, i);
                const MatNode* src = material.find(n.in[i]);
                char val[64];
                if (v.comps == 1) snprintf(val, sizeof(val), "%.3g", v.c[0]);
                else if (v.comps == 2) snprintf(val, sizeof(val), "%.3g, %.3g", v.c[0], v.c[1]);
                else if (v.comps == 3) snprintf(val, sizeof(val), "%.3g, %.3g, %.3g", v.c[0], v.c[1], v.c[2]);
                else snprintf(val, sizeof(val), "%.3g, %.3g, %.3g, %.3g", v.c[0], v.c[1], v.c[2], v.c[3]);
                snprintf(line, sizeof(line), "%s: %s  [%s]", matPinName(n.type, i), val,
                         src ? matNodeName(src->type) : "?");
            }
            r->drawTextLine(row.x, y, ui.ellipsize(line, row.w), { 0.72f, 0.76f, 0.83f }, 1);
            y += DetailRows::LINE - 2;
        }
    }

    y += 12;
    if (row.button("Delete node", { 0.35f, 0.14f, 0.14f }, { 0.95f, 0.82f, 0.82f })) {
        material.removeNode(n.id);
        selected_ = -1;
        dirty = true;
    }
}

// ─── right-click palette (same shape as the Blueprint one) ───
void MaterialEditor::drawPalette(UI& ui, const UIRect& canvas) {
    if (!paletteOpen_) return;
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    const float PW = 250;
    float palH = canvas.h < 400 ? canvas.h - 10 : 400;
    if (palX_ + PW > canvas.x + canvas.w) palX_ = canvas.x + canvas.w - PW;
    if (palY_ + palH > canvas.y + canvas.h) palY_ = canvas.y + canvas.h - palH;
    if (palX_ < canvas.x) palX_ = canvas.x;
    if (palY_ < canvas.y) palY_ = canvas.y;

    // a click outside closes it (checked before the rows consume the press)
    bool inside = in.mouseX >= palX_ && in.mouseX < palX_ + PW && in.mouseY >= palY_ && in.mouseY < palY_ + palH;
    if (in.mousePressed && !inside) { paletteOpen_ = false; palLinkMode_ = false; return; }
    if (in.wheel != 0 && inside) { palScroll_ += in.wheel * 30; ui.consumeWheel(); }

    r->drawRectPx(palX_ + 3, palY_ + 4, PW, palH, { 0, 0, 0 }, 0.35f);
    r->drawRectPx(palX_, palY_, PW, palH, { 0.12f, 0.135f, 0.16f }, 0.99f);
    r->drawRectPx(palX_, palY_, PW, 1, ACCENT, 0.8f);

    // search box: typing goes here while the palette is open
    {
        int len = (int)strlen(palSearch_);
        for (int i = 0; i < in.typedCount; i++) {
            char ch = in.typed[i];
            if (ch >= 32 && ch < 127 && len < (int)sizeof(palSearch_) - 1) { palSearch_[len++] = ch; palSearch_[len] = 0; }
        }
        if (in.keyBackspace && len > 0) palSearch_[len - 1] = 0;
    }
    r->drawRectPx(palX_ + 8, palY_ + 8, PW - 16, 22, { 0.07f, 0.08f, 0.1f }, 1);
    if (palSearch_[0]) r->drawTextLine(palX_ + 14, palY_ + 12, palSearch_, { 0.9f, 0.93f, 1.0f }, 1);
    else r->drawTextLine(palX_ + 14, palY_ + 12, "search node...", { 0.45f, 0.5f, 0.58f }, 1);
    r->drawTextLine(palX_ + 15 + r->textWidth(palSearch_), palY_ + 12, "|", ACCENT, 1);

    // rows: category headers (def == -1) and the nodes under them
    struct PalRow { int def; int cat; };
    std::vector<PalRow> rows;
    auto matches = [&](const char* name) {
        if (!palSearch_[0]) return true;
        std::string a = name, b = palSearch_;
        for (auto& c : a) c = (char)tolower((unsigned char)c);
        for (auto& c : b) c = (char)tolower((unsigned char)c);
        return a.find(b) != std::string::npos;
    };
    bool searching = palSearch_[0] != 0;
    if (searching) {
        for (int t = 1; t < MAT_NODE_COUNT; t++) if (matches(matNodeName(t))) rows.push_back({ t, matNodeCategory(t) });
    } else {
        for (int cat = 0; cat < MCAT_COUNT; cat++) {
            rows.push_back({ -1, cat });
            if (!(palCatOpen_ & (1u << cat))) continue;
            for (int t = 1; t < MAT_NODE_COUNT; t++) if (matNodeCategory(t) == cat) rows.push_back({ t, cat });
        }
    }

    const float RH_CAT = 26, RH_ITEM = 21;
    float listTop = palY_ + 38, listBot = palY_ + palH - 6;
    float contentH = 0;
    for (const auto& row : rows) contentH += row.def < 0 ? RH_CAT : RH_ITEM;
    float minScroll = std::min(0.0f, listBot - listTop - contentH);
    palScroll_ = std::min(0.0f, std::max(minScroll, palScroll_));

    r->setUIScissor(palX_, listTop, PW, listBot - listTop, true);
    float iy = listTop + palScroll_;
    for (int ri = 0; ri < (int)rows.size(); ri++) {
        float rh = rows[ri].def < 0 ? RH_CAT : RH_ITEM;
        float rowY = iy;
        iy += rh;
        if (rowY < listTop - rh || rowY > listBot) continue;
        bool hov = in.mouseX >= palX_ && in.mouseX < palX_ + PW && in.mouseY >= rowY && in.mouseY < rowY + rh - 2;
        if (rows[ri].def < 0) {
            bool open = (palCatOpen_ & (1u << rows[ri].cat)) != 0;
            r->drawRectPx(palX_ + 4, rowY + 2, PW - 8, rh - 5,
                          hov ? Vec3{ 0.19f, 0.22f, 0.28f } : Vec3{ 0.155f, 0.175f, 0.21f }, 1);
            r->drawTextLine(palX_ + 12, ui.textCenterY({ palX_, rowY, PW, rh }), open ? "v" : ">", ACCENT, 1);
            r->drawRectPx(palX_ + 28, rowY + 8, 8, 8, CAT_COLORS[rows[ri].cat], 1);
            r->drawTextLine(palX_ + 44, ui.textCenterY({ palX_, rowY, PW, rh }), matCategoryName(rows[ri].cat), { 0.82f, 0.86f, 0.92f }, 1);
            if (hov && in.mousePressed) palCatOpen_ ^= 1u << rows[ri].cat;
        } else {
            if (hov) r->drawRectPx(palX_ + 6, rowY + 1, PW - 12, rh - 3, { 0.2f, 0.32f, 0.5f }, 1);
            float ix = searching ? 14.0f : 34.0f;
            r->drawRectPx(palX_ + ix, rowY + 7, 7, 7, CAT_COLORS[rows[ri].cat], 1);
            r->drawTextLine(palX_ + ix + 15, ui.textCenterY({ palX_, rowY, PW, rh }, 0.92f), matNodeName(rows[ri].def),
                            hov ? Vec3{ 0.9f, 0.95f, 1.0f } : Vec3{ 0.75f, 0.79f, 0.85f }, 1, 0.92f);
            if (hov && in.mousePressed) {
                int id = material.addNode(rows[ri].def, palWX_, palWY_);
                MatNode* added = material.find(id);
                if (added && matNodeIsParameter(added->type))
                    snprintf(added->paramName, sizeof(added->paramName), "%s",
                             material.uniqueParamName(added->paramName, id).c_str());
                if (palLinkMode_) {
                    // auto-wire: an output drop feeds the new node's first input,
                    // an input drop takes the new node's first output
                    if (palLinkOut_) material.connect(palLinkNode_, palLinkPin_, id, 0);
                    else material.connect(id, 0, palLinkNode_, palLinkPin_);
                    palLinkMode_ = false;
                }
                selected_ = id;
                paletteOpen_ = false;
                dirty = true;
            }
        }
    }
    r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);
}

// ─── material instance: the parent's parameters, each with an override switch ───
void MaterialEditor::drawInstanceParameters(UI& ui, const UIRect& rc) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    r->setUIScissor(rc.x, rc.y, rc.w, rc.h, true);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.095f, 0.10f, 0.12f }, 1);
    r->drawRectPx(rc.x, rc.y, rc.w, 1, ACCENT, 0.7f);
    r->drawTextLine(rc.x + 12, rc.y + 8, "PARAMETERS", { 0.55f, 0.62f, 0.72f }, 1);

    std::vector<MatParamInfo> params = matCollectParameters(parentGraph);
    instance.pruneAgainst(params);

    UIRect body = { rc.x, rc.y + 30, rc.w, rc.h - 30 };
    bool overBody = !inputBlocked(ui) && in.mouseX >= body.x && in.mouseX < body.x + body.w &&
                    in.mouseY >= body.y && in.mouseY < body.y + body.h;
    if (overBody && in.wheel != 0) { instScroll_ -= in.wheel * 34; ui.consumeWheel(); }
    r->setUIScissor(body.x, body.y, body.w, body.h, true);

    const float X = body.x + 16, W = std::min(body.w - 32, 520.0f);
    float y = body.y + 8 - instScroll_;
    const float top = y;

    if (!instance.parent[0]) {
        r->drawTextLine(X, y, "No parent material assigned.", { 0.72f, 0.62f, 0.45f }, 1);
        r->drawTextLine(X, y + 22, "Pick one in the Details panel.", { 0.5f, 0.55f, 0.62f }, 1);
        y += 44;
    } else if (params.empty()) {
        r->drawTextLine(X, y, "The parent exposes no parameters.", { 0.72f, 0.62f, 0.45f }, 1);
        r->drawTextLine(X, y + 22, "Right-click a constant in the parent", { 0.5f, 0.55f, 0.62f }, 1);
        r->drawTextLine(X, y + 40, "and convert it to a parameter.", { 0.5f, 0.55f, 0.62f }, 1);
        y += 62;
    }

    char id[64];
    for (const MatParamInfo& info : params) {
        MatParamOverride& o = instance.ensure(info);
        UIRect row = { X, y, W, 30 };
        bool rowOver = !inputBlocked(ui) && in.mouseX >= row.x && in.mouseX < row.x + row.w &&
                       in.mouseY >= row.y && in.mouseY < row.y + row.h;
        r->drawRectPx(row.x, row.y, row.w, row.h,
                      o.enabled ? Vec3{ 0.14f, 0.18f, 0.24f } : Vec3{ 0.115f, 0.125f, 0.15f }, 1);
        // the override switch, exactly like Unreal's per-parameter check box
        UIRect box = { row.x + 8, row.y + 8, 14, 14 };
        r->drawRectPx(box.x, box.y, box.w, box.h, o.enabled ? ACCENT : Vec3{ 0.28f, 0.31f, 0.37f }, 1);
        if (o.enabled) r->drawRectPx(box.x + 3, box.y + 3, 8, 8, { 0.06f, 0.09f, 0.13f }, 1);
        bool overBox = !inputBlocked(ui) && in.mouseX >= box.x - 3 && in.mouseX < box.x + box.w + 3 &&
                       in.mouseY >= box.y - 3 && in.mouseY < box.y + box.h + 3;
        if (overBox && in.mousePressed) { o.enabled = !o.enabled; dirty = true; }

        const char* kindTag = info.kind == MPK_SCALAR ? "Scalar" : info.kind == MPK_VECTOR ? "Vector" : "Texture";
        Vec3 nameColor = o.enabled ? Vec3{ 0.9f, 0.94f, 1.0f } : Vec3{ 0.60f, 0.64f, 0.71f };
        r->drawTextLine(row.x + 30, ui.textCenterY(row), ui.ellipsize(info.name, 150), nameColor, 1);
        r->drawTextLine(row.x + 190, ui.textCenterY(row), kindTag, { 0.45f, 0.50f, 0.58f }, 1);

        UIRect field = { row.x + 268, row.y + 4, row.w - 276, 22 };
        if (!o.enabled) {
            // showing the parent's value, greyed: this row is inherited
            char shown[96];
            if (info.kind == MPK_SCALAR) snprintf(shown, sizeof(shown), "%.4g", info.scalar);
            else if (info.kind == MPK_VECTOR)
                snprintf(shown, sizeof(shown), "%.2f  %.2f  %.2f", info.color.x, info.color.y, info.color.z);
            else snprintf(shown, sizeof(shown), "%s", info.texture.empty() ? "(none)" : info.texture.c_str());
            if (info.kind == MPK_VECTOR) r->drawRectPx(field.x, field.y + 3, 26, 16, info.color * 0.55f, 1);
            r->drawTextLine(field.x + (info.kind == MPK_VECTOR ? 34.0f : 0.0f), ui.textCenterY(field),
                            ui.ellipsize(shown, field.w - 40), { 0.48f, 0.52f, 0.58f }, 1);
        } else if (info.kind == MPK_SCALAR) {
            snprintf(id, sizeof(id), "mi_s_%s", o.name);
            if (ui.numberFieldRect(id, field, &o.scalar, 0.02f)) dirty = true;
        } else if (info.kind == MPK_VECTOR) {
            r->drawRectPx(field.x, field.y, 46, field.h, o.color, 1);
            r->drawRectPx(field.x, field.y, 46, 1, { 0.35f, 0.4f, 0.48f }, 0.8f);
            bool overSwatch = !inputBlocked(ui) && in.mouseX >= field.x && in.mouseX < field.x + 46 &&
                              in.mouseY >= field.y && in.mouseY < field.y + field.h;
            snprintf(id, sizeof(id), "mi_v_%s", o.name);
            if (overSwatch && in.mousePressed) ui.openColorPicker(id, &o.color, &o.alpha, field.x, field.y + 26);
            if (ui.takeColorPick(id)) dirty = true;
            char rgba[64];
            snprintf(rgba, sizeof(rgba), "%.2f  %.2f  %.2f  a %.2f", o.color.x, o.color.y, o.color.z, o.alpha);
            r->drawTextLine(field.x + 54, ui.textCenterY(field), rgba, { 0.78f, 0.82f, 0.88f }, 1);
        } else {
            snprintf(id, sizeof(id), "mi_t_%s", o.name);
            if (textureAssets) {
                std::vector<UIAssetOption> options;
                UIAssetOption none; none.label = "None"; options.push_back(none);
                int current = 0;
                for (const std::string& rel : *textureAssets) {
                    UIAssetOption option;
                    option.label = rel;
                    option.tex = matLoadTexture(r, projectDir, rel);
                    if (_stricmp(rel.c_str(), o.texturePath) == 0) current = (int)options.size();
                    options.push_back(option);
                }
                float used = 0;
                int picked = ui.assetFieldRect(id, { field.x, row.y + 1, field.w, 28 }, current, options, &used);
                if (used > row.h) y += used - row.h + 2;   // the open list pushes the rows below down
                if (picked >= 0) {
                    if (picked == 0) o.texturePath[0] = 0;
                    else snprintf(o.texturePath, sizeof(o.texturePath), "%s", options[picked].label.c_str());
                    dirty = true;
                }
            } else if (ui.textInputRect(id, o.texturePath, sizeof(o.texturePath), field)) {
                dirty = true;
            }
        }
        (void)rowOver;
        y += 34;
    }

    r->setUIScissor(0, 0, 0, 0, false);
    instScroll_ = std::min(std::max(0.0f, instScroll_), std::max(0.0f, (y - top) - body.h + 16));
    ui.drawScrollbar(body, instScroll_, y - top);
}

// ─── right-click on a node ───
void MaterialEditor::drawNodeMenu(UI& ui, const UIRect& canvas) {
    if (!ctxOpen_) return;
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    MatNode* n = material.find(ctxNode_);
    if (!n) { ctxOpen_ = false; return; }

    struct Item { const char* label; int action; };   // 1 = to parameter, 2 = duplicate, 3 = delete
    std::vector<Item> items;
    int paramForm = matParameterFormOf(n->type);
    if (paramForm >= 0)
        items.push_back({ paramForm == MAT_SCALAR_PARAM ? "Convert to Scalar Parameter"
                                                        : "Convert to Vector Parameter", 1 });
    // the Result is the material itself: there can only ever be one, and it is
    // neither duplicated nor deleted
    if (n->type != MAT_RESULT) { items.push_back({ "Duplicate", 2 }); items.push_back({ "Delete", 3 }); }

    const float IH = 22, PAD = 4;
    float w = 210, h = items.size() * IH + PAD * 2;
    float x = std::min(ctxX_, canvas.x + canvas.w - w - 4);
    float y = std::min(ctxY_, canvas.y + canvas.h - h - 4);
    x = std::max(x, canvas.x + 2); y = std::max(y, canvas.y + 2);

    bool inside = in.mouseX >= x && in.mouseX < x + w && in.mouseY >= y && in.mouseY < y + h;
    if (in.mousePressed && !inside) { ctxOpen_ = false; return; }

    r->drawRectPx(x + 3, y + 4, w, h, { 0, 0, 0 }, 0.35f);
    r->drawRectPx(x, y, w, h, { 0.12f, 0.135f, 0.16f }, 0.99f);
    r->drawRectPx(x, y, w, 1, ACCENT, 0.8f);
    for (int i = 0; i < (int)items.size(); i++) {
        UIRect row = { x + PAD, y + PAD + i * IH, w - PAD * 2, IH };
        bool hov = in.mouseX >= row.x && in.mouseX < row.x + row.w &&
                   in.mouseY >= row.y && in.mouseY < row.y + row.h;
        if (hov) r->drawRectPx(row.x, row.y, row.w, row.h, { 0.2f, 0.32f, 0.5f }, 1);
        Vec3 fg = items[i].action == 3 ? Vec3{ 0.95f, 0.76f, 0.76f } : Vec3{ 0.82f, 0.86f, 0.92f };
        r->drawTextLine(row.x + 10, ui.textCenterY(row), items[i].label, hov ? Vec3{ 0.95f, 0.97f, 1.0f } : fg, 1);
        if (!hov || !in.mousePressed) continue;
        switch (items[i].action) {
        case 1:
            if (material.convertToParameter(n->id)) dirty = true;
            break;
        case 2: {
            // copy the source out first: addNode may reallocate the node vector
            MatNode src = *n;
            int copy = material.addNode(src.type, src.x + 26, src.y + 26);
            if (MatNode* c = material.find(copy)) {
                int keepId = c->id;
                float kx = c->x, ky = c->y;
                *c = src;                      // value, texture, mask — but not the wires
                c->id = keepId; c->x = kx; c->y = ky;
                for (int p = 0; p < MAT_MAX_IN; p++) { c->in[p] = -1; c->inPin[p] = 0; }
                if (matNodeIsParameter(c->type))
                    snprintf(c->paramName, sizeof(c->paramName), "%s",
                             material.uniqueParamName(c->paramName, keepId).c_str());
                selected_ = keepId;
            }
            dirty = true;
            break;
        }
        case 3:
            material.removeNode(n->id);
            selected_ = -1;
            dirty = true;
            break;
        }
        ctxOpen_ = false;
        break;
    }
}
