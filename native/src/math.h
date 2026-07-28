// ─── Pulse Engine native: math core (Vec3, Quat, Mat4 — column-major, RH, Y-up) ───
#pragma once
#include <cmath>

constexpr float PI = 3.14159265358979f;
constexpr float DEG2RAD = PI / 180.0f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    Vec3 operator-(const Vec3& v) const { return { x - v.x, y - v.y, z - v.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3 operator-() const { return { -x, -y, -z }; }
    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3 cross(const Vec3& v) const {
        return { y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x };
    }
    float lengthSq() const { return x * x + y * y + z * z; }
    float length() const { return sqrtf(lengthSq()); }
    Vec3 normalized() const {
        float l = length();
        return l > 1e-12f ? Vec3{ x / l, y / l, z / l } : Vec3{ 0, 0, 0 };
    }
    float distanceTo(const Vec3& v) const { return (*this - v).length(); }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    static Quat axisAngle(const Vec3& axis, float rad) {
        float h = rad * 0.5f, s = sinf(h);
        return { axis.x * s, axis.y * s, axis.z * s, cosf(h) };
    }
    // Euler fields in Unreal order for a Y-up world: X = roll (around forward/Z),
    // Y = pitch (around right/X), Z = yaw (around up/Y). Yaw is applied outermost,
    // so it always turns around the world vertical: q = Ryaw * Rpitch * Rroll.
    static Quat fromEulerDeg(float roll, float pitch, float yaw) {
        float sr = sinf(roll * DEG2RAD / 2), cr = cosf(roll * DEG2RAD / 2);
        float sp = sinf(pitch * DEG2RAD / 2), cp = cosf(pitch * DEG2RAD / 2);
        float sy = sinf(yaw * DEG2RAD / 2), cy = cosf(yaw * DEG2RAD / 2);
        return {
            cy * sp * cr + sy * cp * sr,
            sy * cp * cr - cy * sp * sr,
            cy * cp * sr - sy * sp * cr,
            cy * cp * cr + sy * sp * sr,
        };
    }

    Quat operator*(const Quat& q) const {
        return {
            x * q.w + w * q.x + y * q.z - z * q.y,
            y * q.w + w * q.y + z * q.x - x * q.z,
            z * q.w + w * q.z + x * q.y - y * q.x,
            w * q.w - x * q.x - y * q.y - z * q.z,
        };
    }

    Quat conjugate() const { return { -x, -y, -z, w }; }

    Quat normalized() const {
        float l = sqrtf(x * x + y * y + z * z + w * w);
        if (l < 1e-12f) return {};
        return { x / l, y / l, z / l, w / l };
    }

    // rotate vector
    Vec3 rotate(const Vec3& v) const {
        float ix = w * v.x + y * v.z - z * v.y;
        float iy = w * v.y + z * v.x - x * v.z;
        float iz = w * v.z + x * v.y - y * v.x;
        float iw = -x * v.x - y * v.y - z * v.z;
        return {
            ix * w + iw * -x + iy * -z - iz * -y,
            iy * w + iw * -y + iz * -x - ix * -z,
            iz * w + iw * -z + ix * -y - iy * -x,
        };
    }

    // integrate angular velocity: q += 0.5 * (ω q) * dt, normalized
    Quat integrated(const Vec3& omega, float dt) const {
        float h = dt * 0.5f;
        float ox = omega.x * h, oy = omega.y * h, oz = omega.z * h;
        Quat r = {
            x + ox * w + oy * z - oz * y,
            y + oy * w + oz * x - ox * z,
            z + oz * w + ox * y - oy * x,
            w - ox * x - oy * y - oz * z,
        };
        return r.normalized();
    }
};

// 3x3 rotation basis columns from quaternion
inline void quatAxes(const Quat& q, Vec3& ax, Vec3& ay, Vec3& az) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    ax = { 1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y) };
    ay = { 2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x) };
    az = { 2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y) };
}

// ─── Mat4: float[16], column-major ───
struct Mat4 {
    float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    float& operator[](int i) { return m[i]; }
    float operator[](int i) const { return m[i]; }

