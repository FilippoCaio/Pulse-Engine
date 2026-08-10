// ─── Pulse Engine editor scene: named entities, joints, serialization, prefabs ───
#pragma once
#include "physics.h"
#include "render.h"
#include <map>
#include <string>
#include <vector>

enum BehaviorType { BH_NONE = 0, BH_JUMP_SPACE, BH_SPIN, BH_PUSH_START, BH_COUNT };
extern const char* BEHAVIOR_NAMES[BH_COUNT];

// Inspector component cards. Transform and Collision are fixed base cards;
// only the optional range is user-orderable below them.
enum DetailComponentKind {
    DETAIL_TRANSFORM = 0, DETAIL_COLLISION,
    DETAIL_MESH, DETAIL_PHYSICS, DETAIL_LIGHT, DETAIL_CAMERA,
    DETAIL_AUDIO, DETAIL_SIMPLE_SCRIPT, DETAIL_BLUEPRINT, DETAIL_JOINTS,
    DETAIL_REVERB, DETAIL_AI_AGENT,
    DETAIL_TRIGGER, DETAIL_ANIMATOR, DETAIL_NAV_OCCLUDER,
    DETAIL_INSPECTOR_EVENTS,
    DETAIL_CONSTRAINT,          // Physics Constraint (append at end: serialized indices stay stable)
    DETAIL_POSTPROCESS,         // Post Process Volume
    DETAIL_COMPONENT_COUNT
};

enum StaticFlag : uint32_t {
    STATIC_MOVEMENT = 1u << 0,
    STATIC_LIGHTING = 1u << 1,
    STATIC_NAVIGATION = 1u << 2,
};

// Unity-style persistent Inspector event. `kind` uses the serialized PinKind
// numeric value without coupling the scene format to the Blueprint header.
struct InspectorEventArgument {
    int kind = 1;
    Vec3 value;
    float alpha = 1.0f;
    int objectId = 0;
    std::string text;
};

struct InspectorEventListener {
    int targetEntity = 0;
    bool customEvent = false;
    std::string callable;
    std::vector<InspectorEventArgument> arguments;
};

struct InspectorEventDef {
    std::string name = "Event";
    std::vector<InspectorEventListener> listeners;
};

// Additional Unity-style Blueprint component attached to an Entity. The
// legacy graphPath/override fields below remain the first component so old
// scenes and existing framework code stay source- and data-compatible.
struct BlueprintComponentDef {
    std::string graphPath;
    std::map<std::string, Vec3> varOverrides;
    std::map<std::string, float> varAlphaOverrides;
    bool collapsed = false;
};

struct Entity {
    int id = 0;
    int parentId = 0;            // 0 = root
    char name[48] = "Oggetto";
    RigidBody* body = nullptr;   // always present: carries the transform

    // ── components (Unity-style: each can be added/removed) ──
    bool hasMesh = true;         // Mesh renderer
    MeshType mesh = MESH_CUBE;
    char meshAsset[192] = "";    // imported model path relative to project; empty = primitive
    Vec3 scale = { 1, 1, 1 };
    Vec3 color = { 0.8f, 0.5f, 0.3f };
    float colorAlpha = 1.0f;
    float shininess = 48, specular = 0.35f, checker = 0, emissive = 0;
    bool doubleSided = false;
    char materialAsset[192] = "";   // assigned .mat asset (relative to project); empty = inline params

    bool hasPhysics = true;      // RigidBody in the simulation
    int collision = 0;           // risposta collisione: 0 Blocca (solido), 1 Sovrapponi (trigger)
    bool hasTrigger = false;     // query-only collider component, independent from RigidBody
    int triggerShape = 0;        // Trigger component: 0 Box, 1 Sphere, 2 Capsule
    int layer = 0;               // collision layer (indice in EditorScene::layers)

    bool isLight = false;        // point light
    Vec3 lightColor = { 1, 0.85f, 0.55f };
    float lightIntensity = 3, lightRange = 12;

    bool isCamera = false;       // scene camera: in Play the view comes from here
    float camFov = 70;
    float camOffsetY = 0.6f;     // eye height above the body position
    bool camLinearClipping = true;
    float camNearClip = 0.1f;
    float camClipDistance = 500.0f;
    uint32_t camLayerMask = 0xFFFFu;
    bool camPostProcess = true;  // this view is graded by Post Process Volumes

