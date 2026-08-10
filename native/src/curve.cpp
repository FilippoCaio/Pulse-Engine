#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "curve.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

void CurveAsset::sortKeys() {
    std::sort(keys.begin(), keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
}

float CurveAsset::evaluate(float time) const {
    if (keys.empty()) return 0;
    if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    int i = 0;
    while (i + 1 < (int)keys.size() && time > keys[i + 1].time) i++;
    const CurveKey& a = keys[i];
    const CurveKey& b = keys[i + 1];
    float span = b.time - a.time;
    float u = span > 1e-6f ? (time - a.time) / span : 0;
    if (interp == CURVE_CONSTANT) return a.value;
    if (interp == CURVE_LINEAR) return a.value + (b.value - a.value) * u;

    auto autoTangent = [&](int k) {
        int p = k > 0 ? k - 1 : k;
        int q = k + 1 < (int)keys.size() ? k + 1 : k;
        float dt = keys[q].time - keys[p].time;
        return dt > 1e-6f ? (keys[q].value - keys[p].value) / dt : 0.0f;
    };
    float m1 = a.tangentUser ? a.leaveTangent : autoTangent(i);
    float m2 = b.tangentUser ? b.arriveTangent : autoTangent(i + 1);
    float u2 = u * u, u3 = u2 * u;
    float h00 = 2 * u3 - 3 * u2 + 1;
    float h10 = u3 - 2 * u2 + u;
    float h01 = -2 * u3 + 3 * u2;
    float h11 = u3 - u2;
    return h00 * a.value + h10 * span * m1 + h01 * b.value + h11 * span * m2;
}

std::string CurveAsset::serialize() const {
    std::ostringstream out;
    out << "IMPULSOCURVE 2\n";
    out << "interp " << (int)interp << "\n";
    for (const CurveKey& k : keys)
        out << "key " << k.time << " " << k.value << " " << k.arriveTangent << " "
            << k.leaveTangent << " " << (k.tangentUser ? 1 : 0) << "\n";
    return out.str();
}

bool CurveAsset::deserialize(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    int version = 0;
    if (!std::getline(in, line) || sscanf(line.c_str(), "IMPULSOCURVE %d", &version) != 1) return false;
    keys.clear();
    while (std::getline(in, line)) {
        int mode = 0;
        CurveKey k;
        if (sscanf(line.c_str(), "interp %d", &mode) == 1) interp = (CurveInterp)(mode < 0 ? 0 : mode > 2 ? 2 : mode);
        else {
            int custom = 0;
            int got = sscanf(line.c_str(), "key %f %f %f %f %d", &k.time, &k.value,
                             &k.arriveTangent, &k.leaveTangent, &custom);
            if (got >= 2) {
                k.tangentUser = got >= 5 && custom != 0;
                keys.push_back(k);
            }
        }
    }
    if (keys.empty()) keys = { { 0, 0 }, { 1, 1 } };
    sortKeys();
    return true;
}

bool CurveEditor::loadFrom(const std::string& absPath, const std::string& rel) {
    std::ifstream f(absPath, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!curve.deserialize(data)) return false;
    curPath = rel;
    selected_ = -1;
    viewInitialized_ = false;
    dirty = false;
    return true;
}

bool CurveEditor::save() {
    extern bool gEditorProjectWritesAllowed;
    if (!gEditorProjectWritesAllowed) return false;
    if (curPath.empty() || projectDir.empty()) return false;
    std::ofstream f(projectDir + "\\" + curPath, std::ios::binary);
    if (!f) return false;
    std::string data = curve.serialize();
    f.write(data.data(), (std::streamsize)data.size());
    dirty = !f.good();
    if (!dirty && logFn) logFn(1, "Curve saved: %s", curPath.c_str());
    return !dirty;
}

void CurveEditor::sortKeepingSelection(float time, float value) {
    curve.sortKeys();
    selected_ = -1;
    float best = 1e30f;
    for (int i = 0; i < (int)curve.keys.size(); i++) {
        float d = fabsf(curve.keys[i].time - time) + fabsf(curve.keys[i].value - value);
        if (d < best) { best = d; selected_ = i; }
    }
}

void CurveEditor::fitView() {
    if (curve.keys.empty()) {
        viewTime_ = viewValue_ = 0.5f;
        viewTimeSpan_ = 1.2f;
        viewValueSpan_ = 1.4f;
    } else {
        float t0 = curve.keys.front().time, t1 = t0;
        float v0 = curve.keys.front().value, v1 = v0;
        for (const CurveKey& k : curve.keys) {
            t0 = std::min(t0, k.time); t1 = std::max(t1, k.time);
            v0 = std::min(v0, k.value); v1 = std::max(v1, k.value);
        }
        viewTime_ = (t0 + t1) * 0.5f;
        viewValue_ = (v0 + v1) * 0.5f;
        viewTimeSpan_ = std::max(0.1f, (t1 - t0) * 1.18f);
        viewValueSpan_ = std::max(0.1f, (v1 - v0) * 1.35f);
    }
    viewInitialized_ = true;
}

void CurveEditor::draw(UI& ui) {
    frame_++;
    const UIInput& in = ui.input();
    Renderer* r = ui.r;
    ui.row(4);
    if (ui.button("Save")) save();
    if (ui.button("+ Point")) {
        CurveKey k = curve.keys.empty() ? CurveKey{} : CurveKey{ curve.keys.back().time + 1, curve.keys.back().value };
        curve.keys.push_back(k);
        sortKeepingSelection(k.time, k.value);
        viewInitialized_ = false;
        dirty = true;
    }
    if (ui.button("Delete point") && selected_ >= 0 && curve.keys.size() > 2) {
        curve.keys.erase(curve.keys.begin() + selected_);
        selected_ = -1;
        dirty = true;
    }
    if (ui.button("Frame all")) viewInitialized_ = false;
    std::string title = curPath.empty() ? "Curve" : curPath;
    if (dirty) title += " *";
    ui.label(title, { 0.30f, 0.72f, 0.96f });
    static const char* MODES[] = { "Constant", "Linear", "Smooth" };
    int mode = (int)curve.interp;
    if (ui.combo("Interpolation", &mode, MODES, 3)) { curve.interp = (CurveInterp)mode; dirty = true; }

    UIRect p = ui.panelInner();
    float gy = ui.panelCursorY() + 4;
    float gh = p.y + p.h - gy - 112;
    if (gh < 180) gh = 180;
    UIRect gr{ p.x + 8, gy, p.w - 16, gh };
    ui.spacing(gh + 10);
    if (!viewInitialized_) fitView();
    auto xmin = [&]() { return viewTime_ - viewTimeSpan_ * 0.5f; };
    auto xmax = [&]() { return viewTime_ + viewTimeSpan_ * 0.5f; };
    auto ymin = [&]() { return viewValue_ - viewValueSpan_ * 0.5f; };
    auto ymax = [&]() { return viewValue_ + viewValueSpan_ * 0.5f; };
    auto sx = [&](float t) { return gr.x + (t - xmin()) / viewTimeSpan_ * gr.w; };
    auto sy = [&](float v) { return gr.y + gr.h - (v - ymin()) / viewValueSpan_ * gr.h; };
    auto wt = [&](float x) { return xmin() + (x - gr.x) / gr.w * viewTimeSpan_; };
    auto wv = [&](float y) { return ymin() + (gr.y + gr.h - y) / gr.h * viewValueSpan_; };
    bool inside = !ui.interactionBlocked() && in.mouseX >= gr.x && in.mouseX < gr.x + gr.w && in.mouseY >= gr.y && in.mouseY < gr.y + gr.h;

    // Wheel zoom is centred on the world coordinate below the cursor.
    if (inside && in.wheel != 0) {
        float fx = (in.mouseX - gr.x) / gr.w;
        float fy = 1.0f - (in.mouseY - gr.y) / gr.h;
        float cursorT = xmin() + fx * viewTimeSpan_;
        float cursorV = ymin() + fy * viewValueSpan_;
        float factor = powf(1.15f, -in.wheel);
        viewTimeSpan_ = std::max(0.0001f, std::min(1000000.0f, viewTimeSpan_ * factor));
        viewValueSpan_ = std::max(0.0001f, std::min(1000000.0f, viewValueSpan_ * factor));
        viewTime_ = cursorT - (fx - 0.5f) * viewTimeSpan_;
        viewValue_ = cursorV - (fy - 0.5f) * viewValueSpan_;
    }

    // Middle mouse pans the world; the grid is expressed in world coordinates,
    // therefore it follows the view instead of remaining glued to the screen.
    if (inside && in.mmbPressed) {
        panning_ = true;
        panMouseX_ = in.mouseX;
        panMouseY_ = in.mouseY;
    }
    if (panning_) {
        if (in.mmbDown) {
            float dx = in.mouseX - panMouseX_, dy = in.mouseY - panMouseY_;
            viewTime_ -= dx / gr.w * viewTimeSpan_;
            viewValue_ += dy / gr.h * viewValueSpan_;
            panMouseX_ = in.mouseX;
            panMouseY_ = in.mouseY;
        } else panning_ = false;
    }

    auto autoTangent = [&](int index) {
        int a = index > 0 ? index - 1 : index;
        int b = index + 1 < (int)curve.keys.size() ? index + 1 : index;
        float dt = curve.keys[b].time - curve.keys[a].time;
        return dt > 1e-6f ? (curve.keys[b].value - curve.keys[a].value) / dt : 0.0f;
    };
    auto effectiveTangent = [&](int index, int side) {
        const CurveKey& k = curve.keys[index];
        return k.tangentUser ? (side < 0 ? k.arriveTangent : k.leaveTangent) : autoTangent(index);
    };
    auto handleDt = [&]() { return viewTimeSpan_ * 58.0f / gr.w; };
    auto handleX = [&](int index, int side) { return sx(curve.keys[index].time + handleDt() * side); };
    auto handleY = [&](int index, int side) {
        const CurveKey& k = curve.keys[index];
        return sy(k.value + effectiveTangent(index, side) * handleDt() * side);
    };

    if (inside && in.mousePressed) {
        int hit = -1;
        int hitTangent = 0;
        if (curve.interp == CURVE_SMOOTH && selected_ >= 0 && selected_ < (int)curve.keys.size()) {
            for (int side : { -1, 1 }) {
                if (fabsf(in.mouseX - handleX(selected_, side)) < 9 && fabsf(in.mouseY - handleY(selected_, side)) < 9)
                    hitTangent = side;
            }
        }
        for (int i = 0; i < (int)curve.keys.size(); i++)
            if (fabsf(in.mouseX - sx(curve.keys[i].time)) < 9 && fabsf(in.mouseY - sy(curve.keys[i].value)) < 9) hit = i;
        bool dbl = frame_ - lastClickFrame_ < 22 && fabsf(in.mouseX - lastClickX_) < 8 && fabsf(in.mouseY - lastClickY_) < 8;
        if (hitTangent) {
            tangentDrag_ = hitTangent;
            dragging_ = false;
        } else if (hit >= 0) {
            selected_ = hit;
            dragging_ = true;
            tangentDrag_ = 0;
        }
        else if (dbl) {
            CurveKey k{ wt(in.mouseX), wv(in.mouseY) };
            curve.keys.push_back(k);
            sortKeepingSelection(k.time, k.value);
            dirty = true;
        } else { selected_ = -1; tangentDrag_ = 0; }
        lastClickFrame_ = frame_; lastClickX_ = in.mouseX; lastClickY_ = in.mouseY;
    }
    if (tangentDrag_ && selected_ >= 0 && selected_ < (int)curve.keys.size()) {
        if (in.mouseDown) {
            CurveKey& k = curve.keys[selected_];
            float ht = wt(in.mouseX), hv = wv(in.mouseY);
            float dt = ht - k.time;
            if (fabsf(dt) > viewTimeSpan_ * 0.00001f) {
                float slope = (hv - k.value) / dt;
                if (tangentDrag_ < 0) k.arriveTangent = slope; else k.leaveTangent = slope;
                if (!in.keyAlt) k.arriveTangent = k.leaveTangent = slope;
                k.tangentUser = true;
                dirty = true;
            }
        } else tangentDrag_ = 0;
    }
    if (dragging_ && selected_ >= 0 && selected_ < (int)curve.keys.size()) {
        if (in.mouseDown) {
            curve.keys[selected_].time = wt(std::max(gr.x, std::min(gr.x + gr.w, in.mouseX)));
            curve.keys[selected_].value = wv(std::max(gr.y, std::min(gr.y + gr.h, in.mouseY)));
            dirty = true;
        } else {
            CurveKey k = curve.keys[selected_];
            sortKeepingSelection(k.time, k.value);
            dragging_ = false;
        }
    }
    if (in.keyDelete && selected_ >= 0 && curve.keys.size() > 2) {
        curve.keys.erase(curve.keys.begin() + selected_);
        selected_ = -1;
        dirty = true;
    }

    r->setUIScissor(gr.x, gr.y, gr.w, gr.h, true);
    r->drawRectPx(gr.x, gr.y, gr.w, gr.h, { 0.055f, 0.062f, 0.075f }, 1);
    auto niceStep = [](float raw) {
        float p10 = powf(10.0f, floorf(log10f(std::max(raw, 1e-12f))));
        float n = raw / p10;
        return (n < 2 ? 1.0f : n < 5 ? 2.0f : 5.0f) * p10;
    };
    float txStep = niceStep(viewTimeSpan_ / 10.0f);
    float vyStep = niceStep(viewValueSpan_ / 8.0f);
    float firstT = ceilf(xmin() / txStep) * txStep;
    float firstV = ceilf(ymin() / vyStep) * vyStep;
    for (float t = firstT; t <= xmax() + txStep * 0.1f; t += txStep) {
        float x = sx(t);
        bool axis = fabsf(t) < txStep * 0.01f;
        r->drawLinePx(x, gr.y, x, gr.y + gr.h, axis ? 1.6f : 1, axis ? Vec3{ 0.34f, 0.38f, 0.45f } : Vec3{ 0.13f, 0.15f, 0.18f }, 1);
    }
    for (float v = firstV; v <= ymax() + vyStep * 0.1f; v += vyStep) {
        float y = sy(v);
        bool axis = fabsf(v) < vyStep * 0.01f;
        r->drawLinePx(gr.x, y, gr.x + gr.w, y, axis ? 1.6f : 1, axis ? Vec3{ 0.34f, 0.38f, 0.45f } : Vec3{ 0.13f, 0.15f, 0.18f }, 1);
    }
    const int SAMPLES = 180;
    float px = sx(xmin()), py = sy(curve.evaluate(xmin()));
    for (int i = 1; i <= SAMPLES; i++) {
        float t = xmin() + viewTimeSpan_ * i / SAMPLES;
        float x = sx(t), y = sy(curve.evaluate(t));
        r->drawLinePx(px, py, x, y, 2, { 0.25f, 0.78f, 0.98f }, 1);
        px = x; py = y;
    }
    if (curve.interp == CURVE_SMOOTH && selected_ >= 0 && selected_ < (int)curve.keys.size()) {
        const CurveKey& k = curve.keys[selected_];
        float kx = sx(k.time), ky = sy(k.value);
        for (int side : { -1, 1 }) {
            float hx = handleX(selected_, side), hy = handleY(selected_, side);
            Vec3 c = side < 0 ? Vec3{ 0.98f, 0.55f, 0.28f } : Vec3{ 0.35f, 0.9f, 0.5f };
            r->drawLinePx(kx, ky, hx, hy, 1.5f, c, 1);
            r->drawRectPx(hx - 4, hy - 4, 8, 8, c, 1);
        }
    }
    for (int i = 0; i < (int)curve.keys.size(); i++) {
        float x = sx(curve.keys[i].time), y = sy(curve.keys[i].value);
        Vec3 c = i == selected_ ? Vec3{ 1, 0.78f, 0.25f } : Vec3{ 0.86f, 0.92f, 1.0f };
        r->drawRectPx(x - 5, y - 5, 10, 10, c, 1);
    }
    r->setUIScissor(0, 0, 0, 0, false);
    ui.reclipPanel();
    ui.registerBlockingRect(gr);

    if (selected_ >= 0 && selected_ < (int)curve.keys.size()) {
        ui.label("SELECTED POINT", { 0.30f, 0.72f, 0.96f });
        ui.row(3);
        bool changed = ui.dragFloat("Time", &curve.keys[selected_].time, 0.01f, -100000, 100000);
        changed |= ui.dragFloat("Value", &curve.keys[selected_].value, 0.01f, -100000, 100000);
        float tangent = effectiveTangent(selected_, 1);
        if (ui.dragFloat("Tangent", &tangent, 0.01f, -100000, 100000)) {
            curve.keys[selected_].arriveTangent = curve.keys[selected_].leaveTangent = tangent;
            curve.keys[selected_].tangentUser = true;
            dirty = true;
        }
        if (changed) {
            CurveKey k = curve.keys[selected_];
            sortKeepingSelection(k.time, k.value);
            dirty = true;
        }
        if (curve.keys[selected_].tangentUser && ui.button("Auto tangents")) {
            curve.keys[selected_].tangentUser = false;
            dirty = true;
        }
        ui.label("Smooth: drag the handles to rotate; Alt splits the in/out tangents.", { 0.55f, 0.59f, 0.66f });
    } else {
        ui.label("Wheel: zoom | middle button: pan | double click: add point.", { 0.55f, 0.59f, 0.66f });
    }
}