    static Mat4 identity() { return {}; }

    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 out;
        for (int c = 0; c < 4; c++) {
            float b0 = b[c * 4], b1 = b[c * 4 + 1], b2 = b[c * 4 + 2], b3 = b[c * 4 + 3];
            for (int r = 0; r < 4; r++) {
                out.m[c * 4 + r] = b0 * a[r] + b1 * a[4 + r] + b2 * a[8 + r] + b3 * a[12 + r];
            }
        }
        return out;
    }

    static Mat4 perspective(float fovyRad, float aspect, float nearZ, float farZ) {
        Mat4 out;
        for (auto& v : out.m) v = 0;
        float f = 1.0f / tanf(fovyRad / 2);
        out.m[0] = f / aspect;
        out.m[5] = f;
        out.m[10] = (farZ + nearZ) / (nearZ - farZ);
        out.m[11] = -1;
        out.m[14] = (2 * farZ * nearZ) / (nearZ - farZ);
        return out;
    }

    static Mat4 ortho(float l, float r, float b, float t, float n, float f) {
        Mat4 out;
        for (auto& v : out.m) v = 0;
        out.m[0] = 2 / (r - l);
        out.m[5] = 2 / (t - b);
        out.m[10] = -2 / (f - n);
        out.m[12] = -(r + l) / (r - l);
        out.m[13] = -(t + b) / (t - b);
        out.m[14] = -(f + n) / (f - n);
        out.m[15] = 1;
        return out;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 z = (eye - target).normalized();
        Vec3 x = up.cross(z).normalized();
        Vec3 y = z.cross(x);
        Mat4 o;
        o.m[0] = x.x; o.m[1] = y.x; o.m[2] = z.x; o.m[3] = 0;
        o.m[4] = x.y; o.m[5] = y.y; o.m[6] = z.y; o.m[7] = 0;
        o.m[8] = x.z; o.m[9] = y.z; o.m[10] = z.z; o.m[11] = 0;
        o.m[12] = -x.dot(eye); o.m[13] = -y.dot(eye); o.m[14] = -z.dot(eye); o.m[15] = 1;
        return o;
    }

    static Mat4 compose(const Vec3& p, const Quat& q, const Vec3& s) {
        float x = q.x, y = q.y, z = q.z, w = q.w;
        float x2 = x + x, y2 = y + y, z2 = z + z;
        float xx = x * x2, xy = x * y2, xz = x * z2;
        float yy = y * y2, yz = y * z2, zz = z * z2;
        float wx = w * x2, wy = w * y2, wz = w * z2;
        Mat4 o;
        o.m[0] = (1 - (yy + zz)) * s.x; o.m[1] = (xy + wz) * s.x; o.m[2] = (xz - wy) * s.x; o.m[3] = 0;
        o.m[4] = (xy - wz) * s.y; o.m[5] = (1 - (xx + zz)) * s.y; o.m[6] = (yz + wx) * s.y; o.m[7] = 0;
        o.m[8] = (xz + wy) * s.z; o.m[9] = (yz - wx) * s.z; o.m[10] = (1 - (xx + yy)) * s.z; o.m[11] = 0;
        o.m[12] = p.x; o.m[13] = p.y; o.m[14] = p.z; o.m[15] = 1;
        return o;
    }

    Mat4 inverted() const {
        const float* a = m;
        float a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
        float a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
        float a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
        float a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];
        float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
        float b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
        float b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
        float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
        float b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
        float b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
        float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
        if (fabsf(det) < 1e-20f) return {};
        float d = 1.0f / det;
        Mat4 o;
        o.m[0] = (a11 * b11 - a12 * b10 + a13 * b09) * d;
        o.m[1] = (a02 * b10 - a01 * b11 - a03 * b09) * d;
        o.m[2] = (a31 * b05 - a32 * b04 + a33 * b03) * d;
        o.m[3] = (a22 * b04 - a21 * b05 - a23 * b03) * d;
        o.m[4] = (a12 * b08 - a10 * b11 - a13 * b07) * d;
        o.m[5] = (a00 * b11 - a02 * b08 + a03 * b07) * d;
        o.m[6] = (a32 * b02 - a30 * b05 - a33 * b01) * d;
        o.m[7] = (a20 * b05 - a22 * b02 + a23 * b01) * d;
        o.m[8] = (a10 * b10 - a11 * b08 + a13 * b06) * d;
        o.m[9] = (a01 * b08 - a00 * b10 - a03 * b06) * d;
        o.m[10] = (a30 * b04 - a31 * b02 + a33 * b00) * d;
        o.m[11] = (a21 * b02 - a20 * b04 - a23 * b00) * d;
        o.m[12] = (a11 * b07 - a10 * b09 - a12 * b06) * d;
        o.m[13] = (a00 * b09 - a01 * b07 + a02 * b06) * d;
        o.m[14] = (a31 * b01 - a30 * b03 - a32 * b00) * d;
        o.m[15] = (a20 * b03 - a21 * b01 + a22 * b00) * d;
        return o;
    }

    // transform point with perspective divide
    Vec3 transformPoint(const Vec3& v) const {
        float w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
        if (fabsf(w) < 1e-20f) w = 1;
        return {
            (m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12]) / w,
            (m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13]) / w,
            (m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]) / w,
        };
    }

    // transform a local-space AABB (centre + positive half-extents) by this matrix,
    // returning the tight world-space AABB of the transformed box (Arvo's method).
    void transformAABB(const Vec3& c, const Vec3& h, Vec3& outC, Vec3& outH) const {
        outC = transformPoint(c);
        outH = {
            fabsf(m[0]) * h.x + fabsf(m[4]) * h.y + fabsf(m[8]) * h.z,
            fabsf(m[1]) * h.x + fabsf(m[5]) * h.y + fabsf(m[9]) * h.z,
            fabsf(m[2]) * h.x + fabsf(m[6]) * h.y + fabsf(m[10]) * h.z,
        };
    }

    // 3x3 normal matrix (inverse-transpose of upper-left), column-major float[9]
    void normalMat3(float* out) const {
        float a00 = m[0], a01 = m[1], a02 = m[2];
        float a10 = m[4], a11 = m[5], a12 = m[6];
        float a20 = m[8], a21 = m[9], a22 = m[10];
        float b01 = a22 * a11 - a12 * a21;
        float b11 = -a22 * a10 + a12 * a20;
        float b21 = a21 * a10 - a11 * a20;
        float det = a00 * b01 + a01 * b11 + a02 * b21;
        if (fabsf(det) < 1e-20f) {
            out[0] = 1; out[1] = 0; out[2] = 0;
            out[3] = 0; out[4] = 1; out[5] = 0;
            out[6] = 0; out[7] = 0; out[8] = 1;
            return;
        }
        float d = 1.0f / det;
        float t[9];
        t[0] = b01 * d;
        t[1] = (-a22 * a01 + a02 * a21) * d;
        t[2] = (a12 * a01 - a02 * a11) * d;
        t[3] = b11 * d;
        t[4] = (a22 * a00 - a02 * a20) * d;
        t[5] = (-a12 * a00 + a02 * a10) * d;
        t[6] = b21 * d;
        t[7] = (-a21 * a00 + a01 * a20) * d;
        t[8] = (a11 * a00 - a01 * a10) * d;
        // transpose
        out[0] = t[0]; out[1] = t[3]; out[2] = t[6];
        out[3] = t[1]; out[4] = t[4]; out[5] = t[7];
        out[6] = t[2]; out[7] = t[5]; out[8] = t[8];
    }
};

