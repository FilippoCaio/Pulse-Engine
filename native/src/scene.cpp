// ─── Pulse Engine editor scene implementation ───
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "scene.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <iomanip>

const char* BEHAVIOR_NAMES[BH_COUNT] = {
    "None", "Jump with SPACE", "Continuous rotation", "Initial push",
};

static void normalizeDetailOrder(Entity& e) {
    std::vector<int> normalized;
    for (int id : e.detailOrder) {
        if (id < DETAIL_MESH || id >= DETAIL_COMPONENT_COUNT) continue;
        if (std::find(normalized.begin(), normalized.end(), id) == normalized.end()) normalized.push_back(id);
    }
    for (int id = DETAIL_MESH; id < DETAIL_COMPONENT_COUNT; id++)
        if (std::find(normalized.begin(), normalized.end(), id) == normalized.end()) normalized.push_back(id);
    e.detailOrder = std::move(normalized);
}

static void serializeAdditionalBlueprints(std::ostringstream& out, const Entity& e) {
    for (size_t index = 0; index < e.additionalBlueprints.size(); index++) {
        const BlueprintComponentDef& component = e.additionalBlueprints[index];
        if (component.graphPath.empty()) continue;
        out << "bpcomponent " << index << " " << (component.collapsed ? 1 : 0)
            << " " << std::quoted(component.graphPath) << "\n";
        for (const auto& [name, value] : component.varOverrides)
            out << "bpcomponentvar " << index << " " << std::quoted(name) << " "
                << value.x << " " << value.y << " " << value.z << "\n";
        for (const auto& [name, alpha] : component.varAlphaOverrides)
            out << "bpcomponentalpha " << index << " " << std::quoted(name) << " " << alpha << "\n";
    }
}

Entity* EditorScene::byId(int id) {
    for (auto& e : entities) if (e.id == id) return &e;
    return nullptr;
}

Entity* EditorScene::byBody(const RigidBody* b) {
    for (auto& e : entities) if (e.body == b) return &e;
    return nullptr;
}

Entity& EditorScene::spawnBox(const char* name, Vec3 pos, Vec3 size, Vec3 color,
                              BodyType type, float mass, float rest, float fric, Quat q) {
    Entity e;
    e.id = nextEntityId++;
    snprintf(e.name, sizeof(e.name), "%s", name);
    e.body = world.addBody(Shape::box(size.x / 2, size.y / 2, size.z / 2), type, mass, pos, q, rest, fric);
    e.mesh = MESH_CUBE;
    e.scale = size;
    e.color = color;
    entities.push_back(e);
    return entities.back();
}

Entity& EditorScene::spawnEmpty(const char* name, Vec3 pos) {
    Entity e;
    e.id = nextEntityId++;
    snprintf(e.name, sizeof(e.name), "%s", name);
    // A small disabled body is only the transform/picking backend. It is not a
    // renderer or collider until the user adds those components explicitly.
    e.body = world.addBody(Shape::box(.1f, .1f, .1f), BodyType::Static, 0, pos);
    e.body->enabled = false;
    e.hasMesh = false;
    e.hasPhysics = false;
    e.scale = { 1, 1, 1 };
    entities.push_back(e);
    return entities.back();
}

Entity& EditorScene::spawnSphere(const char* name, Vec3 pos, float diam, Vec3 color,
                                 float mass, float rest, float fric) {
    Entity e;
    e.id = nextEntityId++;
    snprintf(e.name, sizeof(e.name), "%s", name);
    e.body = world.addBody(Shape::sphere(diam / 2), BodyType::Dynamic, mass, pos, {}, rest, fric);
    e.mesh = MESH_SPHERE;
    e.scale = { diam, diam, diam };
    e.color = color;
    e.shininess = 90;
    e.specular = 0.6f;
    entities.push_back(e);
    return entities.back();
}

Entity& EditorScene::spawnLight(const char* name, Vec3 pos) {
    Entity e;
    e.id = nextEntityId++;
    snprintf(e.name, sizeof(e.name), "%s", name);
    e.body = world.addBody(Shape::sphere(0.14f), BodyType::Static, 0, pos, {}, 0.5f, 0.5f);
    e.mesh = MESH_SPHERE;
    e.scale = { 0.28f, 0.28f, 0.28f };
    e.color = { 1, 0.9f, 0.6f };
    e.emissive = 3;
    e.isLight = true;
    entities.push_back(e);
    return entities.back();
}

void EditorScene::collectSubtree(int id, std::vector<int>& out) const {
    out.push_back(id);
    for (const auto& e : entities) {
        if (e.parentId == id) collectSubtree(e.id, out);
    }
}

bool EditorScene::isDescendant(int maybeChild, int ancestor) const {
    const Entity* e = nullptr;
    for (const auto& x : entities) if (x.id == maybeChild) e = &x;
    int guard = 0;
    while (e && e->parentId != 0 && guard++ < 1000) {
        if (e->parentId == ancestor) return true;
        int pid = e->parentId;
        e = nullptr;
        for (const auto& x : entities) if (x.id == pid) e = &x;
    }
    return false;
}

bool EditorScene::setParent(int childId, int newParent) {
    Entity* c = byId(childId);
    if (!c || childId == newParent) return false;
    if (newParent != 0 && (!byId(newParent) || isDescendant(newParent, childId))) return false;
    c->parentId = newParent;
    return true;
}

void EditorScene::moveDescendants(int rootId, const Vec3& delta) {
    std::vector<int> sub;
    collectSubtree(rootId, sub);
    for (size_t i = 1; i < sub.size(); i++) { // skip root
        Entity* e = byId(sub[i]);
        if (!e) continue;
        e->body->position += delta;
        e->body->velocity = {};
        e->body->angularVelocity = {};
        e->body->updateAABB();
    }
}

void EditorScene::rotateDescendants(int rootId, const Vec3& pivot, const Quat& oldRotation,
                                    const Quat& newRotation) {
    Quat delta = (newRotation * oldRotation.conjugate()).normalized();
    std::vector<int> sub;
    collectSubtree(rootId, sub);
    for (size_t i = 1; i < sub.size(); i++) { // skip root; apply the world delta once to every descendant
        Entity* e = byId(sub[i]);
        if (!e || !e->body) continue;
        e->body->position = pivot + delta.rotate(e->body->position - pivot);
        e->body->quat = (delta * e->body->quat).normalized();
        e->body->velocity = {};
        e->body->angularVelocity = {};
        e->body->updateInertiaWorld();
        e->body->updateAABB();
        e->body->wake();
    }
}

static float safeScaleRatio(float next, float previous) {
    return fabsf(previous) > 0.000001f ? next / previous : 1.0f;
}

void EditorScene::scaleDescendants(int rootId, const Vec3& pivot, const Quat& rootRotation,
                                   const Vec3& oldScale, const Vec3& newScale) {
    Vec3 ratio = { safeScaleRatio(newScale.x, oldScale.x),
                   safeScaleRatio(newScale.y, oldScale.y),
                   safeScaleRatio(newScale.z, oldScale.z) };
    std::vector<int> sub;
    collectSubtree(rootId, sub);
    Quat inverseRoot = rootRotation.conjugate();
    for (size_t i = 1; i < sub.size(); i++) {
        Entity* e = byId(sub[i]);
        if (!e || !e->body) continue;
        Vec3 local = inverseRoot.rotate(e->body->position - pivot);
        local = { local.x * ratio.x, local.y * ratio.y, local.z * ratio.z };
        e->body->position = pivot + rootRotation.rotate(local);
        e->scale = { e->scale.x * ratio.x, e->scale.y * ratio.y, e->scale.z * ratio.z };
        e->body->velocity = {};
        e->body->angularVelocity = {};
        syncBodyShape(*e);
    }
}

