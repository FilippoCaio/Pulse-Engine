// ─── Pulse Engine native renderer implementation ───
#include "render.h"
#include <cstdio>
#include <cstring>
#include <wincodec.h>

static const int SHADOW_SIZE = 2048;

// ═══ GLSL 330 shaders (ported from the WebGL2 prototype) ═══
static const char* LIT_VS = R"(#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
uniform mat4 uProj, uView, uModel;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vObjPos;
out vec3 vObjNormal;
void main() {
    vec4 wp = uModel * vec4(aPosition, 1.0);
    vWorldPos = wp.xyz;
    vNormal = uNormalMat * aNormal;
    vObjPos = aPosition;
    vObjNormal = aNormal;
    gl_Position = uProj * uView * wp;
})";

static const char* LIT_FS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vObjPos;
in vec3 vObjNormal;
out vec4 fragColor;
uniform vec3 uColor;
uniform float uOpacity;
uniform float uShininess, uSpecular, uChecker, uEmissive;
uniform int uDoubleSided;
uniform sampler2D uAlbedoTex;
uniform int uUseAlbedo;
uniform float uUvScale;
uniform vec3 uCamPos, uSunDir, uSunColor, uAmbientSky, uAmbientGround, uFogColor;
uniform float uFogDensity, uShadowStrength;
uniform int uLightCount;
uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform float uLightRange[8];
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowMatrix;

float shadowFactor(vec3 N) {
    vec4 sc = uShadowMatrix * vec4(vWorldPos, 1.0);
    vec3 p = sc.xyz / sc.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float bias = max(0.0022 * (1.0 - dot(N, uSunDir)), 0.0006);
    float ref = p.z - bias;
    float texel = 1.0 / 2048.0;
    float sum = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            sum += texture(uShadowMap, vec3(p.xy + vec2(x, y) * texel, ref));
    return mix(1.0, sum / 9.0, uShadowStrength);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    float facing = dot(N, V);
    if (uDoubleSided == 0 && facing <= 0.0) discard;
    if (uDoubleSided != 0 && facing < 0.0) N = -N;
    vec3 base = uColor;
    if (uUseAlbedo != 0) {   // object-space triplanar sampling (no per-vertex UVs)
        vec3 bw = abs(normalize(vObjNormal));
        bw /= max(bw.x + bw.y + bw.z, 1e-4);
        vec3 tx = texture(uAlbedoTex, vObjPos.yz * uUvScale).rgb;
        vec3 ty = texture(uAlbedoTex, vObjPos.xz * uUvScale).rgb;
        vec3 tz = texture(uAlbedoTex, vObjPos.xy * uUvScale).rgb;
        base *= tx * bw.x + ty * bw.y + tz * bw.z;
    }
    if (uChecker > 0.0) {
        vec2 cell = floor(vWorldPos.xz / uChecker);
        base *= mix(0.82, 1.0, mod(cell.x + cell.y, 2.0));
    }
    float hemi = N.y * 0.5 + 0.5;
    vec3 color = base * mix(uAmbientGround, uAmbientSky, hemi);
    float sh = shadowFactor(N);
    float ndl = max(dot(N, uSunDir), 0.0);
    vec3 H = normalize(uSunDir + V);
    float spec = pow(max(dot(N, H), 0.0), uShininess) * uSpecular;
    color += (base * ndl + vec3(spec)) * uSunColor * sh;
    for (int i = 0; i < 8; i++) {
        if (i >= uLightCount) break;
        vec3 L = uLightPos[i] - vWorldPos;
        float dist = length(L);
        L /= max(dist, 1e-4);
        float x = clamp(1.0 - pow(dist / uLightRange[i], 2.0), 0.0, 1.0);
        float nl = max(dot(N, L), 0.0);
        vec3 Hp = normalize(L + V);
        float sp = pow(max(dot(N, Hp), 0.0), uShininess) * uSpecular;
        color += (base * nl + vec3(sp)) * uLightColor[i] * (x * x);
    }
    color += base * uEmissive;
    float fd = length(uCamPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * uFogDensity * fd * fd);
    color = mix(color, uFogColor, clamp(fog, 0.0, 1.0));
    color = aces(color);
    color = pow(color, vec3(1.0 / 2.2));
    fragColor = vec4(color, uOpacity);
})";

static const char* DEPTH_VS = R"(#version 330 core
layout(location=0) in vec3 aPosition;
uniform mat4 uLightMatrix, uModel;
void main() { gl_Position = uLightMatrix * uModel * vec4(aPosition, 1.0); })";

static const char* DEPTH_FS = R"(#version 330 core
void main() {})";

static const char* SKY_VS = R"(#version 330 core
layout(location=0) in vec2 aPosition;
uniform mat4 uInvProjView;
out vec3 vDir;
void main() {
    gl_Position = vec4(aPosition, 0.9999, 1.0);
    vec4 nearP = uInvProjView * vec4(aPosition, -1.0, 1.0);
    vec4 farP = uInvProjView * vec4(aPosition, 1.0, 1.0);
    vDir = farP.xyz / farP.w - nearP.xyz / nearP.w;
})";