    // ── Post Process Volume ──
    // Unbound volumes grade everything; a bound one only grades a view inside
    // its box (the entity's own scaled cube). Priority breaks ties between
    // overlapping volumes and Blend Weight fades the settings in.
    bool hasPostProcess = false;
    bool ppUnbound = false;
    float ppPriority = 0.0f;
    float ppBlendWeight = 1.0f;
    PostSettings ppSettings;

    bool hasAudio = false;       // Audio Source runtime component
    char audioClip[192] = "";    // asset path relative to the project
    float audioVolume = 1.0f;
    bool audioLoop = false;
    bool audioPlayOnAwake = true;
    bool audioSpatial = true;
    float audioMinDistance = 1.0f;
    float audioMaxDistance = 25.0f;
    char audioClass[192] = "";       // reusable .aclass asset
    char audioAttenuation[192] = ""; // reusable .atten asset
    char audioConcurrency[192] = ""; // reusable .concurrency asset

    bool hasReverb = false;       // spherical Audio Reverb Zone
    float reverbRadius = 8.0f;
    float reverbWet = 0.45f;      // 0 dry, 1 strongly reverberant
    float reverbDecay = 1.2f;

    bool hasAIAgent = false;      // NavMesh-style scene agent
    bool aiDebugDraw = true;      // per-agent path/recalculation visualization
    float aiSpeed = 3.5f;
    float aiAcceleration = 10.0f;
    float aiAngularSpeed = 360.0f;
    float aiStoppingDistance = 0.15f;
    float aiBaseOffset = 0.5f;   // pivot height above the baked navigation surface
    int aiTargetEntity = 0;
    Vec3 aiDestination;
    bool aiUseTargetEntity = false;
    bool aiStopped = false;
    bool aiHasPath = false;       // runtime state (not authored)
    float aiRemainingDistance = 0;
    float aiRepathTimer = 0;
    Vec3 aiLastPathTarget;
    std::vector<Vec3> aiPath;
    int aiPathIndex = 0;
    Vec3 aiSteeringVelocity;      // persistent planar velocity used by kinematic agents

    bool hasNavigationOccluder = false; // removes this shape from the baked navigation surface
    float navigationOccluderPadding = 0.0f;

    bool hasAnimator = false;     // runtime Animation Controller component
    char animatorController[192] = "";
    bool animatorPlayOnAwake = true;
    float animatorSpeed = 1.0f;
    int animatorRuntimeState = 0; // runtime-only state
    float animatorRuntimeTime = 0;
    bool animatorRuntimePlaying = false;
    int animatorRuntimePreviousState = 0;
    float animatorRuntimePreviousTime = 0;
    float animatorRuntimeTransitionTime = 0;
    float animatorRuntimeTransitionDuration = 0;
    int animatorRuntimeEventState = 0; // state whose time-zero events were emitted
    std::map<std::string, float> animatorRuntimeParameters;

    bool hasInspectorEvents = false;
    std::vector<InspectorEventDef> inspectorEvents;

    // ── Physics Constraint (Unreal-style): connects two objects ──
    bool hasConstraint = false;
    int constraintObjA = 0, constraintObjB = 0;   // the two connected entities (0 = unset)
    int conLinMode[3] = { 2, 2, 2 };              // per-axis linear: 0 free, 1 limited, 2 locked
    float conLinLimit[3] = { 0.5f, 0.5f, 0.5f };  // metres (half-range) when limited
    int conAngMode[3] = { 0, 0, 0 };              // per-axis angular: 0 free, 1 limited, 2 locked
    float conAngLimit[3] = { 45, 45, 45 };        // degrees when limited
    bool conLinMotor = false; Vec3 conLinMotorTarget; float conLinMotorForce = 100;
    bool conAngMotor = false; Vec3 conAngMotorTarget; float conAngMotorForce = 100;
    float conBreak = 0;                           // break impulse (0 = unbreakable)

    uint32_t staticFlags = 0;     // independent movement/light/navigation checks
    std::vector<std::string> tags; // decoupled gameplay lookup (Player, Enemy, Interactable...)

    char prefabAsset[192] = "";  // relative .pfb source; tinted blue in the Outliner
    int prefabInstanceRoot = 0;   // root entity id shared by every member of the instance

    // local offset vs. parent, captured at Play start: non-simulated children
    // (camera, triggers, static shapes) follow the parent rigid body with these
    Vec3 attachPos;
    Quat attachRot;
    Vec3 attachScale = { 1, 1, 1 };