std::vector<int> EditorScene::duplicateSubtree(int rootId) {
    std::vector<int> sub;
    collectSubtree(rootId, sub);
    std::string text = serializeSubset(sub);
    return instantiateFrom(text, { 1, 0.2f, 0 }, false);
}

void EditorScene::addJoint(int entA, int entB, float len, bool rope) {
    Entity* a = byId(entA);
    Entity* b = byId(entB);
    if (!a || !b) return;
    if (len <= 0) len = a->body->position.distanceTo(b->body->position);
    joints.push_back({ entA, entB, len, rope });
    rebuildConstraints();
}

void EditorScene::removeEntity(int id) {
    std::vector<int> sub;
    collectSubtree(id, sub);
    for (int sid : sub) {
        Entity* e = byId(sid);
        if (!e) continue;
        world.removeBody(e->body);
        joints.erase(std::remove_if(joints.begin(), joints.end(),
            [&](const JointDef& j) { return j.entA == sid || j.entB == sid; }), joints.end());
        entities.erase(std::remove_if(entities.begin(), entities.end(),
            [&](const Entity& x) { return x.id == sid; }), entities.end());
    }
    rebuildConstraints();
}

void EditorScene::clear() {
    world.clear();
    entities.clear();
    joints.clear();
    layers = CollisionLayers{};
    nextEntityId = 1;
    gameModePath.clear();
    hudWidget.clear();
    world.gravity = { 0, gravityY, 0 };
    applyLayersToWorld();
}

void EditorScene::rebuildConstraints() {
    world.constraints.clear();
    for (int i = 0; i < (int)joints.size(); i++) {
        const JointDef& j = joints[i];
        Entity* a = byId(j.entA);
        Entity* b = byId(j.entB);
        if (a && b) {
            world.constraints.emplace_back(a->body, b->body, j.len, j.rope);
            world.constraints.back().breakImpulse = j.breakImp;
            world.constraints.back().userIndex = i;
        }
    }
    // Physics Constraint components: connect their two referenced objects with a rigid
    // link (distance held at the current separation). Per-axis limit/motor settings are
    // authored on the entity; the connection itself is what the solver enforces here.
    for (auto& e : entities) {
        if (!e.hasConstraint) continue;
        Entity* a = byId(e.constraintObjA);
        Entity* b = byId(e.constraintObjB);
        if (a && b && a != b && a->body && b->body) {
            world.constraints.emplace_back(a->body, b->body, -1.0f, false);
            world.constraints.back().breakImpulse = e.conBreak;
            world.constraints.back().userIndex = -1;
        }
    }
}

// Each rendered primitive owns the corresponding physical collider.
Shape shapeForEntity(MeshType mesh, const Vec3& scale) {
    auto cl = [](float v) { return v < 0.02f ? 0.02f : v; };
    if (mesh == MESH_SPHERE) return Shape::sphere(cl(scale.x * 0.5f));
    float radius = cl((scale.x > scale.z ? scale.x : scale.z) * 0.5f);
    if (mesh == MESH_CAPSULE) {
        float segmentHalf = scale.y - radius;
        if (segmentHalf < 0) segmentHalf = 0;
        return Shape::capsule(radius, segmentHalf);
    }
    if (mesh == MESH_CYLINDER) return Shape::cylinder(radius, cl(scale.y * 0.5f));
    if (mesh == MESH_CONE) return Shape::cone(radius, cl(scale.y * 0.5f));
    return Shape::box(cl(scale.x * 0.5f), cl(scale.y * 0.5f), cl(scale.z * 0.5f));
}

CollisionLayers::CollisionLayers() {
    static const char* DEF[6] = { "Default", "Player", "Enemies", "Ground", "Trigger", "Projectiles" };
    count = 6;
    for (int i = 0; i < MAX; i++) snprintf(names[i], sizeof(names[i]), "%s", i < 6 ? DEF[i] : "");
    for (int i = 0; i < MAX; i++)
        for (int j = 0; j < MAX; j++) matrix[i][j] = true;
}

void EditorScene::applyLayersToWorld() {
    for (int i = 0; i < CollisionLayers::MAX; i++)
        for (int j = 0; j < CollisionLayers::MAX; j++)
            world.layerMatrix[i][j] = layers.matrix[i][j];
}

void EditorScene::syncBodyShape(Entity& e) {
    RigidBody* b = e.body;
    // Collision owns no geometry. A Trigger component provides a primitive
    // query shape; otherwise the Mesh Renderer provides its exact primitive.
    MeshType colliderMesh = e.hasTrigger
                          ? (e.triggerShape == 1 ? MESH_SPHERE : e.triggerShape == 2 ? MESH_CAPSULE : MESH_CUBE)
                          : e.mesh;
    b->shape = shapeForEntity(colliderMesh, e.scale);
    bool hasGeometry = e.hasTrigger || e.hasMesh;
    b->queryOnly = e.hasTrigger || (e.hasMesh && !e.hasPhysics);
    b->enabled = hasGeometry;
    b->trigger = b->queryOnly;
    b->layer = e.layer;
    b->setMass(b->type == BodyType::Static ? 0 : (b->mass > 0 ? b->mass : 1));
    b->updateAABB();
    b->wake();
}

