// ─── Pulse Engine native physics implementation ───
#include "physics.h"
#include <algorithm>

static constexpr float BAUMGARTE = 0.2f;
static constexpr float SLOP = 0.005f;
static constexpr float RESTITUTION_THRESHOLD = 1.0f;
static constexpr int VEL_ITERATIONS = 10;
static constexpr float SLEEP_LIN_SQ = 0.02f;
static constexpr float SLEEP_ANG_SQ = 0.05f;
static constexpr float SLEEP_TIME = 0.6f;

// ═══ RigidBody ═══
void RigidBody::setMass(float m) {
    if (type == BodyType::Static || m <= 0) {
        mass = 0; invMass = 0; invInertiaLocal = {};
    } else {
        mass = m; invMass = 1.0f / m;
        if (shape.kind == ShapeKind::Sphere) {
            float i = 0.4f * m * shape.radius * shape.radius;
            invInertiaLocal = { 1 / i, 1 / i, 1 / i };
        } else if (shape.kind == ShapeKind::Capsule) {
            float r = shape.radius, h = shape.halfHeight;
            float cylinderVolume = PI * r * r * (2 * h);
            float sphereVolume = (4.0f / 3.0f) * PI * r * r * r;
            float totalVolume = cylinderVolume + sphereVolume;
            float mc = totalVolume > 1e-9f ? m * cylinderVolume / totalVolume : 0;
            float ms = m - mc;
            float iy = 0.5f * mc * r * r + 0.4f * ms * r * r;
            float hemiOffset = h + 0.375f * r;
            float ix = mc * (3 * r * r + 4 * h * h) / 12.0f
                     + ms * ((83.0f / 320.0f) * r * r + hemiOffset * hemiOffset);
            invInertiaLocal = { 1 / ix, 1 / iy, 1 / ix };
        } else if (shape.kind == ShapeKind::Cylinder) {
            float r = shape.radius, h = shape.halfHeight;
            float ix = m * (3 * r * r + 4 * h * h) / 12.0f;
            float iy = 0.5f * m * r * r;
            invInertiaLocal = { 1 / ix, 1 / iy, 1 / ix };
        } else if (shape.kind == ShapeKind::Cone) {
            float r = shape.radius, h = shape.halfHeight;
            float ix = m * (0.15f * r * r + 0.4f * h * h);
            float iy = 0.3f * m * r * r;
            invInertiaLocal = { 1 / ix, 1 / iy, 1 / ix };
        } else {
            float ex = 2 * shape.h.x, ey = 2 * shape.h.y, ez = 2 * shape.h.z;
            float ix = (m / 12.0f) * (ey * ey + ez * ez);
            float iy = (m / 12.0f) * (ex * ex + ez * ez);
            float iz = (m / 12.0f) * (ex * ex + ey * ey);
            invInertiaLocal = { 1 / ix, 1 / iy, 1 / iz };
        }
    }
    updateInertiaWorld();
}

void RigidBody::updateInertiaWorld() {
    Vec3 cx, cy, cz;
    quatAxes(quat, cx, cy, cz);
    // rows of R
    float r[3][3] = { { cx.x, cy.x, cz.x }, { cx.y, cy.y, cz.y }, { cx.z, cy.z, cz.z } };
    float d[3] = { invInertiaLocal.x, invInertiaLocal.y, invInertiaLocal.z };
    // I⁻¹ = R diag(d) Rᵀ
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float s = 0;
            for (int k = 0; k < 3; k++) s += r[i][k] * d[k] * r[j][k];
            invInertiaWorld[i * 3 + j] = s;
        }
    }
}

Vec3 RigidBody::mulInvInertia(const Vec3& v) const {
    const float* m = invInertiaWorld;
    return {
        m[0] * v.x + m[1] * v.y + m[2] * v.z,
        m[3] * v.x + m[4] * v.y + m[5] * v.z,
        m[6] * v.x + m[7] * v.y + m[8] * v.z,
    };
}

void RigidBody::updateAABB() {
    if (shape.kind == ShapeKind::Sphere) {
        Vec3 r = { shape.radius, shape.radius, shape.radius };
        aabb.min = position - r;
        aabb.max = position + r;
    } else if (shape.kind == ShapeKind::Capsule) {
        Vec3 axis = quat.rotate({ 0, 1, 0 });
        Vec3 e = { fabsf(axis.x) * shape.halfHeight + shape.radius,
                   fabsf(axis.y) * shape.halfHeight + shape.radius,
                   fabsf(axis.z) * shape.halfHeight + shape.radius };
        aabb.min = position - e;
        aabb.max = position + e;
    } else {
        Vec3 cx, cy, cz;
        quatAxes(quat, cx, cy, cz);
        Vec3 e = {
            fabsf(cx.x) * shape.h.x + fabsf(cy.x) * shape.h.y + fabsf(cz.x) * shape.h.z,
            fabsf(cx.y) * shape.h.x + fabsf(cy.y) * shape.h.y + fabsf(cz.y) * shape.h.z,
            fabsf(cx.z) * shape.h.x + fabsf(cy.z) * shape.h.y + fabsf(cz.z) * shape.h.z,
        };
        aabb.min = position - e;
        aabb.max = position + e;
    }
}

void RigidBody::applyForce(const Vec3& f) {
    if (type != BodyType::Dynamic) return;
    wake();
    force += f;
}

void RigidBody::applyForceAt(const Vec3& f, const Vec3& worldPoint) {
    if (type != BodyType::Dynamic) return;
    wake();
    force += f;
    torque += (worldPoint - position).cross(f);
}

void RigidBody::applyTorque(const Vec3& t) {
    if (type != BodyType::Dynamic) return;
    wake();
    torque += t;
}

void RigidBody::applyImpulse(const Vec3& imp) {
    if (type != BodyType::Dynamic) return;
    wake();
    velocity += imp * invMass;
}

void RigidBody::applyImpulseAt(const Vec3& imp, const Vec3& worldPoint) {
    if (type != BodyType::Dynamic) return;
    wake();
    velocity += imp * invMass;
    angularVelocity += mulInvInertia((worldPoint - position).cross(imp));
}