    int behavior = BH_NONE;      // simple behavior script
    float bp[3] = { 6, 0, 0 };

    char graphPath[96] = "";     // blueprint graph asset (relative to project dir)
    std::map<std::string, Vec3> varOverrides; // per-instance values for exposed public vars
    std::map<std::string, float> varAlphaOverrides; // alpha for exposed Color vars
    std::vector<BlueprintComponentDef> additionalBlueprints; // components after graphPath

    std::vector<int> detailOrder = { DETAIL_MESH, DETAIL_PHYSICS, DETAIL_LIGHT, DETAIL_CAMERA,
                                     DETAIL_AUDIO, DETAIL_REVERB, DETAIL_AI_AGENT, DETAIL_SIMPLE_SCRIPT,
                                     DETAIL_BLUEPRINT, DETAIL_JOINTS, DETAIL_TRIGGER, DETAIL_ANIMATOR,
                                     DETAIL_NAV_OCCLUDER, DETAIL_INSPECTOR_EVENTS, DETAIL_CONSTRAINT,
                                     DETAIL_POSTPROCESS };
    uint32_t detailCollapsed = 0; // bit per DetailComponentKind
};

struct JointDef { int entA = 0, entB = 0; float len = 1; bool rope = false; float breakImp = 0; };

// collision layers (Unity-style): each entity is in one layer; the matrix says
// which layers collide (also gates trigger overlap events)
struct CollisionLayers {
    static const int MAX = 16;   // == PhysicsWorld::MAX_LAYERS
    int count = 6;
    char names[MAX][24];
    bool matrix[MAX][MAX];
    CollisionLayers();           // sensible defaults
    bool collide(int a, int b) const {
        if (a < 0 || a >= count || b < 0 || b >= count) return true;
        return matrix[a][b];
    }
};

struct EditorScene {
    PhysicsWorld world;
    std::vector<Entity> entities;
    std::vector<JointDef> joints;
    CollisionLayers layers;
    float gravityY = -9.81f;
    int nextEntityId = 1;
    std::string gameModePath;        // per-level GameMode Blueprint class
    std::string hudWidget;           // per-level UI widget (.wgt) shown in Play

    void applyLayersToWorld();   // copy the layer matrix into the physics world

    Entity* byId(int id);
    Entity* byBody(const RigidBody* b);
    Entity& spawnBox(const char* name, Vec3 pos, Vec3 size, Vec3 color,
                     BodyType type = BodyType::Dynamic, float mass = 1,
                     float rest = 0.25f, float fric = 0.5f, Quat q = {});
    Entity& spawnEmpty(const char* name, Vec3 pos = {});
    Entity& spawnSphere(const char* name, Vec3 pos, float diam, Vec3 color,
                        float mass = 1, float rest = 0.55f, float fric = 0.4f);
    Entity& spawnLight(const char* name, Vec3 pos);
    void addJoint(int entA, int entB, float len = -1, bool rope = false);
    void removeEntity(int id);       // removes the whole subtree
    void clear();
    void rebuildConstraints();
    void syncBodyShape(Entity& e);   // after scale/mesh/type/mass edits

    // hierarchy
    void collectSubtree(int id, std::vector<int>& out) const; // id first, then descendants
    bool isDescendant(int maybeChild, int ancestor) const;
    bool setParent(int childId, int parentId);                // false if it would create a cycle
    void moveDescendants(int rootId, const Vec3& delta);      // root excluded
    void rotateDescendants(int rootId, const Vec3& pivot, const Quat& oldRotation,
                           const Quat& newRotation);          // orbit + orientation, root excluded
    void scaleDescendants(int rootId, const Vec3& pivot, const Quat& rootRotation,
                          const Vec3& oldScale, const Vec3& newScale); // relative scale, root excluded
    std::vector<int> duplicateSubtree(int rootId);            // returns new ids (root first)

    std::string serialize() const;
    std::string serializeSubset(const std::vector<int>& ids) const;
    bool deserialize(const std::string& text);
    // adds entities from `text` into this scene with fresh ids; placeAtPos centers them on pos
    std::vector<int> instantiateFrom(const std::string& text, const Vec3& pos, bool placeAtPos);
};

// prefab/demo builders
void sceneDefault(EditorScene& s);
void sceneTower(EditorScene& s);
void scenePendulums(EditorScene& s);
void sceneDomino(EditorScene& s);