// ═══ serialization (line-based text format, .imp) ═══
std::string EditorScene::serialize() const {
    std::ostringstream o;
    o << "IMPULSO 2\n";
    o << "gravity " << gravityY << "\n";
    o << "nextid " << nextEntityId << "\n";
    o << "gamemode " << (gameModePath.empty() ? "-" : gameModePath) << "\n";
    o << "hudwidget " << (hudWidget.empty() ? "-" : hudWidget) << "\n";
    o << "layers " << layers.count << "\n";
    for (int i = 0; i < layers.count; i++)
        o << "lname " << i << " " << (layers.names[i][0] ? layers.names[i] : "-") << "\n";
    for (int i = 0; i < layers.count; i++) {
        o << "lrow " << i;
        for (int j = 0; j < layers.count; j++) o << " " << (layers.matrix[i][j] ? 1 : 0);
        o << "\n";
    }
    for (const auto& e : entities) {
        const RigidBody* b = e.body;
        o << "entity " << e.id << "\n";
        o << "parent " << e.parentId << "\n";
        o << "name " << e.name << "\n";
        o << "mesh " << (int)e.mesh << "\n";
        o << "meshasset " << (e.meshAsset[0] ? e.meshAsset : "-") << "\n";
        o << "scale " << e.scale.x << " " << e.scale.y << " " << e.scale.z << "\n";
        o << "color " << e.color.x << " " << e.color.y << " " << e.color.z << " " << e.colorAlpha << "\n";
        o << "mat " << e.shininess << " " << e.specular << " " << e.checker << " " << e.emissive << "\n";
        o << "doublesided " << (e.doubleSided ? 1 : 0) << "\n";
        o << "materialasset " << (e.materialAsset[0] ? e.materialAsset : "-") << "\n";
        o << "body " << (b->type == BodyType::Static ? 1 : 0) << " " << (b->mass > 0 ? b->mass : 1)
          << " " << b->restitution << " " << b->friction
          << " " << b->linearDamping << " " << b->angularDamping
          << " " << (b->useGravity ? 1 : 0) << "\n";
        o << "pos " << b->position.x << " " << b->position.y << " " << b->position.z << "\n";
        o << "rot " << b->quat.x << " " << b->quat.y << " " << b->quat.z << " " << b->quat.w << "\n";
        o << "light " << (e.isLight ? 1 : 0) << " " << e.lightColor.x << " " << e.lightColor.y << " "
          << e.lightColor.z << " " << e.lightIntensity << " " << e.lightRange << "\n";
        if (e.isCamera) o << "camera 1 " << e.camFov << " " << e.camOffsetY << " "
                          << e.camNearClip << " " << e.camClipDistance << " "
                          << (e.camLinearClipping ? 1 : 0) << " " << e.camLayerMask << " "
                          << (e.camPostProcess ? 1 : 0) << "\n";
        o << "audio " << (e.hasAudio ? 1 : 0) << " " << e.audioVolume << " "
          << (e.audioLoop ? 1 : 0) << " " << (e.audioPlayOnAwake ? 1 : 0) << " "
          << (e.audioSpatial ? 1 : 0) << " " << e.audioMinDistance << " " << e.audioMaxDistance
          << " " << (e.audioClip[0] ? e.audioClip : "-") << "\n";
        o << "audio_class " << (e.audioClass[0] ? e.audioClass : "-") << "\n";
        o << "audio_atten " << (e.audioAttenuation[0] ? e.audioAttenuation : "-") << "\n";
        o << "audio_concurrency " << (e.audioConcurrency[0] ? e.audioConcurrency : "-") << "\n";
        o << "reverb " << (e.hasReverb ? 1 : 0) << " " << e.reverbRadius << " " << e.reverbWet << " " << e.reverbDecay << "\n";
        if (e.hasPostProcess) {
            const PostSettings& pp = e.ppSettings;
            o << "postprocess 1 " << (e.ppUnbound ? 1 : 0) << " " << e.ppPriority << " " << e.ppBlendWeight << "\n";
            o << "pp_grade " << pp.exposure << " " << pp.contrast << " " << pp.saturation << " "
              << pp.tint.x << " " << pp.tint.y << " " << pp.tint.z << " " << pp.vignette << "\n";
            o << "pp_fx " << pp.bloomThreshold << " " << pp.bloomIntensity << " "
              << pp.chromaticAberration << " " << pp.grain << "\n";
        }
        o << "aiagent " << (e.hasAIAgent ? 1 : 0) << " " << e.aiSpeed << " " << e.aiAcceleration << " "
          << e.aiAngularSpeed << " " << e.aiStoppingDistance << " " << e.aiBaseOffset << " "
          << (e.aiDebugDraw ? 1 : 0) << "\n";
        o << "animator " << (e.hasAnimator ? 1 : 0) << " " << (e.animatorPlayOnAwake ? 1 : 0) << " "
          << e.animatorSpeed << " " << (e.animatorController[0] ? e.animatorController : "-") << "\n";
        o << "navoccluder " << (e.hasNavigationOccluder ? 1 : 0) << " " << e.navigationOccluderPadding << "\n";
        if (e.hasConstraint) {
            o << "constraint " << e.constraintObjA << " " << e.constraintObjB << " " << e.conBreak << "\n";
            o << "conlin " << e.conLinMode[0] << " " << e.conLinMode[1] << " " << e.conLinMode[2]
              << " " << e.conLinLimit[0] << " " << e.conLinLimit[1] << " " << e.conLinLimit[2] << "\n";
            o << "conang " << e.conAngMode[0] << " " << e.conAngMode[1] << " " << e.conAngMode[2]
              << " " << e.conAngLimit[0] << " " << e.conAngLimit[1] << " " << e.conAngLimit[2] << "\n";
            o << "conmotor " << (e.conLinMotor ? 1 : 0) << " " << e.conLinMotorTarget.x << " " << e.conLinMotorTarget.y
              << " " << e.conLinMotorTarget.z << " " << e.conLinMotorForce
              << " " << (e.conAngMotor ? 1 : 0) << " " << e.conAngMotorTarget.x << " " << e.conAngMotorTarget.y
              << " " << e.conAngMotorTarget.z << " " << e.conAngMotorForce << "\n";
        }
        o << "inspectorevents " << (e.hasInspectorEvents ? 1 : 0) << "\n";
        for (const InspectorEventDef& event : e.inspectorEvents) {
            o << "inspectorevent " << std::quoted(event.name) << "\n";
            for (const InspectorEventListener& listener : event.listeners) {
                o << "inspectorlistener " << listener.targetEntity << " " << (listener.customEvent ? 1 : 0)
                  << " " << std::quoted(listener.callable) << "\n";
                for (const InspectorEventArgument& argument : listener.arguments)
                    o << "inspectorarg " << argument.kind << " " << argument.value.x << " " << argument.value.y
                      << " " << argument.value.z << " " << argument.alpha << " " << argument.objectId
                      << " " << std::quoted(argument.text) << "\n";
            }
        }
        o << "prefabref " << (e.prefabAsset[0] ? e.prefabAsset : "-") << " " << e.prefabInstanceRoot << "\n";
        o << "staticflags " << e.staticFlags << "\n";
        for (const std::string& tag : e.tags) o << "tag " << tag << "\n";
        o << "behavior " << e.behavior << " " << e.bp[0] << " " << e.bp[1] << " " << e.bp[2] << "\n";
        o << "comps " << (e.hasMesh ? 1 : 0) << " " << (e.hasPhysics ? 1 : 0) << " " << e.collision << "\n";
        o << "triggercomp " << (e.hasTrigger ? 1 : 0) << "\n";
        o << "triggershape " << e.triggerShape << "\n";
        o << "elayer " << e.layer << "\n";
        o << "graph " << (e.graphPath[0] ? e.graphPath : "-") << "\n";
        o << "dcollapse " << e.detailCollapsed << "\n";
        o << "dorder";
        for (int component : e.detailOrder) o << " " << component;
        o << "\n";
        for (const auto& [k, v] : e.varOverrides) {
            o << "varov " << k << " " << v.x << " " << v.y << " " << v.z << "\n";
        }
        for(const auto&[k,a]:e.varAlphaOverrides)o<<"varova "<<k<<" "<<a<<"\n";
        serializeAdditionalBlueprints(o, e);
        o << "end\n";
    }
    for (const auto& j : joints) {
        o << "joint " << j.entA << " " << j.entB << " " << j.len << " " << (j.rope ? 1 : 0) << " " << j.breakImp << "\n";
    }
    return o.str();
}