// ─── view frustum: the 6 clip planes, with inward-pointing normals ───
// A point p is inside when n[i]·p + d[i] >= 0 for every plane. Works for any
// projection*view (perspective camera or the sun's ortho box for shadows).
struct Frustum {
    Vec3 n[6];      // left, right, bottom, top, near, far
    float d[6];

    // Gribb–Hartmann extraction. Our Mat4 is column-major (m[col*4+row]), so the
    // clip-space rows that produce x/y/z/w are strided by 4.
    static Frustum fromMatrix(const Mat4& mat) {
        float r[4][4];
        for (int i = 0; i < 4; i++) {
            r[i][0] = mat[i]; r[i][1] = mat[4 + i]; r[i][2] = mat[8 + i]; r[i][3] = mat[12 + i];
        }
        // inside the unit clip cube means -w <= x,y,z <= w  →  w±x, w±y, w±z >= 0
        float p[6][4];
        for (int k = 0; k < 4; k++) {
            p[0][k] = r[3][k] + r[0][k];   // left
            p[1][k] = r[3][k] - r[0][k];   // right
            p[2][k] = r[3][k] + r[1][k];   // bottom
            p[3][k] = r[3][k] - r[1][k];   // top
            p[4][k] = r[3][k] + r[2][k];   // near
            p[5][k] = r[3][k] - r[2][k];   // far
        }
        Frustum f;
        for (int i = 0; i < 6; i++) {
            float len = sqrtf(p[i][0] * p[i][0] + p[i][1] * p[i][1] + p[i][2] * p[i][2]);
            if (len < 1e-20f) len = 1;
            f.n[i] = { p[i][0] / len, p[i][1] / len, p[i][2] / len };
            f.d[i] = p[i][3] / len;
        }
        return f;
    }

    bool intersectsSphere(const Vec3& c, float radius) const {
        for (int i = 0; i < 6; i++) if (n[i].dot(c) + d[i] < -radius) return false;
        return true;
    }

    // AABB as centre + positive half-extents. Conservative "n-vertex" test: the
    // box is rejected only when it lies fully behind a plane.
    bool intersectsAABB(const Vec3& c, const Vec3& h) const {
        for (int i = 0; i < 6; i++) {
            float reach = fabsf(n[i].x) * h.x + fabsf(n[i].y) * h.y + fabsf(n[i].z) * h.z;
            if (n[i].dot(c) + d[i] < -reach) return false;
        }
        return true;
    }
};
