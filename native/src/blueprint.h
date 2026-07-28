// ─── Pulse Engine blueprint v2: variables, functions, custom events, macros ───
#pragma once
#include "math.h"
#include "ui.h"
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

struct Entity;
struct EditorScene;
struct CollisionLayers;
// current scene's collision layers, so the trace node's Details can list them
// (set by the app; the blueprint editor is otherwise scene-agnostic)
extern const CollisionLayers* gBPLayers;
extern std::string gBPProjectDir;

// order is stable for serialization: new kinds go at the end
// PIN_WIDGET is a reference to a widget instance, not a number: it carries a
// handle but behaves like an object pin — its own colour, no inline literal, and
// only ever connectable to another widget pin. Appended last so the int value
// saved graphs already store keeps meaning the same kind.
enum PinKind { PIN_EXEC = 0, PIN_NUM, PIN_VEC, PIN_BOOL, PIN_ENT, PIN_ANY, PIN_INT, PIN_VEC2, PIN_STR, PIN_TRANSFORM, PIN_DELEGATE, PIN_TIMER_HANDLE, PIN_ENUM,
               PIN_ANIMATION_CLIP, PIN_ANIMATOR_CONTROLLER, PIN_COLOR, PIN_WIDGET };
#define PIN_KIND_COUNT 17