bool EditorScene::deserialize(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("IMPULSO", 0) != 0) return false;

    clear();
    Entity cur;
    bool inEntity = false;
    Vec3 pos;
    Quat rot;
    int btype = 0, grav = 1;
    float mass = 1, rest = 0.25f, fric = 0.5f, ldamp = 0.01f, adamp = 0.05f;
    bool sawTriggerComponent = false;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.rfind("gravity ", 0) == 0) {
            sscanf(line.c_str(), "gravity %f", &gravityY);
            world.gravity = { 0, gravityY, 0 };
        } else if (line.rfind("nextid ", 0) == 0) {
            sscanf(line.c_str(), "nextid %d", &nextEntityId);
        } else if (line.rfind("gamemode ", 0) == 0) {
            gameModePath = line.substr(9);
            if (gameModePath == "-") gameModePath.clear();
        } else if (line.rfind("hudwidget ", 0) == 0) {
            hudWidget = line.substr(10);
            if (hudWidget == "-") hudWidget.clear();
        } else if (line.rfind("layers ", 0) == 0) {
            int c = 6;
            sscanf(line.c_str(), "layers %d", &c);
            layers.count = (c >= 1 && c <= CollisionLayers::MAX) ? c : 6;
        } else if (line.rfind("lname ", 0) == 0) {
            int i = -1;
            char nm[24] = "";
            if (sscanf(line.c_str(), "lname %d %23s", &i, nm) >= 1 && i >= 0 && i < CollisionLayers::MAX) {
                snprintf(layers.names[i], sizeof(layers.names[i]), "%s", (nm[0] && strcmp(nm, "-") != 0) ? nm : "");
            }
        } else if (line.rfind("lrow ", 0) == 0) {
            std::istringstream ls(line);
            std::string tok;
            int i = -1, v;
            ls >> tok >> i;
            if (i >= 0 && i < CollisionLayers::MAX) {
                int j = 0;
                while (ls >> v && j < CollisionLayers::MAX) layers.matrix[i][j++] = (v != 0);
            }
        } else if (line.rfind("entity ", 0) == 0) {
            cur = Entity{};
            pos = {}; rot = {};
            btype = 0; grav = 1; mass = 1; rest = 0.25f; fric = 0.5f; ldamp = 0.01f; adamp = 0.05f;
            sawTriggerComponent = false;
            sscanf(line.c_str(), "entity %d", &cur.id);
            inEntity = true;
        } else if (!inEntity) {
            if (line.rfind("joint ", 0) == 0) {
                JointDef j;
                int rope = 0;
                sscanf(line.c_str(), "joint %d %d %f %d %f", &j.entA, &j.entB, &j.len, &rope, &j.breakImp);
                j.rope = rope != 0;
                joints.push_back(j);
            }
        } else if (line.rfind("parent ", 0) == 0) {
            sscanf(line.c_str(), "parent %d", &cur.parentId);
        } else if (line.rfind("name ", 0) == 0) {
            snprintf(cur.name, sizeof(cur.name), "%s", line.c_str() + 5);
        } else if (line.rfind("mesh ", 0) == 0) {
            int m = 0;
            sscanf(line.c_str(), "mesh %d", &m);
            cur.mesh = (MeshType)(m >= 0 && m < MESH_COUNT ? m : 0);
        } else if (line.rfind("meshasset ", 0) == 0) {
            const char* asset=line.c_str()+10;
            snprintf(cur.meshAsset,sizeof(cur.meshAsset),"%s",strcmp(asset,"-")==0?"":asset);
        } else if (line.rfind("scale ", 0) == 0) {
            sscanf(line.c_str(), "scale %f %f %f", &cur.scale.x, &cur.scale.y, &cur.scale.z);
        } else if (line.rfind("color ", 0) == 0) {
            cur.colorAlpha=1.0f;
            sscanf(line.c_str(), "color %f %f %f %f", &cur.color.x, &cur.color.y, &cur.color.z, &cur.colorAlpha);
        } else if (line.rfind("mat ", 0) == 0) {
            sscanf(line.c_str(), "mat %f %f %f %f", &cur.shininess, &cur.specular, &cur.checker, &cur.emissive);
        } else if (line.rfind("doublesided ", 0) == 0) {
            int enabled = 0; sscanf(line.c_str(), "doublesided %d", &enabled); cur.doubleSided = enabled != 0;
        } else if (line.rfind("materialasset ", 0) == 0) {
            const char* asset = line.c_str() + 14;
            snprintf(cur.materialAsset, sizeof(cur.materialAsset), "%s", strcmp(asset, "-") == 0 ? "" : asset);
        } else if (line.rfind("body ", 0) == 0) {
            grav = 1;   // file vecchi senza il settimo campo: gravita' attiva
            sscanf(line.c_str(), "body %d %f %f %f %f %f %d", &btype, &mass, &rest, &fric, &ldamp, &adamp, &grav);
        } else if (line.rfind("pos ", 0) == 0) {
            sscanf(line.c_str(), "pos %f %f %f", &pos.x, &pos.y, &pos.z);
        } else if (line.rfind("rot ", 0) == 0) {
            sscanf(line.c_str(), "rot %f %f %f %f", &rot.x, &rot.y, &rot.z, &rot.w);
        } else if (line.rfind("light ", 0) == 0) {
            int isL = 0;
            sscanf(line.c_str(), "light %d %f %f %f %f %f", &isL,
                   &cur.lightColor.x, &cur.lightColor.y, &cur.lightColor.z,
                   &cur.lightIntensity, &cur.lightRange);
            cur.isLight = isL != 0;
        } else if (line.rfind("camera ", 0) == 0) {
            int isCam = 0;
            unsigned mask = cur.camLayerMask; int linear = cur.camLinearClipping ? 1 : 0;
            int post = cur.camPostProcess ? 1 : 0;   // absent in older scenes: stays on
            sscanf(line.c_str(), "camera %d %f %f %f %f %d %u %d", &isCam, &cur.camFov, &cur.camOffsetY,
                   &cur.camNearClip, &cur.camClipDistance, &linear, &mask, &post);
            cur.isCamera = isCam != 0;
            cur.camLinearClipping = linear != 0;
            cur.camLayerMask = mask;
            cur.camPostProcess = post != 0;
            cur.camNearClip = clampf(cur.camNearClip, .001f, 1000.0f);
            cur.camClipDistance = (std::max)(cur.camNearClip + .01f, cur.camClipDistance);
        } else if (line.rfind("audio ", 0) == 0) {
            int has = 0, loop = 0, awake = 1, spatial = 1;
            char clip[192] = "-";
            int n = sscanf(line.c_str(), "audio %d %f %d %d %d %f %f %191[^\n]", &has, &cur.audioVolume,
                           &loop, &awake, &spatial, &cur.audioMinDistance, &cur.audioMaxDistance, clip);
            if (n >= 7) {
                cur.hasAudio = has != 0;
                cur.audioVolume = clampf(cur.audioVolume, 0.0f, 2.0f);
                cur.audioLoop = loop != 0;
                cur.audioPlayOnAwake = awake != 0;
                cur.audioSpatial = spatial != 0;
                snprintf(cur.audioClip, sizeof(cur.audioClip), "%s", (n >= 8 && strcmp(clip, "-") != 0) ? clip : "");
            }
        } else if (line.rfind("audio_class ", 0) == 0) {
            std::string path = line.substr(12);
            snprintf(cur.audioClass, sizeof(cur.audioClass), "%s", path == "-" ? "" : path.c_str());
        } else if (line.rfind("audio_atten ", 0) == 0) {
            std::string path = line.substr(12);
            snprintf(cur.audioAttenuation, sizeof(cur.audioAttenuation), "%s", path == "-" ? "" : path.c_str());
        } else if (line.rfind("audio_concurrency ", 0) == 0) {
            std::string path = line.substr(18);
            snprintf(cur.audioConcurrency, sizeof(cur.audioConcurrency), "%s", path == "-" ? "" : path.c_str());
        } else if (line.rfind("reverb ", 0) == 0) {
            int enabled = 0;
            if (sscanf(line.c_str(), "reverb %d %f %f %f", &enabled, &cur.reverbRadius, &cur.reverbWet, &cur.reverbDecay) >= 1)
                cur.hasReverb = enabled != 0;
        } else if (line.rfind("postprocess ", 0) == 0) {
            int enabled = 0, unbound = 0;
            if (sscanf(line.c_str(), "postprocess %d %d %f %f", &enabled, &unbound,
                       &cur.ppPriority, &cur.ppBlendWeight) >= 1) {
                cur.hasPostProcess = enabled != 0;
                cur.ppUnbound = unbound != 0;
            }
        } else if (line.rfind("pp_grade ", 0) == 0) {
            PostSettings& pp = cur.ppSettings;
            sscanf(line.c_str(), "pp_grade %f %f %f %f %f %f %f", &pp.exposure, &pp.contrast, &pp.saturation,
                   &pp.tint.x, &pp.tint.y, &pp.tint.z, &pp.vignette);
        } else if (line.rfind("pp_fx ", 0) == 0) {
            PostSettings& pp = cur.ppSettings;
            sscanf(line.c_str(), "pp_fx %f %f %f %f", &pp.bloomThreshold, &pp.bloomIntensity,
                   &pp.chromaticAberration, &pp.grain);
        } else if (line.rfind("aiagent ", 0) == 0) {
            int enabled = 0, debugDraw = 1;
            int fields = sscanf(line.c_str(), "aiagent %d %f %f %f %f %f %d", &enabled, &cur.aiSpeed, &cur.aiAcceleration,
                                &cur.aiAngularSpeed, &cur.aiStoppingDistance, &cur.aiBaseOffset, &debugDraw);
            if (fields >= 1) cur.hasAIAgent = enabled != 0;
            if (fields >= 7) cur.aiDebugDraw = debugDraw != 0;
            cur.aiBaseOffset=(std::max)(0.0f,cur.aiBaseOffset);
        } else if (line.rfind("animator ", 0) == 0) {
            int enabled = 0, awake = 1; char controller[192] = "-";
            int fields = sscanf(line.c_str(), "animator %d %d %f %191s", &enabled, &awake, &cur.animatorSpeed, controller);
            if (fields >= 1) cur.hasAnimator = enabled != 0;
            if (fields >= 2) cur.animatorPlayOnAwake = awake != 0;
            cur.animatorSpeed = (std::max)(0.0f, cur.animatorSpeed);
            if (fields >= 4 && strcmp(controller, "-") != 0)
                snprintf(cur.animatorController, sizeof(cur.animatorController), "%s", controller);
        } else if (line.rfind("navoccluder ", 0) == 0) {
            int enabled = 0;
            if (sscanf(line.c_str(), "navoccluder %d %f", &enabled, &cur.navigationOccluderPadding) >= 1)
                cur.hasNavigationOccluder = enabled != 0;
            cur.navigationOccluderPadding = (std::max)(0.0f, cur.navigationOccluderPadding);
        } else if (line.rfind("constraint ", 0) == 0) {
            cur.hasConstraint = true;
            sscanf(line.c_str(), "constraint %d %d %f", &cur.constraintObjA, &cur.constraintObjB, &cur.conBreak);
        } else if (line.rfind("conlin ", 0) == 0) {
            sscanf(line.c_str(), "conlin %d %d %d %f %f %f", &cur.conLinMode[0], &cur.conLinMode[1], &cur.conLinMode[2],
                   &cur.conLinLimit[0], &cur.conLinLimit[1], &cur.conLinLimit[2]);
        } else if (line.rfind("conang ", 0) == 0) {
            sscanf(line.c_str(), "conang %d %d %d %f %f %f", &cur.conAngMode[0], &cur.conAngMode[1], &cur.conAngMode[2],
                   &cur.conAngLimit[0], &cur.conAngLimit[1], &cur.conAngLimit[2]);
        } else if (line.rfind("conmotor ", 0) == 0) {
            int lm = 0, am = 0;
            sscanf(line.c_str(), "conmotor %d %f %f %f %f %d %f %f %f %f", &lm,
                   &cur.conLinMotorTarget.x, &cur.conLinMotorTarget.y, &cur.conLinMotorTarget.z, &cur.conLinMotorForce,
                   &am, &cur.conAngMotorTarget.x, &cur.conAngMotorTarget.y, &cur.conAngMotorTarget.z, &cur.conAngMotorForce);
            cur.conLinMotor = lm != 0; cur.conAngMotor = am != 0;
        } else if (line.rfind("inspectorevents ", 0) == 0) {
            int enabled=0;if(sscanf(line.c_str(),"inspectorevents %d",&enabled)==1)cur.hasInspectorEvents=enabled!=0;
        } else if (line.rfind("inspectorevent ", 0) == 0) {
            std::istringstream ls(line.substr(15));InspectorEventDef event;ls>>std::quoted(event.name);
            if(event.name.empty())event.name="Event";cur.inspectorEvents.push_back(std::move(event));
        } else if (line.rfind("inspectorlistener ", 0) == 0 && !cur.inspectorEvents.empty()) {
            std::istringstream ls(line.substr(18));InspectorEventListener listener;int custom=0;
            ls>>listener.targetEntity>>custom>>std::quoted(listener.callable);listener.customEvent=custom!=0;
            cur.inspectorEvents.back().listeners.push_back(std::move(listener));
        } else if (line.rfind("inspectorarg ", 0) == 0 && !cur.inspectorEvents.empty() &&
                   !cur.inspectorEvents.back().listeners.empty()) {
            std::istringstream ls(line.substr(13));InspectorEventArgument argument;
            ls>>argument.kind>>argument.value.x>>argument.value.y>>argument.value.z>>argument.alpha
              >>argument.objectId>>std::quoted(argument.text);
            cur.inspectorEvents.back().listeners.back().arguments.push_back(std::move(argument));
        } else if (line.rfind("prefabref ", 0) == 0) {
            char asset[192] = "-"; int root = 0;
            if (sscanf(line.c_str(), "prefabref %191s %d", asset, &root) >= 1) {
                snprintf(cur.prefabAsset, sizeof(cur.prefabAsset), "%s", strcmp(asset, "-") == 0 ? "" : asset);
                cur.prefabInstanceRoot = root;
            }
        } else if (line.rfind("staticflags ", 0) == 0) {
            unsigned flags = 0;
            if (sscanf(line.c_str(), "staticflags %u", &flags) == 1) cur.staticFlags = flags;
        } else if (line.rfind("tag ", 0) == 0) {
            std::string tag = line.substr(4);
            if (!tag.empty() && std::find(cur.tags.begin(), cur.tags.end(), tag) == cur.tags.end()) cur.tags.push_back(tag);
        } else if (line.rfind("behavior ", 0) == 0) {
            sscanf(line.c_str(), "behavior %d %f %f %f", &cur.behavior, &cur.bp[0], &cur.bp[1], &cur.bp[2]);
            if (cur.behavior < 0 || cur.behavior >= BH_COUNT) cur.behavior = 0;
        } else if (line.rfind("comps ", 0) == 0) {
            int m = 1, p = 1, col = -1;
            sscanf(line.c_str(), "comps %d %d %d", &m, &p, &col);
            cur.hasMesh = m != 0;
            cur.hasPhysics = p != 0;
            // migrazione file vecchi (senza campo collisione): volume fisico senza
            // mesh = trigger, altrimenti solido
            cur.collision = col >= 0 ? col : (cur.hasPhysics && !cur.hasMesh ? 1 : 0);
        } else if (line.rfind("triggershape ", 0) == 0) {
            sscanf(line.c_str(), "triggershape %d", &cur.triggerShape);
            if (cur.triggerShape < 0 || cur.triggerShape > 2) cur.triggerShape = 0;
        } else if (line.rfind("triggercomp ", 0) == 0) {
            int v = 0; sscanf(line.c_str(), "triggercomp %d", &v);
            cur.hasTrigger = v != 0; sawTriggerComponent = true;
        } else if (line.rfind("elayer ", 0) == 0) {
            sscanf(line.c_str(), "elayer %d", &cur.layer);
            if (cur.layer < 0 || cur.layer >= CollisionLayers::MAX) cur.layer = 0;
        } else if (line.rfind("graph ", 0) == 0) {
            std::string gp = line.substr(6);
            if (gp == "-") gp.clear();
            snprintf(cur.graphPath, sizeof(cur.graphPath), "%s", gp.c_str());
        } else if (line.rfind("bpcomponent ", 0) == 0) {
            std::istringstream componentLine(line.substr(12));
            size_t index = 0; int collapsed = 0; std::string path;
            if (componentLine >> index >> collapsed >> std::quoted(path)) {
                if (index >= cur.additionalBlueprints.size()) cur.additionalBlueprints.resize(index + 1);
                cur.additionalBlueprints[index].graphPath = path;
                cur.additionalBlueprints[index].collapsed = collapsed != 0;
            }
        } else if (line.rfind("bpcomponentvar ", 0) == 0) {
            std::istringstream valueLine(line.substr(15));
            size_t index = 0; std::string name; Vec3 value;
            if (valueLine >> index >> std::quoted(name) >> value.x >> value.y >> value.z) {
                if (index >= cur.additionalBlueprints.size()) cur.additionalBlueprints.resize(index + 1);
                cur.additionalBlueprints[index].varOverrides[name] = value;
            }
        } else if (line.rfind("bpcomponentalpha ", 0) == 0) {
            std::istringstream alphaLine(line.substr(17));
            size_t index = 0; std::string name; float alpha = 1.0f;
            if (alphaLine >> index >> std::quoted(name) >> alpha) {
                if (index >= cur.additionalBlueprints.size()) cur.additionalBlueprints.resize(index + 1);
                cur.additionalBlueprints[index].varAlphaOverrides[name] = alpha;
            }
        } else if (line.rfind("dcollapse ", 0) == 0) {
            unsigned value = 0;
            sscanf(line.c_str(), "dcollapse %u", &value);
            cur.detailCollapsed = value;
        } else if (line.rfind("dorder", 0) == 0) {
            std::istringstream order(line);
            std::string tag; int component;
            order >> tag;
            cur.detailOrder.clear();
            while (order >> component) cur.detailOrder.push_back(component);
        } else if (line.rfind("varov ", 0) == 0) {
            char nm[40];
            Vec3 v;
            if (sscanf(line.c_str(), "varov %39s %f %f %f", nm, &v.x, &v.y, &v.z) == 4) {
                cur.varOverrides[nm] = v;
            }
        } else if(line.rfind("varova ",0)==0){char nm[32];float alpha=1;if(sscanf(line.c_str(),"varova %31s %f",nm,&alpha)==2)cur.varAlphaOverrides[nm]=alpha;
        } else if (line == "end") {
            cur.additionalBlueprints.erase(std::remove_if(cur.additionalBlueprints.begin(), cur.additionalBlueprints.end(),
                [](const BlueprintComponentDef& component) { return component.graphPath.empty(); }), cur.additionalBlueprints.end());
            if (!cur.graphPath[0] && !cur.additionalBlueprints.empty()) {
                BlueprintComponentDef first = std::move(cur.additionalBlueprints.front());
                cur.additionalBlueprints.erase(cur.additionalBlueprints.begin());
                snprintf(cur.graphPath, sizeof(cur.graphPath), "%s", first.graphPath.c_str());
                cur.varOverrides = std::move(first.varOverrides);
                cur.varAlphaOverrides = std::move(first.varAlphaOverrides);
                if (first.collapsed) cur.detailCollapsed |= 1u << DETAIL_BLUEPRINT;
            }
            normalizeDetailOrder(cur);
            // Migration: old invisible volumes encoded a Trigger as
            // !hasMesh + hasPhysics + collision==Overlap.
            if (!sawTriggerComponent && !cur.hasMesh && cur.hasPhysics && cur.collision == 1) {
                cur.hasTrigger = true;
                cur.hasPhysics = false;
            }
            MeshType colliderMesh = cur.hasTrigger
                                  ? (cur.triggerShape == 1 ? MESH_SPHERE : cur.triggerShape == 2 ? MESH_CAPSULE : MESH_CUBE)
                                  : cur.mesh;
            Shape shape = shapeForEntity(colliderMesh, cur.scale);
            cur.body = world.addBody(shape, btype ? BodyType::Static : BodyType::Dynamic,
                                     mass, pos, rot, rest, fric);
            cur.body->linearDamping = ldamp;
            cur.body->angularDamping = adamp;
            cur.body->queryOnly = cur.hasTrigger || (cur.hasMesh && !cur.hasPhysics);
            cur.body->enabled = cur.hasTrigger || cur.hasMesh;
            cur.body->trigger = cur.body->queryOnly;
            cur.body->layer = cur.layer;
            cur.body->useGravity = grav != 0;
            entities.push_back(cur);
            if (cur.id >= nextEntityId) nextEntityId = cur.id + 1;
            inEntity = false;
        }
    }
    rebuildConstraints();
    applyLayersToWorld();
    return true;
}

