#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "material.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

// ─── node metadata ───
int matNodeInputCount(int type) {
    switch (type) {
    case MAT_RESULT: return 4;
    case MAT_MULTIPLY:
    case MAT_ADD: return 2;
    case MAT_LERP: return 3;
    default: return 0;   // constants, texture
    }
}
const char* matNodeName(int type) {
    switch (type) {
    case MAT_RESULT: return "Result";
    case MAT_CONST_COLOR: return "Constant Color";
    case MAT_CONST_FLOAT: return "Constant Float";
    case MAT_TEXTURE: return "Texture Sample";
    case MAT_MULTIPLY: return "Multiply";
    case MAT_ADD: return "Add";
    case MAT_LERP: return "Lerp";
    }
    return "?";
}
const char* matPinName(int type, int pin) {
    if (type == MAT_RESULT) {
        static const char* r[] = { "Base Color", "Metallic", "Roughness", "Emissive" };
        return (pin >= 0 && pin < 4) ? r[pin] : "";
    }
    if (type == MAT_LERP) { static const char* l[] = { "A", "B", "Alpha" }; return (pin >= 0 && pin < 3) ? l[pin] : ""; }
    static const char* ab[] = { "A", "B" };
    return (pin >= 0 && pin < 2) ? ab[pin] : "";
}

static Vec3 cmul(const Vec3& a, const Vec3& b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }

// ─── MaterialAsset ───
void MaterialAsset::makeDefault() {
    nodes.clear();
    nextId = 1;
    int result = addNode(MAT_RESULT, 430, 120);
    int col = addNode(MAT_CONST_COLOR, 120, 120);
    find(col)->color = { 0.75f, 0.75f, 0.78f };
    find(result)->in[0] = col;   // BaseColor <- ConstColor
}

MatNode* MaterialAsset::find(int id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
const MatNode* MaterialAsset::find(int id) const {
    for (auto& n : nodes) if (n.id == id) return &n;
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
    n.x = x; n.y = y;
    nodes.push_back(n);
    return n.id;
}
void MaterialAsset::removeNode(int id) {
    const MatNode* n = find(id);
    if (!n || n->type == MAT_RESULT) return;   // never remove the Result node
    for (auto& o : nodes)
        for (int i = 0; i < 4; i++) if (o.in[i] == id) o.in[i] = -1;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const MatNode& o) { return o.id == id; }), nodes.end());
}

MatValue MaterialAsset::evalNode(int id, int depth) const {
    MatValue v;
    const MatNode* n = find(id);
    if (!n || depth > 64) return v;
    auto input = [&](int pin) -> MatValue {
        if (pin < 0 || pin >= 4 || n->in[pin] < 0) { MatValue d; return d; }  // unconnected = white
        return evalNode(n->in[pin], depth + 1);
    };
    switch (n->type) {
    case MAT_CONST_COLOR: v.tint = n->color; break;
    case MAT_CONST_FLOAT: v.tint = { n->scalar, n->scalar, n->scalar }; break;
    case MAT_TEXTURE: v.hasTex = n->texturePath[0] != 0; v.tex = n->texturePath; break;
    case MAT_MULTIPLY: { MatValue a = input(0), b = input(1); v.tint = cmul(a.tint, b.tint);
        v.hasTex = a.hasTex || b.hasTex; v.tex = a.hasTex ? a.tex : b.tex; } break;
    case MAT_ADD: { MatValue a = input(0), b = input(1); v.tint = a.tint + b.tint;
        v.hasTex = a.hasTex || b.hasTex; v.tex = a.hasTex ? a.tex : b.tex; } break;
    case MAT_LERP: { MatValue a = input(0), b = input(1), t = input(2); float tv = t.tint.x;
        v.tint = a.tint * (1 - tv) + b.tint * tv; v.hasTex = tv < 0.5f ? a.hasTex : b.hasTex;
        v.tex = tv < 0.5f ? a.tex : b.tex; } break;
    default: break;
    }
    return v;
}