enum BPType {
    // eventi
    BP_EV_START = 0, BP_EV_TICK, BP_EV_HIT, BP_EV_KEY, BP_EV_CUSTOM,
    // azioni
    BP_ACT_IMPULSE, BP_ACT_FORCE, BP_ACT_SETVEL, BP_ACT_TORQUE, BP_ACT_COLOR, BP_ACT_DESTROY, BP_ACT_PRINT,
    BP_CALL_EVENT, BP_SEND_MSG, BP_CALL_FUNC, BP_FN_RETURN,
    // valori
    BP_VAL_NUM, BP_VAL_VEC, BP_VAL_POS, BP_VAL_VEL, BP_VAL_TIME, BP_VAL_KEYDOWN, BP_VAL_RANDOM,
    BP_SELF, BP_FIND, BP_ISVALID,
    // variabili
    BP_VAR_GET, BP_VAR_SET, BP_LOCAL_GET, BP_LOCAL_SET,
    BP_ARR_GET, BP_ARR_ADD, BP_ARR_LEN, BP_ARR_REMOVE, BP_ARR_CLEAR,
    BP_MAP_GET, BP_MAP_SET, BP_MAP_REMOVE, BP_MAP_LEN,
    // matematica
    BP_M_ADD, BP_M_SUB, BP_M_MUL, BP_M_SIN, BP_M_SCALEV, BP_M_ADDV, BP_M_LEN,
    // logica pura
    BP_L_CMP, BP_L_NOT, BP_L_AND, BP_L_OR, BP_L_XOR,
    // flusso / macro
    BP_L_IF, BP_FLOW_FOR, BP_FLOW_FOREACH, BP_FLOW_SEQ, BP_FLOW_DOONCE, BP_FLOW_FLIPFLOP, BP_FLOW_GATE,
    // funzioni
    BP_FN_ENTRY,
    // aggiunte v3 (in coda per compatibilita' di serializzazione)
    BP_M_DIV, BP_BREAK_V3, BP_BREAK_V2, BP_VAL_VEC2,
    BP_REROUTE, BP_REROUTE_EX,
    // transform / componenti (input oggetto: scollegato = me stesso)
    BP_GET_COMPONENT, BP_DIR_FWD, BP_DIR_RIGHT, BP_DIR_UP,
    BP_WLOC, BP_WROT, BP_LLOC, BP_LROT,
    BP_SET_WLOC, BP_SET_WROT, BP_SET_LLOC, BP_SET_LROT,
    // input assi (mouse / coppie di tasti)
    BP_EV_AXIS, BP_VAL_AXIS,
    // matematica aggiuntiva
    BP_M_DOT, BP_M_MOD, BP_M_NORM, BP_M_DIST,
    // interpolazioni (in coda per compatibilita' di serializzazione)
    BP_M_LERP, BP_M_FINTERP, BP_M_VINTERP,
    // tracce (raycast / sphere cast)
    BP_TRACE_LINE, BP_TRACE_SPHERE,
    // matematica / trasformazione spaziale (in coda per compatibilita')
    BP_M_ABS, BP_M_POW, BP_M_CROSS,
    BP_TF_INV_DIR, BP_TF_DIR, BP_TF_INV_LOC, BP_TF_LOC,
    // transform (comporre / scomporre)
    BP_MAKE_TF, BP_BREAK_TF,
    // accesso ai componenti: renderer (colore) e fisica (tipo dinamica/statica)
    BP_GET_COLOR, BP_SET_MATCOLOR, BP_SET_PHYSTYPE, BP_GET_PHYSTYPE,
    // inverse transform su un valore Transform (non un oggetto)
    BP_TF_INV_DIR_T, BP_TF_INV_LOC_T,
    // event dispatcher: bind di un evento (via pin delegate) e chiamata del dispatcher
    BP_BIND_EVENT, BP_CALL_DISPATCH,
    // azioni latenti e timer (in coda per compatibilita' dei file esistenti)
    BP_TIMER_SET, BP_TIMER_PAUSE, BP_TIMER_UNPAUSE, BP_TIMER_CLEAR,
    BP_FLOW_DELAY, BP_FLOW_RETRIGGER_DELAY,
    // asset Curve: campiona un valore float al tempo richiesto
    BP_CURVE_EVAL,
    // timer v2: validita' handle e callback tramite riferimento funzione
    BP_TIMER_IS_VALID, BP_TIMER_SET_FUNC,
    // Audio Source runtime control
    BP_AUDIO_PLAY, BP_AUDIO_STOP, BP_AUDIO_SET_VOLUME,
    BP_AUDIO_SET_CLIP, BP_AUDIO_FADE_IN, BP_AUDIO_FADE_OUT,
    BP_CREATE_EVENT, BP_M_CLAMP_FLOAT,
    // costante matematica (in coda per compatibilita' di serializzazione)
    BP_M_PI,
    // spawn / enum / construction (sempre in coda: id precedenti stabili)
    BP_SPAWN_PREFAB, BP_SELECT_ENUM, BP_SWITCH_ENUM, BP_EV_CONSTRUCT,
    // Navigation / AI Agent control
    BP_AI_SET_TARGET, BP_AI_SET_DESTINATION, BP_AI_SET_SPEED, BP_AI_SET_STOPPED,
    BP_AI_REMAINING_DISTANCE, BP_AI_HAS_PATH,
    // decoupled scene queries
    BP_FIND_BY_TAG, BP_GET_ALL_WITH_CLASS, BP_GET_ALL_WITH_TAG,
    // Unreal-style gameplay framework references
    BP_GET_GAME_MODE, BP_GET_GAME_INSTANCE, BP_GET_PLAYER_CONTROLLER, BP_GET_PLAYER_PAWN,
    // persistent SaveGame slots (serialize variables of a Blueprint object)
    BP_SAVE_GAME_SLOT, BP_LOAD_GAME_SLOT, BP_SAVE_GAME_EXISTS,
    BP_CREATE_SAVE_GAME,
    // overlap events and component delegate bindings
    BP_EV_BEGIN_OVERLAP, BP_EV_END_OVERLAP,
    BP_BIND_BEGIN_OVERLAP, BP_BIND_END_OVERLAP,
    // Animator Controller parameters
    BP_ANIM_SET_FLOAT, BP_ANIM_SET_BOOL, BP_ANIM_SET_TRIGGER,
    // explicit string conversions and float truncation
    BP_FLOAT_TO_STRING, BP_INT_TO_STRING, BP_BOOL_TO_STRING, BP_M_TRUNCATE,
    BP_ANIM_BIND_TRIGGER,
    // runtime type query against Blueprint Interface assets
    BP_DOES_IMPLEMENT_INTERFACE,
    // Unreal-style safe interface function invocation on another Object
    BP_INTERFACE_MESSAGE,
    // persistent Unity-style event list configured on a scene object
    BP_INVOKE_INSPECTOR_EVENT,
    // safe runtime cast from Object to a native component or Blueprint class
    BP_CAST_TO_CLASS,
    // accesso contestuale a un membro pubblico del Blueprint referenziato.
    // choice: 0 get var, 1 set var, 2 call impura, 3 call pura, 4 custom event
    BP_MEMBER_ACCESS,
    // extra math (pure) + flow control (in coda: id precedenti stabili)
    BP_M_ACOS, BP_M_ATAN2, BP_M_LOOKAT, BP_M_CEIL, BP_M_FLOOR, BP_M_FRAC,
    BP_FLOW_MULTIGATE, BP_FLOW_DON,
    // set the object's scale (the engine has no hierarchical scale: this is the local scale)
    BP_SET_SCALE,
    // Physics Constraint: set the two connected objects on a constraint entity.
    // Leaving an object pin unset (0) keeps that side unchanged.
    BP_SET_CONSTRAINT_OBJECTS,
    // Level streaming: load another scene (replacing the current one) and query
    // the running level's name. Level name resolves to a .imp under the project.
    BP_OPEN_LEVEL, BP_GET_CURRENT_LEVEL,
    // UMG-style UI widgets driven from Blueprint: create an instance from a .wgt,
    // show/hide it on the viewport, and set a named element's text/value at runtime.
    BP_CREATE_WIDGET, BP_ADD_WIDGET_VIEWPORT, BP_REMOVE_WIDGET_VIEWPORT,
    BP_SET_WIDGET_TEXT, BP_SET_WIDGET_VALUE,
    // Widget Blueprint events. A widget graph has no Begin Play: it is built and
    // then reacts to the pointer, exactly like UMG.
    BP_EV_W_INITIALIZED, BP_EV_W_PRECONSTRUCT, BP_EV_W_CONSTRUCT,
    BP_EV_W_MOUSE_ENTER, BP_EV_W_MOUSE_LEAVE, BP_EV_W_MOUSE_DOWN, BP_EV_W_MOUSE_UP,
    BP_EV_W_TICK,        // per-frame while the widget is on the viewport
    BP_L_STREQ,          // string equality (Compare only handles numbers)
    // Read/write any component property by name (Percent, Font Size, Color, ...).
    // One pair per value kind so the pins stay typed.
    BP_GET_WIDGET_NUM, BP_SET_WIDGET_NUM,
    BP_GET_WIDGET_STR, BP_SET_WIDGET_STR,
    BP_GET_WIDGET_COLOR, BP_SET_WIDGET_COLOR,
    BP_GET_WIDGET_BOOL, BP_SET_WIDGET_BOOL,
    // Direct slot setters. Same reach as Set Widget Number, but each one names
    // the property it writes and offers the right drop-down, so no combo hunting.
    // Alignment applies wherever the parent lays its children out; Anchor and
    // Pivot only bite in a Canvas slot (see widgetSlotKind).
    BP_SET_WIDGET_PERCENT, BP_SET_WIDGET_HALIGN, BP_SET_WIDGET_VALIGN,
    BP_SET_WIDGET_ANCHOR, BP_SET_WIDGET_PIVOT,
    BP_SET_WIDGET_RANGE,   // Progress Bar / Slider Min..Max in one node
    // Direct readers, one per direct setter (plus the everyday reads), so a
    // component property never needs the generic Get-with-a-combo node.
    BP_GET_WIDGET_PERCENT, BP_GET_WIDGET_RANGE,
    BP_GET_WIDGET_HALIGN, BP_GET_WIDGET_VALIGN, BP_GET_WIDGET_ANCHOR, BP_GET_WIDGET_PIVOT,
    BP_GET_WIDGET_TEXT, BP_GET_WIDGET_VISIBLE, BP_GET_WIDGET_ENABLED, BP_GET_WIDGET_OPACITY,
    BP_GET_WIDGET_SIZE, BP_GET_WIDGET_POSITION, BP_GET_WIDGET_COLOR_DIRECT,
    BP_TYPE_COUNT,
};

// true for the events that only exist inside a Widget Blueprint graph
inline bool bpIsWidgetEvent(int def) {
    return (def >= BP_EV_W_INITIALIZED && def <= BP_EV_W_MOUSE_UP) || def == BP_EV_W_TICK;
}
// widget events carrying the name of the element under the pointer on out pin 1
inline bool bpIsWidgetPointerEvent(int def) {
    return def >= BP_EV_W_MOUSE_ENTER && def <= BP_EV_W_MOUSE_UP;
}
// true for the actor events that a Widget Blueprint graph must not offer
inline bool bpIsActorOnlyEvent(int def) {
    return def == BP_EV_START || def == BP_EV_TICK || def == BP_EV_HIT ||
           def == BP_EV_CONSTRUCT || def == BP_EV_BEGIN_OVERLAP || def == BP_EV_END_OVERLAP;
}

