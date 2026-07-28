// ─── Pulse Engine native physics: rigid bodies, collisions, sequential impulses ───
#pragma once
#include "math.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdint>

enum class ShapeKind { Sphere, Box, Capsule, Cylinder, Cone };
enum class BodyType { Dynamic, Static };

struct Shape {
    ShapeKind kind = ShapeKind::Box;
    float radius = 0.5f;          // sphere
    float halfHeight = 0.5f;      // capsule segment half-length / cylinder-cone half-height
    Vec3 h = { 0.5f, 0.5f, 0.5f }; // box half extents
    static Shape sphere(float r) { Shape s; s.kind = ShapeKind::Sphere; s.radius = r; return s; }
    static Shape box(float hx, float hy, float hz) { Shape s; s.kind = ShapeKind::Box; s.h = { hx, hy, hz }; return s; }
    static Shape capsule(float r, float segmentHalf) {
        Shape s; s.kind = ShapeKind::Capsule; s.radius = r; s.halfHeight = segmentHalf;
        s.h = { r, segmentHalf + r, r }; return s;
    }
    static Shape cylinder(float r, float hh) {
        Shape s; s.kind = ShapeKind::Cylinder; s.radius = r; s.halfHeight = hh; s.h = { r, hh, r }; return s;
    }
    static Shape cone(float r, float hh) {
        Shape s; s.kind = ShapeKind::Cone; s.radius = r; s.halfHeight = hh; s.h = { r, hh, r }; return s;
    }
};

struct AABB { Vec3 min, max; };

struct RigidBody {
    int id = 0;
    int userIndex = -1;           // index into app entity list
    int layer = 0;                // collision layer (filtered by the world matrix)
    BodyType type = BodyType::Dynamic;
    Shape shape;

    Vec3 position;
    Quat quat;
    Vec3 velocity;
    Vec3 angularVelocity;
    Vec3 force;
    Vec3 torque;

    float mass = 1, invMass = 1;
    Vec3 invInertiaLocal;
    float invInertiaWorld[9] = {}; // 3x3 row-major

    float restitution = 0.3f;
    float friction = 0.5f;
    float linearDamping = 0.01f;
    float angularDamping = 0.05f;

    bool sleeping = false;
    bool canSleep = true;
    bool enabled = true;    // false = excluded from simulation (still raycastable for picking)
    bool trigger = false;   // overlap volume: fires contact events, no collision response
    bool queryOnly = false; // Mesh Renderer collider without RigidBody: overlap only, never integrated
    bool useGravity = true; // false = the world gravity is not applied to this body
    float sleepTimer = 0;

    AABB aabb;

    void setMass(float m);
    void updateInertiaWorld();
    Vec3 mulInvInertia(const Vec3& v) const;
    void updateAABB();
    void applyForce(const Vec3& f);
    void applyForceAt(const Vec3& f, const Vec3& worldPoint);
    void applyTorque(const Vec3& t);
    void applyImpulse(const Vec3& imp);
    void applyImpulseAt(const Vec3& imp, const Vec3& worldPoint);
    void wake() { sleeping = false; sleepTimer = 0; }
    void sleep() { sleeping = true; velocity = {}; angularVelocity = {}; }
};

struct Contact {
    Vec3 point;
    Vec3 normal; // from A to B
    float depth = 0;
};

// narrowphase entry: fills contacts (max 4), returns count
int collide(const RigidBody& a, const RigidBody& b, Contact* out);

struct RayHit {
    RigidBody* body = nullptr;
    Vec3 point, normal;
    float t = 0;
};
bool raycastBody(const RigidBody& b, const Vec3& origin, const Vec3& dir, float maxDist, RayHit& hit);

// ─── contact manifold with warm starting ───
struct ContactPoint {
    Vec3 rA, rB, normal, tangent1, tangent2;
    Vec3 localA;
    float normalMass = 0, tangentMass1 = 0, tangentMass2 = 0;
    float bias = 0, depth = 0;
    float impulseN = 0, impulseT1 = 0, impulseT2 = 0;
};

struct Manifold {
    RigidBody* a = nullptr;
    RigidBody* b = nullptr;
    ContactPoint points[4];
    int numPoints = 0;
    float friction = 0.5f, restitution = 0.3f;
    bool fresh = true;

    void update(const Contact* contacts, int count);
    void prepare(float invDt);
    void warmStart();
    void solve();
    float sumNormalImpulse() const;

private:
    float effectiveMass(const ContactPoint& cp, const Vec3& dir) const;
    Vec3 relVelocity(const ContactPoint& cp) const;
    void applyAt(const ContactPoint& cp, const Vec3& imp);
};

// ─── distance constraint (rod or rope) ───
struct DistanceConstraint {
    RigidBody* a = nullptr;
    RigidBody* b = nullptr;
    Vec3 localAnchorA, localAnchorB;
    float length = 1;
    bool rope = false;
    float breakImpulse = 0;   // 0 = unbreakable
    bool broken = false;
    int userIndex = -1;       // index into scene joints

    Vec3 rA, rB, dir;
    float effMass = 0, bias = 0, impulse = 0;
    bool active = true;

    DistanceConstraint(RigidBody* a, RigidBody* b, float len = -1, bool rope = false);
    void prepare(float invDt);
    void solve();
private:
    void applyImp(const Vec3& imp);
};

// ─── world ───
struct ContactEvent { RigidBody* a; RigidBody* b; float impulse; };
struct OverlapEvent { RigidBody* a; RigidBody* b; bool begin; };

class PhysicsWorld {
public:
    static const int MAX_LAYERS = 16;
    Vec3 gravity = { 0, -9.81f, 0 };
    bool layerMatrix[MAX_LAYERS][MAX_LAYERS];   // which layers collide (default all true)
    PhysicsWorld() { for (int i = 0; i < MAX_LAYERS; i++) for (int j = 0; j < MAX_LAYERS; j++) layerMatrix[i][j] = true; }
    bool layersCollide(int a, int b) const {
        if (a < 0 || a >= MAX_LAYERS || b < 0 || b >= MAX_LAYERS) return true;
        return layerMatrix[a][b];
    }
    std::vector<std::unique_ptr<RigidBody>> bodies;
    std::vector<DistanceConstraint> constraints;
    std::vector<ContactEvent> contactEvents; // fresh touches from last step
    std::vector<OverlapEvent> overlapEvents; // begin/end phases for trigger/query pairs
    int contactCount = 0;
    double time = 0;

    RigidBody* addBody(const Shape& shape, BodyType type, float mass, const Vec3& pos,
                       const Quat& q = {}, float restitution = 0.3f, float friction = 0.5f);
    void removeBody(RigidBody* b);
    void clear();
    void step(float dt);
    bool raycast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& hit);

    // iterate active manifolds (for contact-point debug rendering)
    template <typename F>
    void eachManifold(F&& f) const { for (auto& kv : manifolds_) f(kv.second); }

private:
    std::unordered_map<uint64_t, Manifold> manifolds_;
    int nextId_ = 1;
};