MaterialEval MaterialAsset::evaluate() const {
    MaterialEval e;
    int rid = resultId();
    const MatNode* rn = find(rid);
    if (!rn) return e;
    if (rn->in[0] >= 0) { MatValue v = evalNode(rn->in[0], 0); e.baseColor = v.tint; if (v.hasTex) e.baseColorTex = v.tex; }
    if (rn->in[1] >= 0) e.metallic = std::max(0.0f, std::min(1.0f, evalNode(rn->in[1], 0).tint.x));
    if (rn->in[2] >= 0) e.roughness = std::max(0.0f, std::min(1.0f, evalNode(rn->in[2], 0).tint.x));
    if (rn->in[3] >= 0) e.emissive = std::max(0.0f, evalNode(rn->in[3], 0).tint.x);
    return e;
}

std::string MaterialAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSOMAT 1\n";
    o << "view " << viewX << " " << viewY << "\n";
    for (const auto& n : nodes) {
        o << "node " << n.id << " " << n.type << " " << n.x << " " << n.y << " "
          << n.color.x << " " << n.color.y << " " << n.color.z << " " << n.scalar << " "
          << n.in[0] << " " << n.in[1] << " " << n.in[2] << " " << n.in[3] << "\n";
        if (n.texturePath[0]) o << "tex " << n.id << " " << n.texturePath << "\n";
    }
    return o.str();
}

bool MaterialAsset::deserialize(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("IMPULSOMAT", 0) != 0) return false;
    nodes.clear();
    nextId = 1;
    while (std::getline(in, line)) {
        if (line.rfind("view ", 0) == 0) { sscanf(line.c_str(), "view %f %f", &viewX, &viewY); }
        else if (line.rfind("node ", 0) == 0) {
            MatNode n;
            int rd = sscanf(line.c_str(), "node %d %d %f %f %f %f %f %f %d %d %d %d",
                            &n.id, &n.type, &n.x, &n.y, &n.color.x, &n.color.y, &n.color.z, &n.scalar,
                            &n.in[0], &n.in[1], &n.in[2], &n.in[3]);
            if (rd >= 8) { nodes.push_back(n); if (n.id >= nextId) nextId = n.id + 1; }
        } else if (line.rfind("tex ", 0) == 0) {
            int id = 0; char path[256] = "";
            if (sscanf(line.c_str(), "tex %d %255[^\n]", &id, path) == 2) {
                MatNode* n = find(id);
                if (n) { strncpy(n->texturePath, path, sizeof(n->texturePath) - 1); n->texturePath[sizeof(n->texturePath) - 1] = 0; }
            }
        }
    }
    if (resultId() < 0) makeDefault();
    return true;
}