std::string EditorScene::serializeSubset(const std::vector<int>& ids) const {
    std::ostringstream o;
    o << "IMPULSO 2\n";
    for (const auto& e : entities) {
        bool inSet = false;
        for (int id : ids) if (id == e.id) inSet = true;
        if (!inSet) continue;
        bool parentInSet = false;
        for (int id : ids) if (id == e.parentId) parentInSet = true;
        const RigidBody* b = e.body;
        o << "entity " << e.id << "\n";
        o << "parent " << (parentInSet ? e.parentId : 0) << "\n";
        o << "name " << e.name << "\n";
        o << "mesh " << (int)e.mesh << "\n";
        o << "meshasset " << (e.meshAsset[0] ? e.meshAsset : "-") << "\n";
        o << "scale " << e.scale.x << " " << e.scale.y << " " << e.scale.z << "\n";
        o << "color " << e.color.x << " " << e.color.y << " " << e.color.z << " " << e.colorAlpha << "\n";
        o << "mat " << e.shininess << " " << e.specular << " " << e.checker << " " << e.emissive << "\n";
        o << "doublesided " << (e.doubleSided ? 1 : 0) << "\n";
        o << "materialasset " << (e.materialAsset[0] ? e.materialAsset : "-") << "\n";
        o << "body " << (b->type == BodyType::Static ? 1 : 0) << " " << (b->mass > 0 ? b->mass : 1)
          << " " << b->restitution << " " << b->friction
          << " " << b->linearDamping << " " << b->angularDamping
          << " " << (b->useGravity ? 1 : 0) << "\n";
        o << "pos " << b->position.x << " " << b->position.y << " " << b->position.z << "\n";
        o << "rot " << b->quat.x << " " << b->quat.y << " " << b->quat.z << " " << b->quat.w << "\n";
        o << "light " << (e.isLight ? 1 : 0) << " " << e.lightColor.x << " " << e.lightColor.y << " "
          << e.lightColor.z << " " << e.lightIntensity << " " << e.lightRange << "\n";
        if (e.isCamera) o << "camera 1 " << e.camFov << " " << e.camOffsetY << " "
                          << e.camNearClip << " " << e.camClipDistance << " "
                          << (e.camLinearClipping ? 1 : 0) << " " << e.camLayerMask << " "
                          << (e.camPostProcess ? 1 : 0) << "\n";
        o << "audio " << (e.hasAudio ? 1 : 0) << " " << e.audioVolume << " "
          << (e.audioLoop ? 1 : 0) << " " << (e.audioPlayOnAwake ? 1 : 0) << " "
          << (e.audioSpatial ? 1 : 0) << " " << e.audioMinDistance << " " << e.audioMaxDistance
          << " " << (e.audioClip[0] ? e.audioClip : "-") << "\n";
        o << "audio_class " << (e.audioClass[0] ? e.audioClass : "-") << "\n";
        o << "audio_atten " << (e.audioAttenuation[0] ? e.audioAttenuation : "-") << "\n";
        o << "audio_concurrency " << (e.audioConcurrency[0] ? e.audioConcurrency : "-") << "\n";
        o << "reverb " << (e.hasReverb ? 1 : 0) << " " << e.reverbRadius << " " << e.reverbWet << " " << e.reverbDecay << "\n";
        if (e.hasPostProcess) {
            const PostSettings& pp = e.ppSettings;
            o << "postprocess 1 " << (e.ppUnbound ? 1 : 0) << " " << e.ppPriority << " " << e.ppBlendWeight << "\n";
            o << "pp_grade " << pp.exposure << " " << pp.contrast << " " << pp.saturation << " "
              << pp.tint.x << " " << pp.tint.y << " " << pp.tint.z << " " << pp.vignette << "\n";
            o << "pp_fx " << pp.bloomThreshold << " " << pp.bloomIntensity << " "
              << pp.chromaticAberration << " " << pp.grain << "\n";
        }
        o << "aiagent " << (e.hasAIAgent ? 1 : 0) << " " << e.aiSpeed << " " << e.aiAcceleration << " "
          << e.aiAngularSpeed << " " << e.aiStoppingDistance << " " << e.aiBaseOffset << " "
          << (e.aiDebugDraw ? 1 : 0) << "\n";
        o << "animator " << (e.hasAnimator ? 1 : 0) << " " << (e.animatorPlayOnAwake ? 1 : 0) << " "
          << e.animatorSpeed << " " << (e.animatorController[0] ? e.animatorController : "-") << "\n";
        o << "navoccluder " << (e.hasNavigationOccluder ? 1 : 0) << " " << e.navigationOccluderPadding << "\n";
        if (e.hasConstraint) {
            o << "constraint " << e.constraintObjA << " " << e.constraintObjB << " " << e.conBreak << "\n";
            o << "conlin " << e.conLinMode[0] << " " << e.conLinMode[1] << " " << e.conLinMode[2]
              << " " << e.conLinLimit[0] << " " << e.conLinLimit[1] << " " << e.conLinLimit[2] << "\n";
            o << "conang " << e.conAngMode[0] << " " << e.conAngMode[1] << " " << e.conAngMode[2]
              << " " << e.conAngLimit[0] << " " << e.conAngLimit[1] << " " << e.conAngLimit[2] << "\n";
            o << "conmotor " << (e.conLinMotor ? 1 : 0) << " " << e.conLinMotorTarget.x << " " << e.conLinMotorTarget.y
              << " " << e.conLinMotorTarget.z << " " << e.conLinMotorForce
              << " " << (e.conAngMotor ? 1 : 0) << " " << e.conAngMotorTarget.x << " " << e.conAngMotorTarget.y
              << " " << e.conAngMotorTarget.z << " " << e.conAngMotorForce << "\n";
        }
        o << "inspectorevents " << (e.hasInspectorEvents ? 1 : 0) << "\n";
        for (const InspectorEventDef& event : e.inspectorEvents) {
            o << "inspectorevent " << std::quoted(event.name) << "\n";
            for (const InspectorEventListener& listener : event.listeners) {
                o << "inspectorlistener " << listener.targetEntity << " " << (listener.customEvent ? 1 : 0)
                  << " " << std::quoted(listener.callable) << "\n";
                for (const InspectorEventArgument& argument : listener.arguments)
                    o << "inspectorarg " << argument.kind << " " << argument.value.x << " " << argument.value.y
                      << " " << argument.value.z << " " << argument.alpha << " " << argument.objectId
                      << " " << std::quoted(argument.text) << "\n";
            }
        }
        o << "prefabref " << (e.prefabAsset[0] ? e.prefabAsset : "-") << " " << e.prefabInstanceRoot << "\n";
        o << "staticflags " << e.staticFlags << "\n";
        for (const std::string& tag : e.tags) o << "tag " << tag << "\n";
        o << "behavior " << e.behavior << " " << e.bp[0] << " " << e.bp[1] << " " << e.bp[2] << "\n";
        o << "comps " << (e.hasMesh ? 1 : 0) << " " << (e.hasPhysics ? 1 : 0) << " " << e.collision << "\n";
        o << "triggercomp " << (e.hasTrigger ? 1 : 0) << "\n";
        o << "triggershape " << e.triggerShape << "\n";
        o << "elayer " << e.layer << "\n";
        o << "graph " << (e.graphPath[0] ? e.graphPath : "-") << "\n";
        o << "dcollapse " << e.detailCollapsed << "\n";
        o << "dorder";
        for (int component : e.detailOrder) o << " " << component;
        o << "\n";
        for (const auto& [k, v] : e.varOverrides) {
            o << "varov " << k << " " << v.x << " " << v.y << " " << v.z << "\n";
        }
        for(const auto&[k,a]:e.varAlphaOverrides)o<<"varova "<<k<<" "<<a<<"\n";
        serializeAdditionalBlueprints(o, e);
        o << "end\n";
    }
    for (const auto& j : joints) {
        bool aIn = false, bIn = false;
        for (int id : ids) { if (id == j.entA) aIn = true; if (id == j.entB) bIn = true; }
        if (aIn && bIn) o << "joint " << j.entA << " " << j.entB << " " << j.len << " " << (j.rope ? 1 : 0) << " " << j.breakImp << "\n";
    }
    return o.str();
}