static const char* SKY_FS = R"(#version 330 core
in vec3 vDir;
out vec4 fragColor;
uniform vec3 uHorizon, uZenith, uGroundCol, uSunDir;
void main() {
    vec3 d = normalize(vDir);
    vec3 sky;
    if (d.y >= 0.0) {
        sky = mix(uHorizon, uZenith, pow(d.y, 0.5));
        sky += vec3(1.0, 0.93, 0.8) * pow(max(dot(d, uSunDir), 0.0), 800.0) * 1.2;
        sky += vec3(0.35, 0.28, 0.18) * pow(max(dot(d, uSunDir), 0.0), 8.0) * 0.25;
    } else {
        sky = mix(uHorizon, uGroundCol, min(1.0, -d.y * 3.0));
    }
    fragColor = vec4(pow(sky, vec3(1.0 / 2.2)), 1.0);
})";

static const char* GRID_VS = R"(#version 330 core
layout(location=0) in vec2 aPosition;
uniform mat4 uProj, uView;
uniform vec3 uCamPos;
out vec2 vXZ;
void main() {
    vec3 wp = vec3(aPosition.x * 300.0 + uCamPos.x, 0.0, aPosition.y * 300.0 + uCamPos.z);
    vXZ = wp.xz;
    gl_Position = uProj * uView * vec4(wp, 1.0);
})";

static const char* GRID_FS = R"(#version 330 core
in vec2 vXZ;
out vec4 fragColor;
uniform vec3 uCamPos;
float gridLine(vec2 p, float scale) {
    vec2 g = abs(fract(p / scale - 0.5) - 0.5) / fwidth(p / scale);
    return 1.0 - min(min(g.x, g.y), 1.0);
}
void main() {
    float g1 = gridLine(vXZ, 1.0) * 0.35;
    float g10 = gridLine(vXZ, 10.0) * 0.6;
    float axisX = 1.0 - min(abs(vXZ.y) / fwidth(vXZ.y), 1.0);
    float axisZ = 1.0 - min(abs(vXZ.x) / fwidth(vXZ.x), 1.0);
    float fade = 1.0 - smoothstep(20.0, 120.0, length(vXZ - uCamPos.xz));
    vec3 col = vec3(0.42, 0.45, 0.5);
    float a = max(g1, g10);
    if (axisX > 0.0) { col = vec3(0.9, 0.3, 0.32); a = max(a, axisX * 0.9); }
    if (axisZ > 0.0) { col = vec3(0.3, 0.5, 0.95); a = max(a, axisZ * 0.9); }
    fragColor = vec4(col, a * fade * 0.85);
    if (fragColor.a < 0.003) discard;
})";

static const char* LINE_VS = R"(#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aColor;
uniform mat4 uProjView;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = uProjView * vec4(aPosition, 1.0); })";

static const char* LINE_FS = R"(#version 330 core
in vec3 vColor;
out vec4 fragColor;
uniform float uAlpha;
void main() { fragColor = vec4(vColor, uAlpha); })";

static const char* TEXT_VS = R"(#version 330 core
layout(location=0) in vec4 aPosUV;
layout(location=1) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPosUV.x / uScreen.x * 2.0 - 1.0, 1.0 - aPosUV.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aPosUV.zw;
    vColor = aColor;
})";

static const char* TEXT_FS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUV).r);
})";
static const char* IMAGE_FS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
uniform sampler2D uTex;
void main() { fragColor = texture(uTex, vUV) * vColor; })";