// ═══ narrowphase ═══
static int sphereSphere(const RigidBody& a, const RigidBody& b, Contact* out) {
    float ra = a.shape.radius, rb = b.shape.radius;
    Vec3 d = b.position - a.position;
    float distSq = d.lengthSq(), rSum = ra + rb;
    if (distSq >= rSum * rSum) return 0;
    float dist = sqrtf(distSq);
    Vec3 n = dist > 1e-9f ? d * (1 / dist) : Vec3{ 0, 1, 0 };
    out[0].normal = n;
    out[0].depth = rSum - dist;
    out[0].point = a.position + n * (ra - out[0].depth * 0.5f);
    return 1;
}

static int boxSphere(const RigidBody& box, const RigidBody& sph, Contact* out) {
    const Shape& s = box.shape;
    float r = sph.shape.radius;
    Vec3 local = box.quat.conjugate().rotate(sph.position - box.position);
    Vec3 c = { clampf(local.x, -s.h.x, s.h.x), clampf(local.y, -s.h.y, s.h.y), clampf(local.z, -s.h.z, s.h.z) };
    Vec3 d = local - c;
    float distSq = d.lengthSq();
    Vec3 nLocal;
    float depth;
    if (distSq > 1e-12f) {
        if (distSq >= r * r) return 0;
        float dist = sqrtf(distSq);
        nLocal = d * (1 / dist);
        depth = r - dist;
    } else {
        float px = s.h.x - fabsf(local.x), py = s.h.y - fabsf(local.y), pz = s.h.z - fabsf(local.z);
        if (px < py && px < pz) { nLocal = { local.x >= 0 ? 1.0f : -1.0f, 0, 0 }; depth = px + r; }
        else if (py < pz)       { nLocal = { 0, local.y >= 0 ? 1.0f : -1.0f, 0 }; depth = py + r; }
        else                    { nLocal = { 0, 0, local.z >= 0 ? 1.0f : -1.0f }; depth = pz + r; }
    }
    out[0].normal = box.quat.rotate(nLocal);
    out[0].point = box.quat.rotate(c) + box.position;
    out[0].depth = depth;
    return 1;
}

// box-box SAT with reference-face clipping
struct Poly { Vec3 v[8]; int n = 0; };

static Poly clipPoly(const Poly& in, const Vec3& n, float dist) {
    Poly out;
    for (int i = 0; i < in.n; i++) {
        const Vec3& p0 = in.v[i];
        const Vec3& p1 = in.v[(i + 1) % in.n];
        float d0 = n.dot(p0) - dist, d1 = n.dot(p1) - dist;
        if (d0 <= 0 && out.n < 8) out.v[out.n++] = p0;
        if (((d0 < 0 && d1 > 0) || (d0 > 0 && d1 < 0)) && out.n < 8) {
            float t = d0 / (d0 - d1);
            out.v[out.n++] = p0 + (p1 - p0) * t;
        }
    }
    return out;
}

static float projectBox(const RigidBody& b, const Vec3 axes[3], const Vec3& axis) {
    return fabsf(axes[0].dot(axis)) * b.shape.h.x +
           fabsf(axes[1].dot(axis)) * b.shape.h.y +
           fabsf(axes[2].dot(axis)) * b.shape.h.z;
}

static Poly faceVerts(const RigidBody& b, const Vec3 axes[3], int axisIdx, float sign) {
    float h[3] = { b.shape.h.x, b.shape.h.y, b.shape.h.z };
    int u = (axisIdx + 1) % 3, v = (axisIdx + 2) % 3;
    Vec3 c = b.position + axes[axisIdx] * (h[axisIdx] * sign);
    Poly p;
    const float su[4] = { 1, -1, -1, 1 }, sv[4] = { 1, 1, -1, -1 };
    for (int i = 0; i < 4; i++) {
        p.v[p.n++] = c + axes[u] * (h[u] * su[i]) + axes[v] * (h[v] * sv[i]);
    }
    return p;
}

static int faceContact(const RigidBody& ref, const RigidBody& inc,
                       const Vec3 axesRef[3], const Vec3 axesInc[3],
                       const Vec3& refNormal, bool flipped, Contact* out) {
    int refIdx = 0; float refBest = -1e30f, refSign = 1;
    for (int i = 0; i < 3; i++) {
        float dp = axesRef[i].dot(refNormal);
        if (fabsf(dp) > refBest) { refBest = fabsf(dp); refIdx = i; refSign = dp >= 0 ? 1.0f : -1.0f; }
    }
    int incIdx = 0; float incBest = 1e30f, incSign = 1;
    for (int i = 0; i < 3; i++) {
        float dp = axesInc[i].dot(refNormal);
        if (dp < incBest) { incBest = dp; incIdx = i; incSign = 1; }
        if (-dp < incBest) { incBest = -dp; incIdx = i; incSign = -1; }
    }
    Poly poly = faceVerts(inc, axesInc, incIdx, incSign);

    float h[3] = { ref.shape.h.x, ref.shape.h.y, ref.shape.h.z };
    int u = (refIdx + 1) % 3, v = (refIdx + 2) % 3;
    const Vec3 sideAxes[2] = { axesRef[u], axesRef[v] };
    const float sideH[2] = { h[u], h[v] };
    for (int i = 0; i < 2; i++) {
        float cdot = sideAxes[i].dot(ref.position);
        poly = clipPoly(poly, sideAxes[i], cdot + sideH[i]);
        if (!poly.n) return 0;
        poly = clipPoly(poly, -sideAxes[i], -(cdot - sideH[i]));
        if (!poly.n) return 0;
    }

    Vec3 faceN = axesRef[refIdx] * refSign;
    float faceDist = faceN.dot(ref.position) + h[refIdx];
    Contact tmp[8];
    int n = 0;
    for (int i = 0; i < poly.n && n < 8; i++) {
        float depth = faceDist - faceN.dot(poly.v[i]);
        if (depth >= -1e-4f) {
            tmp[n].point = poly.v[i] + faceN * (depth * 0.5f);
            tmp[n].normal = flipped ? -faceN : faceN;
            tmp[n].depth = depth > 0 ? depth : 0;
            n++;
        }
    }
    if (!n) return 0;
    std::sort(tmp, tmp + n, [](const Contact& x, const Contact& y) { return x.depth > y.depth; });
    int keep = n < 4 ? n : 4;
    for (int i = 0; i < keep; i++) out[i] = tmp[i];
    return keep;
}