std::vector<int> EditorScene::instantiateFrom(const std::string& text, const Vec3& pos, bool placeAtPos) {
    std::vector<int> newIds;
    EditorScene temp;
    if (!temp.deserialize(text) || temp.entities.empty()) return newIds;

    Vec3 offset = pos;
    if (placeAtPos) {
        Vec3 centroid = {};
        float minY = 1e30f;
        for (const auto& e : temp.entities) {
            centroid += e.body->position;
            if (e.body->position.y < minY) minY = e.body->position.y;
        }
        centroid *= 1.0f / temp.entities.size();
        offset = { pos.x - centroid.x, pos.y - minY, pos.z - centroid.z };
    }

    std::vector<std::pair<int, int>> idMap; // old → new
    for (const auto& src : temp.entities) {
        Entity e = src;
        e.id = nextEntityId++;
        idMap.push_back({ src.id, e.id });
        const RigidBody* sb = src.body;
        e.body = world.addBody(sb->shape, sb->type, sb->mass > 0 ? sb->mass : 1,
                               sb->position + offset, sb->quat, sb->restitution, sb->friction);
        e.body->linearDamping = sb->linearDamping;
        e.body->angularDamping = sb->angularDamping;
        e.body->canSleep = sb->canSleep;
        e.body->queryOnly = e.hasTrigger || (e.hasMesh && !e.hasPhysics);
        e.body->enabled = e.hasTrigger || e.hasMesh;
        e.body->trigger = e.body->queryOnly;
        e.body->layer = e.layer;
        entities.push_back(e);
        newIds.push_back(e.id);
    }
    auto remap = [&](int oldId) -> int {
        for (auto& [o, n] : idMap) if (o == oldId) return n;
        return 0;
    };
    for (int nid : newIds) {
        Entity* e = byId(nid);
        if (!e) continue;
        if (e->parentId != 0) e->parentId = remap(e->parentId);
        if (e->prefabInstanceRoot != 0) e->prefabInstanceRoot = remap(e->prefabInstanceRoot);
        if (e->hasConstraint) {   // keep constraint refs pointing at the instantiated copies
            if (e->constraintObjA) e->constraintObjA = remap(e->constraintObjA);
            if (e->constraintObjB) e->constraintObjB = remap(e->constraintObjB);
        }
    }
    for (const auto& j : temp.joints) {
        int a = remap(j.entA), b = remap(j.entB);
        if (a && b) joints.push_back({ a, b, j.len, j.rope });
    }
    rebuildConstraints();
    return newIds;
}