// ═══ helpers ═══
static GLuint compileProgram(const char* vs, const char* fs, const char* name) {
    auto make = [&](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            char msg[2304];
            snprintf(msg, sizeof(msg), "Shader '%s' error:\n%s", name, log);
            MessageBoxA(nullptr, msg, "Pulse Engine — shader error", MB_ICONERROR);
            return 0;
        }
        return s;
    };
    GLuint v = make(GL_VERTEX_SHADER, vs);
    GLuint f = make(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        MessageBoxA(nullptr, log, "Pulse Engine — shader link error", MB_ICONERROR);
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ═══ primitives ═══
static void primCube(std::vector<float>& d, std::vector<unsigned>& idx) {
    const float F[6][9] = {
        { 1,0,0,  0,1,0,  0,0,1 }, { -1,0,0,  0,0,1,  0,1,0 },
        { 0,1,0,  0,0,1,  1,0,0 }, { 0,-1,0,  1,0,0,  0,0,1 },
        { 0,0,1,  1,0,0,  0,1,0 }, { 0,0,-1,  0,1,0,  1,0,0 },
    };
    const float S[4][2] = { { -1,-1 }, { 1,-1 }, { 1,1 }, { -1,1 } };
    for (int f = 0; f < 6; f++) {
        unsigned base = (unsigned)(d.size() / 6);
        for (int c = 0; c < 4; c++) {
            for (int k = 0; k < 3; k++) {
                d.push_back((F[f][k] + F[f][3 + k] * S[c][0] + F[f][6 + k] * S[c][1]) * 0.5f);
            }
            d.push_back(F[f][0]); d.push_back(F[f][1]); d.push_back(F[f][2]);
        }
        unsigned q[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
        for (unsigned i : q) idx.push_back(i);
    }
}

static void primSphere(std::vector<float>& d, std::vector<unsigned>& idx, int W = 28, int H = 18) {
    for (int y = 0; y <= H; y++) {
        float phi = (float)y / H * PI;
        for (int x = 0; x <= W; x++) {
            float th = (float)x / W * 2 * PI;
            float nx = sinf(phi) * cosf(th), ny = cosf(phi), nz = sinf(phi) * sinf(th);
            d.push_back(nx * 0.5f); d.push_back(ny * 0.5f); d.push_back(nz * 0.5f);
            d.push_back(nx); d.push_back(ny); d.push_back(nz);
        }
    }
    int row = W + 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned a = y * row + x, b = a + row;
            idx.push_back(a); idx.push_back(b); idx.push_back(a + 1);
            idx.push_back(a + 1); idx.push_back(b); idx.push_back(b + 1);
        }
}

static void primCylinder(std::vector<float>& d, std::vector<unsigned>& idx, int R = 24) {
    for (int i = 0; i <= R; i++) {
        float t = (float)i / R * 2 * PI, c = cosf(t), s = sinf(t);
        d.push_back(c * 0.5f); d.push_back(0.5f); d.push_back(s * 0.5f);
        d.push_back(c); d.push_back(0); d.push_back(s);
        d.push_back(c * 0.5f); d.push_back(-0.5f); d.push_back(s * 0.5f);
        d.push_back(c); d.push_back(0); d.push_back(s);
    }
    for (int i = 0; i < R; i++) {
        unsigned a = i * 2;
        idx.push_back(a); idx.push_back(a + 1); idx.push_back(a + 2);
        idx.push_back(a + 2); idx.push_back(a + 1); idx.push_back(a + 3);
    }
    for (int sign = 1; sign >= -1; sign -= 2) {
        unsigned center = (unsigned)(d.size() / 6);
        d.push_back(0); d.push_back(0.5f * sign); d.push_back(0);
        d.push_back(0); d.push_back((float)sign); d.push_back(0);
        for (int i = 0; i <= R; i++) {
            float t = (float)i / R * 2 * PI;
            d.push_back(cosf(t) * 0.5f); d.push_back(0.5f * sign); d.push_back(sinf(t) * 0.5f);
            d.push_back(0); d.push_back((float)sign); d.push_back(0);
        }
        for (int i = 0; i < R; i++) {
            if (sign > 0) { idx.push_back(center); idx.push_back(center + 2 + i); idx.push_back(center + 1 + i); }
            else { idx.push_back(center); idx.push_back(center + 1 + i); idx.push_back(center + 2 + i); }
        }
    }
}

// capsule: sphere split at the equator with hemispheres pushed apart along Y.
// unit size: radius 0.5, total height 2 (like a Unity capsule with scale 1).
static void primCapsule(std::vector<float>& d, std::vector<unsigned>& idx, int W = 24, int H = 16) {
    for (int y = 0; y <= H; y++) {
        float phi = (float)y / H * PI;
        float shift = cosf(phi) >= 0 ? 0.5f : -0.5f;
        for (int x = 0; x <= W; x++) {
            float th = (float)x / W * 2 * PI;
            float nx = sinf(phi) * cosf(th), ny = cosf(phi), nz = sinf(phi) * sinf(th);
            d.push_back(nx * 0.5f);
            d.push_back(ny * 0.5f + shift);
            d.push_back(nz * 0.5f);
            d.push_back(nx); d.push_back(ny); d.push_back(nz);
        }
    }
    int row = W + 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned a = y * row + x, b = a + row;
            idx.push_back(a); idx.push_back(b); idx.push_back(a + 1);
            idx.push_back(a + 1); idx.push_back(b); idx.push_back(b + 1);
        }
}

static void primCone(std::vector<float>& d, std::vector<unsigned>& idx, int R = 16) {
    float nl = sqrtf(1.25f);
    for (int i = 0; i <= R; i++) {
        float t = (float)i / R * 2 * PI, c = cosf(t), s = sinf(t);
        d.push_back(c * 0.5f); d.push_back(-0.5f); d.push_back(s * 0.5f);
        d.push_back(c / nl); d.push_back(0.5f / nl); d.push_back(s / nl);
        d.push_back(0); d.push_back(0.5f); d.push_back(0);
        d.push_back(c / nl); d.push_back(0.5f / nl); d.push_back(s / nl);
    }
    for (int i = 0; i < R; i++) {
        unsigned a = i * 2;
        idx.push_back(a); idx.push_back(a + 1); idx.push_back(a + 2);
    }
    unsigned center = (unsigned)(d.size() / 6);
    d.push_back(0); d.push_back(-0.5f); d.push_back(0);
    d.push_back(0); d.push_back(-1); d.push_back(0);
    for (int i = 0; i <= R; i++) {
        float t = (float)i / R * 2 * PI;
        d.push_back(cosf(t) * 0.5f); d.push_back(-0.5f); d.push_back(sinf(t) * 0.5f);
        d.push_back(0); d.push_back(-1); d.push_back(0);
    }
    for (int i = 0; i < R; i++) {
        idx.push_back(center); idx.push_back(center + 1 + i); idx.push_back(center + 2 + i);
    }
}