static int edgeContact(const RigidBody& a, const RigidBody& b,
                       const Vec3 axesA[3], const Vec3 axesB[3],
                       int ea, int eb, const Vec3& normal, float pen, Contact* out) {
    auto suppEdge = [](const RigidBody& box, const Vec3 axes[3], int axisIdx, const Vec3& n, Vec3& p, Vec3& q) {
        float h[3] = { box.shape.h.x, box.shape.h.y, box.shape.h.z };
        int u = (axisIdx + 1) % 3, v = (axisIdx + 2) % 3;
        float su = axes[u].dot(n) >= 0 ? 1.0f : -1.0f;
        float sv = axes[v].dot(n) >= 0 ? 1.0f : -1.0f;
        Vec3 mid = box.position + axes[u] * (h[u] * su) + axes[v] * (h[v] * sv);
        p = mid - axes[axisIdx] * h[axisIdx];
        q = mid + axes[axisIdx] * h[axisIdx];
    };
    Vec3 p1, q1, p2, q2;
    suppEdge(a, axesA, ea, normal, p1, q1);
    suppEdge(b, axesB, eb, -normal, p2, q2);

    Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float A = d1.lengthSq(), E = d2.lengthSq(), F = d2.dot(r);
    float B = d1.dot(d2), C = d1.dot(r);
    float den = A * E - B * B;
    float s = den > 1e-9f ? clampf((B * F - C * E) / den, 0, 1) : 0;
    float t = E > 1e-9f ? clampf((B * s + F) / E, 0, 1) : 0;
    s = A > 1e-9f ? clampf((B * t - C) / A, 0, 1) : 0;
    Vec3 c1 = p1 + d1 * s, c2 = p2 + d2 * t;
    out[0].point = (c1 + c2) * 0.5f;
    out[0].normal = normal;
    out[0].depth = pen;
    return 1;
}

static int boxBox(const RigidBody& a, const RigidBody& b, Contact* out) {
    Vec3 axesA[3], axesB[3];
    quatAxes(a.quat, axesA[0], axesA[1], axesA[2]);
    quatAxes(b.quat, axesB[0], axesB[1], axesB[2]);
    Vec3 d = b.position - a.position;

    float minPen = 1e30f;
    Vec3 bestAxis;
    int bestType = -1, bestEA = -1, bestEB = -1;

    auto testAxis = [&](Vec3 axis, int type, int ea, int eb) -> bool {
        float lenSq = axis.lengthSq();
        if (lenSq < 1e-8f) return true;
        axis *= 1.0f / sqrtf(lenSq);
        float dist = fabsf(d.dot(axis));
        float pen = projectBox(a, axesA, axis) + projectBox(b, axesB, axis) - dist;
        if (pen < 0) return false;
        float weighted = type >= 6 ? pen * 0.95f - 1e-4f : pen;
        float bestWeighted = bestType >= 6 ? minPen * 0.95f - 1e-4f : minPen;
        if (weighted < bestWeighted) {
            minPen = pen; bestAxis = axis; bestType = type; bestEA = ea; bestEB = eb;
        }
        return true;
    };

    for (int i = 0; i < 3; i++) if (!testAxis(axesA[i], i, -1, -1)) return 0;
    for (int i = 0; i < 3; i++) if (!testAxis(axesB[i], 3 + i, -1, -1)) return 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (!testAxis(axesA[i].cross(axesB[j]), 6 + i * 3 + j, i, j)) return 0;

    if (bestType < 0) return 0;
    Vec3 normal = bestAxis;
    if (normal.dot(d) < 0) normal = -normal;

    if (bestType >= 6) return edgeContact(a, b, axesA, axesB, bestEA, bestEB, normal, minPen, out);
    if (bestType < 3) return faceContact(a, b, axesA, axesB, normal, false, out);
    return faceContact(b, a, axesB, axesA, -normal, true, out);
}

static void capsuleSegment(const RigidBody& capsule, Vec3& p0, Vec3& p1) {
    Vec3 axis = capsule.quat.rotate({ 0, 1, 0 });
    p0 = capsule.position - axis * capsule.shape.halfHeight;
    p1 = capsule.position + axis * capsule.shape.halfHeight;
}

static Vec3 closestPointSegment(const Vec3& a, const Vec3& b, const Vec3& p) {
    Vec3 d = b - a;
    float den = d.lengthSq();
    float t = den > 1e-10f ? clampf((p - a).dot(d) / den, 0, 1) : 0;
    return a + d * t;
}