#define BP_MAX_PINS 16

struct BPPinDef { const char* name; PinKind kind; };

struct BPNodeDef {
    const char* key;
    const char* title;
    int category;            // 0 eventi 1 azioni 2 valori 3 variabili 4 matematica 5 logica 6 flusso
    BPPinDef ins[BP_MAX_PINS];
    int nIns;
    BPPinDef outs[BP_MAX_PINS];
    int nOuts;
    int propKind;            // 0 none, 1 number, 2 key choice, 3 compare op
    bool usesName;           // node references a name (variable / event / function / object)
    bool isEvent;
};

const BPNodeDef* bpDefs();
int bpDefByKey(const char* key);
extern const char* BP_KEY_NAMES[];
extern const int BP_KEY_VKS[];
extern const int BP_NKEYS;
extern const char* BP_CMP_OPS[];
extern const char* BP_COMP_NAMES[];   // component classes for Get Component
extern const int BP_NCOMPS;
extern const char* BP_AXIS_NAMES[];   // input axes (mouse / key pairs)
extern const int BP_NAXES;
extern const char* BP_CAT_NAMES[];
extern const int BP_NCATS;

struct BPEnumAsset {
    std::vector<std::string> values = { "Value0", "Value1" };
    std::string serialize() const;
    bool deserialize(const std::string& text);
};

struct BPNode {
    int id = 0;
    int def = 0;
    float x = 0, y = 0;
    float prop = 0;
    int choice = 0;
    char sname[96] = "";     // referenced name/path (var/event/function/object/curve)
    Vec3 lit[BP_MAX_PINS];
    float litAlpha[BP_MAX_PINS] = {};
    std::string slit[BP_MAX_PINS];   // literal di testo per-pin (input string)
};

struct BPLink { int fromNode = 0, fromPin = 0, toNode = 0, toPin = 0; };

// zona di commento (dietro ai nodi): testo, colore e dimensione font personalizzabili
struct BPComment {
    float x = 0, y = 0, w = 240, h = 130;
    Vec3 color{ 0.24f, 0.5f, 0.85f };
    float fontSize = 1.5f;
    char text[96] = "Commento";
};

struct BPCanvas {
    std::vector<BPNode> nodes;
    std::vector<BPLink> links;
    std::vector<BPComment> comments;
    int nextId = 1;

    BPNode* byId(int id);
    const BPNode* byId(int id) const;
    int addNode(int def, float x, float y);
    void removeNode(int id);
    bool detachLinkAtPin(int node, int pin, bool outputPin, BPLink& detached);
    void connect(int fromNode, int fromPin, int toNode, int toPin);
    const BPLink* linkInto(int node, int pin) const;
    const BPLink* linkFromExec(int node, int pin) const;
    void clear() { nodes.clear(); links.clear(); comments.clear(); nextId = 1; }
};

// ── variables ──
enum VarScope { VS_PUBLIC = 0, VS_PROTECTED, VS_PRIVATE };
enum VarContainer { VC_SINGLE = 0, VC_ARRAY, VC_MAP }; // lista = array
enum BPClassKind { BP_CLASS_ACTOR = 0, BP_CLASS_GAMEMODE, BP_CLASS_GAMEINSTANCE, BP_CLASS_PLAYERCONTROLLER, BP_CLASS_SAVEGAME };
extern const char* BP_CLASS_KIND_NAMES[];
extern const char* BP_SCOPE_NAMES[];
extern const char* BP_CONT_NAMES[];
extern const char* BP_VARTYPE_NAMES[]; // num/vec/bool/ent

struct BPVarDef {
    char name[32] = "var";
    PinKind type = PIN_NUM;          // NUM/INT/BOOL/VEC2/VEC/STR/ENT
    VarContainer container = VC_SINGLE;
    VarScope scope = VS_PUBLIC;
    bool expose = true;              // visible in Details, independently from access scope
    bool exposeOnSpawn = false;      // dynamic input pin on Spawn Prefab
    Vec3 def;                        // default (num/int/bool in x, vec2 in xy, position se transform)
    Vec3 defRot;                     // transform: rotazione di default (euler gradi)
    Vec3 defScl{ 1, 1, 1 };          // transform: scala di default
    float defAlpha = 1.0f;           // Color: alpha di default
    char strDef[64] = "";            // default for PIN_STR
    // Optional class constraint for Object references. Empty accepts every
    // scene object; component:* and blueprint:* are editor/runtime metadata.
    char refClass[96] = "";
    char enumAsset[96] = "";        // .enum asset when type == PIN_ENUM
    char assetPath[192] = "";       // .anim/.animctrl resource reference
    bool requiredGenerated = false; // generated from Blueprint Settings: readable, never settable/deletable
    int requiredIndex = -1;         // index into BPGraph::requiredComponents
    // generated from the Widget designer (a component flagged "Is Variable"):
    // gettable only — never renamed, set or deleted from the graph
    bool widgetGenerated = false;
    char widgetType[32] = "";       // component type shown on the row, e.g. "Button"
    char category[32] = "";         // My Blueprint subcategory (empty = uncategorized)
};

enum BPRequiredKind {
    BP_REQ_MESH = 0, BP_REQ_RIGID_BODY, BP_REQ_TRIGGER, BP_REQ_LIGHT, BP_REQ_CAMERA,
    BP_REQ_AUDIO, BP_REQ_REVERB, BP_REQ_AI_AGENT, BP_REQ_NAV_OCCLUDER,
    BP_REQ_ANIMATOR, BP_REQ_BLUEPRINT, BP_REQ_COUNT
};

struct BPRequiredComponent {
    BPRequiredKind kind = BP_REQ_MESH;
    std::string blueprintAsset;      // used only by BP_REQ_BLUEPRINT
    char variableName[32] = "MeshRenderer";
};

// a user-defined function pin (input or output of a function)
struct BPFuncPin {
    char name[24] = "in";
    PinKind kind = PIN_NUM;
};