// ═══ camera ═══
void OrbitCamera::update(float aspect) {
    if (fpActive) {
        // view through a scene camera (first person): position + rigid-body rotation
        eye = fpEye;
        Vec3 fwd = fpRot.rotate({ 0, 0, -1 });
        Vec3 up = fpRot.rotate({ 0, 1, 0 });
        view = Mat4::lookAt(eye, eye + fwd, up);
        proj = Mat4::perspective(fpFov * DEG2RAD, aspect, nearZ, farZ);
        projView = Mat4::multiply(proj, view);
        invProjView = projView.inverted();
        return;
    }
    pitch = clampf(pitch, -1.55f, 1.55f);
    distance = clampf(distance, 1.2f, 220.0f);
    float cp = cosf(pitch);
    eye = {
        target.x + cosf(yaw) * cp * distance,
        target.y + sinf(pitch) * distance,
        target.z + sinf(yaw) * cp * distance,
    };
    view = Mat4::lookAt(eye, target, { 0, 1, 0 });
    proj = Mat4::perspective(fovDeg * DEG2RAD, aspect, nearZ, farZ);
    projView = Mat4::multiply(proj, view);
    invProjView = projView.inverted();
}

void OrbitCamera::orbit(float dx, float dy) {
    yaw += dx * 0.006f;
    pitch += dy * 0.006f;
}

void OrbitCamera::freeLook(float dx, float dy) {
    // same yaw/pitch change as orbit, but re-anchor the rig so the eye does not move:
    // the camera turns in place (first person) instead of swinging around the target.
    Vec3 keepEye = eye;                 // eye from the last update()
    yaw += dx * 0.006f;
    pitch = clampf(pitch + dy * 0.006f, -1.55f, 1.55f);
    float cp = cosf(pitch);
    Vec3 offset = { cosf(yaw) * cp * distance, sinf(pitch) * distance, sinf(yaw) * cp * distance };
    target = keepEye - offset;          // eye = target + offset  ⇒  keeps eye == keepEye
}

void OrbitCamera::pan(float dx, float dy) {
    float s = distance * 0.0016f;
    target.x += (-view[0] * dx + view[1] * dy) * s;
    target.y += (-view[4] * dx + view[5] * dy) * s;
    target.z += (-view[8] * dx + view[9] * dy) * s;
}

void OrbitCamera::zoom(float delta) {
    distance *= powf(1.0012f, delta);
}

void OrbitCamera::flyMove(float fwd, float right, float up) {
    // uses the basis from the previous update(): forward from eye→target (into the
    // scene), right from the view matrix, up in world space. Moving `target` slides
    // the whole rig — eye is recomputed from it on the next update().
    Vec3 f = (target - eye).normalized();
    Vec3 r = { view[0], view[4], view[8] };
    target = target + f * fwd + r * right + Vec3{ 0, 1, 0 } * up;
}

void OrbitCamera::screenRay(float ndcX, float ndcY, Vec3& origin, Vec3& dir) const {
    Vec3 nearP = invProjView.transformPoint({ ndcX, ndcY, -1 });
    Vec3 farP = invProjView.transformPoint({ ndcX, ndcY, 1 });
    origin = eye;
    dir = (farP - nearP).normalized();
}

// ═══ renderer ═══
Renderer::Mesh Renderer::createMesh(const std::vector<float>& data, const std::vector<unsigned>& idx) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    GLuint vbo, ibo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    m.count = (int)idx.size();
    return m;
}

bool Renderer::init() {
    lit_.id = compileProgram(LIT_VS, LIT_FS, "lit");
    depth_.id = compileProgram(DEPTH_VS, DEPTH_FS, "depth");
    sky_.id = compileProgram(SKY_VS, SKY_FS, "sky");
    grid_.id = compileProgram(GRID_VS, GRID_FS, "grid");
    line_.id = compileProgram(LINE_VS, LINE_FS, "line");
    text_.id = compileProgram(TEXT_VS, TEXT_FS, "text");
    image_.id = compileProgram(TEXT_VS, IMAGE_FS, "image");
    if (!lit_.id || !depth_.id || !sky_.id || !grid_.id || !line_.id || !text_.id) return false;

    std::vector<float> d;
    std::vector<unsigned> idx;
    primCube(d, idx); meshes_[MESH_CUBE] = createMesh(d, idx);
    d.clear(); idx.clear();
    primSphere(d, idx); meshes_[MESH_SPHERE] = createMesh(d, idx);
    d.clear(); idx.clear();
    primCylinder(d, idx); meshes_[MESH_CYLINDER] = createMesh(d, idx);
    d.clear(); idx.clear();
    primCone(d, idx); meshes_[MESH_CONE] = createMesh(d, idx);
    d.clear(); idx.clear();
    primCapsule(d, idx); meshes_[MESH_CAPSULE] = createMesh(d, idx);

    auto makeVao2D = [](const float* verts, int n) -> GLuint {
        GLuint vao, vbo;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, n * sizeof(float), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
        return vao;
    };
    const float tri[] = { -1, -1, 3, -1, -1, 3 };
    const float quad[] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
    screenVao_ = makeVao2D(tri, 6);
    gridVao_ = makeVao2D(quad, 12);

    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glBindVertexArray(0);

    // UI batch: 4 floats posUV + 4 floats color
    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 32, (void*)16);
    glBindVertexArray(0);

    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    buildFont();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    return true;
}