// ═══ prefab scenes ═══
static void addGroundTo(EditorScene& s, float size = 26) {
    Entity& e = s.spawnBox("Floor", { 0, -0.5f, 0 }, { size, 1, size },
                           { 0.42f, 0.45f, 0.5f }, BodyType::Static, 0, 0.25f, 0.7f);
    e.checker = 2;
    e.shininess = 24;
    e.specular = 0.12f;
}

void sceneDefault(EditorScene& s) {
    s.clear();
    addGroundTo(s);
    char nm[48];
    for (int i = 0; i < 5; i++) {
        snprintf(nm, sizeof(nm), "Stack cube %d", i + 1);
        s.spawnBox(nm, { 0, 0.5f + i * 1.01f, 0 }, { 1, 1, 1 },
                   { 0.4f + i * 0.12f, 0.55f, 0.85f - i * 0.12f }, BodyType::Dynamic, 1, 0.1f, 0.6f);
    }
    Entity& jumper = s.spawnBox("Jumping cube", { 3, 0.5f, 2 }, { 1, 1, 1 }, { 0.9f, 0.6f, 0.2f }, BodyType::Dynamic, 1, 0.15f, 0.6f);
    jumper.behavior = BH_JUMP_SPACE;
    jumper.bp[0] = 6;
    s.spawnSphere("Blue sphere", { 2.5f, 3, -1.5f }, 1, { 0.35f, 0.65f, 0.95f });
    s.spawnSphere("Heavy sphere", { -2.5f, 2, 1.5f }, 1.4f, { 0.9f, 0.5f, 0.2f }, 2.5f);
    s.spawnLight("Warm light", { -3.5f, 3.5f, 2.5f });
}