// ─── shared helpers ───
void applyMaterialEval(const MaterialEval& e, Vec3& color, float& specular, float& shininess, float& emissive) {
    color = e.baseColor;
    specular = 0.04f + e.metallic * 0.85f;
    shininess = 6.0f + (1.0f - e.roughness) * 134.0f;
    emissive = e.emissive;
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
bool MaterialEditor::loadFrom(const std::string& absPath, const std::string& rel) {
    std::ifstream f(absPath, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!material.deserialize(data)) return false;
    curPath = rel;
    selected_ = -1;
    dirty = false;
    return true;
}
bool MaterialEditor::save() {
    if (curPath.empty() || projectDir.empty()) return false;
    std::ofstream f(projectDir + "\\" + curPath, std::ios::binary);
    if (!f) return false;
    std::string data = material.serialize();
    f.write(data.data(), (std::streamsize)data.size());
    dirty = !f.good();
    if (!dirty && logFn) logFn(1, "Material saved: %s", curPath.c_str());
    return !dirty;
}

// node geometry in screen space
static float nodeHeight(const MatNode& n) { return 30.0f + matNodeInputCount(n.type) * 20.0f + 8.0f; }
static const float NODE_W = 152.0f;

void MaterialEditor::draw(UI& ui) {
    const UIInput& in = ui.input();
    Renderer* r = ui.r;

    // ── top toolbar ──
    ui.row(4);
    if (ui.button("Save")) save();
    std::string title = curPath.empty() ? "Material" : curPath;
    if (dirty) title += " *";
    ui.label(title, { 0.85f, 0.55f, 0.95f });
    ui.spacing(2);
    ui.label("Add node:", { 0.6f, 0.66f, 0.74f });
    ui.row(6);
    struct { const char* lbl; int type; } adds[] = {
        { "Color", MAT_CONST_COLOR }, { "Float", MAT_CONST_FLOAT }, { "Texture", MAT_TEXTURE },
        { "Multiply", MAT_MULTIPLY }, { "Add", MAT_ADD }, { "Lerp", MAT_LERP } };
    UIRect pin = ui.panelInner();
    for (auto& a : adds) {
        if (ui.button(a.lbl)) {
            int id = material.addNode(a.type, pin.w * 0.38f - material.viewX, pin.h * 0.30f - material.viewY);
            selected_ = id; dirty = true;
        }
    }

    // ── canvas area ──
    float top = ui.panelCursorY() + 6;
    UIRect canvas = { pin.x, top, pin.w, pin.y + pin.h - top };
    ui.spacing(canvas.h);
    r->setUIScissor(canvas.x, canvas.y, canvas.w, canvas.h, true);
    r->drawRectPx(canvas.x, canvas.y, canvas.w, canvas.h, { 0.075f, 0.08f, 0.095f }, 1);
    // dotted grid
    for (float gx = fmodf(material.viewX, 32.0f); gx < canvas.w; gx += 32.0f)
        r->drawRectPx(canvas.x + gx, canvas.y, 1, canvas.h, { 0.11f, 0.12f, 0.14f }, 1);
    for (float gy = fmodf(material.viewY, 32.0f); gy < canvas.h; gy += 32.0f)
        r->drawRectPx(canvas.x, canvas.y + gy, canvas.w, 1, { 0.11f, 0.12f, 0.14f }, 1);

    bool overCanvas = !ui.interactionBlocked() && in.mouseX >= canvas.x && in.mouseX < canvas.x + canvas.w &&
                      in.mouseY >= canvas.y && in.mouseY < canvas.y + canvas.h;

    auto nodeScreen = [&](const MatNode& n) -> UIRect {
        return { canvas.x + material.viewX + n.x, canvas.y + material.viewY + n.y, NODE_W, nodeHeight(n) };
    };
    auto inPinPos = [&](const MatNode& n, int pin, float& px, float& py) {
        UIRect rc = nodeScreen(n); px = rc.x; py = rc.y + 30 + pin * 20 + 6;
    };
    auto outPinPos = [&](const MatNode& n, float& px, float& py) {
        UIRect rc = nodeScreen(n); px = rc.x + rc.w; py = rc.y + 16;
    };

    // ── pan (MMB or RMB drag on empty canvas) ──
    if (overCanvas && (in.mmbPressed || (in.rmbPressed))) { panning_ = true; panMouseX_ = in.mouseX; panMouseY_ = in.mouseY; }
    if (panning_) {
        if (in.mmbDown || in.rmbDown) { material.viewX += in.mouseX - panMouseX_; material.viewY += in.mouseY - panMouseY_; panMouseX_ = in.mouseX; panMouseY_ = in.mouseY; }
        else panning_ = false;
    }

    // ── wires (behind nodes) ──
    for (const auto& n : material.nodes) {
        int nin = matNodeInputCount(n.type);
        for (int i = 0; i < nin; i++) {
            if (n.in[i] < 0) continue;
            const MatNode* s = material.find(n.in[i]);
            if (!s) continue;
            float ax, ay, bx, by; outPinPos(*s, ax, ay); inPinPos(n, i, bx, by);
            r->drawLinePx(ax, ay, bx, by, 2.0f, { 0.45f, 0.62f, 0.85f }, 0.95f);
        }
    }

    // ── nodes ──
    bool anyHeaderHit = false;
    for (auto& n : material.nodes) {
        UIRect rc = nodeScreen(n);
        bool sel = n.id == selected_;
        r->drawRectPx(rc.x - 1, rc.y - 1, rc.w + 2, rc.h + 2, sel ? Vec3{ 0.35f, 0.62f, 0.99f } : Vec3{ 0.04f, 0.05f, 0.06f }, 1);
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.14f, 0.155f, 0.185f }, 1);
        Vec3 hdr = n.type == MAT_RESULT ? Vec3{ 0.30f, 0.22f, 0.36f } : Vec3{ 0.18f, 0.22f, 0.30f };
        r->drawRectPx(rc.x, rc.y, rc.w, 22, hdr, 1);
        r->drawTextLine(rc.x + 8, rc.y + 4, matNodeName(n.type), { 0.9f, 0.93f, 0.98f }, 1);

        int nin = matNodeInputCount(n.type);
        for (int i = 0; i < nin; i++) {
            float px, py; inPinPos(n, i, px, py);
            bool conn = n.in[i] >= 0;
            r->drawRectPx(px - 5, py - 5, 10, 10, conn ? Vec3{ 0.45f, 0.72f, 1.0f } : Vec3{ 0.5f, 0.54f, 0.6f }, 1);
            r->drawTextLine(px + 10, py - 7, matPinName(n.type, i), { 0.68f, 0.73f, 0.8f }, 1);
        }
        if (n.type != MAT_RESULT) {   // output pin + preview swatch
            float px, py; outPinPos(n, px, py);
            r->drawRectPx(px - 5, py - 5, 10, 10, { 0.55f, 0.85f, 0.55f }, 1);
            if (n.type == MAT_CONST_COLOR)
                r->drawRectPx(rc.x + 8, rc.y + 28, rc.w - 16, 14, n.color, 1);
            else if (n.type == MAT_CONST_FLOAT) {
                char b[24]; snprintf(b, sizeof(b), "%.3g", n.scalar);
                r->drawTextLine(rc.x + 10, rc.y + 28, b, { 0.86f, 0.9f, 0.96f }, 1);
            } else if (n.type == MAT_TEXTURE) {
                std::string t = n.texturePath[0] ? std::string("[img] ") + n.texturePath : std::string("(none)");
                r->drawTextLine(rc.x + 8, rc.y + 28, ui.ellipsize(t, rc.w - 14), { 0.8f, 0.85f, 0.92f }, 1);
            }
        }

        // header drag / selection
        UIRect hd = { rc.x, rc.y, rc.w, 22 };
        bool overHdr = overCanvas && in.mouseX >= hd.x && in.mouseX < hd.x + hd.w && in.mouseY >= hd.y && in.mouseY < hd.y + hd.h;
        if (overHdr && in.mousePressed && linkFromNode_ < 0) {
            selected_ = n.id; dragNode_ = true; anyHeaderHit = true;
            dragOffX_ = in.mouseX - rc.x; dragOffY_ = in.mouseY - rc.y;
        }
        // output pin press → start a link
        if (n.type != MAT_RESULT) {
            float px, py; outPinPos(n, px, py);
            if (overCanvas && in.mousePressed && fabsf(in.mouseX - px) < 8 && fabsf(in.mouseY - py) < 8) linkFromNode_ = n.id;
        }
        // input pin press → disconnect
        for (int i = 0; i < nin; i++) {
            float px, py; inPinPos(n, i, px, py);
            if (overCanvas && in.mousePressed && fabsf(in.mouseX - px) < 8 && fabsf(in.mouseY - py) < 8 && linkFromNode_ < 0) {
                n.in[i] = -1; dirty = true;
            }
        }
    }

    // ── node drag ──
    if (dragNode_) {
        if (in.mouseDown) {
            MatNode* n = material.find(selected_);
            if (n) { n->x = in.mouseX - canvas.x - material.viewX - dragOffX_; n->y = in.mouseY - canvas.y - material.viewY - dragOffY_; dirty = true; }
        } else dragNode_ = false;
    }

    // ── link drag ──
    if (linkFromNode_ >= 0) {
        const MatNode* s = material.find(linkFromNode_);
        if (s && in.mouseDown) { float ax, ay; outPinPos(*s, ax, ay); r->drawLinePx(ax, ay, in.mouseX, in.mouseY, 2.0f, { 0.6f, 0.85f, 1.0f }, 0.9f); }
        if (!in.mouseDown) {
            // drop on an input pin?
            for (auto& n : material.nodes) {
                int nin = matNodeInputCount(n.type);
                for (int i = 0; i < nin; i++) {
                    float px, py; inPinPos(n, i, px, py);
                    if (fabsf(in.mouseX - px) < 9 && fabsf(in.mouseY - py) < 9 && n.id != linkFromNode_) {
                        n.in[i] = linkFromNode_; dirty = true;
                    }
                }
            }
            linkFromNode_ = -1;
        }
    }

    // click empty canvas → deselect
    if (overCanvas && in.mousePressed && !anyHeaderHit && linkFromNode_ < 0 && !panning_) {
        bool onNode = false;
        for (const auto& n : material.nodes) { UIRect rc = nodeScreen(n); if (in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h) onNode = true; }
        if (!onNode) selected_ = -1;
    }
    // Delete removes the selected node
    if (selected_ >= 0 && in.keyDelete) { material.removeNode(selected_); selected_ = -1; dirty = true; }

    r->setUIScissor(0, 0, 0, 0, false);

    // ── right overlay: preview + selected node editor ──
    float sbw = 250.0f;
    UIRect sb = { canvas.x + canvas.w - sbw - 8, canvas.y + 8, sbw, canvas.h - 16 };
    r->drawRectPx(sb.x, sb.y, sb.w, sb.h, { 0.10f, 0.11f, 0.135f }, 0.96f);
    r->drawRectPx(sb.x, sb.y, sb.w, 1, { 0.30f, 0.62f, 0.99f }, 0.7f);

    // 3D preview of the current material
    UIRect pv = { sb.x + 10, sb.y + 10, sb.w - 20, 150 };
    MaterialEval ev = material.evaluate();
    {
        OrbitCamera cam;
        cam.target = { 0, 0, 0 }; cam.distance = 3.1f;
        cam.yaw = previewYaw_; cam.pitch = previewPitch_;
        cam.update(pv.w / pv.h);
        Frame f;
        f.showGrid = false;
        f.shadowCenter = { 0, 0, 0 };
        DrawItem it;
        it.mesh = MESH_SPHERE;
        it.model = Mat4::identity();
        applyMaterialEval(ev, it.color, it.specular, it.shininess, it.emissive);
        it.opacity = 1.0f; it.doubleSided = false; it.castShadow = false;
        it.albedoTex = ev.baseColorTex.empty() ? 0 : matLoadTexture(r, projectDir, ev.baseColorTex);
        f.items.push_back(it);
        r->render(f, cam, (int)pv.x, (int)pv.y, (int)pv.w, (int)pv.h, false);
    }
    r->drawRectPx(pv.x - 1, pv.y - 1, pv.w + 2, 1, { 0.04f, 0.05f, 0.06f }, 1);
    // preview orbit
    bool overPv = !ui.interactionBlocked() && in.mouseX >= pv.x && in.mouseX < pv.x + pv.w && in.mouseY >= pv.y && in.mouseY < pv.y + pv.h;
    if (overPv && in.mousePressed) { previewOrbit_ = true; previewMouseX_ = in.mouseX; previewMouseY_ = in.mouseY; }
    if (previewOrbit_) {
        if (in.mouseDown) { previewYaw_ += (in.mouseX - previewMouseX_) * 0.01f; previewPitch_ = std::max(-1.5f, std::min(1.5f, previewPitch_ + (in.mouseY - previewMouseY_) * 0.01f)); previewMouseX_ = in.mouseX; previewMouseY_ = in.mouseY; }
        else previewOrbit_ = false;
    }

    // selected node editor
    float ey = pv.y + pv.h + 12;
    r->drawTextLine(sb.x + 12, ey, "NODE PROPERTIES", { 0.55f, 0.62f, 0.72f }, 1); ey += 22;
    MatNode* sn = selected_ >= 0 ? material.find(selected_) : nullptr;
    if (!sn) {
        r->drawTextLine(sb.x + 12, ey, "No node selected.", { 0.6f, 0.65f, 0.72f }, 1);
        r->drawTextLine(sb.x + 12, ey + 20, "Drag the pins to connect.", { 0.5f, 0.55f, 0.62f }, 1);
    } else {
        r->drawTextLine(sb.x + 12, ey, matNodeName(sn->type), { 0.88f, 0.9f, 0.96f }, 1); ey += 24;
        if (sn->type == MAT_CONST_COLOR) {
            UIRect sw = { sb.x + 12, ey, sb.w - 24, 26 };
            r->drawRectPx(sw.x, sw.y, sw.w, sw.h, sn->color, 1);
            r->drawRectPx(sw.x, sw.y, sw.w, 1, { 0.04f, 0.05f, 0.06f }, 1);
            bool over = in.mouseX >= sw.x && in.mouseX < sw.x + sw.w && in.mouseY >= sw.y && in.mouseY < sw.y + sw.h;
            if (over && in.mousePressed) ui.openColorPicker("matcol", &sn->color, nullptr, sw.x, sw.y + 30);
            r->drawTextLine(sw.x + 8, sw.y + 6, "Click: pick color", { 0.05f, 0.06f, 0.08f }, 1);
            ey += 34;
        } else if (sn->type == MAT_CONST_FLOAT) {
            UIRect dr = { sb.x + 12, ey, sb.w - 24, 26 };
            r->drawRectPx(dr.x, dr.y, dr.w, dr.h, { 0.16f, 0.17f, 0.21f }, 1);
            char b[32]; snprintf(b, sizeof(b), "%.3f", sn->scalar);
            r->drawTextLine(dr.x + 8, dr.y + 6, b, { 0.86f, 0.9f, 0.96f }, 1);
            r->drawTextLine(dr.x + dr.w - 66, dr.y + 6, "wheel", { 0.5f, 0.55f, 0.62f }, 1);
            bool over = in.mouseX >= dr.x && in.mouseX < dr.x + dr.w && in.mouseY >= dr.y && in.mouseY < dr.y + dr.h;
            if (over && in.wheel != 0) { sn->scalar = std::max(0.0f, sn->scalar + in.wheel * 0.05f); dirty = true; }
            ey += 30;
            // - / + buttons
            UIRect bm = { dr.x, ey, 28, 22 }, bp = { dr.x + 32, ey, 28, 22 };
            r->drawRectPx(bm.x, bm.y, bm.w, bm.h, { 0.2f, 0.22f, 0.27f }, 1); r->drawTextLine(bm.x + 10, bm.y + 4, "-", { 0.9f, 0.9f, 0.95f }, 1);
            r->drawRectPx(bp.x, bp.y, bp.w, bp.h, { 0.2f, 0.22f, 0.27f }, 1); r->drawTextLine(bp.x + 9, bp.y + 4, "+", { 0.9f, 0.9f, 0.95f }, 1);
            if (in.mousePressed && in.mouseX >= bm.x && in.mouseX < bm.x + bm.w && in.mouseY >= bm.y && in.mouseY < bm.y + bm.h) { sn->scalar = std::max(0.0f, sn->scalar - 0.05f); dirty = true; }
            if (in.mousePressed && in.mouseX >= bp.x && in.mouseX < bp.x + bp.w && in.mouseY >= bp.y && in.mouseY < bp.y + bp.h) { sn->scalar += 0.05f; dirty = true; }
            r->drawTextLine(bp.x + 40, bp.y + 4, "(wheel)", { 0.5f, 0.55f, 0.62f }, 1);
            ey += 28;
        } else if (sn->type == MAT_TEXTURE) {
            UIRect tr = { sb.x + 12, ey, sb.w - 24, 24 };
            if (ui.textInputRect("mattex", sn->texturePath, sizeof(sn->texturePath), tr)) dirty = true;
            r->drawTextLine(tr.x, tr.y + 28, ".png path (project-relative)", { 0.5f, 0.55f, 0.62f }, 1);
            ey += 52;
        }
        if (sn->type != MAT_RESULT) {
            UIRect del = { sb.x + 12, sb.y + sb.h - 34, sb.w - 24, 24 };
            r->drawRectPx(del.x, del.y, del.w, del.h, { 0.35f, 0.14f, 0.14f }, 1);
            r->drawTextLine(del.x + del.w * 0.5f - 34, del.y + 5, "Delete node", { 0.95f, 0.8f, 0.8f }, 1);
            if (in.mousePressed && in.mouseX >= del.x && in.mouseX < del.x + del.w && in.mouseY >= del.y && in.mouseY < del.y + del.h) {
                material.removeNode(selected_); selected_ = -1; dirty = true;
            }
        }
    }
}