void Renderer::buildFont() {
    const int AW = 512, AH = 128;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = AW;
    bi.bmiHeader.biHeight = -AH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dc, bmp);
    // ANTIALIASED gives a clean grayscale coverage in every channel (we read green),
    // avoiding the colour fringing a ClearType atlas leaves on a single-channel texture
    HFONT font = CreateFontA(-fontH_, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SelectObject(dc, font);
    SetBkColor(dc, RGB(0, 0, 0));
    SetTextColor(dc, RGB(255, 255, 255));
    RECT full = { 0, 0, AW, AH };
    FillRect(dc, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));

    int x = 1, y = 1, rowH = 0;
    for (int c = 32; c < 127; c++) {
        char ch = (char)c;
        SIZE sz;
        GetTextExtentPoint32A(dc, &ch, 1, &sz);
        if (x + sz.cx + 1 >= AW) { x = 1; y += rowH + 1; rowH = 0; }
        TextOutA(dc, x, y, &ch, 1);
        Glyph& g = glyphs_[c - 32];
        g.u0 = (float)x / AW;
        g.v0 = (float)y / AH;
        g.u1 = (float)(x + sz.cx) / AW;
        g.v1 = (float)(y + sz.cy) / AH;
        g.w = sz.cx;
        g.h = sz.cy;
        x += sz.cx + 1;
        rowH = rowH > sz.cy ? rowH : sz.cy;
    }
    GdiFlush();

    std::vector<unsigned char> red(AW * AH);
    const unsigned char* px = (const unsigned char*)bits;
    for (int i = 0; i < AW * AH; i++) red[i] = px[i * 4 + 1];

    // solid-white texel for UI rects (bottom-right corner)
    red[AW * AH - 1] = 255;
    red[AW * AH - 2] = 255;
    red[AW * (AH - 1) - 1] = 255;
    red[AW * (AH - 1) - 2] = 255;
    whiteU_ = (AW - 1.0f) / AW;
    whiteV_ = (AH - 1.0f) / AH;

    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AW, AH, 0, 0x1903 /*GL_RED*/, GL_UNSIGNED_BYTE, red.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
}

float Renderer::textWidth(const std::string& s, float scale) const {
    float w = 0;
    for (char ch : s) {
        int ci = (unsigned char)ch;
        if (ci < 32 || ci > 126) ci = '?';
        w += glyphs_[ci - 32].w * scale;
    }
    return w;
}

void Renderer::drawTextLine(float x, float y, const std::string& s, Vec3 color, float alpha, float scale) {
    for (char ch : s) {
        int ci = (unsigned char)ch;
        if (ci < 32 || ci > 126) ci = '?';
        const Glyph& g = glyphs_[ci - 32];
        float w = g.w * scale, h = g.h * scale;
        float quad[6][4] = {
            { x, y, g.u0, g.v0 }, { x + w, y, g.u1, g.v0 }, { x + w, y + h, g.u1, g.v1 },
            { x, y, g.u0, g.v0 }, { x + w, y + h, g.u1, g.v1 }, { x, y + h, g.u0, g.v1 },
        };
        for (auto& v : quad) {
            uiXform(v[0], v[1]);
            uiBatch_.push_back(v[0]); uiBatch_.push_back(v[1]);
            uiBatch_.push_back(v[2]); uiBatch_.push_back(v[3]);
            uiBatch_.push_back(color.x); uiBatch_.push_back(color.y); uiBatch_.push_back(color.z);
            uiBatch_.push_back(alpha);
        }
        x += w;
    }
}

void Renderer::setUITransform(float m00, float m01, float m10, float m11, float tx, float ty) {
    uiXf_[0] = m00; uiXf_[1] = m01; uiXf_[2] = m10; uiXf_[3] = m11; uiXf_[4] = tx; uiXf_[5] = ty;
    uiXfOn_ = !(m00 == 1 && m01 == 0 && m10 == 0 && m11 == 1 && tx == 0 && ty == 0);
}

void Renderer::drawRectPx(float x, float y, float w, float h, Vec3 color, float alpha) {
    float quad[6][2] = {
        { x, y }, { x + w, y }, { x + w, y + h },
        { x, y }, { x + w, y + h }, { x, y + h },
    };
    for (auto& v : quad) {
        uiXform(v[0], v[1]);
        uiBatch_.push_back(v[0]); uiBatch_.push_back(v[1]);
        uiBatch_.push_back(whiteU_); uiBatch_.push_back(whiteV_);
        uiBatch_.push_back(color.x); uiBatch_.push_back(color.y); uiBatch_.push_back(color.z);
        uiBatch_.push_back(alpha);
    }
}

void Renderer::drawTriPx(float x1, float y1, float x2, float y2, float x3, float y3, Vec3 color, float alpha) {
    float pts[3][2] = { { x1, y1 }, { x2, y2 }, { x3, y3 } };
    for (auto& v : pts) {
        uiXform(v[0], v[1]);
        uiBatch_.push_back(v[0]); uiBatch_.push_back(v[1]);
        uiBatch_.push_back(whiteU_); uiBatch_.push_back(whiteV_);
        uiBatch_.push_back(color.x); uiBatch_.push_back(color.y); uiBatch_.push_back(color.z);
        uiBatch_.push_back(alpha);
    }
}