struct BPFunc {
    char name[32] = "Funzione";
    bool pure = false;             // pure: nessun pin exec, valutata quando serve un output
    VarScope scope = VS_PUBLIC;    // accesso da altri Blueprint (indipendente dall'Inspector)
    char category[32] = "";        // My Blueprint subcategory (empty = uncategorized)
    BPCanvas body;
    std::vector<BPFuncPin> ins;    // function inputs  (Entry outputs / Call inputs)
    std::vector<BPFuncPin> outs;   // function outputs (Return inputs / Call outputs)
    // default = legacy signature (p1, p2 → value), so old graphs still work;
    // the editor clears these for freshly created functions (only exec pin)
    BPFunc() {
        BPFuncPin a, b, r;
        snprintf(a.name, sizeof(a.name), "p1"); a.kind = PIN_NUM;
        snprintf(b.name, sizeof(b.name), "p2"); b.kind = PIN_NUM;
        snprintf(r.name, sizeof(r.name), "value"); r.kind = PIN_ANY;
        ins.push_back(a); ins.push_back(b); outs.push_back(r);
    }
};

static const int BP_MAX_FUNC_PINS = 5;   // 1 exec + 5 = BP_MAX_PINS

// firma di un Custom Event: i suoi parametri diventano pin dati in uscita sull'evento
// e pin dati in ingresso su Chiama Evento (come gli input di una funzione)
struct BPEventDef {
    char name[32] = "";
    VarScope scope = VS_PUBLIC;    // richiamabile da altri Blueprint solo se public
    std::vector<BPFuncPin> params;
};

struct BPDispatcherDef {
    char name[32] = "Dispatcher";
    char category[32] = "";        // My Blueprint subcategory (empty = uncategorized)
    std::vector<BPFuncPin> params; // values broadcast by Call and exposed by bound Custom Events
};

struct BPGraph {
    BPClassKind classKind = BP_CLASS_ACTOR;
    std::string parentAsset;          // optional parent .bp, relative to the project
    std::string defaultPawnClass;     // GameMode: Blueprint class used as Player Pawn
    std::string playerControllerClass;// GameMode: PlayerController Blueprint class
    std::vector<std::string> defaultTags; // applied to Actor instances at Play
    std::vector<BPFunc> graphs;      // event graphs; [0] = "EventGraph", sempre presente
    std::vector<BPFunc> funcs;
    std::vector<BPVarDef> vars;
    std::vector<BPEventDef> events;  // firme dei custom event (per nome)
    std::vector<std::string> interfaces;
    std::vector<std::string> interfaceAssets; // .bpi implementate nei Blueprint Settings
    std::vector<BPDispatcherDef> dispatchers;
    bool uniquePerObject = true;
    std::vector<BPRequiredComponent> requiredComponents;

    BPGraph() { ensureDefaults(); }
    void ensureDefaults() {
        if (graphs.empty()) {
            BPFunc gph;
            snprintf(gph.name, sizeof(gph.name), "EventGraph");
            graphs.push_back(gph);
        }
        bool hasConstruction = false;
        for (const BPFunc& gph : graphs)
            if (strcmp(gph.name, "ConstructionScript") == 0) hasConstruction = true;
        if (!hasConstruction) {
            BPFunc construction;
            snprintf(construction.name, sizeof(construction.name), "ConstructionScript");
            construction.body.addNode(BP_EV_CONSTRUCT, 40, 80);
            graphs.push_back(construction);
        }
    }
    BPCanvas& main() { return graphs[0].body; }
    const BPCanvas& main() const { return graphs[0].body; }

    BPVarDef* findVar(const char* name);
    BPFunc* findFunc(const char* name);
    BPEventDef* findEvent(const char* name);
    BPDispatcherDef* findDispatcher(const char* name);
    void syncRequiredVariables();
    // Mirror the Widget designer's "Is Variable" components into read-only vars.
    // `members` is (component name, component type name), in hierarchy order.
    void syncWidgetVariables(const std::vector<std::pair<std::string, std::string>>& members);
    void clear() {
        classKind = BP_CLASS_ACTOR;
        parentAsset.clear();
        defaultPawnClass.clear();
        playerControllerClass.clear();
        defaultTags.clear();
        graphs.clear();
        ensureDefaults();
        vars.clear();
        funcs.clear();
        events.clear();
        interfaces.clear();
        interfaceAssets.clear();
        dispatchers.clear();
        uniquePerObject = true;
        requiredComponents.clear();
    }

    std::string serialize() const;
    bool deserialize(const std::string& text);
};

// ── runtime ──
struct BPValue {
    float num = 0;
    Vec3 vec;                        // vettore, o posizione se kind == PIN_TRANSFORM
    Vec3 rot, scl{ 1, 1, 1 };        // transform: rotazione (euler gradi) e scala
    bool b = false;
    int ent = 0;                     // entity id
    std::string str;
    PinKind kind = PIN_NUM;
    float alpha = 1.0f;
    bool isVec() const { return kind == PIN_VEC || kind == PIN_VEC2; }
    float asNum() const { return kind == PIN_BOOL ? (b ? 1.0f : 0.0f) : (isVec() ? vec.length() : num); }
    // a Color is an RGB triple in `vec` plus `alpha`; without it here every
    // asVec() on a colour value silently returned {num,num,num} == black
    Vec3 asVec() const { return (isVec() || kind == PIN_TRANSFORM || kind == PIN_COLOR) ? vec : Vec3{ num, num, num }; }
    bool asBool() const { return kind == PIN_BOOL ? b : asNum() != 0; }
    int asEnt() const { return kind == PIN_ENT ? ent : (int)num; }
    int asTimerHandle() const { return (int)num; }
    static BPValue N(float v) { BPValue x; x.kind = PIN_NUM; x.num = v; return x; }
    static BPValue I(int v) { BPValue x; x.kind = PIN_INT; x.num = (float)v; return x; }
    // a widget instance reference; the handle rides in `num` like an int, but the
    // kind keeps it out of numeric pins
    static BPValue W(int handle) { BPValue x; x.kind = PIN_WIDGET; x.num = (float)handle; return x; }
    static BPValue V(Vec3 v) { BPValue x; x.kind = PIN_VEC; x.vec = v; return x; }
    static BPValue V2(float vx, float vy) { BPValue x; x.kind = PIN_VEC2; x.vec = { vx, vy, 0 }; return x; }
    static BPValue B(bool v) { BPValue x; x.kind = PIN_BOOL; x.b = v; return x; }
    static BPValue E(int id) { BPValue x; x.kind = PIN_ENT; x.ent = id; return x; }
    static BPValue S(const std::string& s) { BPValue x; x.kind = PIN_STR; x.str = s; return x; }
    static BPValue T(Vec3 pos, Vec3 r, Vec3 s) { BPValue x; x.kind = PIN_TRANSFORM; x.vec = pos; x.rot = r; x.scl = s; return x; }
    static BPValue H(int id) { BPValue x; x.kind = PIN_TIMER_HANDLE; x.num = (float)id; return x; }
    static BPValue En(int value) { BPValue x; x.kind = PIN_ENUM; x.num = (float)value; return x; }
    static BPValue C(Vec3 rgb,float a=1.0f) { BPValue x;x.kind=PIN_COLOR;x.vec=rgb;x.alpha=a;return x; }
    static BPValue Asset(PinKind kind, const std::string& path) { BPValue x; x.kind = kind; x.str = path; return x; }
};

