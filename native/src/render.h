// ─── Pulse Engine native renderer: OpenGL 3.3 core, shadow mapping, UI batch ───
#pragma once
#include "glext.h"
#include "math.h"
#include <vector>
#include <string>

enum MeshType { MESH_CUBE = 0, MESH_SPHERE, MESH_CYLINDER, MESH_CONE, MESH_CAPSULE, MESH_COUNT };

struct DrawItem {
    MeshType mesh = MESH_CUBE;
    Mat4 model;
    Vec3 color = { 0.8f, 0.5f, 0.3f };
    float opacity = 1.0f;
    float shininess = 48, specular = 0.35f, checker = 0, emissive = 0;
    bool castShadow = true;
    bool doubleSided = false;
    GLuint albedoTex = 0;      // material base-colour texture (triplanar); 0 = none
};

// Local-space bounds of each primitive, centred on the origin (half-extents).
// Every primitive is authored inside a unit box except the capsule, whose two
// hemispheres are shifted ±0.5 along Y.
Vec3 meshLocalHalfExtent(MeshType mesh);
// World-space AABB (centre + positive half-extents) of a draw item.
void drawItemBounds(const DrawItem& item, Vec3& center, Vec3& halfExtent);

struct PointLight {
    Vec3 pos;
    Vec3 color = { 1, 0.85f, 0.55f }; // premultiplied by intensity
    float range = 12;
};

struct Env {
    Vec3 sunDir = Vec3(0.5f, 0.8f, 0.4f).normalized();
    Vec3 sunColor = { 1.15f, 1.10f, 0.99f };
    Vec3 ambientSky = { 0.24f, 0.29f, 0.38f };
    Vec3 ambientGround = { 0.14f, 0.125f, 0.11f };
    Vec3 fogColor = { 0.45f, 0.53f, 0.66f };
    float fogDensity = 0.006f;
    Vec3 horizon = { 0.5f, 0.66f, 0.9f };
    Vec3 zenith = { 0.09f, 0.24f, 0.55f };
    float shadowStrength = 0.85f;
};

struct LineVert { Vec3 p; Vec3 c; };

struct Frame {
    std::vector<DrawItem> items;
    std::vector<DrawItem> overlay;      // drawn on top with cleared depth (gizmo)
    std::vector<PointLight> lights;
    std::vector<LineVert> linesDepth;
    std::vector<LineVert> trianglesDepth; // translucent debug geometry
    std::vector<LineVert> linesOverlay;
    std::vector<LineVert> linesOverlayThick; // transform gizmos / snap ticks
    Env env;
    Vec3 shadowCenter;
    bool showGrid = true;               // floor grid (hidden in Play)
};

struct OrbitCamera {
    Vec3 target = { 0, 1.5f, 0 };
    float yaw = -0.7f, pitch = 0.42f, distance = 16;
    float fovDeg = 55, nearZ = 0.1f, farZ = 500;
    Vec3 eye;
    Mat4 view, proj, projView, invProjView;

    // first-person override: view from a scene camera entity (Play mode)
    bool fpActive = false;
    Vec3 fpEye;
    Quat fpRot;
    float fpFov = 70;

    void update(float aspect);
    void orbit(float dx, float dy);
    // first-person look: rotate the view around the camera's own position (eye stays
    // fixed, the orbit target is moved) — used for RMB mouse-look during WASD flight.
    void freeLook(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float delta);
    // fly the whole orbit rig through the scene along its own axes (WASD/QE nav):
    // fwd = eye→target, right = camera right, up = world up. Call before update().
    void flyMove(float fwd, float right, float up);
    void screenRay(float ndcX, float ndcY, Vec3& origin, Vec3& dir) const;
};

class Renderer {
public:
    bool init();                    // requires current GL context
    void resize(int w, int h) { width_ = w; height_ = h; }
    int width() const { return width_; }
    int height() const { return height_; }
    // renders the 3D scene into the given viewport rect (pixels, top-left origin);
    // the rest of the framebuffer is cleared so the UI can draw over it. vw<=0 = no 3D.
    // clearAll=false: overlay mode — skips the full-framebuffer clear and repaints
    // only the rect on top of whatever is already there (used to draw a floating
    // viewport over docked panels, at the correct z-order).
    void render(const Frame& frame, const OrbitCamera& cam, int vx, int vy, int vw, int vh, bool clearAll = true);