void Renderer::drawLinePx(float x1, float y1, float x2, float y2, float thickness, Vec3 color, float alpha) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;
    drawTriPx(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, color, alpha);
    drawTriPx(x1 + nx, y1 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, color, alpha);
}

GLuint Renderer::loadPngTexture(const std::string& path) {
    int wlen=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
    std::wstring wide((size_t)wlen,0);MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,&wide[0],wlen);
    IWICImagingFactory* factory=nullptr;IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* conv=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return 0;
    HRESULT hr=factory->CreateDecoderFromFilename(wide.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
    if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);
    if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&conv);
    if(SUCCEEDED(hr))hr=conv->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    UINT w=0,h=0;if(SUCCEEDED(hr))hr=conv->GetSize(&w,&h);
    std::vector<unsigned char> px((size_t)w*h*4);
    if(SUCCEEDED(hr))hr=conv->CopyPixels(nullptr,w*4,(UINT)px.size(),px.data());
    GLuint tex=0;if(SUCCEEDED(hr)&&w&&h){glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,px.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    if(conv)conv->Release();if(frame)frame->Release();if(decoder)decoder->Release();factory->Release();return tex;
}

void Renderer::drawImagePx(GLuint texture,float x,float y,float w,float h,Vec3 tint,float alpha){
    if(!texture)return;flushUI();float q[6][8]={
        {x,y,0,0,tint.x,tint.y,tint.z,alpha},{x+w,y,1,0,tint.x,tint.y,tint.z,alpha},{x+w,y+h,1,1,tint.x,tint.y,tint.z,alpha},
        {x,y,0,0,tint.x,tint.y,tint.z,alpha},{x+w,y+h,1,1,tint.x,tint.y,tint.z,alpha},{x,y+h,0,1,tint.x,tint.y,tint.z,alpha}};
    for(auto& v:q)uiXform(v[0],v[1]);   // images bend with the same affine as rects and text
    glUseProgram(image_.id);glUniform2f(image_.u("uScreen"),(float)width_,(float)height_);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(image_.u("uTex"),0);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    glBindVertexArray(textVao_);glBindBuffer(GL_ARRAY_BUFFER,textVbo_);glBufferData(GL_ARRAY_BUFFER,sizeof(q),q,GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
    glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);drawCalls++;
}

void Renderer::setUIScissor(float x, float y, float w, float h, bool enable) {
    flushUI();
    if (enable) {
        glEnable(GL_SCISSOR_TEST);
        int sy = height_ - (int)(y + h);
        glScissor((int)x, sy < 0 ? 0 : sy, (int)(w < 0 ? 0 : w), (int)(h < 0 ? 0 : h));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void Renderer::flushUI() {
    if (uiBatch_.empty()) return;
    glUseProgram(text_.id);
    glUniform2f(text_.u("uScreen"), (float)width_, (float)height_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glUniform1i(text_.u("uTex"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, uiBatch_.size() * sizeof(float), uiBatch_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(uiBatch_.size() / 8));
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    uiBatch_.clear();
    drawCalls++;
}

void Renderer::drawLineBatch(const std::vector<LineVert>& verts, bool depthTest, const Mat4& projView, float thickness) {
    if (verts.empty()) return;
    glUseProgram(line_.id);
    glUniformMatrix4fv(line_.u("uProjView"), 1, GL_FALSE, projView.m);
    glUniform1f(line_.u("uAlpha"), 0.9f);
    if (!depthTest) glDisable(GL_DEPTH_TEST);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(LineVert), verts.data(), GL_DYNAMIC_DRAW);
    glLineWidth(thickness);
    glDrawArrays(GL_LINES, 0, (GLsizei)verts.size());
    glLineWidth(1);
    glBindVertexArray(0);
    if (!depthTest) glEnable(GL_DEPTH_TEST);
    drawCalls++;
}

void Renderer::drawTriangleBatch(const std::vector<LineVert>& verts, const Mat4& projView) {
    if (verts.empty()) return;
    glUseProgram(line_.id);
    glUniformMatrix4fv(line_.u("uProjView"), 1, GL_FALSE, projView.m);
    glUniform1f(line_.u("uAlpha"), 0.28f);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(LineVert), verts.data(), GL_DYNAMIC_DRAW);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    drawCalls++;
}

Vec3 meshLocalHalfExtent(MeshType mesh) {
    // capsule = sphere of radius .5 shifted ±.5 on Y, so it is 2 units tall
    if (mesh == MESH_CAPSULE) return { 0.5f, 1.0f, 0.5f };
    return { 0.5f, 0.5f, 0.5f };   // cube / sphere / cylinder / cone: unit box
}

void drawItemBounds(const DrawItem& item, Vec3& center, Vec3& halfExtent) {
    item.model.transformAABB({ 0, 0, 0 }, meshLocalHalfExtent(item.mesh), center, halfExtent);
}

void Renderer::drawItemsLit(const std::vector<const DrawItem*>& items, bool overlayPass, int transparencyPass) {
    GLint uModel = lit_.u("uModel");
    GLint uNormalMat = lit_.u("uNormalMat");
    GLint uColor = lit_.u("uColor");
    GLint uOpacity = lit_.u("uOpacity");
    GLint uShininess = lit_.u("uShininess");
    GLint uSpecular = lit_.u("uSpecular");
    GLint uChecker = lit_.u("uChecker");
    GLint uEmissive = lit_.u("uEmissive");
    GLint uDoubleSided = lit_.u("uDoubleSided");
    GLint uUseAlbedo = lit_.u("uUseAlbedo");
    GLint uAlbedoTex = lit_.u("uAlbedoTex");
    GLint uUvScale = lit_.u("uUvScale");
    glUniform1f(uUvScale, 0.5f);   // texture repeats every 2 object-space units
    float nm[9];
    for (const DrawItem* itemPtr : items) {
        const DrawItem& item = *itemPtr;
        if(transparencyPass==0&&item.opacity<.999f)continue;
        if(transparencyPass==1&&item.opacity>=.999f)continue;
        glUniformMatrix4fv(uModel, 1, GL_FALSE, item.model.m);
        item.model.normalMat3(nm);
        glUniformMatrix3fv(uNormalMat, 1, GL_FALSE, nm);
        glUniform3f(uColor, item.color.x, item.color.y, item.color.z);
        glUniform1f(uOpacity, item.opacity);
        glUniform1f(uShininess, item.shininess);
        glUniform1f(uSpecular, item.specular);
        glUniform1f(uChecker, item.checker);
        glUniform1f(uEmissive, overlayPass ? (item.emissive > 0 ? item.emissive : 2.0f) : item.emissive);
        glUniform1i(uDoubleSided, item.doubleSided ? 1 : 0);
        // material base-colour texture (unit 1; unit 0 is the shadow map)
        bool useTex = item.albedoTex != 0 && !overlayPass;
        glUniform1i(uUseAlbedo, useTex ? 1 : 0);
        if (useTex) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, item.albedoTex);
            glUniform1i(uAlbedoTex, 1);
            glActiveTexture(GL_TEXTURE0);
        }
        glBindVertexArray(meshes_[item.mesh].vao);
        glDrawElements(GL_TRIANGLES, meshes_[item.mesh].count, GL_UNSIGNED_INT, nullptr);
        drawCalls++;
    }
}