struct BPContext {
    Entity* entity = nullptr;
    EditorScene* scene = nullptr;
    float dt = 1.0f / 60.0f;
    double time = 0;
    const bool* keysDown = nullptr;
    float eventImpulse = 0;
    int eventKey = -1;
    int eventOther = 0;              // entity id from collision
    int gameModeEntity = 0;
    int gameInstanceEntity = 0;
    int playerControllerEntity = 0;
    int playerPawnEntity = 0;
    const float* axisValues = nullptr;   // per-frame axis values (BP_NAXES entries)
    void (*log)(int, const char*, ...) = nullptr;
    void (*printString)(Entity*,const char*,const Vec3&,float) = nullptr;
    void (*requestDestroy)(Entity*) = nullptr;
    void (*sendMessage)(int targetEntityId, const char* eventName) = nullptr;
    // trace debug drawing (Unreal-style): start, end, radius (0=line), hit, hit point
    void (*drawDebugTrace)(const Vec3&, const Vec3&, float, bool, const Vec3&) = nullptr;
    float (*evalCurve)(const char* relativePath, float time) = nullptr;
    void (*playAudio)(Entity*) = nullptr;
    void (*stopAudio)(Entity*) = nullptr;
    void (*setAudioVolume)(Entity*, float) = nullptr;
    void (*setAudioClip)(Entity*, const char*) = nullptr;
    void (*fadeInAudio)(Entity*, float) = nullptr;
    void (*fadeOutAudio)(Entity*, float) = nullptr;
    int (*spawnPrefab)(const char* relativePath, const BPValue& transform,
                       const std::vector<BPValue>& exposedValues) = nullptr;
    void (*aiSetTarget)(Entity*, int targetEntityId) = nullptr;
    void (*aiSetDestination)(Entity*, const Vec3&) = nullptr;
    void (*aiSetSpeed)(Entity*, float) = nullptr;
    void (*aiSetStopped)(Entity*, bool) = nullptr;
    float (*aiRemainingDistance)(Entity*) = nullptr;
    bool (*aiHasPath)(Entity*) = nullptr;
    bool (*saveGameSlot)(int objectId, const char* slot) = nullptr;
    bool (*loadGameSlot)(int objectId, const char* slot) = nullptr;
    bool (*saveGameExists)(const char* slot) = nullptr;
    int (*createSaveGame)(const char* classPath) = nullptr;
    void (*openLevel)(const char* levelName) = nullptr;   // load another scene (replaces current)
    const char* currentLevelName = "";                    // name of the running level
    // set while a widget's own graph runs: the handle of that widget (so the
    // widget nodes can leave their Widget pin unwired and mean "this one") and
    // the element under the pointer for the mouse events.
    int selfWidget = 0;
    const char* eventWidgetElement = "";
    int (*createWidget)(const char* assetPath) = nullptr; // instantiate a .wgt → handle
    void (*addWidgetToViewport)(int handle) = nullptr;
    void (*removeWidgetFromViewport)(int handle) = nullptr;
    void (*setWidgetText)(int handle, const char* element, const char* text) = nullptr;
    void (*setWidgetValue)(int handle, const char* element, float value) = nullptr;
    // Any component property by name. Get returns false when the widget, the
    // element or the property does not exist, leaving the output at its default.
    bool (*getWidgetNumber)(int handle, const char* element, const char* prop, float& out) = nullptr;
    void (*setWidgetNumber)(int handle, const char* element, const char* prop, float value) = nullptr;
    bool (*getWidgetString)(int handle, const char* element, const char* prop, std::string& out) = nullptr;
    void (*setWidgetString)(int handle, const char* element, const char* prop, const char* value) = nullptr;
    bool (*getWidgetColor)(int handle, const char* element, const char* prop, Vec3& rgb, float& alpha) = nullptr;
    void (*setWidgetColor)(int handle, const char* element, const char* prop, const Vec3& rgb, float alpha) = nullptr;
    bool (*getWidgetBool)(int handle, const char* element, const char* prop, bool& out) = nullptr;
    void (*setWidgetBool)(int handle, const char* element, const char* prop, bool value) = nullptr;
    void (*setAnimatorParameter)(Entity*, const char* name, int type, float value) = nullptr;
    void (*bindAnimationTrigger)(Entity* listener, Entity* animator, const char* trigger, const char* customEvent) = nullptr;
    std::vector<BPValue> (*callInterfaceMessage)(int targetEntityId,const char* interfaceAsset,
                                                 const char* functionName,const std::vector<BPValue>& args) = nullptr;
    BPValue (*getBlueprintMember)(int targetEntityId,const char* classAsset,const char* memberName) = nullptr;
    bool (*setBlueprintMember)(int targetEntityId,const char* classAsset,const char* memberName,const BPValue& value) = nullptr;
    std::vector<BPValue> (*callBlueprintMember)(int targetEntityId,const char* classAsset,
                                                const char* functionName,const std::vector<BPValue>& args) = nullptr;
    void (*fireBlueprintMemberEvent)(int targetEntityId,const char* classAsset,
                                     const char* eventName,const std::vector<BPValue>& args) = nullptr;
    void (*invokeInspectorEvent)(Entity* target,const char* eventName) = nullptr;
    // ── Event Dispatchers across Blueprints / Widgets ──
    // Register `eventName` (a Custom Event on the caller) against `dispatcher`,
    // which belongs to another instance: `targetWidget` addresses a runtime
    // widget, otherwise `targetEntity` addresses an object's Blueprint. Returns
    // false when the target, the dispatcher or the signature does not line up.
    bool (*bindDispatcher)(int targetEntity, int targetWidget, const char* dispatcher,
                           int listenerEntity, int listenerWidget, const char* eventName) = nullptr;
    // Fire a bound event back on the listener's own instance when the owner
    // calls the dispatcher.
    void (*fireDispatcherEvent)(int listenerEntity, int listenerWidget, const char* eventName,
                                const std::vector<BPValue>& args) = nullptr;
    // ── members of a Widget reached through a variable (its class is "widget:…") ──
    // Same shape as the Blueprint member callbacks, but the target is a widget
    // instance handle rather than an entity id.
    BPValue (*getWidgetMember)(int widgetHandle, const char* memberName) = nullptr;
    bool (*setWidgetMember)(int widgetHandle, const char* memberName, const BPValue& value) = nullptr;
    std::vector<BPValue> (*callWidgetMember)(int widgetHandle, const char* functionName,
                                             const std::vector<BPValue>& args) = nullptr;
    void (*fireWidgetMemberEvent)(int widgetHandle, const char* eventName,
                                  const std::vector<BPValue>& args) = nullptr;
};