static void closestSegmentSegment(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2,
                                  Vec3& c1, Vec3& c2) {
    Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float a = d1.lengthSq(), e = d2.lengthSq(), f = d2.dot(r);
    float s = 0, t = 0;
    if (a <= 1e-10f && e <= 1e-10f) { c1 = p1; c2 = p2; return; }
    if (a <= 1e-10f) {
        t = clampf(f / e, 0, 1);
    } else {
        float c = d1.dot(r);
        if (e <= 1e-10f) {
            s = clampf(-c / a, 0, 1);
        } else {
            float b = d1.dot(d2), den = a * e - b * b;
            if (den > 1e-10f) s = clampf((b * f - c * e) / den, 0, 1);
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = clampf(-c / a, 0, 1); }
            else if (t > 1) { t = 1; s = clampf((b - c) / a, 0, 1); }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

static float pointAabbDistSq(const Vec3& p, const Vec3& h, Vec3& closest) {
    closest = { clampf(p.x, -h.x, h.x), clampf(p.y, -h.y, h.y), clampf(p.z, -h.z, h.z) };
    return (p - closest).lengthSq();
}

// Exact closest pair between a segment and an axis-aligned box.  The squared
// distance is piecewise quadratic; crossings of the six face planes split the
// parameter range into intervals that can each be minimized analytically.
static float closestSegmentAabb(const Vec3& p0, const Vec3& p1, const Vec3& h,
                                Vec3& segPoint, Vec3& boxPoint) {
    Vec3 d = p1 - p0;
    float cuts[8] = { 0, 1 };
    int cutCount = 2;
    for (int axis = 0; axis < 3; axis++) {
        float o = (&p0.x)[axis], v = (&d.x)[axis], hh = (&h.x)[axis];
        if (fabsf(v) < 1e-10f) continue;
        float ta = (-hh - o) / v, tb = (hh - o) / v;
        if (ta > 0 && ta < 1) cuts[cutCount++] = ta;
        if (tb > 0 && tb < 1) cuts[cutCount++] = tb;
    }
    std::sort(cuts, cuts + cutCount);
    float uniqueCuts[8]; int n = 0;
    for (int i = 0; i < cutCount; i++)
        if (!n || fabsf(cuts[i] - uniqueCuts[n - 1]) > 1e-6f) uniqueCuts[n++] = cuts[i];

    float best = 1e30f, bestT = 0;
    auto evaluate = [&](float t) {
        Vec3 p = p0 + d * t, c;
        float ds = pointAabbDistSq(p, h, c);
        if (ds < best - 1e-9f || (fabsf(ds - best) <= 1e-9f && fabsf(t - .5f) < fabsf(bestT - .5f))) {
            best = ds; bestT = t; segPoint = p; boxPoint = c;
        }
    };
    for (int i = 0; i + 1 < n; i++) {
        float lo = uniqueCuts[i], hi = uniqueCuts[i + 1], mid = (lo + hi) * .5f;
        float qa = 0, qb = 0;
        for (int axis = 0; axis < 3; axis++) {
            float o = (&p0.x)[axis], v = (&d.x)[axis], hh = (&h.x)[axis];
            float m = o + v * mid, bound;
            if (m < -hh) bound = -hh;
            else if (m > hh) bound = hh;
            else continue;
            qa += v * v;
            qb += v * (o - bound);
        }
        float t = qa > 1e-12f ? clampf(-qb / qa, lo, hi) : clampf(.5f, lo, hi);
        evaluate(lo); evaluate(t); evaluate(hi);
    }
    return best;
}

static bool boxPointSphereContact(const RigidBody& box, const Vec3& center, float radius, Contact& out) {
    Vec3 local = box.quat.conjugate().rotate(center - box.position);
    Vec3 closest;
    float distSq = pointAabbDistSq(local, box.shape.h, closest);
    Vec3 nLocal;
    if (distSq > 1e-12f) {
        if (distSq >= radius * radius) return false;
        float dist = sqrtf(distSq);
        nLocal = (local - closest) * (1.0f / dist);
        out.depth = radius - dist;
    } else {
        float px = box.shape.h.x - fabsf(local.x);
        float py = box.shape.h.y - fabsf(local.y);
        float pz = box.shape.h.z - fabsf(local.z);
        if (px < py && px < pz) { nLocal = { local.x >= 0 ? 1.0f : -1.0f, 0, 0 }; closest.x = nLocal.x * box.shape.h.x; out.depth = px + radius; }
        else if (py < pz) { nLocal = { 0, local.y >= 0 ? 1.0f : -1.0f, 0 }; closest.y = nLocal.y * box.shape.h.y; out.depth = py + radius; }
        else { nLocal = { 0, 0, local.z >= 0 ? 1.0f : -1.0f }; closest.z = nLocal.z * box.shape.h.z; out.depth = pz + radius; }
    }
    out.normal = box.quat.rotate(nLocal);
    out.point = box.position + box.quat.rotate(closest);
    return true;
}

// Box -> capsule. Endpoint sphere contacts give the correct torque when the
// capsule is tilted; the middle contact covers edge/face hits along its shaft.
static int boxCapsule(const RigidBody& box, const RigidBody& capsule, Contact* out) {
    Vec3 p0, p1;
    capsuleSegment(capsule, p0, p1);
    int n = 0;
    Contact c;
    if (boxPointSphereContact(box, p0, capsule.shape.radius, c)) out[n++] = c;
    if (boxPointSphereContact(box, p1, capsule.shape.radius, c)) {
        if (!n || (c.point - out[0].point).lengthSq() > 1e-6f) out[n++] = c;
    }

    Quat inv = box.quat.conjugate();
    Vec3 lp0 = inv.rotate(p0 - box.position), lp1 = inv.rotate(p1 - box.position);
    Vec3 ls, lb;
    float distSq = closestSegmentAabb(lp0, lp1, box.shape.h, ls, lb);
    if (distSq < capsule.shape.radius * capsule.shape.radius) {
        Vec3 worldCenter = box.position + box.quat.rotate(ls);
        if (boxPointSphereContact(box, worldCenter, capsule.shape.radius, c)) {
            bool duplicate = false;
            for (int i = 0; i < n; i++)
                if ((c.point - out[i].point).lengthSq() < 1e-6f && c.normal.dot(out[i].normal) > .99f) duplicate = true;
            if (!duplicate && n < 3) out[n++] = c;
        }
    }
    return n;
}

static int capsuleSphere(const RigidBody& capsule, const RigidBody& sphere, Contact* out) {
    Vec3 p0, p1;
    capsuleSegment(capsule, p0, p1);
    Vec3 ca = closestPointSegment(p0, p1, sphere.position);
    Vec3 d = sphere.position - ca;
    float distSq = d.lengthSq(), r = capsule.shape.radius + sphere.shape.radius;
    if (distSq >= r * r) return 0;
    float dist = sqrtf(distSq);
    Vec3 normal = dist > 1e-8f ? d * (1.0f / dist) : (sphere.position - capsule.position).normalized();
    if (normal.lengthSq() < 1e-8f) normal = { 0, 1, 0 };
    out[0].normal = normal;
    out[0].depth = r - dist;
    Vec3 onA = ca + normal * capsule.shape.radius;
    Vec3 onB = sphere.position - normal * sphere.shape.radius;
    out[0].point = (onA + onB) * .5f;
    return 1;
}

static int capsuleCapsule(const RigidBody& a, const RigidBody& b, Contact* out) {
    Vec3 a0, a1, b0, b1, ca, cb;
    capsuleSegment(a, a0, a1); capsuleSegment(b, b0, b1);
    closestSegmentSegment(a0, a1, b0, b1, ca, cb);
    Vec3 d = cb - ca;
    float distSq = d.lengthSq(), r = a.shape.radius + b.shape.radius;
    if (distSq >= r * r) return 0;
    float dist = sqrtf(distSq);
    Vec3 normal = dist > 1e-8f ? d * (1.0f / dist) : (b.position - a.position).normalized();
    if (normal.lengthSq() < 1e-8f) normal = { 0, 1, 0 };
    out[0].normal = normal;
    out[0].depth = r - dist;
    out[0].point = ((ca + normal * a.shape.radius) + (cb - normal * b.shape.radius)) * .5f;
    return 1;
}

static Vec3 convexSupport(const RigidBody& b, const Vec3& worldDir) {
    Vec3 d = worldDir;
    float dl = d.length();
    if (dl < 1e-8f) d = { 1, 0, 0 };
    Quat inv = b.quat.conjugate();
    Vec3 ld = inv.rotate(d);
    Vec3 p;
    switch (b.shape.kind) {
    case ShapeKind::Sphere:
        return b.position + d.normalized() * b.shape.radius;
    case ShapeKind::Box:
        p = { ld.x >= 0 ? b.shape.h.x : -b.shape.h.x,
              ld.y >= 0 ? b.shape.h.y : -b.shape.h.y,
              ld.z >= 0 ? b.shape.h.z : -b.shape.h.z };
        break;
    case ShapeKind::Capsule: {
        Vec3 n = ld.lengthSq() > 1e-10f ? ld.normalized() : Vec3{ 1, 0, 0 };
        p = n * b.shape.radius;
        p.y += ld.y >= 0 ? b.shape.halfHeight : -b.shape.halfHeight;
        break;
    }
    case ShapeKind::Cylinder: {
        float rl = sqrtf(ld.x * ld.x + ld.z * ld.z);
        p = rl > 1e-8f ? Vec3{ ld.x * b.shape.radius / rl, 0, ld.z * b.shape.radius / rl } : Vec3{};
        if (ld.y > 1e-7f) p.y = b.shape.halfHeight;
        else if (ld.y < -1e-7f) p.y = -b.shape.halfHeight;
        else p.y = 0; // side contact: use the middle of the cylinder axis
        break;
    }
    case ShapeKind::Cone: {
        float rl = sqrtf(ld.x * ld.x + ld.z * ld.z);
        Vec3 base = rl > 1e-8f ? Vec3{ ld.x * b.shape.radius / rl, -b.shape.halfHeight,
                                       ld.z * b.shape.radius / rl }
                                    : Vec3{ 0, -b.shape.halfHeight, 0 };
        Vec3 apex = { 0, b.shape.halfHeight, 0 };
        p = apex.dot(ld) > base.dot(ld) ? apex : base;
        break;
    }
    }
    return b.position + b.quat.rotate(p);
}

// SAT over analytic support mappings.  Curved shapes contribute a radial fan;
// boxes also contribute their edge cross-products.  This keeps the solver's
// single-contact interface while using the actual capsule/cylinder/cone hulls.
static int convexContact(const RigidBody& a, const RigidBody& b, Contact* out) {
    std::vector<Vec3> axes;
    auto addAxis = [&](Vec3 v) {
        float l2 = v.lengthSq();
        if (l2 > 1e-10f) axes.push_back(v * (1.0f / sqrtf(l2)));
    };
    Vec3 ax[3], bx[3];
    quatAxes(a.quat, ax[0], ax[1], ax[2]);
    quatAxes(b.quat, bx[0], bx[1], bx[2]);
    addAxis(b.position - a.position);
    for (int i = 0; i < 3; i++) { addAxis(ax[i]); addAxis(bx[i]); }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) addAxis(ax[i].cross(bx[j]));

    auto curvedAxes = [&](const RigidBody& s, const RigidBody& other) {
        if (s.shape.kind == ShapeKind::Capsule) {
            Vec3 y = s.quat.rotate({ 0, 1, 0 });
            addAxis(other.position - (s.position + y * s.shape.halfHeight));
            addAxis(other.position - (s.position - y * s.shape.halfHeight));
        }
        if (s.shape.kind == ShapeKind::Cylinder || s.shape.kind == ShapeKind::Cone) {
            for (int i = 0; i < 16; i++) {
                float t = 6.28318530718f * i / 16.0f;
                Vec3 radial = { cosf(t), 0, sinf(t) };
                addAxis(s.quat.rotate(radial));
                if (s.shape.kind == ShapeKind::Cone) {
                    float rise = s.shape.halfHeight > 1e-5f ? s.shape.radius / (2.0f * s.shape.halfHeight) : 1.0f;
                    addAxis(s.quat.rotate({ radial.x, rise, radial.z }));
                }
            }
        }
    };
    curvedAxes(a, b);
    curvedAxes(b, a);

    float minPen = 1e30f;
    Vec3 best = { 0, 1, 0 };
    for (Vec3 axis : axes) {
        float maxA = convexSupport(a, axis).dot(axis);
        float minA = convexSupport(a, -axis).dot(axis);
        float maxB = convexSupport(b, axis).dot(axis);
        float minB = convexSupport(b, -axis).dot(axis);
        float overlap = (maxA < maxB ? maxA : maxB) - (minA > minB ? minA : minB);
        if (overlap <= 0) return 0;
        if (overlap < minPen) { minPen = overlap; best = axis; }
    }
    if (best.dot(b.position - a.position) < 0) best = -best;
    Vec3 pa = convexSupport(a, best);
    Vec3 pb = convexSupport(b, -best);
    // A box support point is ambiguous along a face and may be a far corner;
    // use the curved primitive's unique support feature for the contact arm.
    out[0].normal = best;
    out[0].depth = minPen;
    if (a.shape.kind == ShapeKind::Box && b.shape.kind != ShapeKind::Box) {
        // The curved body's real support point supplies the lever arm.  Using
        // its center here would make tilted cylinders/cones land without
        // receiving the angular impulse generated by the lower rim.
        out[0].point = pb + best * (minPen * .5f);
    } else if (b.shape.kind == ShapeKind::Box && a.shape.kind != ShapeKind::Box) {
        out[0].point = pa - best * (minPen * .5f);
    } else {
        out[0].point = (pa + pb) * .5f;
    }
    return 1;
}

int collide(const RigidBody& a, const RigidBody& b, Contact* out) {
    if (a.shape.kind == ShapeKind::Box && b.shape.kind == ShapeKind::Capsule)
        return boxCapsule(a, b, out);
    if (a.shape.kind == ShapeKind::Capsule && b.shape.kind == ShapeKind::Box) {
        int n = boxCapsule(b, a, out);
        for (int i = 0; i < n; i++) out[i].normal = -out[i].normal;
        return n;
    }
    if (a.shape.kind == ShapeKind::Capsule && b.shape.kind == ShapeKind::Sphere)
        return capsuleSphere(a, b, out);
    if (a.shape.kind == ShapeKind::Sphere && b.shape.kind == ShapeKind::Capsule) {
        int n = capsuleSphere(b, a, out);
        for (int i = 0; i < n; i++) out[i].normal = -out[i].normal;
        return n;
    }
    if (a.shape.kind == ShapeKind::Capsule && b.shape.kind == ShapeKind::Capsule)
        return capsuleCapsule(a, b, out);
    bool special = a.shape.kind != ShapeKind::Sphere && a.shape.kind != ShapeKind::Box;
    special = special || (b.shape.kind != ShapeKind::Sphere && b.shape.kind != ShapeKind::Box);
    if (special) return convexContact(a, b, out);
    bool sa = a.shape.kind == ShapeKind::Sphere, sb = b.shape.kind == ShapeKind::Sphere;
    if (sa && sb) return sphereSphere(a, b, out);
    if (sa && !sb) {
        int n = boxSphere(b, a, out);
        for (int i = 0; i < n; i++) out[i].normal = -out[i].normal;
        return n;
    }
    if (!sa && sb) return boxSphere(a, b, out);
    return boxBox(a, b, out);
}

// ═══ raycast ═══
bool raycastBody(const RigidBody& b, const Vec3& origin, const Vec3& dir, float maxDist, RayHit& hit) {
    if (b.shape.kind == ShapeKind::Sphere) {
        Vec3 oc = origin - b.position;
        float bb = oc.dot(dir);
        float c = oc.lengthSq() - b.shape.radius * b.shape.radius;
        float disc = bb * bb - c;
        if (disc < 0) return false;
        float t = -bb - sqrtf(disc);
        if (t < 0 || t > maxDist) return false;
        hit.t = t;
        hit.point = origin + dir * t;
        hit.normal = (hit.point - b.position).normalized();
        return true;
    }
    Quat inv = b.quat.conjugate();
    Vec3 o = inv.rotate(origin - b.position);
    Vec3 dd = inv.rotate(dir);
    float h[3] = { b.shape.h.x, b.shape.h.y, b.shape.h.z };
    float O[3] = { o.x, o.y, o.z }, D[3] = { dd.x, dd.y, dd.z };
    float tmin = 0, tmax = maxDist;
    int nAxis = 0; float nSign = 1;
    for (int i = 0; i < 3; i++) {
        if (fabsf(D[i]) < 1e-9f) {
            if (O[i] < -h[i] || O[i] > h[i]) return false;
        } else {
            float t1 = (-h[i] - O[i]) / D[i];
            float t2 = (h[i] - O[i]) / D[i];
            float sign = -1;
            if (t1 > t2) { std::swap(t1, t2); sign = 1; }
            if (t1 > tmin) { tmin = t1; nAxis = i; nSign = sign; }
            tmax = tmax < t2 ? tmax : t2;
            if (tmin > tmax) return false;
        }
    }
    if (tmin <= 0 || tmin > maxDist) return false;
    Vec3 ln = {};
    (&ln.x)[nAxis] = nSign;
    hit.t = tmin;
    hit.normal = b.quat.rotate(ln);
    hit.point = origin + dir * tmin;
    return true;
}

// ═══ manifold ═══
void Manifold::update(const Contact* contacts, int count) {
    ContactPoint old[4];
    int numOld = numPoints;
    for (int i = 0; i < numOld; i++) old[i] = points[i];

    friction = sqrtf(a->friction * b->friction);
    restitution = a->restitution > b->restitution ? a->restitution : b->restitution;
    Quat invQ = a->quat.conjugate();

    numPoints = count < 4 ? count : 4;
    for (int i = 0; i < numPoints; i++) {
        ContactPoint& cp = points[i];
        cp = ContactPoint{};
        cp.rA = contacts[i].point - a->position;
        cp.rB = contacts[i].point - b->position;
        cp.normal = contacts[i].normal;
        cp.depth = contacts[i].depth;
        cp.localA = invQ.rotate(cp.rA);
        // warm start from nearest old point
        float bestD = 0.02f * 0.02f;
        int best = -1;
        for (int j = 0; j < numOld; j++) {
            float dsq = (old[j].localA - cp.localA).lengthSq();
            if (dsq < bestD) { bestD = dsq; best = j; }
        }
        if (best >= 0) {
            cp.impulseN = old[best].impulseN;
            cp.impulseT1 = old[best].impulseT1;
            cp.impulseT2 = old[best].impulseT2;
        }
    }
}

float Manifold::effectiveMass(const ContactPoint& cp, const Vec3& dir) const {
    float k = a->invMass + b->invMass;
    Vec3 t = a->mulInvInertia(cp.rA.cross(dir)).cross(cp.rA);
    k += t.dot(dir);
    t = b->mulInvInertia(cp.rB.cross(dir)).cross(cp.rB);
    k += t.dot(dir);
    return k > 1e-9f ? k : 1e-9f;
}

Vec3 Manifold::relVelocity(const ContactPoint& cp) const {
    return b->velocity + b->angularVelocity.cross(cp.rB)
         - a->velocity - a->angularVelocity.cross(cp.rA);
}

void Manifold::applyAt(const ContactPoint& cp, const Vec3& imp) {
    if (a->invMass > 0) {
        a->velocity -= imp * a->invMass;
        a->angularVelocity -= a->mulInvInertia(cp.rA.cross(imp));
    }
    if (b->invMass > 0) {
        b->velocity += imp * b->invMass;
        b->angularVelocity += b->mulInvInertia(cp.rB.cross(imp));
    }
}

void Manifold::prepare(float invDt) {
    for (int i = 0; i < numPoints; i++) {
        ContactPoint& cp = points[i];
        const Vec3& n = cp.normal;
        cp.tangent1 = (fabsf(n.x) > 0.7f ? Vec3{ -n.y, n.x, 0 } : Vec3{ 0, -n.z, n.y }).normalized();
        cp.tangent2 = n.cross(cp.tangent1);
        cp.normalMass = 1.0f / effectiveMass(cp, n);
        cp.tangentMass1 = 1.0f / effectiveMass(cp, cp.tangent1);
        cp.tangentMass2 = 1.0f / effectiveMass(cp, cp.tangent2);
        float vn = relVelocity(cp).dot(n);
        float bounce = vn < -RESTITUTION_THRESHOLD ? -restitution * vn : 0;
        float baum = BAUMGARTE * invDt * (cp.depth - SLOP > 0 ? cp.depth - SLOP : 0);
        cp.bias = bounce > baum ? bounce : baum;
    }
}

void Manifold::warmStart() {
    for (int i = 0; i < numPoints; i++) {
        ContactPoint& cp = points[i];
        applyAt(cp, cp.normal * cp.impulseN + cp.tangent1 * cp.impulseT1 + cp.tangent2 * cp.impulseT2);
    }
}

void Manifold::solve() {
    for (int i = 0; i < numPoints; i++) {
        ContactPoint& cp = points[i];
        float vn = relVelocity(cp).dot(cp.normal);
        float dL = -(vn - cp.bias) * cp.normalMass;
        float newImp = cp.impulseN + dL;
        if (newImp < 0) newImp = 0;
        dL = newImp - cp.impulseN;
        cp.impulseN = newImp;
        applyAt(cp, cp.normal * dL);

        float maxF = friction * cp.impulseN;
        Vec3 v2 = relVelocity(cp);

        float vt1 = v2.dot(cp.tangent1);
        float dT1 = -vt1 * cp.tangentMass1;
        float nT1 = clampf(cp.impulseT1 + dT1, -maxF, maxF);
        dT1 = nT1 - cp.impulseT1;
        cp.impulseT1 = nT1;

        float vt2 = v2.dot(cp.tangent2);
        float dT2 = -vt2 * cp.tangentMass2;
        float nT2 = clampf(cp.impulseT2 + dT2, -maxF, maxF);
        dT2 = nT2 - cp.impulseT2;
        cp.impulseT2 = nT2;

        applyAt(cp, cp.tangent1 * dT1 + cp.tangent2 * dT2);
    }
}

float Manifold::sumNormalImpulse() const {
    float s = 0;
    for (int i = 0; i < numPoints; i++) s += points[i].impulseN;
    return s;
}

// ═══ distance constraint ═══
DistanceConstraint::DistanceConstraint(RigidBody* pa, RigidBody* pb, float len, bool isRope)
    : a(pa), b(pb), rope(isRope) {
    Vec3 wa = a->quat.rotate(localAnchorA) + a->position;
    Vec3 wb = b->quat.rotate(localAnchorB) + b->position;
    length = len > 0 ? len : wa.distanceTo(wb);
}

void DistanceConstraint::applyImp(const Vec3& imp) {
    if (a->invMass > 0) {
        a->velocity -= imp * a->invMass;
        a->angularVelocity -= a->mulInvInertia(rA.cross(imp));
    }
    if (b->invMass > 0) {
        b->velocity += imp * b->invMass;
        b->angularVelocity += b->mulInvInertia(rB.cross(imp));
    }
}

void DistanceConstraint::prepare(float invDt) {
    Vec3 wa = a->quat.rotate(localAnchorA) + a->position;
    Vec3 wb = b->quat.rotate(localAnchorB) + b->position;
    rA = wa - a->position;
    rB = wb - b->position;
    dir = wb - wa;
    float dist = dir.length();
    float C = dist - length;
    active = !rope || C > 0;
    if (!active) return;
    dir = dist > 1e-9f ? dir * (1 / dist) : Vec3{ 0, 1, 0 };

    float k = a->invMass + b->invMass;
    k += a->mulInvInertia(rA.cross(dir)).cross(rA).dot(dir);
    k += b->mulInvInertia(rB.cross(dir)).cross(rB).dot(dir);
    effMass = k > 1e-9f ? 1 / k : 0;
    bias = 0.2f * invDt * C;

    impulse *= 0.9f;
    applyImp(dir * impulse);
}

void DistanceConstraint::solve() {
    if (!active || broken) return;
    Vec3 vRel = b->velocity + b->angularVelocity.cross(rB)
              - a->velocity - a->angularVelocity.cross(rA);
    float v = vRel.dot(dir);
    float dL = -(v + bias) * effMass;
    if (rope) {
        float newImp = impulse + dL;
        if (newImp > 0) newImp = 0;
        dL = newImp - impulse;
        impulse = newImp;
    } else {
        impulse += dL;
    }
    applyImp(dir * dL);
}

// ═══ world ═══
RigidBody* PhysicsWorld::addBody(const Shape& shape, BodyType type, float mass, const Vec3& pos,
                                 const Quat& q, float restitution, float friction) {
    auto b = std::make_unique<RigidBody>();
    b->id = nextId_++;
    b->type = type;
    b->shape = shape;
    b->position = pos;
    b->quat = q.normalized();
    b->restitution = restitution;
    b->friction = friction;
    b->setMass(type == BodyType::Static ? 0 : mass);
    bodies.push_back(std::move(b));
    return bodies.back().get();
}

void PhysicsWorld::removeBody(RigidBody* target) {
    for (auto it = manifolds_.begin(); it != manifolds_.end();) {
        if (it->second.a == target || it->second.b == target) it = manifolds_.erase(it);
        else ++it;
    }
    constraints.erase(std::remove_if(constraints.begin(), constraints.end(),
        [&](const DistanceConstraint& c) { return c.a == target || c.b == target; }), constraints.end());
    bodies.erase(std::remove_if(bodies.begin(), bodies.end(),
        [&](const std::unique_ptr<RigidBody>& b) { return b.get() == target; }), bodies.end());
}

void PhysicsWorld::clear() {
    bodies.clear();
    constraints.clear();
    manifolds_.clear();
    contactEvents.clear();
    overlapEvents.clear();
    time = 0;
}

void PhysicsWorld::step(float dt) {
    if (dt <= 0) return;
    time += dt;
    float invDt = 1.0f / dt;
    contactEvents.clear();
    overlapEvents.clear();

    // 1. integrate velocities
    for (auto& bp : bodies) {
        RigidBody& b = *bp;
        if (b.type != BodyType::Dynamic || b.queryOnly || b.sleeping || !b.enabled) continue;
        if (b.useGravity) b.velocity += gravity * dt;
        b.velocity += b.force * (b.invMass * dt);
        b.angularVelocity += b.mulInvInertia(b.torque) * dt;
        float ld = 1 - b.linearDamping * dt; if (ld < 0) ld = 0;
        float ad = 1 - b.angularDamping * dt; if (ad < 0) ad = 0;
        b.velocity *= ld;
        b.angularVelocity *= ad;
        b.force = {};
        b.torque = {};
    }

    // 2. broadphase: sweep and prune on X
    for (auto& b : bodies) b->updateAABB();
    std::vector<RigidBody*> sorted;
    sorted.reserve(bodies.size());
    for (auto& b : bodies) sorted.push_back(b.get());
    std::sort(sorted.begin(), sorted.end(),
              [](RigidBody* a, RigidBody* b) { return a->aabb.min.x < b->aabb.min.x; });

    std::vector<std::pair<RigidBody*, RigidBody*>> pairs;
    for (size_t i = 0; i < sorted.size(); i++) {
        RigidBody* a = sorted[i];
        for (size_t j = i + 1; j < sorted.size(); j++) {
            RigidBody* b = sorted[j];
            if (b->aabb.min.x > a->aabb.max.x) break;
            if (!a->enabled || !b->enabled) continue;
            if (!layersCollide(a->layer, b->layer)) continue;   // collision layer matrix
            bool overlapPair = a->trigger || b->trigger;
            if (a->type != BodyType::Dynamic && b->type != BodyType::Dynamic && !overlapPair) continue;
            if (!overlapPair) {
                if (a->sleeping && b->sleeping) continue;
                if (a->sleeping && b->type != BodyType::Dynamic) continue;
                if (b->sleeping && a->type != BodyType::Dynamic) continue;
            }
            if (a->aabb.min.y > b->aabb.max.y || a->aabb.max.y < b->aabb.min.y) continue;
            if (a->aabb.min.z > b->aabb.max.z || a->aabb.max.z < b->aabb.min.z) continue;
            pairs.emplace_back(a->id < b->id ? a : b, a->id < b->id ? b : a);
        }
    }

    // 3. narrowphase + manifold cache
    contactCount = 0;
    std::vector<uint64_t> activeKeys;
    std::vector<uint64_t> newTouchKeys;
    Contact contacts[8];
    for (auto& [a, b] : pairs) {
        uint64_t key = (uint64_t)a->id * 100000ull + b->id;
        int n = collide(*a, *b, contacts);
        if (n > 0) {
            activeKeys.push_back(key);
            auto it = manifolds_.find(key);
            bool isNew = it == manifolds_.end();
            if (isNew) {
                Manifold m;
                m.a = a; m.b = b;
                it = manifolds_.emplace(key, m).first;
            }
            it->second.update(contacts, n);
            if (isNew) newTouchKeys.push_back(key);
            contactCount += it->second.numPoints;
            if (a->sleeping != b->sleeping) {
                RigidBody* awake = a->sleeping ? b : a;
                RigidBody* asleep = a->sleeping ? a : b;
                if (awake->velocity.lengthSq() > SLEEP_LIN_SQ * 4 ||
                    awake->angularVelocity.lengthSq() > SLEEP_ANG_SQ * 4) {
                    asleep->wake();
                }
            }
        }
    }
    // Drop stale manifolds and emit End Overlap before their pair disappears.
    for (auto it = manifolds_.begin(); it != manifolds_.end();) {
        if (std::find(activeKeys.begin(), activeKeys.end(), it->first) == activeKeys.end()) {
            if (it->second.a->trigger || it->second.b->trigger)
                overlapEvents.push_back({ it->second.a, it->second.b, false });
            it = manifolds_.erase(it);
        } else ++it;
    }

    // 4. solve
    std::vector<Manifold*> solveList;
    for (auto& kv : manifolds_) {
        Manifold& mf = kv.second;
        if (mf.a->trigger || mf.b->trigger) continue;   // overlap only: no response
        if (mf.a->sleeping && mf.b->sleeping) continue;
        mf.prepare(invDt);
        mf.warmStart();
        solveList.push_back(&mf);
    }
    for (auto& c : constraints) {
        if (!c.a->enabled || !c.b->enabled) { c.active = false; continue; }
        if (!c.a->sleeping || !c.b->sleeping) { c.a->wake(); c.b->wake(); }
        c.prepare(invDt);
    }
    for (int it = 0; it < VEL_ITERATIONS; it++) {
        for (auto& c : constraints) c.solve();
        for (auto* mf : solveList) mf->solve();
    }
    // joint breaking
    for (auto& c : constraints) {
        if (c.breakImpulse > 0 && !c.broken && fabsf(c.impulse) > c.breakImpulse) {
            c.broken = true;
            c.active = false;
        }
    }

    // 5. integrate positions
    for (auto& bp : bodies) {
        RigidBody& b = *bp;
        if (b.type != BodyType::Dynamic || b.queryOnly || b.sleeping || !b.enabled) continue;
        b.position += b.velocity * dt;
        b.quat = b.quat.integrated(b.angularVelocity, dt);
        b.updateInertiaWorld();
    }

    // 6. sleeping
    for (auto& bp : bodies) {
        RigidBody& b = *bp;
        if (b.type != BodyType::Dynamic || b.queryOnly || b.sleeping || !b.canSleep) continue;
        if (b.velocity.lengthSq() < SLEEP_LIN_SQ && b.angularVelocity.lengthSq() < SLEEP_ANG_SQ) {
            b.sleepTimer += dt;
            if (b.sleepTimer > SLEEP_TIME) b.sleep();
        } else {
            b.sleepTimer = 0;
        }
    }

    // 7. phase events: solid pairs produce Hit; trigger/query pairs produce Begin Overlap.
    for (uint64_t key : newTouchKeys) {
        auto found = manifolds_.find(key);
        if (found == manifolds_.end()) continue;
        Manifold* mf = &found->second;
        if (mf->a->trigger || mf->b->trigger) overlapEvents.push_back({ mf->a, mf->b, true });
        else contactEvents.push_back({ mf->a, mf->b, mf->sumNormalImpulse() });
    }
}

bool PhysicsWorld::raycast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& hit) {
    bool found = false;
    RayHit h;
    for (auto& b : bodies) {
        if (raycastBody(*b, origin, dir, maxDist, h) && (!found || h.t < hit.t)) {
            hit = h;
            hit.body = b.get();
            found = true;
        }
    }
    return found;
}