    // ── 2D UI batch (pixel coords, origin top-left, submission order preserved) ──
    void drawTextLine(float x, float y, const std::string& s, Vec3 color, float alpha = 1, float scale = 1);
    void drawRectPx(float x, float y, float w, float h, Vec3 color, float alpha = 1);
    void drawTriPx(float x1, float y1, float x2, float y2, float x3, float y3, Vec3 color, float alpha = 1);
    void drawLinePx(float x1, float y1, float x2, float y2, float thickness, Vec3 color, float alpha = 1);
    GLuint loadPngTexture(const std::string& path);
    void drawImagePx(GLuint texture, float x, float y, float w, float h,
                     Vec3 tint = { 1, 1, 1 }, float alpha = 1);
    float textWidth(const std::string& s, float scale = 1) const;
    float fontHeight() const { return (float)fontH_; }
    // flushes pending batch, then applies (or disables) scissor for subsequent draws
    void setUIScissor(float x, float y, float w, float h, bool enable);
    void flushUI();
    // ── 2D affine applied to every subsequent UI vertex (pixel space) ──
    // Callers keep drawing in plain rect coordinates; rects, triangles, text and
    // images all bend together. Used for widget Render Transforms.
    // [m00 m01; m10 m11] is column-major on (x, y), then (tx, ty) is added.
    void setUITransform(float m00, float m01, float m10, float m11, float tx, float ty);
    void clearUITransform() { uiXfOn_ = false; }
    bool uiTransformActive() const { return uiXfOn_; }

    // ── frustum culling ──
    // Meshes whose world AABB falls fully outside the camera frustum are skipped.
    // Shadow casters are culled against the sun's ortho box instead (geometry
    // outside it cannot reach the shadow map), so the image is bit-identical
    // either way — turning this off is purely a debugging aid.
    bool frustumCulling = true;
    int drawCalls = 0;
    int drawnItems = 0;          // scene meshes submitted this frame
    int culledItems = 0;         // scene meshes rejected by the camera frustum
    int culledShadowItems = 0;   // shadow casters rejected by the light frustum

private:
    struct Mesh { GLuint vao = 0; int count = 0; };
    struct Program { GLuint id = 0; GLint u(const char* name) const { return glGetUniformLocation(id, name); } };

    Mesh meshes_[MESH_COUNT];
    Program lit_, depth_, sky_, grid_, line_, text_, image_;
    GLuint shadowTex_ = 0, shadowFbo_ = 0;
    GLuint screenVao_ = 0, gridVao_ = 0;
    GLuint lineVao_ = 0, lineVbo_ = 0;
    GLuint fontTex_ = 0, textVao_ = 0, textVbo_ = 0;
    Mat4 lightMatrix_;
    int width_ = 1600, height_ = 900;

    struct Glyph { float u0, v0, u1, v1; int w, h; };
    Glyph glyphs_[95] = {};
    int fontH_ = 17;
    float whiteU_ = 0, whiteV_ = 0; // solid-white texel for rects
    std::vector<float> uiBatch_;    // x,y,u,v,r,g,b,a
    float uiXf_[6] = { 1, 0, 0, 1, 0, 0 };   // active 2D affine
    bool uiXfOn_ = false;
    // maps a point through the active affine; a no-op while none is set, so the
    // common path costs one branch
    void uiXform(float& x, float& y) const {
        if (!uiXfOn_) return;
        float nx = uiXf_[0] * x + uiXf_[2] * y + uiXf_[4];
        float ny = uiXf_[1] * x + uiXf_[3] * y + uiXf_[5];
        x = nx; y = ny;
    }
    // per-frame draw lists (pointers into the caller's Frame; reused to avoid
    // reallocating every frame)
    std::vector<const DrawItem*> visibleItems_, overlayItems_;

    Mesh createMesh(const std::vector<float>& data, const std::vector<unsigned>& idx);
    void buildFont();
    void drawLineBatch(const std::vector<LineVert>& verts, bool depthTest, const Mat4& projView, float thickness = 1);
    void drawTriangleBatch(const std::vector<LineVert>& verts, const Mat4& projView);
    void drawItemsLit(const std::vector<const DrawItem*>& items, bool overlayPass, int transparencyPass = 0);
};