// Resolves a Blueprint together with its parent chain. Child variables and
// functions override same-named parent members; event graphs from both remain active.
bool bpLoadResolvedGraph(const std::string& projectDir, const std::string& relativePath, BPGraph& out);
bool bpResolveBlueprintAssetPath(const std::string& projectDir, const std::string& requestedPath,
                                 std::string& resolvedPath);
// A member target's class is "blueprint:<rel.bp>" or "widget:<rel.wgt>"; the
// widget variant is loaded from the graph half of the .wgt.
bool bpMemberClassIsWidget(const std::string& classSpec);
bool bpLoadWidgetGraph(const std::string& projectDir, const std::string& classSpec, BPGraph& out);
// The class a pin carries ("blueprint:…", "widget:…", "component:N"), empty when
// it is not a reference. This is what turns a dragged wire into a contextual
// member list, so a reference type only "behaves like an object" if it answers here.
std::string bpPinRefClass(const BPCanvas& cv, const BPGraph& graph,
                          int nodeId, int pin, bool isOut, int depth = 0);
// Load the graph a reference class names ("blueprint:<rel.bp>" / "widget:<rel.wgt>")
bool bpLoadClassGraph(const std::string& projectDir, const std::string& classSpec, BPGraph& out);
// The dispatcher a Bind node targets: looked up in the class wired into its
// Target pin, or in this Blueprint when Target is unwired.
bool bpFindBindDispatcher(const std::string& projectDir, const BPGraph& local,
                          const BPCanvas& cv, const BPNode& bindNode, BPDispatcherDef& out);
// effective kind of a pin, after wildcards and variable types are resolved
PinKind bpEffKind(const BPCanvas& cv, const BPGraph& g, int nodeId, int pin, bool isOut, int depth);
// may these two pin kinds be wired together?
bool bpPinKindsCompatible(PinKind a, PinKind b);
// the pin layout a member-access node ends up with (its Target kind depends on
// whether the class is a Blueprint or a Widget)
BPNodeDef bpNodeDefForTest(const std::string& projectDir, const BPNode& node);

// Shared node clipboard used by every open Blueprint editor tab. Event
// signatures are copied with the nodes so their dynamic pins survive when
// pasting into another Blueprint asset.
void bpCopyNodesToClipboard(const BPGraph& sourceGraph, const BPCanvas& source,
                            const std::set<int>& nodeIds);
std::vector<int> bpPasteNodesFromClipboard(BPGraph& targetGraph, BPCanvas& target,
                                           float worldX, float worldY);
bool bpNodeClipboardEmpty();

struct BPVarStore {
    BPValue single;
    std::vector<BPValue> arr;
    std::map<std::string, BPValue> mapv;
};

// result of a Line/Sphere Trace node (stored per node when its exec runs)
struct BPTraceResult {
    bool hit = false;
    Vec3 point, normal;
    int actor = 0;                   // entity id hit (0 = none)
};

struct BPFrame {
    std::vector<BPValue> params;     // function inputs, in signature order
    std::vector<BPValue> rets;       // function outputs, in signature order
    bool returned = false;
    std::map<std::string, BPValue> locals;
};

class BPInstance {
public:
    BPGraph* graph = nullptr;
    Entity* entity = nullptr;
    bool dead = false;
    std::map<std::string, BPVarStore> vars;

    void initVars(const std::map<std::string, Vec3>* overrides,
                  const std::map<std::string, float>* alphaOverrides = nullptr);
    // risolve i riferimenti a oggetti assegnati alle variabili Transform esposte
    // (override x = id oggetto → transform vivo dell'oggetto); chiamare dopo initVars
    void applyRefOverrides(const std::map<std::string, Vec3>* overrides, EditorScene* scene);
    void fire(int evType, BPContext& ctx, int outPin = 0);
    void fireCustom(const char* name, BPContext& ctx);
    void fireCustomWithArgs(const char* name,const std::vector<BPValue>& args,BPContext& ctx);
    void fireOverlapBinding(bool begin, int componentId, int otherActorId, BPContext& ctx);
    std::vector<BPValue> callNamedFunction(const char* name,const std::vector<BPValue>& args,BPContext& ctx);

    // ── Event Dispatchers, including across Blueprints and Widgets ──
    // A binding always lives on the instance that OWNS the dispatcher, and
    // remembers who is listening. A listener of 0/0 is the owner itself (the
    // ordinary local bind); otherwise the event is fired back on the listener's
    // own instance when the dispatcher is called.
    struct BPDispatchBinding {
        std::string eventName;      // Custom Event on the listener
        int listenerEntity = 0;     // 0 with listenerWidget 0 = bound to itself
        int listenerWidget = 0;     // runtime widget handle
    };
    // Register a listener against `dispatcher`. Returns false when this instance
    // has no such dispatcher, or when the listener's event does not match its
    // signature — `why` then carries a message worth logging.
    bool bindDispatcher(const char* dispatcher, int listenerEntity, int listenerWidget,
                        const char* eventName, const BPGraph* listenerGraph, std::string* why = nullptr);
    const BPDispatcherDef* findDispatcherDef(const char* name) const;
    // Evaluate one output pin of a pure node — the self-tests read component
    // getters through exactly the path the runtime uses.
    BPValue evalOutForTest(const BPCanvas& cv, const BPNode& n, int outPin, BPContext& ctx) {
        return evalOut(cv, n, outPin, ctx, 0);
    }

private:
    std::map<int, float> nodeState_;         // doonce / flipflop / gate
    std::map<int, std::vector<BPValue>> lastRets_;  // call-node id → last returns
    std::map<int, BPTraceResult> traceResults_;     // trace-node id → last hit
    std::map<int, BPValue> loopEl_;          // foreach element per node
    std::map<int, float> loopIdx_;           // for/foreach index per node
    std::map<int, int> spawnResults_;
    std::vector<BPFrame> frames_;
    std::vector<BPValue> curEventArgs_;      // argomenti del custom event in esecuzione
    std::map<std::string, std::vector<BPDispatchBinding>> dispatchBindings_;  // dispatcher → listeners
    struct BPOverlapBinding { bool begin = true; int componentId = 0; std::string eventName; };
    std::vector<BPOverlapBinding> overlapBindings_;