void Renderer::render(const Frame& frame, const OrbitCamera& cam, int vx, int vy, int vw, int vh, bool clearAll) {
    drawCalls = 0;
    const Env& env = frame.env;

    // ── shadow pass ──
    float ext = 26;
    Vec3 lightEye = frame.shadowCenter + env.sunDir * 60.0f;
    Vec3 up = fabsf(env.sunDir.y) > 0.98f ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 };
    Mat4 lightView = Mat4::lookAt(lightEye, frame.shadowCenter, up);
    Mat4 lightProj = Mat4::ortho(-ext, ext, -ext, ext, 1, 140);
    lightMatrix_ = Mat4::multiply(lightProj, lightView);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(depth_.id);
    glUniformMatrix4fv(depth_.u("uLightMatrix"), 1, GL_FALSE, lightMatrix_.m);
    glCullFace(GL_FRONT);
    GLint uDepthModel = depth_.u("uModel");
    // Casters outside the sun's ortho box are clipped by GL anyway, so skipping
    // them here costs nothing in image quality and saves the draw call.
    Frustum lightFrustum = Frustum::fromMatrix(lightMatrix_);
    culledShadowItems = 0;
    for (const auto& item : frame.items) {
        if (!item.castShadow) continue;
        Vec3 sc, sh;
        drawItemBounds(item, sc, sh);
        if (!lightFrustum.intersectsAABB(sc, sh)) {
            culledShadowItems++;
            if (frustumCulling) continue;
        }
        glUniformMatrix4fv(uDepthModel, 1, GL_FALSE, item.model.m);
        glBindVertexArray(meshes_[item.mesh].vao);
        glDrawElements(GL_TRIANGLES, meshes_[item.mesh].count, GL_UNSIGNED_INT, nullptr);
        drawCalls++;
    }
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── main pass ──
    // clear the whole framebuffer to a neutral colour (the UI draws over it),
    // then render the 3D scene only inside the viewport rect
    if (clearAll) {
        glViewport(0, 0, width_, height_);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.09f, 0.10f, 0.115f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    if (vw <= 0 || vh <= 0) { glBindVertexArray(0); return; }

    int glY = height_ - (vy + vh);        // GL origin is bottom-left
    glViewport(vx, glY, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, glY, vw, vh);
    // overlay mode: the framebuffer already holds UI under this rect and stale
    // depth from an earlier pass — clear just this rect's depth (scissor-limited)
    // so the scene composes correctly. The sky pass repaints the colour.
    if (!clearAll) {
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    glDisable(GL_DEPTH_TEST);
    glUseProgram(sky_.id);
    glUniformMatrix4fv(sky_.u("uInvProjView"), 1, GL_FALSE, cam.invProjView.m);
    glUniform3f(sky_.u("uHorizon"), env.horizon.x, env.horizon.y, env.horizon.z);
    glUniform3f(sky_.u("uZenith"), env.zenith.x, env.zenith.y, env.zenith.z);
    glUniform3f(sky_.u("uGroundCol"), env.fogColor.x, env.fogColor.y, env.fogColor.z);
    glUniform3f(sky_.u("uSunDir"), env.sunDir.x, env.sunDir.y, env.sunDir.z);
    glBindVertexArray(screenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    drawCalls++;

    // ── camera frustum culling ──
    // Build the visible set once; both the opaque and the translucent pass draw
    // from it. Pointers stay valid for the rest of this call.
    // The test always runs so the counters stay meaningful (and comparable) even
    // with culling switched off; only the skip itself is conditional.
    Frustum camFrustum = Frustum::fromMatrix(cam.projView);
    visibleItems_.clear();
    visibleItems_.reserve(frame.items.size());
    culledItems = 0;
    for (const DrawItem& item : frame.items) {
        Vec3 c, h;
        drawItemBounds(item, c, h);
        if (!camFrustum.intersectsAABB(c, h)) {
            culledItems++;
            if (frustumCulling) continue;
        }
        visibleItems_.push_back(&item);
    }
    drawnItems = (int)visibleItems_.size();

    glUseProgram(lit_.id);
    glUniformMatrix4fv(lit_.u("uProj"), 1, GL_FALSE, cam.proj.m);
    glUniformMatrix4fv(lit_.u("uView"), 1, GL_FALSE, cam.view.m);
    glUniform3f(lit_.u("uCamPos"), cam.eye.x, cam.eye.y, cam.eye.z);
    glUniform3f(lit_.u("uSunDir"), env.sunDir.x, env.sunDir.y, env.sunDir.z);
    glUniform3f(lit_.u("uSunColor"), env.sunColor.x, env.sunColor.y, env.sunColor.z);
    glUniform3f(lit_.u("uAmbientSky"), env.ambientSky.x, env.ambientSky.y, env.ambientSky.z);
    glUniform3f(lit_.u("uAmbientGround"), env.ambientGround.x, env.ambientGround.y, env.ambientGround.z);
    glUniform3f(lit_.u("uFogColor"), env.fogColor.x, env.fogColor.y, env.fogColor.z);
    glUniform1f(lit_.u("uFogDensity"), env.fogDensity);
    glUniform1f(lit_.u("uShadowStrength"), env.shadowStrength);
    glUniformMatrix4fv(lit_.u("uShadowMatrix"), 1, GL_FALSE, lightMatrix_.m);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glUniform1i(lit_.u("uShadowMap"), 0);

    int lightCount = (int)frame.lights.size();
    if (lightCount > 8) lightCount = 8;
    glUniform1i(lit_.u("uLightCount"), lightCount);
    if (lightCount) {
        float pos[24], col[24], rng[8];
        for (int i = 0; i < lightCount; i++) {
            const PointLight& L = frame.lights[i];
            pos[i * 3] = L.pos.x; pos[i * 3 + 1] = L.pos.y; pos[i * 3 + 2] = L.pos.z;
            col[i * 3] = L.color.x; col[i * 3 + 1] = L.color.y; col[i * 3 + 2] = L.color.z;
            rng[i] = L.range;
        }
        glUniform3fv(lit_.u("uLightPos"), lightCount, pos);
        glUniform3fv(lit_.u("uLightColor"), lightCount, col);
        glUniform1fv(lit_.u("uLightRange"), lightCount, rng);
    }

    // render meshes two-sided: normals are stored outward, but some primitives
    // (sphere / cylinder sides / capsule) have inverted triangle winding, so
    // back-face culling would hide their outside. Closed opaque meshes look
    // identical two-sided (the outer face wins the depth test), just a little
    // extra overdraw — and it is correct regardless of winding.
    glDisable(GL_CULL_FACE);
    drawItemsLit(visibleItems_, false, 0);

    // Transparent material pass: opaque geometry has already populated depth;
    // translucent objects blend over it without overwriting the depth buffer.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawItemsLit(visibleItems_, false, 1);

    // grid
    if (frame.showGrid) {
        glUseProgram(grid_.id);
        glUniformMatrix4fv(grid_.u("uProj"), 1, GL_FALSE, cam.proj.m);
        glUniformMatrix4fv(grid_.u("uView"), 1, GL_FALSE, cam.view.m);
        glUniform3f(grid_.u("uCamPos"), cam.eye.x, cam.eye.y, cam.eye.z);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(gridVao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_CULL_FACE);
        drawCalls++;
    }

    drawTriangleBatch(frame.trianglesDepth, cam.projView);
    drawLineBatch(frame.linesDepth, true, cam.projView);
    drawLineBatch(frame.linesOverlay, false, cam.projView);
    drawLineBatch(frame.linesOverlayThick, false, cam.projView, 3.5f);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // overlay 3D items (gizmo): always on top, also two-sided — never culled,
    // the gizmo is drawn in the camera's face by construction
    if (!frame.overlay.empty()) {
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_CULL_FACE);
        glUseProgram(lit_.id);
        overlayItems_.clear();
        overlayItems_.reserve(frame.overlay.size());
        for (const DrawItem& item : frame.overlay) overlayItems_.push_back(&item);
        drawItemsLit(overlayItems_, true);
    }
    glEnable(GL_CULL_FACE);   // restore for the next frame's shadow pass
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width_, height_);
    glBindVertexArray(0);
}