void sceneTower(EditorScene& s) {
    s.clear();
    addGroundTo(s, 30);
    const Vec3 pal[4] = { { 0.85f, 0.55f, 0.25f }, { 0.75f, 0.35f, 0.3f }, { 0.8f, 0.65f, 0.35f }, { 0.6f, 0.55f, 0.45f } };
    char nm[48];
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 4; x++) {
            snprintf(nm, sizeof(nm), "Brick %d", y * 4 + x + 1);
            s.spawnBox(nm, { (x - 1.5f) * 1.02f + (y % 2 ? 0.25f : 0.0f), 0.5f + y * 1.01f, 0 },
                       { 1, 1, 1 }, pal[(x + y) % 4], BodyType::Dynamic, 1, 0.1f, 0.6f);
        }
    Entity& ball = s.spawnSphere("Wrecking ball", { 0.4f, 3, 13 }, 1.7f, { 0.25f, 0.28f, 0.33f }, 14, 0.3f, 0.4f);
    ball.shininess = 120;
    ball.specular = 0.9f;
    ball.behavior = BH_PUSH_START;
    ball.bp[0] = 0; ball.bp[1] = 3.5f; ball.bp[2] = -16;
    s.spawnLight("Light", { 6, 4, 6 });
}

void scenePendulums(EditorScene& s) {
    s.clear();
    addGroundTo(s, 20);
    const int n = 5;
    const float spacing = 1.02f, y0 = 6.5f, len = 3.4f;
    char nm[48];
    for (int i = 0; i < n; i++) {
        float x = (i - (n - 1) / 2.0f) * spacing;
        snprintf(nm, sizeof(nm), "Pivot %d", i + 1);
        int anchorId = s.spawnBox(nm, { x, y0, 0 }, { 0.24f, 0.24f, 0.24f }, { 0.3f, 0.32f, 0.38f }, BodyType::Static).id;
        Vec3 startPos = i == 0
            ? Vec3{ x - len / 1.41421f, y0 - len / 1.41421f, 0 }
            : Vec3{ x, y0 - len, 0 };
        snprintf(nm, sizeof(nm), "Pendulum %d", i + 1);
        Entity& ball = s.spawnSphere(nm, startPos, 1, { 0.75f, 0.78f, 0.85f }, 2, 0.93f, 0.05f);
        ball.shininess = 160;
        ball.specular = 1;
        ball.body->linearDamping = 0.002f;
        ball.body->canSleep = false;
        s.addJoint(anchorId, ball.id, len, false);
    }
    s.spawnLight("Light", { 0, 5, 5 });
}

void sceneDomino(EditorScene& s) {
    s.clear();
    addGroundTo(s, 34);
    char nm[48];
    for (int i = 0; i < 16; i++) {
        Quat q = (i == 0) ? Quat::fromEulerDeg(-28, 0, 0) : Quat{};   // roll: inclina verso la tessera dopo
        snprintf(nm, sizeof(nm), "Domino %d", i + 1);
        s.spawnBox(nm, { -7 + i * 1.05f, 0.85f, 0 }, { 0.28f, 1.7f, 0.9f },
                   i % 2 ? Vec3{ 0.9f, 0.88f, 0.82f } : Vec3{ 0.85f, 0.3f, 0.3f },
                   BodyType::Dynamic, 0.8f, 0.05f, 0.45f, q);
    }
    s.spawnBox("Pedestal", { 10.2f, 1, 0 }, { 0.8f, 2, 0.8f }, { 0.4f, 0.42f, 0.5f }, BodyType::Static);
    s.spawnSphere("Grand finale", { 10.2f, 2.5f, 0 }, 1, { 0.95f, 0.65f, 0.2f }, 1, 0.7f);
    s.spawnLight("Light", { 0, 4, 4 });
}