    struct BPDelayState {
        const BPCanvas* canvas = nullptr;
        int nodeId = 0;
        float remaining = 0;
    };
    struct BPTimerState {
        const BPCanvas* canvas = nullptr;
        int nodeId = 0;
        float remaining = 0;
        float rate = 0;
        bool looping = false;
        bool paused = false;
        std::string eventName;
        std::string functionName;
    };
    std::map<std::pair<const BPCanvas*, int>, BPDelayState> delays_;
    std::map<int, BPTimerState> timers_;
    std::map<std::pair<const BPCanvas*, int>, int> timerNodeHandles_;
    int nextTimerHandle_ = 1;

    void execChain(const BPCanvas& cv, int nodeId, int outPin, BPContext& ctx, int depth);
    bool execNode(const BPCanvas& cv, BPNode& n, BPContext& ctx, int depth, int& nextOut);
    BPValue evalOut(const BPCanvas& cv, const BPNode& n, int outPin, BPContext& ctx, int depth);
    BPValue readIn(const BPCanvas& cv, const BPNode& n, int pinIdx, BPContext& ctx, int depth);
    int timerHandleFor(const BPCanvas& cv, int nodeId);
    void updateLatent(BPContext& ctx);
    std::vector<BPValue> callFunction(BPFunc& fn, const std::vector<BPValue>& args, BPContext& ctx, int depth);
    BPVarStore* store(const char* name);
};

// ── node canvas editor ──
class BPEditor {
public:
    BPGraph graph;
    std::string curPath;
    std::string projectDir;
    void* hwnd = nullptr;
    void (*logFn)(int, const char*, ...) = nullptr;
    bool assignRequested = false;
    bool dirty = false;
    // Widget Blueprint graph: offers the UMG events and hides the actor-only ones
    bool widgetMode = false;

    void draw(UI& ui);
    void openFunction(int idx) { switchCanvas(0, idx); }   // switch the canvas to a function body
    bool saveTo(const std::string& absPath);
    bool loadFrom(const std::string& absPath, const std::string& rel);
    void newGraph();
    void undo();
    void redo();
    bool canUndo() const { return !undoHistory.empty(); }
    bool canRedo() const { return !redoHistory.empty(); }
    bool wantsTextInput() const { return paletteOpen || litEditNode != 0; }
    bool listeningKey() const { return keyListenNode != 0; }   // "premi un tasto..." attivo
    bool implementInterfaceAsset(const std::string& relativePath);
    // Refuse a delegate wire whose Custom Event does not match the Dispatcher the
    // Bind node targets; `why` explains the refusal. Non-delegate links always pass.
    bool delegateLinkAllowed(const BPCanvas& cv, int fromNode, int fromPin,
                             int toNode, int toPin, std::string& why) const;
    // Drop a Custom Event's declaration once no node defines it any more, so its
    // name becomes free again. Safe to call with a name that is still in use.
    void pruneCustomEvent(const char* name);
    // A Widget Blueprint has no tool row of its own: the widget editor draws these
    // two toggles next to its Save button, so it needs to read and flip them.
    bool panelsVisible() const { return showVars; }
    void togglePanels() { showVars = !showVars; }
    bool settingsVisible() const { return settingsOpen; }
    void toggleSettings() { settingsOpen = !settingsOpen; if (settingsOpen) showVars = true; }

private:
    int curGraph = 0;                // index into graph.graphs when curFunc < 0
    int curFunc = -1;                // >= 0: editing a function body
    float panX = 40, panY = 40;
    float zoom = 1;                  // canvas zoom (mouse wheel)
    int selNode = 0;
    std::set<int> selSet;            // multi-selection (marquee / group ops)
    bool selecting = false;          // marquee drag in corso
    float selX0 = 0, selY0 = 0;
    int dragNode = 0;
    float dragOffX = 0, dragOffY = 0;
    bool panning = false;            // RMB pan
    float lastMX = 0, lastMY = 0;
    float rmbLastX = 0, rmbLastY = 0;
    int wireNode = 0, wirePin = 0;
    bool wireFromOut = false;
    PinKind wireKind = PIN_EXEC;
    bool wiring = false;
    bool paletteOpen = false;
    float palX = 0, palY = 0, palWX = 0, palWY = 0, palScroll = 0;
    char palSearch[32] = "";
    unsigned palCatOpen = 0;         // bitmask: category submenu expanded
    std::set<std::string> palInterfaceOpen; // nested .bpi submenus below Interfaces
    bool palLinkMode = false;        // palette opened by dragging a wire onto empty canvas
    int palLinkNode = 0, palLinkPin = 0;
    bool palLinkOut = false;
    PinKind palLinkKind = PIN_EXEC;
    bool contextSensitive = true;
    std::string palLinkRefClass;    // blueprint:* / component:* carried by the dragged Object pin
    unsigned secOpen = 0x27;         // My Blueprint sections expanded (bitmask; 32 = widgets)
    int litNode = 0, litPin = 0, litComp = 0;
    bool litDragging = false;
    float litMoved = 0;              // drag distance, to tell click from drag
    int litEditNode = 0, litEditPin = 0, litEditComp = 0;   // literal being typed
    char litEditBuf[256] = "";
    // deferred name validation: while a rename field is focused the user types freely;
    // the field turns red on a duplicate/empty name and only reverts to the committed
    // value on commit (Enter / focus loss). Baseline = committed name, keyed by field id.
    std::map<std::string, std::string> renameBaseline_;
    bool nameField(UI& ui, const char* id, char* buf, int cap,
                   const std::function<bool(const char*)>& isDuplicate,
                   const std::function<void(const char* oldName)>& onCommit);
    int litEditCursor = 0, litEditAnchor = 0;
    bool litEditSelecting = false;
    int litLastClickFrame = -1000, litLastClickNode = 0, litLastClickPin = -1, litLastClickComp = -1;
    float rmbPressX = 0, rmbPressY = 0;
    char renameBuf[32] = "";
    int renameFor = 0;               // node id the rename buffer belongs to
    char nameSearch[32] = "";        // filter for the variable/function picker in Details
    bool traceMaskOpen = false;      // trace node: layer multi-select dropdown expanded
    bool showVars = true;
    bool settingsOpen = false;
    float varColW = 230, detColW = 260;
    bool dragVarCol = false, dragDetCol = false;
    Renderer* r_ = nullptr;          // valid during draw (node auto-sizing)
    int lastCanvasClickF = -1000;    // wire double-click detection
    float lastCanvasClickX = 0, lastCanvasClickY = 0;
    int frame_ = 0;                  // draw counter (double-click timing)
    std::vector<std::string> undoHistory;
    std::vector<std::string> redoHistory;
    bool historyGestureActive = false;
    std::string historyGestureBefore;

    void clearHistory();
    void pushUndoState(const std::string& state);
    void finishHistoryFrame(const std::string& before, bool mouseDown);

    int selComment = -1;             // comment selected (index into current canvas)
    int dragComment = -1, resizeComment = -1;
    float dragCOffX = 0, dragCOffY = 0;
    int selVar = -1;                 // variable selected in the left list (details on the right)
    int selDispatcher = -1;
    int dragVarIdx = -1;             // variable being dragged toward the canvas
    bool dragVarActive = false;
    float dragVarX = 0, dragVarY = 0;
    int dragVarOver = -1;            // list row currently targeted for reordering
    int dragFuncIdx = -1;            // function being dragged toward the canvas (→ Call Function)
    bool dragFuncActive = false;
    float dragFuncX = 0, dragFuncY = 0;
    int dragFuncOver = -1;           // list row currently targeted for reordering
    std::vector<std::string> settingsInterfaceAssets;
    std::vector<std::string> settingsInterfaceLabels;
    std::string settingsScanProject;
    int settingsScanFrame = -10000;
    int settingsInterfacePick = 0;
    int settingsImplementedPick = 0;
    int settingsInterfaceContext = -1;
    float settingsInterfaceContextX = 0;
    float settingsInterfaceContextY = 0;
    int settingsRequiredKindPick = 0;
    int settingsRequiredBlueprintPick = 0;
    std::vector<std::string> refClassValues; // cached Object class picker values
    std::vector<std::string> refClassLabels;
    std::string refClassScanProject;
    int refClassScanFrame = -10000;
    bool varMenuOpen = false;        // "Get / Set" chooser after a variable drop
    float varMenuX = 0, varMenuY = 0, varMenuWX = 0, varMenuWY = 0;
    int varMenuIdx = -1;

    int ctxKind = 0;                 // 0 none, 1 node, 2 pin
    int ctxNode = 0, ctxPin = 0;
    bool ctxPinOut = false;
    float ctxX = 0, ctxY = 0;
    int keyListenNode = 0;           // node waiting for a key press to bind

    // My Blueprint right-click menu (variable / function / dispatcher rows)
    int mbMenuKind = 0;              // 0 none, 1 var, 2 func, 3 dispatcher
    int mbMenuIdx = -1;
    float mbMenuX = 0, mbMenuY = 0;
    bool mbMenuConfirmDelete = false;   // second-level delete confirmation submenu
    // inline rename popup launched from that menu
    int mbRenameKind = 0, mbRenameIdx = -1;
    char mbRenameBuf[32] = "";
    bool mbRenameFocus = false;      // request keyboard focus on the rename field
    // subcategory grouping in the My Blueprint lists
    std::set<std::string> mbCatCollapsed;   // collapsed subcategory headings
    std::string dragOverCat;                // category heading under the dragged row
    bool dragOverCatValid = false;
    void drawMyBlueprintMenus(UI& ui);      // context menu + rename popup overlay

    BPCanvas& canvas();
    const BPCanvas& canvas() const;
    // effective node definition: dynamic pins for function Entry/Return/Call and
    // the variable-length Sequence, else a copy of the static DEFS entry
    BPNodeDef effDef(const BPNode& n) const;
    // tipo effettivo di un pin d'ingresso ai fini dell'editor inline (il valore
    // del Set assume il tipo della variabile: bool→check, string→testo, ecc.)
    PinKind editorKind(const BPNode& n, const BPNodeDef& d, int pin) const;
    void drawFuncSignature(UI& ui);              // add/remove/type function pins
    void removeFuncPin(bool input, int idx);     // drop a function pin + fix wires
    void setFunctionPure(bool pure);
    void nodeRect(const BPNode& n, float* w, float* h) const;
    bool rerouteFlipped(const BPNode& n) const;
    float pinTangentDir(const BPNode& n, bool out) const;
    void pinPos(const BPNode& n, float ox, float oy, int pin, bool out, float* px, float* py) const;
    float litOffset(const BPNodeDef& d, int p) const;
    float literalBoxWidth(const BPNode& n, int pin, PinKind kind) const;
    void drawNodeInputValues(UI& ui, BPNode& n, const BPNodeDef& d);
    bool doSaveDialog(char* out);
    void cycleName(BPNode& n, int dir);
    void drawMyBlueprint(UI& ui);
    void drawVarDetails(UI& ui);
    void drawDispatcherDetails(UI& ui);
    void drawNodeDetails(UI& ui);
    void drawBlueprintSettings(UI& ui);
    void scanInterfaceAssets();
    void syncImplementedInterfaces();
    bool isInterfaceAsset() const;
    void normalizeInterfaceAsset();
    void renameFunctionReferences(const char* oldName, const char* newName);
    void drawCommentDetails(UI& ui);
    void disconnectPin(int nodeId, int pin, bool out);
    void copySelection(BPCanvas& C);
    void pasteClipboard(BPCanvas& C, float wx, float wy);
    void deleteSelection(BPCanvas& C);
    void switchCanvas(int graphIdx, int funcIdx);
};

extern BPEditor gBPEditor;
