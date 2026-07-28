// ─── Pulse Engine blueprint v2 implementation ───
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "blueprint.h"
#include "scene.h"
#include "animation.h"
#include "glext.h"
#include "widget.h"      // component property table, for the widget property nodes
#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

BPEditor gBPEditor;
const CollisionLayers* gBPLayers = nullptr;
std::string gBPProjectDir;

// ═══ registry ═══
// i primi 11 restano in questo ordine per compatibilita' con i grafi salvati
const char* BP_KEY_NAMES[] = {
    "SPACE", "W", "A", "S", "D", "Q", "E", "SU", "DOWN", "SX", "DX",
    "B", "C", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "R", "T", "U", "V", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "SHIFT", "CTRL",
};
const int BP_KEY_VKS[] = {
    VK_SPACE, 'W', 'A', 'S', 'D', 'Q', 'E', VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
    'B', 'C', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'R', 'T', 'U', 'V', 'X', 'Y', 'Z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', VK_SHIFT, VK_CONTROL,
};
const int BP_NKEYS = 43;
const char* BP_CMP_OPS[] = { ">", "<", "=", "<=", ">=" };
const char* BP_CLASS_KIND_NAMES[] = { "Actor", "GameMode", "GameInstance", "PlayerController", "SaveGame" };
// Keep the first four indices stable for existing serialized graphs.
const char* BP_COMP_NAMES[] = { "Camera", "Luce", "Mesh Renderer", "Rigid Body", "Audio Source", "Transform", "Blueprint", "Audio Reverb Zone", "AI Agent", "Trigger", "Animator" };
const int BP_NCOMPS = 11;
static const char* BP_PHYSTYPE_NAMES[] = { "Dynamic", "Static" };   // enum Tipo Fisica (n.choice)
const char* BP_AXIS_NAMES[] = { "Mouse X", "Mouse Y", "Wheel", "A / D", "W / S", "Frecce SX/DX", "DOWN/UP arrows" };
const int BP_NAXES = 7;
const char* BP_CAT_NAMES[] = { "Events", "Actions", "Values", "Variables", "Math", "Logic", "Flow Control" };
const int BP_NCATS = 7;
const char* BP_SCOPE_NAMES[] = { "public", "protected", "private" };
const char* BP_CONT_NAMES[] = { "singola", "array", "mappa" };
// PIN_ANIMATOR_CONTROLLER remains serialized for backwards compatibility only.
// New controller references are Object variables constrained to
// asset:AnimatorController, so they share Object pins and colours.
const char* BP_VARTYPE_NAMES[] = { "exec?", "Float", "Vector3", "Bool", "Object", "any?", "Int", "Vector2", "String", "Transform", "Delegate", "Timer Handle", "Enum", "Animation Clip", "Object", "Color", "Widget" };

static bool bpIsAnimatorControllerObject(const BPVarDef& v) {
    return v.type == PIN_ENT && strcmp(v.refClass, "asset:AnimatorController") == 0;
}
// A Widget Object variable holds a widget instance handle — the same INT that
// Create Widget hands out, so it plugs straight into Add to Viewport and the
// Set/Get nodes. `refClass` carries the class: "widget:" is the generic Widget,
// "widget:<rel.wgt>" narrows it to one of the user's own widget assets, exactly
// how "blueprint:<rel.bp>" narrows an Object variable.
static bool bpIsWidgetObject(const BPVarDef& v) { return v.type == PIN_WIDGET; }

std::string BPEnumAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSO_ENUM 1\n";
    for (const std::string& value : values) o << "value " << value << "\n";
    return o.str();
}

bool BPEnumAsset::deserialize(const std::string& text) {
    if (text.rfind("IMPULSO_ENUM", 0) != 0) return false;
    values.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("value ", 0) == 0 && values.size() < 7) values.push_back(line.substr(6));
    }
    if (values.empty()) values.push_back("Value0");
    return true;
}

static const BPNodeDef DEFS[BP_TYPE_COUNT] = {
    { "ev_start", "Event BeginPlay", 0, {}, 0, { { "", PIN_EXEC } }, 1, 0, false, true },
    { "ev_tick", "Event Tick", 0, {}, 0, { { "", PIN_EXEC }, { "dt", PIN_NUM } }, 2, 0, false, true },
    { "ev_hit", "Event Hit", 0, {}, 0, { { "", PIN_EXEC }, { "impulse", PIN_NUM }, { "other", PIN_ENT } }, 3, 0, false, true },
    { "ev_key", "InputAction", 0, {}, 0, { { "Triggered", PIN_EXEC }, { "Started", PIN_EXEC }, { "Completed", PIN_EXEC }, { "Value", PIN_ANY } }, 4, 2, false, true },
    { "ev_custom", "Custom Event", 0, {}, 0, { { "", PIN_EXEC } }, 1, 0, true, true },

    { "act_imp", "Add Impulse", 1, { { "", PIN_EXEC }, { "impulse", PIN_VEC } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "act_force", "Add Force", 1, { { "", PIN_EXEC }, { "force", PIN_VEC } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "act_setvel", "Set Velocity", 1, { { "", PIN_EXEC }, { "velocity", PIN_VEC } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "act_torque", "Add Torque", 1, { { "", PIN_EXEC }, { "torque", PIN_VEC } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "act_color", "Set Color", 1, { { "", PIN_EXEC }, { "rgb", PIN_VEC } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "act_destroy", "Destroy Actor", 1, { { "", PIN_EXEC } }, 1, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "act_print", "Print String", 1, { { "", PIN_EXEC }, { "Text", PIN_STR }, { "Color", PIN_COLOR } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "call_event", "Call Event", 1, { { "", PIN_EXEC } }, 1, { { "", PIN_EXEC } }, 1, 0, true, false },
    { "send_msg", "Send Message", 1, { { "", PIN_EXEC }, { "target", PIN_ENT } }, 2, { { "", PIN_EXEC } }, 1, 0, true, false },
    { "call_func", "Call Function", 1, { { "", PIN_EXEC }, { "p1", PIN_NUM }, { "p2", PIN_NUM } }, 3, { { "", PIN_EXEC }, { "return", PIN_ANY } }, 2, 0, true, false },
    { "fn_return", "Return", 1, { { "", PIN_EXEC }, { "value", PIN_ANY } }, 2, {}, 0, 0, false, false },

    { "val_num", "Float", 2, {}, 0, { { "", PIN_NUM } }, 1, 1, false, false },
    { "val_vec", "Make Vector", 2, { { "x", PIN_NUM }, { "y", PIN_NUM }, { "z", PIN_NUM } }, 3, { { "", PIN_VEC } }, 1, 0, false, false },
    { "val_pos", "Get Actor Location", 2, {}, 0, { { "", PIN_VEC } }, 1, 0, false, false },
    { "val_vel", "Get Velocity", 2, {}, 0, { { "", PIN_VEC } }, 1, 0, false, false },
    { "val_time", "Get Game Time", 2, {}, 0, { { "s", PIN_NUM } }, 1, 0, false, false },
    { "val_keydown", "Is Key Down", 2, {}, 0, { { "", PIN_BOOL } }, 1, 2, false, false },
    { "val_random", "Random Float in Range", 2, { { "min", PIN_NUM }, { "max", PIN_NUM } }, 2, { { "", PIN_NUM } }, 1, 0, false, false },
    { "self", "Self", 2, {}, 0, { { "", PIN_ENT } }, 1, 0, false, false },
    { "find", "Find Actor", 2, {}, 0, { { "", PIN_ENT } }, 1, 0, true, false },
    { "isvalid", "Is Valid", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_BOOL } }, 1, 0, false, false },

    { "var_get", "Get", 3, {}, 0, { { "", PIN_ANY } }, 1, 0, true, false },
    { "var_set", "Set", 3, { { "", PIN_EXEC }, { "value", PIN_ANY } }, 2, { { "", PIN_EXEC }, { "", PIN_ANY } }, 2, 0, true, false },
    { "loc_get", "Get Local", 3, {}, 0, { { "", PIN_ANY } }, 1, 0, true, false },
    { "loc_set", "Set Local", 3, { { "", PIN_EXEC }, { "value", PIN_ANY } }, 2, { { "", PIN_EXEC }, { "", PIN_ANY } }, 2, 0, true, false },
    { "arr_get", "Array Get", 3, { { "index", PIN_NUM } }, 1, { { "", PIN_ANY } }, 1, 0, true, false },
    { "arr_add", "Array Add", 3, { { "", PIN_EXEC }, { "value", PIN_ANY } }, 2, { { "", PIN_EXEC } }, 1, 0, true, false },
    { "arr_len", "Array Length", 3, {}, 0, { { "", PIN_NUM } }, 1, 0, true, false },
    { "arr_rem", "Array Remove At", 3, { { "", PIN_EXEC }, { "index", PIN_NUM } }, 2, { { "", PIN_EXEC } }, 1, 0, true, false },
    { "arr_clear", "Array Clear", 3, { { "", PIN_EXEC } }, 1, { { "", PIN_EXEC } }, 1, 0, true, false },
    { "map_get", "Map Find", 3, { { "key", PIN_NUM } }, 1, { { "value", PIN_ANY }, { "found", PIN_BOOL } }, 2, 0, true, false },
    { "map_set", "Map Add", 3, { { "", PIN_EXEC }, { "key", PIN_NUM }, { "value", PIN_ANY } }, 3, { { "", PIN_EXEC } }, 1, 0, true, false },
    { "map_rem", "Map Remove", 3, { { "", PIN_EXEC }, { "key", PIN_NUM } }, 2, { { "", PIN_EXEC } }, 1, 0, true, false },
    { "map_len", "Map Length", 3, {}, 0, { { "", PIN_NUM } }, 1, 0, true, false },

    // wildcard math: A and B accept float / int / vector; the node adapts to its inputs
    { "m_add", "Add", 4, { { "A", PIN_ANY }, { "B", PIN_ANY } }, 2, { { "", PIN_ANY } }, 1, 0, false, false },
    { "m_sub", "Subtract", 4, { { "A", PIN_ANY }, { "B", PIN_ANY } }, 2, { { "", PIN_ANY } }, 1, 0, false, false },
    { "m_mul", "Multiply", 4, { { "A", PIN_ANY }, { "B", PIN_ANY } }, 2, { { "", PIN_ANY } }, 1, 0, false, false },
    { "m_sin", "Sin", 4, { { "x", PIN_NUM } }, 1, { { "", PIN_NUM } }, 1, 0, false, false },
    { "m_scalev", "Vector * Float", 4, { { "V", PIN_VEC }, { "s", PIN_NUM } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },
    { "m_addv", "Vector + Vector", 4, { { "A", PIN_VEC }, { "B", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },
    { "m_len", "Vector Length", 4, { { "V", PIN_VEC } }, 1, { { "", PIN_NUM } }, 1, 0, false, false },

    { "l_cmp", "Compare", 5, { { "A", PIN_NUM }, { "B", PIN_NUM } }, 2, { { "", PIN_BOOL } }, 1, 3, false, false },
    { "l_not", "NOT", 5, { { "a", PIN_BOOL } }, 1, { { "", PIN_BOOL } }, 1, 0, false, false },
    { "l_and", "AND", 5, { { "A", PIN_BOOL }, { "B", PIN_BOOL } }, 2, { { "", PIN_BOOL } }, 1, 0, false, false },
    { "l_or", "OR", 5, { { "A", PIN_BOOL }, { "B", PIN_BOOL } }, 2, { { "", PIN_BOOL } }, 1, 0, false, false },
    { "l_xor", "XOR", 5, { { "A", PIN_BOOL }, { "B", PIN_BOOL } }, 2, { { "", PIN_BOOL } }, 1, 0, false, false },

    { "l_if", "Branch", 6, { { "", PIN_EXEC }, { "cond", PIN_BOOL } }, 2, { { "True", PIN_EXEC }, { "False", PIN_EXEC } }, 2, 0, false, false },
    { "flow_for", "ForLoop", 6, { { "", PIN_EXEC }, { "first", PIN_NUM }, { "last", PIN_NUM } }, 3, { { "Loop Body", PIN_EXEC }, { "Index", PIN_NUM }, { "Completed", PIN_EXEC } }, 3, 0, false, false },
    { "flow_foreach", "ForEach", 6, { { "", PIN_EXEC } }, 1, { { "Loop Body", PIN_EXEC }, { "Element", PIN_ANY }, { "Index", PIN_NUM }, { "Completed", PIN_EXEC } }, 4, 0, true, false },
    { "flow_seq", "Sequence", 6, { { "", PIN_EXEC } }, 1, { { "Then 0", PIN_EXEC }, { "Then 1", PIN_EXEC }, { "Then 2", PIN_EXEC } }, 3, 0, false, false },
    { "flow_doonce", "DoOnce", 6, { { "", PIN_EXEC }, { "Reset", PIN_EXEC } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "flow_flipflop", "FlipFlop", 6, { { "", PIN_EXEC } }, 1, { { "A", PIN_EXEC }, { "B", PIN_EXEC }, { "Is A", PIN_BOOL } }, 3, 0, false, false },
    { "flow_gate", "Gate", 6, { { "Enter", PIN_EXEC }, { "Open", PIN_EXEC }, { "Close", PIN_EXEC } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },

    { "fn_entry", "Function Entry", 0, {}, 0, { { "", PIN_EXEC }, { "p1", PIN_NUM }, { "p2", PIN_NUM } }, 3, 0, false, true },

    { "m_div", "Divide", 4, { { "A", PIN_ANY }, { "B", PIN_ANY } }, 2, { { "", PIN_ANY } }, 1, 0, false, false },
    { "break_v3", "Break Vector", 4, { { "V", PIN_VEC } }, 1, { { "x", PIN_NUM }, { "y", PIN_NUM }, { "z", PIN_NUM } }, 3, 0, false, false },
    { "break_v2", "Break Vector2", 4, { { "V", PIN_VEC2 } }, 1, { { "x", PIN_NUM }, { "y", PIN_NUM } }, 2, 0, false, false },
    { "val_vec2", "Make Vector2", 2, { { "x", PIN_NUM }, { "y", PIN_NUM } }, 2, { { "", PIN_VEC2 } }, 1, 0, false, false },

    { "reroute", "Add Reroute Node", 6, { { "", PIN_ANY } }, 1, { { "", PIN_ANY } }, 1, 0, false, false },
    { "reroute_ex", "Add Reroute Node (exec)", 6, { { "", PIN_EXEC } }, 1, { { "", PIN_EXEC } }, 1, 0, false, false },

    // transform / component access — object pin unconnected = self
    { "get_comp", "Get Component by Class", 2, { { "Object", PIN_ENT } }, 1,
      { { "Component", PIN_ENT }, { "Found", PIN_BOOL } }, 2, 4, false, false },
    { "dir_fwd", "Get Forward Vector", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "dir_right", "Get Right Vector", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "dir_up", "Get Up Vector", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "wloc", "Get World Location", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "wrot", "Get World Rotation", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "lloc", "Get Local Location", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "lrot", "Get Local Rotation", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },

    { "set_wloc", "Set World Location", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "location", PIN_VEC } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_wrot", "Set World Rotation", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "rotation", PIN_VEC } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_lloc", "Set Local Location", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "location", PIN_VEC } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_lrot", "Set Local Rotation", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "rotation", PIN_VEC } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },

    // input axes: the event fires every frame; Axis Value = mouse delta or -1/0/+1
    { "ev_axis", "InputAxis", 0, {}, 0, { { "", PIN_EXEC }, { "Axis Value", PIN_NUM } }, 2, 5, false, true },
    { "val_axis", "Get Axis Value", 2, {}, 0, { { "", PIN_NUM } }, 1, 5, false, false },

    { "m_dot", "Dot Product", 4, { { "A", PIN_VEC }, { "B", PIN_VEC } }, 2, { { "", PIN_NUM } }, 1, 0, false, false },
    { "m_mod", "Percent (Modulo)", 4, { { "A", PIN_NUM }, { "B", PIN_NUM } }, 2, { { "", PIN_NUM } }, 1, 0, false, false },
    { "m_norm", "Normalize", 4, { { "V", PIN_VEC } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "m_dist", "Distance", 4, { { "A", PIN_VEC }, { "B", PIN_VEC } }, 2, { { "", PIN_NUM } }, 1, 0, false, false },

    // interpolazioni: Lerp e' wildcard (float/vec2/vec3); FInterp/VInterp stile Unreal
    { "m_lerp", "Lerp", 4, { { "A", PIN_ANY }, { "B", PIN_ANY }, { "Alpha", PIN_NUM } }, 3, { { "", PIN_ANY } }, 1, 0, false, false },
    { "m_finterp", "FInterp To", 4, { { "Current", PIN_NUM }, { "Target", PIN_NUM }, { "DeltaTime", PIN_NUM }, { "Speed", PIN_NUM } }, 4, { { "", PIN_NUM } }, 1, 0, false, false },
    { "m_vinterp", "VInterp To", 4, { { "Current", PIN_VEC }, { "Target", PIN_VEC }, { "DeltaTime", PIN_NUM }, { "Speed", PIN_NUM } }, 4, { { "", PIN_VEC } }, 1, 0, false, false },

    // tracce: raycast lineare e sfera lungo un segmento (categoria Azioni)
    // il filtro layer e' una maschera nei Details (n.choice), non un pin
    { "trace_line", "Line Trace", 1, { { "", PIN_EXEC }, { "Start", PIN_VEC }, { "End", PIN_VEC } }, 3,
      { { "", PIN_EXEC }, { "Hit", PIN_BOOL }, { "Location", PIN_VEC }, { "Normal", PIN_VEC }, { "Object", PIN_ENT } }, 5, 0, false, false },
    { "trace_sphere", "Sphere Trace", 1, { { "", PIN_EXEC }, { "Start", PIN_VEC }, { "End", PIN_VEC }, { "Radius", PIN_NUM } }, 4,
      { { "", PIN_EXEC }, { "Hit", PIN_BOOL }, { "Location", PIN_VEC }, { "Normal", PIN_VEC }, { "Object", PIN_ENT } }, 5, 0, false, false },

    // matematica aggiuntiva: Abs wildcard (float/vec), Power, Cross (Dot esiste gia')
    { "m_abs", "Abs", 4, { { "A", PIN_ANY } }, 1, { { "", PIN_ANY } }, 1, 0, false, false },
    { "m_pow", "Power", 4, { { "Base", PIN_NUM }, { "Exp", PIN_NUM } }, 2, { { "", PIN_NUM } }, 1, 0, false, false },
    { "m_cross", "Cross Product", 4, { { "A", PIN_VEC }, { "B", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },

    // trasformazione di un vettore relativo a un oggetto (object scollegato = self)
    { "tf_inv_dir", "InverseTransformDirection", 2, { { "object", PIN_ENT }, { "Direction", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },
    { "tf_dir", "TransformDirection", 2, { { "object", PIN_ENT }, { "Direction", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },
    { "tf_inv_loc", "InverseTransformLocation", 2, { { "object", PIN_ENT }, { "Location", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },
    { "tf_loc", "TransformLocation", 2, { { "object", PIN_ENT }, { "Location", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },

    // transform: comporre da location/rotation/scale e scomporre
    { "make_tf", "Make Transform", 2, { { "Location", PIN_VEC }, { "Rotation", PIN_VEC }, { "Scale", PIN_VEC } }, 3, { { "", PIN_TRANSFORM } }, 1, 0, false, false },
    { "break_tf", "Break Transform", 2, { { "Transform", PIN_TRANSFORM } }, 1, { { "Location", PIN_VEC }, { "Rotation", PIN_VEC }, { "Scale", PIN_VEC } }, 3, 0, false, false },

    // accesso ai componenti (object scollegato = self): renderer e fisica
    { "get_color", "Get Material Color", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_VEC } }, 1, 0, false, false },
    { "set_matcolor", "Set Material Color", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "rgb", PIN_VEC } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_phys_type", "Set Physics Type", 1, { { "", PIN_EXEC }, { "object", PIN_ENT } }, 2, { { "", PIN_EXEC } }, 1, 6, false, false },
    { "get_phys_type", "Get Physics Type", 2, { { "object", PIN_ENT } }, 1, { { "", PIN_INT } }, 1, 0, false, false },

    // inverse transform relativi a un valore Transform (non a un oggetto)
    { "tf_inv_dir_t", "InverseTransformDirection (T)", 2, { { "Transform", PIN_TRANSFORM }, { "Direction", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },
    { "tf_inv_loc_t", "InverseTransformLocation (T)", 2, { { "Transform", PIN_TRANSFORM }, { "Location", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },

    // event dispatcher (stile Unreal): bind di un evento a un dispatcher e chiamata
    // Target/Target Widget are appended AFTER Event on purpose: saved graphs
    // wire the delegate to pin 1, and inserting ahead of it would move it.
    // Both unwired = bind to this Blueprint's own dispatcher.
    { "bind_ev", "Bind Event to Dispatcher", 1,
      { { "", PIN_EXEC }, { "Event", PIN_DELEGATE }, { "Target", PIN_ENT }, { "Target Widget", PIN_WIDGET } }, 4,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "call_disp", "Call Dispatcher", 1, { { "", PIN_EXEC } }, 1, { { "", PIN_EXEC } }, 1, 0, true, false },

    // timer: Started continua subito; Completed scatta alla scadenza (anche a ogni ciclo)
    { "timer_set", "Set Timer by Event", 1, { { "", PIN_EXEC }, { "Time", PIN_NUM }, { "Looping", PIN_BOOL }, { "Event", PIN_DELEGATE } }, 4,
      { { "Started", PIN_EXEC }, { "Completed", PIN_EXEC }, { "Handle", PIN_TIMER_HANDLE } }, 3, 0, false, false },
    { "timer_pause", "Pause Timer", 1, { { "", PIN_EXEC }, { "Handle", PIN_TIMER_HANDLE } }, 2,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "timer_unpause", "Unpause Timer", 1, { { "", PIN_EXEC }, { "Handle", PIN_TIMER_HANDLE } }, 2,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "timer_clear", "Clear Timer", 1, { { "", PIN_EXEC }, { "Handle", PIN_TIMER_HANDLE } }, 2,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "flow_delay", "Delay", 6, { { "", PIN_EXEC }, { "Duration", PIN_NUM } }, 2,
      { { "Completed", PIN_EXEC } }, 1, 0, false, false },
    { "flow_retrigger_delay", "Retriggerable Delay", 6, { { "", PIN_EXEC }, { "Duration", PIN_NUM } }, 2,
      { { "Completed", PIN_EXEC } }, 1, 0, false, false },
    { "curve_eval", "Evaluate Curve", 2, { { "Time", PIN_NUM } }, 1,
      { { "Value", PIN_NUM } }, 1, 0, true, false },
    { "timer_is_valid", "Is Timer Valid", 2, { { "Handle", PIN_TIMER_HANDLE } }, 1,
      { { "Valid", PIN_BOOL } }, 1, 0, false, false },
    { "timer_set_func", "Set Timer by Function", 1, { { "", PIN_EXEC }, { "Time", PIN_NUM }, { "Looping", PIN_BOOL } }, 3,
      { { "Started", PIN_EXEC }, { "Completed", PIN_EXEC }, { "Handle", PIN_TIMER_HANDLE } }, 3, 0, false, false },
    { "audio_play", "Play Audio Source", 1, { { "", PIN_EXEC }, { "object", PIN_ENT } }, 2,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "audio_stop", "Stop Audio Source", 1, { { "", PIN_EXEC }, { "object", PIN_ENT } }, 2,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "audio_volume", "Set Audio Volume", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "Volume", PIN_NUM } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "audio_clip", "Set Audio Clip", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "Clip", PIN_STR } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "audio_fade_in", "Fade In Audio Source", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "Duration", PIN_NUM } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "audio_fade_out", "Fade Out Audio Source", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "Duration", PIN_NUM } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "create_event", "Create Event", 2, {}, 0, { { "Event", PIN_DELEGATE } }, 1, 0, true, false },
    { "m_clamp_float", "Clamp Float", 4, { { "Value", PIN_NUM }, { "Min", PIN_NUM }, { "Max", PIN_NUM } }, 3,
      { { "Result", PIN_NUM } }, 1, 0, false, false },
    { "m_pi", "Pi", 4, {}, 0, { { "Value", PIN_NUM } }, 1, 0, false, false },
    { "spawn_prefab", "Spawn Prefab", 1, { { "", PIN_EXEC }, { "Spawn Transform", PIN_TRANSFORM } }, 2,
      { { "", PIN_EXEC }, { "Spawned Object", PIN_ENT } }, 2, 0, false, false },
    { "select_enum", "Select", 5, { { "Selection", PIN_ENUM }, { "Value0", PIN_ANY }, { "Value1", PIN_ANY } }, 3,
      { { "Result", PIN_ANY } }, 1, 0, false, false },
    { "switch_enum", "Switch on Enum", 6, { { "", PIN_EXEC }, { "Selection", PIN_ENUM } }, 2,
      { { "Value0", PIN_EXEC }, { "Value1", PIN_EXEC }, { "Default", PIN_EXEC } }, 3, 0, false, false },
    { "ev_construct", "Construction Script", 0, {}, 0, { { "", PIN_EXEC } }, 1, 0, false, true },
    { "ai_set_target", "AI Set Target", 1, { { "", PIN_EXEC }, { "Agent", PIN_ENT }, { "Target", PIN_ENT } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "ai_set_destination", "AI Set Destination", 1, { { "", PIN_EXEC }, { "Agent", PIN_ENT }, { "Destination", PIN_VEC } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "ai_set_speed", "AI Set Speed", 1, { { "", PIN_EXEC }, { "Agent", PIN_ENT }, { "Speed", PIN_NUM } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "ai_set_stopped", "AI Set Is Stopped", 1, { { "", PIN_EXEC }, { "Agent", PIN_ENT }, { "Stopped", PIN_BOOL } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "ai_remaining", "AI Remaining Distance", 2, { { "Agent", PIN_ENT } }, 1,
      { { "Distance", PIN_NUM } }, 1, 0, false, false },
    { "ai_has_path", "AI Has Path", 2, { { "Agent", PIN_ENT } }, 1,
      { { "Has Path", PIN_BOOL } }, 1, 0, false, false },
    { "find_by_tag", "Find Actor by Tag", 2, { { "Tag", PIN_STR } }, 1,
      { { "Actor", PIN_ENT }, { "Found", PIN_BOOL } }, 2, 0, false, false },
    { "all_with_class", "Get All Actors With Class", 6, { { "", PIN_EXEC } }, 1,
      { { "Loop Body", PIN_EXEC }, { "Actor", PIN_ENT }, { "Index", PIN_INT }, { "Completed", PIN_EXEC } }, 4, 0, true, false },
    { "all_with_tag", "Get All Actors With Tag", 6, { { "", PIN_EXEC }, { "Tag", PIN_STR } }, 2,
      { { "Loop Body", PIN_EXEC }, { "Actor", PIN_ENT }, { "Index", PIN_INT }, { "Completed", PIN_EXEC } }, 4, 0, false, false },
    { "get_game_mode", "Get Game Mode", 2, {}, 0, { { "GameMode", PIN_ENT } }, 1, 0, false, false },
    { "get_game_instance", "Get Game Instance", 2, {}, 0, { { "GameInstance", PIN_ENT } }, 1, 0, false, false },
    { "get_player_controller", "Get Player Controller", 2, {}, 0, { { "PlayerController", PIN_ENT } }, 1, 0, false, false },
    { "get_player_pawn", "Get Player Pawn", 2, {}, 0, { { "Player", PIN_ENT } }, 1, 0, false, false },
    { "save_game_slot", "Save Game to Slot", 1, { { "", PIN_EXEC }, { "Object", PIN_ENT }, { "Slot", PIN_STR } }, 3,
      { { "", PIN_EXEC }, { "Success", PIN_BOOL } }, 2, 0, false, false },
    { "load_game_slot", "Load Game from Slot", 1, { { "", PIN_EXEC }, { "Object", PIN_ENT }, { "Slot", PIN_STR } }, 3,
      { { "", PIN_EXEC }, { "Success", PIN_BOOL } }, 2, 0, false, false },
    { "save_game_exists", "Does Save Game Exist", 2, { { "Slot", PIN_STR } }, 1,
      { { "Exists", PIN_BOOL } }, 1, 0, false, false },
    { "create_save_game", "Create Save Game Object", 1, { { "", PIN_EXEC } }, 1,
      { { "", PIN_EXEC }, { "SaveGame", PIN_ENT } }, 2, 0, true, false },
    { "ev_begin_overlap", "Event Begin Overlap", 0, {}, 0,
      { { "", PIN_EXEC }, { "Component", PIN_ENT }, { "Other Actor", PIN_ENT } }, 3, 0, false, true },
    { "ev_end_overlap", "Event End Overlap", 0, {}, 0,
      { { "", PIN_EXEC }, { "Component", PIN_ENT }, { "Other Actor", PIN_ENT } }, 3, 0, false, true },
    { "bind_begin_overlap", "Bind Event to Begin Overlap", 1,
      { { "", PIN_EXEC }, { "Mesh Renderer", PIN_ENT }, { "Event", PIN_DELEGATE } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "bind_end_overlap", "Bind Event to End Overlap", 1,
      { { "", PIN_EXEC }, { "Mesh Renderer", PIN_ENT }, { "Event", PIN_DELEGATE } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "anim_set_float", "Animator Set Float", 1,
      { { "", PIN_EXEC }, { "Animator", PIN_ENT }, { "Value", PIN_NUM } }, 3,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "anim_set_bool", "Animator Set Bool", 1,
      { { "", PIN_EXEC }, { "Animator", PIN_ENT }, { "Value", PIN_BOOL } }, 3,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "anim_set_trigger", "Animator Set Trigger", 1,
      { { "", PIN_EXEC }, { "Animator", PIN_ENT } }, 2,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "float_to_string", "Float to String", 2, { { "Value", PIN_NUM } }, 1,
      { { "String", PIN_STR } }, 1, 0, false, false },
    { "int_to_string", "Int to String", 2, { { "Value", PIN_INT } }, 1,
      { { "String", PIN_STR } }, 1, 0, false, false },
    { "bool_to_string", "Bool to String", 2, { { "Value", PIN_BOOL } }, 1,
      { { "String", PIN_STR } }, 1, 0, false, false },
    { "m_truncate", "Truncate", 4, { { "Value", PIN_NUM } }, 1,
      { { "Integer", PIN_INT } }, 1, 0, false, false },
    { "anim_bind_trigger", "Bind Animation Trigger", 1,
      { { "", PIN_EXEC }, { "Animator", PIN_ENT }, { "Event", PIN_DELEGATE } }, 3,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "does_implement_interface", "Does Implement Interface", 2,
      { { "Object", PIN_ENT } }, 1,
      { { "Return Value", PIN_BOOL } }, 1, 0, true, false },
    { "interface_message", "Interface Message", 1,
      { { "", PIN_EXEC }, { "Target", PIN_ENT } }, 2,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "invoke_inspector_event", "Invoke Inspector Event", 1,
      { { "", PIN_EXEC }, { "Target", PIN_ENT }, { "Event Name", PIN_STR } }, 3,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "cast_to_class", "Cast To Class", 1,
      { { "", PIN_EXEC }, { "Object", PIN_ENT } }, 2,
      { { "Cast Succeeded", PIN_EXEC }, { "Cast Failed", PIN_EXEC }, { "As Object", PIN_ENT } }, 3,
      0, false, false },
    { "member_access", "Blueprint Member", 1,
      { { "", PIN_EXEC }, { "Target", PIN_ENT } }, 2,
      { { "", PIN_EXEC } }, 1, 0, true, false },

    // extra math (pure)
    { "m_acos", "Acos", 4, { { "x", PIN_NUM } }, 1, { { "", PIN_NUM } }, 1, 0, false, false },
    { "m_atan2", "Atan2", 4, { { "Y", PIN_NUM }, { "X", PIN_NUM } }, 2, { { "", PIN_NUM } }, 1, 0, false, false },
    { "m_lookat", "Find Look At Rotation", 4, { { "Start", PIN_VEC }, { "Target", PIN_VEC } }, 2, { { "", PIN_VEC } }, 1, 0, false, false },
    { "m_ceil", "Ceil", 4, { { "x", PIN_NUM } }, 1, { { "", PIN_INT } }, 1, 0, false, false },
    { "m_floor", "Floor", 4, { { "x", PIN_NUM } }, 1, { { "", PIN_INT } }, 1, 0, false, false },
    { "m_frac", "Fraction", 4, { { "x", PIN_NUM } }, 1, { { "", PIN_NUM } }, 1, 0, false, false },
    // flow control
    { "flow_multigate", "MultiGate", 6, { { "", PIN_EXEC }, { "Reset", PIN_EXEC } }, 2,
      { { "Out 0", PIN_EXEC }, { "Out 1", PIN_EXEC }, { "Out 2", PIN_EXEC }, { "Out 3", PIN_EXEC } }, 4, 0, false, false },
    { "flow_don", "Do N", 6, { { "", PIN_EXEC }, { "Reset", PIN_EXEC }, { "N", PIN_NUM } }, 3,
      { { "Exit", PIN_EXEC }, { "Counter", PIN_INT } }, 2, 0, false, false },
    { "set_scale", "Set Scale", 1, { { "", PIN_EXEC }, { "object", PIN_ENT }, { "scale", PIN_VEC } }, 3, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_constraint_objs", "Set Constraint Objects", 1, { { "", PIN_EXEC }, { "constraint", PIN_ENT }, { "object 1", PIN_ENT }, { "object 2", PIN_ENT } }, 4, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "open_level", "Open Level", 1, { { "", PIN_EXEC }, { "Level", PIN_STR } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "get_current_level", "Get Current Level Name", 2, {}, 0, { { "Name", PIN_STR } }, 1, 0, false, false },
    { "create_widget", "Create Widget", 1, { { "", PIN_EXEC } }, 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET } }, 2, 0, true, false },
    { "add_widget_viewport", "Add to Viewport", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "remove_widget_viewport", "Remove from Viewport", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET } }, 2, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_widget_text", "Set Widget Text", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Text", PIN_STR } }, 4, { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_widget_value", "Set Widget Value", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Value", PIN_NUM } }, 4, { { "", PIN_EXEC } }, 1, 0, false, false },
    // ── Widget Blueprint events (category 0 = events, isEvent = true) ──
    { "ev_w_init", "Event On Initialized", 0, {}, 0, { { "", PIN_EXEC } }, 1, 0, false, true },
    { "ev_w_preconstruct", "Event Pre Construct", 0, {}, 0, { { "", PIN_EXEC } }, 1, 0, false, true },
    { "ev_w_construct", "Event Construct", 0, {}, 0, { { "", PIN_EXEC } }, 1, 0, false, true },
    // the pointer events report which named element the cursor is over, so the
    // graph can compare it with a WIDGETS component variable
    { "ev_w_mouse_enter", "On Mouse Enter", 0, {}, 0, { { "", PIN_EXEC }, { "Element", PIN_STR } }, 2, 0, false, true },
    { "ev_w_mouse_leave", "On Mouse Exit", 0, {}, 0, { { "", PIN_EXEC }, { "Element", PIN_STR } }, 2, 0, false, true },
    { "ev_w_mouse_down", "On Mouse Click", 0, {}, 0, { { "", PIN_EXEC }, { "Element", PIN_STR } }, 2, 0, false, true },
    { "ev_w_mouse_up", "On Mouse Release", 0, {}, 0, { { "", PIN_EXEC }, { "Element", PIN_STR } }, 2, 0, false, true },
    { "ev_w_tick", "Event Tick", 0, {}, 0, { { "", PIN_EXEC }, { "Delta", PIN_NUM } }, 2, 0, false, true },
    // Compare works on numbers; this is the string one (used to test which
    // element a widget pointer event came from)
    { "l_streq", "Equal (String)", 5, { { "A", PIN_STR }, { "B", PIN_STR } }, 2, { { "", PIN_BOOL } }, 1, 0, false, false },
    // Component properties by name. The Get side is pure (category 2 = values),
    // the Set side is an action; `sname` holds the property, picked in Details.
    { "get_widget_num", "Get Widget Number", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Value", PIN_NUM } }, 1, 0, true, false },
    { "set_widget_num", "Set Widget Number", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Value", PIN_NUM } }, 4,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "get_widget_str", "Get Widget String", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Value", PIN_STR } }, 1, 0, true, false },
    { "set_widget_str", "Set Widget String", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Value", PIN_STR } }, 4,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "get_widget_color", "Get Widget Color", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Value", PIN_COLOR } }, 1, 0, true, false },
    { "set_widget_color", "Set Widget Color", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Value", PIN_COLOR } }, 4,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    { "get_widget_bool", "Get Widget Bool", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Value", PIN_BOOL } }, 1, 0, true, false },
    { "set_widget_bool", "Set Widget Bool", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Value", PIN_BOOL } }, 4,
      { { "", PIN_EXEC } }, 1, 0, true, false },
    // ── direct slot setters (no property combo: the node IS the property) ──
    { "set_widget_percent", "Set Percent", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Percent", PIN_NUM } }, 4,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_widget_halign", "Set Horizontal Alignment", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Alignment", PIN_ENUM } }, 4,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_widget_valign", "Set Vertical Alignment", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Alignment", PIN_ENUM } }, 4,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_widget_anchor", "Set Anchors", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Anchor", PIN_ENUM } }, 4,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_widget_pivot", "Set Canvas Alignment", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Alignment", PIN_VEC2 } }, 4,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    { "set_widget_range", "Set Bar Range", 1, { { "", PIN_EXEC }, { "Widget", PIN_WIDGET }, { "Element", PIN_STR }, { "Min", PIN_NUM }, { "Max", PIN_NUM } }, 5,
      { { "", PIN_EXEC } }, 1, 0, false, false },
    // ── direct readers (category 2 = pure: no exec pins, evaluated on demand) ──
    { "get_widget_percent", "Get Percent", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Percent", PIN_NUM } }, 1, 0, false, false },
    { "get_widget_range", "Get Bar Range", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Min", PIN_NUM }, { "Max", PIN_NUM } }, 2, 0, false, false },
    { "get_widget_halign", "Get Horizontal Alignment", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Alignment", PIN_ENUM } }, 1, 0, false, false },
    { "get_widget_valign", "Get Vertical Alignment", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Alignment", PIN_ENUM } }, 1, 0, false, false },
    { "get_widget_anchor", "Get Anchors", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Anchor", PIN_ENUM } }, 1, 0, false, false },
    { "get_widget_pivot", "Get Canvas Alignment", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Alignment", PIN_VEC2 } }, 1, 0, false, false },
    { "get_widget_text", "Get Widget Text", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Text", PIN_STR } }, 1, 0, false, false },
    { "get_widget_visible", "Get Visible", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Visible", PIN_BOOL } }, 1, 0, false, false },
    { "get_widget_enabled", "Get Is Enabled", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Enabled", PIN_BOOL } }, 1, 0, false, false },
    { "get_widget_opacity", "Get Render Opacity", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Opacity", PIN_NUM } }, 1, 0, false, false },
    { "get_widget_size", "Get Size", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Width", PIN_NUM }, { "Height", PIN_NUM } }, 2, 0, false, false },
    { "get_widget_position", "Get Position", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "X", PIN_NUM }, { "Y", PIN_NUM } }, 2, 0, false, false },
    { "get_widget_color2", "Get Color", 2, { { "Widget", PIN_WIDGET }, { "Element", PIN_STR } }, 2,
      { { "Color", PIN_COLOR } }, 1, 0, false, false },
};

static bool isReroute(int def) { return def == BP_REROUTE || def == BP_REROUTE_EX; }

// Sequence has a variable number of exec outputs (the "+" adds one). The count
// lives in n.choice; 0 (old saved graphs) means the original 3.
static int seqCount(const BPNode& n) {
    if (n.def != BP_FLOW_SEQ) return 0;
    int count = n.choice >= 1 ? n.choice : 3;
    return count > BP_MAX_PINS ? BP_MAX_PINS : count;
}
static int nodeOutCount(const BPNode& n) {
    return n.def == BP_FLOW_SEQ ? seqCount(n) : DEFS[n.def].nOuts;
}

// InputAction bindings: every key, plus the mouse axes (X, Y, XY = Vector2)
#define BP_NBINDS (BP_NKEYS + 3)
static const char* BP_BIND_AXIS_NAMES[3] = { "Mouse X", "Mouse Y", "Mouse XY" };

static const char* bpBindName(int choice) {
    choice %= BP_NBINDS;
    return choice < BP_NKEYS ? BP_KEY_NAMES[choice] : BP_BIND_AXIS_NAMES[choice - BP_NKEYS];
}

static const char* const* bpBindNames() {
    static std::vector<const char*> v;
    if (v.empty()) {
        for (int i = 0; i < BP_NKEYS; i++) v.push_back(BP_KEY_NAMES[i]);
        for (int i = 0; i < 3; i++) v.push_back(BP_BIND_AXIS_NAMES[i]);
    }
    return v.data();
}

// wire compatibility: exec only with exec, ANY with any data, the rest by family
// (num / int / bool are interchangeable)
static bool pinsCompatible(PinKind a, PinKind b) {
    if (a == PIN_EXEC || b == PIN_EXEC) return a == b;
    if (a == PIN_ANY || b == PIN_ANY) return true;
    if (a == PIN_ENT || b == PIN_ENT) return a == b;
    if (a == PIN_VEC || b == PIN_VEC) return a == b;
    if (a == PIN_VEC2 || b == PIN_VEC2) return a == b;
    if (a == PIN_STR || b == PIN_STR) return a == b;
    if (a == PIN_TRANSFORM || b == PIN_TRANSFORM) return a == b;
    if (a == PIN_DELEGATE || b == PIN_DELEGATE) return a == b;
    if (a == PIN_TIMER_HANDLE || b == PIN_TIMER_HANDLE) return a == b;
    if (a == PIN_ENUM || b == PIN_ENUM) return a == b;
    if (a == PIN_ANIMATION_CLIP || b == PIN_ANIMATION_CLIP) return a == b;
    if (a == PIN_ANIMATOR_CONTROLLER || b == PIN_ANIMATOR_CONTROLLER) return a == b;
    if (a == PIN_COLOR || b == PIN_COLOR) return a == b;
    // a widget reference is not an int: only another widget pin accepts it
    if (a == PIN_WIDGET || b == PIN_WIDGET) return a == b;
    return true;
}

// first pin of `d` (input side if wantInput) that can connect to kind `k`
static int firstCompatiblePin(const BPNodeDef& d, PinKind k, bool wantInput) {
    int count = wantInput ? d.nIns : d.nOuts;
    for (int i = 0; i < count; i++) {
        if (pinsCompatible(wantInput ? d.ins[i].kind : d.outs[i].kind, k)) return i;
    }
    return -1;
}

const BPNodeDef* bpDefs() { return DEFS; }

int bpDefByKey(const char* key) {
    for (int i = 0; i < BP_TYPE_COUNT; i++) {
        if (strcmp(DEFS[i].key, key) == 0) return i;
    }
    return -1;
}

// ═══ canvas ═══
BPNode* BPCanvas::byId(int id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
const BPNode* BPCanvas::byId(int id) const {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

int BPCanvas::addNode(int def, float x, float y) {
    BPNode n;
    n.id = nextId++;
    n.def = def;
    n.x = x;
    n.y = y;
    if (def == BP_ACT_IMPULSE) n.lit[1] = { 0, 6, 0 };
    if (def == BP_VAL_NUM) n.prop = 1;
    if (def == BP_VAL_RANDOM) n.lit[1] = { 1, 0, 0 };
    if (def == BP_FLOW_FOR) n.lit[2] = { 3, 0, 0 };
    if (def == BP_M_LERP) n.lit[2] = { 0.5f, 0, 0 };                 // Alpha
    if (def == BP_M_CLAMP_FLOAT) n.lit[2] = { 1.0f, 0, 0 };          // Max
    if (def == BP_M_FINTERP || def == BP_M_VINTERP) n.lit[3] = { 5, 0, 0 }; // Speed
    if (def == BP_TRACE_SPHERE) n.lit[3] = { 0.5f, 0, 0 };           // Radius (layer mask in n.choice)
    if (def == BP_ACT_PRINT) { n.lit[2]={1,1,1};n.litAlpha[2]=1.0f; }
    if (def == BP_TIMER_SET || def == BP_TIMER_SET_FUNC || def == BP_FLOW_DELAY || def == BP_FLOW_RETRIGGER_DELAY)
        n.lit[1] = { 1.0f, 0, 0 };                                  // durata iniziale: 1 secondo
    if (DEFS[def].usesName) snprintf(n.sname, sizeof(n.sname), "name");
    if (def == BP_GET_ALL_WITH_CLASS) snprintf(n.sname, sizeof(n.sname), "component:0");
    if (def == BP_CAST_TO_CLASS) snprintf(n.sname, sizeof(n.sname), "component:0");
    if (def == BP_DOES_IMPLEMENT_INTERFACE) n.sname[0] = 0;
    if (def == BP_INTERFACE_MESSAGE) n.sname[0] = 0;
    nodes.push_back(n);
    return n.id;
}

void BPCanvas::removeNode(int id) {
    const BPNode* target = byId(id);
    bool bypass = target && isReroute(target->def);
    std::vector<BPLink> incoming, outgoing;
    if (bypass) {
        for (const BPLink& l : links) {
            if (l.toNode == id && l.toPin == 0 && l.fromNode != id) incoming.push_back(l);
            if (l.fromNode == id && l.fromPin == 0 && l.toNode != id) outgoing.push_back(l);
        }
    }
    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const BPLink& l) { return l.fromNode == id || l.toNode == id; }), links.end());
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const BPNode& n) { return n.id == id; }), nodes.end());
    // Removing a reroute is transparent: reconnect every upstream endpoint to
    // every downstream endpoint so deleting layout helpers never breaks logic.
    if (bypass) {
        for (const BPLink& in : incoming)
            for (const BPLink& out : outgoing)
                if (byId(in.fromNode) && byId(out.toNode)) connect(in.fromNode, in.fromPin, out.toNode, out.toPin);
    }
}

bool BPCanvas::detachLinkAtPin(int node, int pin, bool outputPin, BPLink& detached) {
    // Prefer the most recently created link when a fan-out/converging pin owns
    // more than one connection; a subsequent Ctrl+drag can move the others too.
    for (int i = (int)links.size() - 1; i >= 0; i--) {
        const BPLink& l = links[i];
        bool match = outputPin ? (l.fromNode == node && l.fromPin == pin)
                               : (l.toNode == node && l.toPin == pin);
        if (!match) continue;
        detached = l;
        links.erase(links.begin() + i);
        return true;
    }
    return false;
}

void BPCanvas::connect(int fromNode, int fromPin, int toNode, int toPin) {
    const BPNode* fn = byId(fromNode);
    const BPNode* tn = byId(toNode);
    // Custom Event outputs after pin 0 are dynamic data/delegate pins. Looking
    // only at the static definition incorrectly treated them as exec pins and
    // replaced the previous wire, preventing delegate fan-out.
    bool execWire = fn && (fn->def == BP_FLOW_SEQ || fn->def == BP_SWITCH_ENUM ||
                    (fn->def == BP_EV_CUSTOM ? fromPin == 0
                     : fromPin < DEFS[fn->def].nOuts && DEFS[fn->def].outs[fromPin].kind == PIN_EXEC));
    // data pins: one source per input. Exec pins: many wires may CONVERGE into
    // the same input, but one exec output can never split into two wires.
    if (!execWire) {
        links.erase(std::remove_if(links.begin(), links.end(),
            [&](const BPLink& l) { return l.toNode == toNode && l.toPin == toPin; }), links.end());
    } else {
        links.erase(std::remove_if(links.begin(), links.end(),
            [&](const BPLink& l) { return l.fromNode == fromNode && l.fromPin == fromPin; }), links.end());
        links.erase(std::remove_if(links.begin(), links.end(),
            [&](const BPLink& l) {
                return l.fromNode == fromNode && l.fromPin == fromPin &&
                       l.toNode == toNode && l.toPin == toPin;
            }), links.end());
    }
    (void)tn;
    links.push_back({ fromNode, fromPin, toNode, toPin });
}

const BPLink* BPCanvas::linkInto(int node, int pin) const {
    for (const auto& l : links) if (l.toNode == node && l.toPin == pin) return &l;
    return nullptr;
}

const BPLink* BPCanvas::linkFromExec(int node, int pin) const {
    for (const auto& l : links) if (l.fromNode == node && l.fromPin == pin) return &l;
    return nullptr;
}

// ═══ graph asset ═══
BPVarDef* BPGraph::findVar(const char* name) {
    for (auto& v : vars) if (strcmp(v.name, name) == 0) return &v;
    return nullptr;
}
BPFunc* BPGraph::findFunc(const char* name) {
    for (auto& f : funcs) if (strcmp(f.name, name) == 0) return &f;
    return nullptr;
}
BPEventDef* BPGraph::findEvent(const char* name) {
    for (auto& e : events) if (strcmp(e.name, name) == 0) return &e;
    return nullptr;
}
BPDispatcherDef* BPGraph::findDispatcher(const char* name) {
    for(auto& dispatcher:dispatchers)if(strcmp(dispatcher.name,name)==0)return &dispatcher;
    return nullptr;
}

static const char* bpRequiredBaseName(BPRequiredKind kind) {
    static const char* names[] = {
        "MeshRenderer", "RigidBody", "Trigger", "Light", "Camera", "AudioSource",
        "AudioReverbZone", "AIAgent", "NavigationOccluder", "Animator", "Blueprint"
    };
    return kind >= 0 && kind < BP_REQ_COUNT ? names[(int)kind] : "Component";
}

static const char* bpRequiredRefClass(const BPRequiredComponent& required) {
    static const char* classes[] = {
        "component:Mesh", "component:Physics", "component:Trigger", "component:Light",
        "component:Camera", "component:AudioSource", "component:ReverbZone",
        "component:AIAgent", "component:NavigationOccluder", "component:Animator", ""
    };
    return required.kind >= 0 && required.kind < BP_REQ_COUNT ? classes[(int)required.kind] : "";
}

void BPGraph::syncRequiredVariables() {
    vars.erase(std::remove_if(vars.begin(), vars.end(),
                              [](const BPVarDef& value) { return value.requiredGenerated; }),
               vars.end());
    for (int i = 0; i < (int)requiredComponents.size(); i++) {
        BPRequiredComponent& required = requiredComponents[i];
        BPVarDef value;
        snprintf(value.name, sizeof(value.name), "%s", required.variableName);
        value.type = PIN_ENT;
        value.container = VC_SINGLE;
        value.scope = VS_PUBLIC;
        value.expose = false;
        value.exposeOnSpawn = false;
        value.requiredGenerated = true;
        value.requiredIndex = i;
        if (required.kind == BP_REQ_BLUEPRINT)
            snprintf(value.refClass, sizeof(value.refClass), "blueprint:%s", required.blueprintAsset.c_str());
        else
            snprintf(value.refClass, sizeof(value.refClass), "%s", bpRequiredRefClass(required));
        vars.push_back(value);
    }
}

// The Widget designer owns this list: every component flagged "Is Variable"
// becomes a read-only String variable holding its own name, which is exactly
// what Set Widget Text/Value take on their Element pin.
void BPGraph::syncWidgetVariables(const std::vector<std::pair<std::string, std::string>>& members) {
    vars.erase(std::remove_if(vars.begin(), vars.end(),
                              [](const BPVarDef& value) { return value.widgetGenerated; }),
               vars.end());
    for (const auto& member : members) {
        BPVarDef value;
        snprintf(value.name, sizeof(value.name), "%s", member.first.c_str());
        snprintf(value.strDef, sizeof(value.strDef), "%s", member.first.c_str());
        snprintf(value.widgetType, sizeof(value.widgetType), "%s", member.second.c_str());
        value.type = PIN_STR;
        value.container = VC_SINGLE;
        value.scope = VS_PUBLIC;
        value.expose = false;
        value.exposeOnSpawn = false;
        value.widgetGenerated = true;
        vars.push_back(value);
    }
}

static bool bpSameName(const char* a, const char* b) {
    return a && b && _stricmp(a, b) == 0;
}

static bool bpRequiredNameTaken(const BPGraph& graph, const char* name, int exceptRequired = -1) {
    for (int i = 0; i < (int)graph.requiredComponents.size(); i++)
        if (i != exceptRequired && bpSameName(graph.requiredComponents[i].variableName, name)) return true;
    return false;
}

static std::string bpUniqueMemberName(const BPGraph& graph, const std::string& base, int memberType,
                                      int exceptIndex = -1, int exceptRequired = -1) {
    auto taken = [&](const std::string& candidate) {
        if (bpRequiredNameTaken(graph, candidate.c_str(), exceptRequired)) return true;
        if (memberType == 0) {
            for (int i = 0; i < (int)graph.vars.size(); i++)
                if (i != exceptIndex && !graph.vars[i].requiredGenerated && bpSameName(graph.vars[i].name, candidate.c_str())) return true;
        } else if (memberType == 1) {
            for (int i = 0; i < (int)graph.funcs.size(); i++)
                if (i != exceptIndex && bpSameName(graph.funcs[i].name, candidate.c_str())) return true;
        } else {
            for (int i = 0; i < (int)graph.events.size(); i++)
                if (i != exceptIndex && bpSameName(graph.events[i].name, candidate.c_str())) return true;
        }
        return false;
    };
    std::string clean = base.empty() ? (memberType == 0 ? "var" : memberType == 1 ? "Function" : "CustomEvent") : base;
    if (!taken(clean)) return clean;
    for (int suffix = 2;; suffix++) {
        std::string candidate = clean + std::to_string(suffix);
        if (!taken(candidate)) return candidate;
    }
}

static std::string bpUniqueRequiredName(const BPGraph& graph, const std::string& base, int exceptRequired = -1) {
    auto taken = [&](const std::string& candidate) {
        if (bpRequiredNameTaken(graph, candidate.c_str(), exceptRequired)) return true;
        for (const BPVarDef& value : graph.vars)
            if (!value.requiredGenerated && bpSameName(value.name, candidate.c_str())) return true;
        for (const BPFunc& value : graph.funcs) if (bpSameName(value.name, candidate.c_str())) return true;
        for (const BPEventDef& value : graph.events) if (bpSameName(value.name, candidate.c_str())) return true;
        return false;
    };
    std::string clean = base.empty() ? "RequiredComponent" : base;
    if (!taken(clean)) return clean;
    for (int suffix = 2;; suffix++) {
        std::string candidate = clean + std::to_string(suffix);
        if (!taken(candidate)) return candidate;
    }
}

static bool bpReadTextFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.assign(size > 0 ? (size_t)size : 0, 0);
    if (size > 0) fread(out.data(), 1, (size_t)size, f);
    fclose(f);
    return true;
}

static bool bpLoadEnumAsset(const std::string& projectDir, const char* relativePath, BPEnumAsset& asset) {
    if (projectDir.empty() || !relativePath || !relativePath[0]) return false;
    std::string data;
    return bpReadTextFile(projectDir + "\\" + relativePath, data) && asset.deserialize(data);
}

static std::vector<std::string> bpFindProjectAssets(const std::string& projectDir, const char* extension) {
    std::vector<std::string> result;
    std::error_code ec;
    if (projectDir.empty() || !fs::exists(projectDir, ec)) return result;
    for (fs::recursive_directory_iterator it(projectDir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
        if (ext != extension) continue;
        std::string rel = fs::relative(it->path(), projectDir, ec).string();
        if (!ec) result.push_back(rel); else ec.clear();
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct BPSpawnPinInfo {
    std::string name;
    PinKind kind = PIN_NUM;
    std::string enumAsset;
};

static std::vector<BPSpawnPinInfo> bpSpawnPinInfo(const std::string& projectDir, const char* prefabPath) {
    std::vector<BPSpawnPinInfo> result;
    if (projectDir.empty() || !prefabPath || !prefabPath[0]) return result;
    std::string prefabData;
    EditorScene prefab;
    if (!bpReadTextFile(projectDir + "\\" + prefabPath, prefabData) || !prefab.deserialize(prefabData)) return result;
    const Entity* root = nullptr;
    for (const Entity& e : prefab.entities) if (e.parentId == 0) { root = &e; break; }
    if (!root || !root->graphPath[0]) return result; // only the root Blueprint defines spawn pins
    std::string graphData;
    BPGraph rootGraph;
    if (!bpReadTextFile(projectDir + "\\" + root->graphPath, graphData) || !rootGraph.deserialize(graphData)) return result;
    for (const BPVarDef& var : rootGraph.vars) {
        if (!var.exposeOnSpawn || var.scope != VS_PUBLIC || var.container != VC_SINGLE) continue;
        result.push_back({ var.name, var.type, var.enumAsset });
        if ((int)result.size() >= BP_MAX_PINS - 2) break;
    }
    return result;
}

static void serializeCanvas(std::ostringstream& o, const BPCanvas& cv) {
    for (const auto& n : cv.nodes) {
        o << "node " << n.id << " " << DEFS[n.def].key << " " << n.x << " " << n.y
          << " " << n.prop << " " << n.choice << " " << (n.sname[0] ? n.sname : "-") << "\n";
        // Keeps asset/function names containing spaces intact. The legacy token
        // above remains for backwards compatibility and nstr is authoritative.
        if (n.sname[0]) o << "nstr " << n.id << " " << n.sname << "\n";
        for (int i = 0; i < BP_MAX_PINS; i++) {
            if (n.lit[i].x != 0 || n.lit[i].y != 0 || n.lit[i].z != 0) {
                o << "lit " << n.id << " " << i << " " << n.lit[i].x << " " << n.lit[i].y << " " << n.lit[i].z << "\n";
            }
            if (!n.slit[i].empty()) {
                o << "slit " << n.id << " " << i << " " << n.slit[i] << "\n";
            }
            if (n.litAlpha[i] != 0) o << "lita " << n.id << " " << i << " " << n.litAlpha[i] << "\n";
        }
    }
    for (const auto& l : cv.links) {
        o << "link " << l.fromNode << " " << l.fromPin << " " << l.toNode << " " << l.toPin << "\n";
    }
    for (const auto& c : cv.comments) {
        o << "comment " << c.x << " " << c.y << " " << c.w << " " << c.h << " "
          << c.color.x << " " << c.color.y << " " << c.color.z << " " << c.fontSize << " " << c.text << "\n";
    }
}

std::string BPGraph::serialize() const {
    std::ostringstream o;
    o << "IMPULSOBP 6\n";
    o << "classkind " << (int)classKind << "\n";
    o << "uniqueinstance " << (uniquePerObject ? 1 : 0) << "\n";
    for (const BPRequiredComponent& required : requiredComponents)
        o << "required " << (int)required.kind << " " << std::quoted(std::string(required.variableName))
          << " " << std::quoted(required.blueprintAsset) << "\n";
    if (!parentAsset.empty()) o << "parentbp " << parentAsset << "\n";
    if (!defaultPawnClass.empty()) o << "defaultpawn " << defaultPawnClass << "\n";
    if (!playerControllerClass.empty()) o << "playercontroller " << playerControllerClass << "\n";
    for (const std::string& tag : defaultTags) if (!tag.empty()) o << "defaulttag " << tag << "\n";
    for (const auto& v : vars) {
        if (v.requiredGenerated || v.widgetGenerated) continue;   // rebuilt from their source
        o << "var " << v.name << " " << (int)v.type << " " << (int)v.container << " "
          << (int)v.scope << " " << (v.expose ? 1 : 0) << " " << v.def.x << " " << v.def.y << " " << v.def.z
          << " " << (v.exposeOnSpawn ? 1 : 0) << "\n";
        if (v.type == PIN_STR && v.strDef[0]) o << "vstr " << v.name << " " << v.strDef << "\n";
        // Object variables and Widget Object variables both carry a class
        if ((v.type == PIN_ENT || bpIsWidgetObject(v)) && v.refClass[0])
            o << "vclass " << v.name << " " << v.refClass << "\n";
        if (v.type == PIN_ENUM && v.enumAsset[0]) o << "venum " << v.name << " " << v.enumAsset << "\n";
        if ((v.type == PIN_ANIMATION_CLIP || v.type == PIN_ANIMATOR_CONTROLLER || bpIsAnimatorControllerObject(v)) && v.assetPath[0])
            o << "vasset " << v.name << " " << v.assetPath << "\n";
        if (v.type == PIN_COLOR) o << "vcolor " << v.name << " " << v.defAlpha << "\n";
        if (v.type == PIN_TRANSFORM)
            o << "vtf " << v.name << " " << v.defRot.x << " " << v.defRot.y << " " << v.defRot.z
              << " " << v.defScl.x << " " << v.defScl.y << " " << v.defScl.z << "\n";
        if (v.category[0]) o << "vcat " << v.name << " " << v.category << "\n";
    }
    for (const auto& e : events) {
        o << "event " << e.name << "\n";
        o << "eventmeta " << (int)e.scope << "\n";
        for (const auto& p : e.params) o << "evpin " << p.name << " " << (int)p.kind << "\n";
    }
    for (const auto& s : interfaces) o << "iface " << s << "\n";
    for (const auto& s : interfaceAssets) o << "ifaceasset " << s << "\n";
    for (const BPDispatcherDef& dispatcher : dispatchers) {
        o << "disp " << dispatcher.name << "\n";
        if (dispatcher.category[0]) o << "dispcat " << dispatcher.category << "\n";
        for(const BPFuncPin& pin:dispatcher.params)o<<"disppin "<<pin.name<<" "<<(int)pin.kind<<"\n";
    }
    o << "canvas -\n";
    serializeCanvas(o, main());
    for (size_t i = 1; i < graphs.size(); i++) {
        o << "canvas *" << graphs[i].name << "\n";   // '*' = extra event graph
        serializeCanvas(o, graphs[i].body);
    }
    for (const auto& f : funcs) {
        o << "canvas " << f.name << "\n";
        o << "fnpure " << (f.pure ? 1 : 0) << "\n";
        o << "fnmeta " << (int)f.scope << "\n";
        if (f.category[0]) o << "fncat " << f.category << "\n";
        for (const auto& p : f.ins) o << "fnin " << p.name << " " << (int)p.kind << "\n";
        for (const auto& p : f.outs) o << "fnout " << p.name << " " << (int)p.kind << "\n";
        serializeCanvas(o, f.body);
    }
    return o.str();
}

bool BPGraph::deserialize(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    int version = 0;
    if (!std::getline(in, line) || sscanf(line.c_str(), "IMPULSOBP %d", &version) != 1) return false;
    clear();
    BPCanvas* cur = &main();
    int curFnIdx = -1;         // index into funcs when the current canvas is a function
    bool fnSigCleared = false; // legacy default sig replaced by the first fnin/fnout line
    while (std::getline(in, line)) {
        if (line.rfind("classkind ", 0) == 0) {
            int kind = 0;
            if (sscanf(line.c_str(), "classkind %d", &kind) == 1 && kind >= 0 && kind <= BP_CLASS_SAVEGAME)
                classKind = (BPClassKind)kind;
        } else if (line.rfind("uniqueinstance ", 0) == 0) {
            int unique = 1;
            if (sscanf(line.c_str(), "uniqueinstance %d", &unique) == 1) uniquePerObject = unique != 0;
        } else if (line.rfind("required ", 0) == 0) {
            std::istringstream requiredLine(line.substr(9));
            int kind = 0;
            std::string variableName, asset;
            if (requiredLine >> kind >> std::quoted(variableName) >> std::quoted(asset) &&
                kind >= 0 && kind < BP_REQ_COUNT) {
                BPRequiredComponent required;
                required.kind = (BPRequiredKind)kind;
                required.blueprintAsset = asset;
                snprintf(required.variableName, sizeof(required.variableName), "%s", variableName.c_str());
                requiredComponents.push_back(std::move(required));
            }
        } else if (line.rfind("parentbp ", 0) == 0) {
            parentAsset = line.substr(9);
        } else if (line.rfind("defaultpawn ", 0) == 0) {
            defaultPawnClass = line.substr(12);
        } else if (line.rfind("playercontroller ", 0) == 0) {
            playerControllerClass = line.substr(17);
        } else if (line.rfind("defaulttag ", 0) == 0) {
            std::string tag = line.substr(11);
            if (!tag.empty() && std::find(defaultTags.begin(), defaultTags.end(), tag) == defaultTags.end()) defaultTags.push_back(tag);
        } else if (line.rfind("var ", 0) == 0) {
            BPVarDef v;
            int type, cont, scope, expose, exposeOnSpawn = 0;
            int got = sscanf(line.c_str(), "var %31s %d %d %d %d %f %f %f %d",
                             v.name, &type, &cont, &scope, &expose, &v.def.x, &v.def.y, &v.def.z, &exposeOnSpawn);
            if (got >= 8) {
                v.type = (PinKind)type;
                // v1 compatibility: Animator Controller used to be a standalone
                // variable/pin type. It is now an Object class, like other refs.
                if (v.type == PIN_ANIMATOR_CONTROLLER) {
                    v.type = PIN_ENT;
                    snprintf(v.refClass, sizeof(v.refClass), "%s", "asset:AnimatorController");
                }
                v.container = (VarContainer)cont;
                v.scope = (VarScope)scope;
                v.expose = expose != 0;
                v.exposeOnSpawn = got >= 9 && exposeOnSpawn != 0;
                vars.push_back(v);
            }
        } else if (line.rfind("vstr ", 0) == 0) {
            size_t sp = line.find(' ', 5);
            if (sp != std::string::npos) {
                std::string nm = line.substr(5, sp - 5);
                BPVarDef* v = findVar(nm.c_str());
                if (v) snprintf(v->strDef, sizeof(v->strDef), "%s", line.substr(sp + 1).c_str());
            }
        } else if (line.rfind("vclass ", 0) == 0) {
            size_t sp = line.find(' ', 7);
            if (sp != std::string::npos) {
                std::string nm = line.substr(7, sp - 7);
                BPVarDef* v = findVar(nm.c_str());
                if (v) {
                    snprintf(v->refClass, sizeof(v->refClass), "%s", line.substr(sp + 1).c_str());
                    // Widget references were briefly stored as an Int carrying a
                    // handle; promote them to the real reference kind.
                    if (v->type == PIN_INT && bpMemberClassIsWidget(v->refClass)) v->type = PIN_WIDGET;
                }
            }
        } else if (line.rfind("venum ", 0) == 0) {
            size_t sp = line.find(' ', 6);
            if (sp != std::string::npos) {
                std::string nm = line.substr(6, sp - 6);
                BPVarDef* v = findVar(nm.c_str());
                if (v) snprintf(v->enumAsset, sizeof(v->enumAsset), "%s", line.substr(sp + 1).c_str());
            }
        } else if (line.rfind("vasset ", 0) == 0) {
            size_t sp = line.find(' ', 7);
            if (sp != std::string::npos) {
                std::string nm = line.substr(7, sp - 7);
                BPVarDef* v = findVar(nm.c_str());
                if (v) snprintf(v->assetPath, sizeof(v->assetPath), "%s", line.substr(sp + 1).c_str());
            }
        } else if (line.rfind("vcat ", 0) == 0) {
            size_t sp = line.find(' ', 5);
            if (sp != std::string::npos) {
                std::string nm = line.substr(5, sp - 5);
                BPVarDef* v = findVar(nm.c_str());
                if (v) snprintf(v->category, sizeof(v->category), "%s", line.substr(sp + 1).c_str());
            }
        } else if (line.rfind("vcolor ", 0) == 0) {
            char name[32];float alpha=1;
            if(sscanf(line.c_str(),"vcolor %31s %f",name,&alpha)==2){BPVarDef*v=findVar(name);if(v)v->defAlpha=alpha;}
        } else if (line.rfind("vtf ", 0) == 0) {
            char nm[32];
            Vec3 r, s;
            if (sscanf(line.c_str(), "vtf %31s %f %f %f %f %f %f", nm, &r.x, &r.y, &r.z, &s.x, &s.y, &s.z) == 7) {
                BPVarDef* v = findVar(nm);
                if (v) { v->defRot = r; v->defScl = s; }
            }
        } else if (line.rfind("event ", 0) == 0) {
            BPEventDef e;
            snprintf(e.name, sizeof(e.name), "%s", line.substr(6).c_str());
            events.push_back(e);
        } else if (line.rfind("eventmeta ", 0) == 0 && !events.empty()) {
            int scope = VS_PUBLIC;
            if (sscanf(line.c_str(), "eventmeta %d", &scope) == 1)
                events.back().scope = (VarScope)clampf((float)scope, (float)VS_PUBLIC, (float)VS_PRIVATE);
        } else if (line.rfind("evpin ", 0) == 0) {
            if (!events.empty()) {
                BPFuncPin p;
                int kind = 0;
                if (sscanf(line.c_str(), "evpin %23s %d", p.name, &kind) == 2) {
                    p.kind = (PinKind)kind;
                    events.back().params.push_back(p);
                }
            }
        } else if (line.rfind("iface ", 0) == 0) {
            interfaces.push_back(line.substr(6));
        } else if (line.rfind("ifaceasset ", 0) == 0) {
            interfaceAssets.push_back(line.substr(11));
        } else if (line.rfind("disp ", 0) == 0) {
            BPDispatcherDef dispatcher;snprintf(dispatcher.name,sizeof(dispatcher.name),"%s",line.substr(5).c_str());dispatchers.push_back(dispatcher);
        } else if(line.rfind("dispcat ",0)==0&&!dispatchers.empty()){
            snprintf(dispatchers.back().category,sizeof(dispatchers.back().category),"%s",line.substr(8).c_str());
        } else if(line.rfind("disppin ",0)==0&&!dispatchers.empty()){
            BPFuncPin pin;int kind=0;if(sscanf(line.c_str(),"disppin %23s %d",pin.name,&kind)==2){pin.kind=(PinKind)kind;dispatchers.back().params.push_back(pin);}
        } else if (line.rfind("canvas ", 0) == 0) {
            std::string nm = line.substr(7);
            curFnIdx = -1;
            fnSigCleared = false;
            if (nm == "-") {
                cur = &main();
            } else if (!nm.empty() && nm[0] == '*') {
                const char* graphName = nm.c_str() + 1;
                BPFunc* existing = nullptr;
                for (BPFunc& gph : graphs)
                    if (strcmp(gph.name, graphName) == 0) { existing = &gph; break; }
                if (existing) {
                    existing->body.clear();
                    cur = &existing->body;
                } else {
                    BPFunc gph;
                    snprintf(gph.name, sizeof(gph.name), "%s", graphName);
                    graphs.push_back(gph);
                    cur = &graphs.back().body;
                }
            } else {
                BPFunc f;
                snprintf(f.name, sizeof(f.name), "%s", nm.c_str());
                if (version >= 4) { f.ins.clear(); f.outs.clear(); fnSigCleared = true; }
                funcs.push_back(f);
                cur = &funcs.back().body;
                curFnIdx = (int)funcs.size() - 1;
            }
        } else if (line.rfind("fncat ", 0) == 0 && curFnIdx >= 0 && curFnIdx < (int)funcs.size()) {
            snprintf(funcs[curFnIdx].category, sizeof(funcs[curFnIdx].category), "%s", line.substr(6).c_str());
        } else if (line.rfind("fnpure ", 0) == 0 && curFnIdx >= 0 && curFnIdx < (int)funcs.size()) {
            int pure = 0;
            if (sscanf(line.c_str(), "fnpure %d", &pure) == 1) funcs[curFnIdx].pure = pure != 0;
        } else if (line.rfind("fnmeta ", 0) == 0 && curFnIdx >= 0 && curFnIdx < (int)funcs.size()) {
            int scope = 0, expose = 1;
            if (sscanf(line.c_str(), "fnmeta %d %d", &scope, &expose) >= 1) {
                funcs[curFnIdx].scope = (VarScope)clampf((float)scope, (float)VS_PUBLIC, (float)VS_PRIVATE);
            }
        } else if ((line.rfind("fnin ", 0) == 0 || line.rfind("fnout ", 0) == 0) &&
                   curFnIdx >= 0 && curFnIdx < (int)funcs.size()) {
            BPFunc& f = funcs[curFnIdx];
            if (!fnSigCleared) { f.ins.clear(); f.outs.clear(); fnSigCleared = true; }
            bool isIn = line[2] == 'i';   // "fnin" vs "fnout"
            BPFuncPin p;
            int kind = 0;
            if (sscanf(line.c_str(), isIn ? "fnin %23s %d" : "fnout %23s %d", p.name, &kind) == 2) {
                p.kind = (PinKind)kind;
                if (isIn) f.ins.push_back(p); else f.outs.push_back(p);
            }
        } else if (line.rfind("node ", 0) == 0) {
            BPNode n;
            char key[48], sn[96] = "-";
            int got = sscanf(line.c_str(), "node %d %47s %f %f %f %d %95s", &n.id, key, &n.x, &n.y, &n.prop, &n.choice, sn);
            if (got >= 6) {
                int d = bpDefByKey(key);
                if (d >= 0) {
                    n.def = d;
                    if(d==BP_ACT_PRINT){n.lit[2]={1,1,1};n.litAlpha[2]=1.0f;}
                    if (got == 7 && strcmp(sn, "-") != 0) snprintf(n.sname, sizeof(n.sname), "%s", sn);
                    cur->nodes.push_back(n);
                    if (n.id >= cur->nextId) cur->nextId = n.id + 1;
                }
            }
        } else if (line.rfind("nstr ", 0) == 0) {
            int id = 0;
            if (sscanf(line.c_str(), "nstr %d", &id) == 1) {
                size_t split = line.find(' ', 5);
                if (split != std::string::npos) {
                    BPNode* n = cur->byId(id);
                    if (n) snprintf(n->sname, sizeof(n->sname), "%s", line.substr(split + 1).c_str());
                }
            }
        } else if (line.rfind("lit ", 0) == 0) {
            int id, pin;
            Vec3 v;
            if (sscanf(line.c_str(), "lit %d %d %f %f %f", &id, &pin, &v.x, &v.y, &v.z) == 5) {
                BPNode* n = cur->byId(id);
                if (n && pin >= 0 && pin < BP_MAX_PINS) n->lit[pin] = v;
            }
        } else if (line.rfind("lita ", 0) == 0) {
            int id=0,pin=0;float alpha=1;
            if(sscanf(line.c_str(),"lita %d %d %f",&id,&pin,&alpha)==3){BPNode*n=cur->byId(id);if(n&&pin>=0&&pin<BP_MAX_PINS)n->litAlpha[pin]=alpha;}
        } else if (line.rfind("slit ", 0) == 0) {
            int id = 0, pin = 0;
            if (sscanf(line.c_str(), "slit %d %d", &id, &pin) == 2) {
                size_t p1 = line.find(' ', 5);                                        // dopo id
                size_t p2 = p1 == std::string::npos ? p1 : line.find(' ', p1 + 1);    // dopo pin
                if (p2 != std::string::npos) {
                    BPNode* n = cur->byId(id);
                    if (n && pin >= 0 && pin < BP_MAX_PINS) n->slit[pin] = line.substr(p2 + 1);
                }
            }
        } else if (line.rfind("link ", 0) == 0) {
            BPLink l;
            if (sscanf(line.c_str(), "link %d %d %d %d", &l.fromNode, &l.fromPin, &l.toNode, &l.toPin) == 4) {
                if (cur->byId(l.fromNode) && cur->byId(l.toNode)) cur->links.push_back(l);
            }
        } else if (line.rfind("comment ", 0) == 0) {
            BPComment c;
            int off = 0;
            if (sscanf(line.c_str(), "comment %f %f %f %f %f %f %f %f%n",
                       &c.x, &c.y, &c.w, &c.h, &c.color.x, &c.color.y, &c.color.z, &c.fontSize, &off) == 8) {
                const char* t = line.c_str() + off;
                while (*t == ' ') t++;
                snprintf(c.text, sizeof(c.text), "%s", t);
                cur->comments.push_back(c);
            }
        }
    }
    syncRequiredVariables();
    return true;
}

// ═══ runtime ═══
// coerce a value to a declared variable type (Set nodes keep the var's type)
static void bpMergeInherited(BPGraph& parent, const BPGraph& child) {
    parent.classKind = child.classKind;
    parent.uniquePerObject = child.uniquePerObject;
    parent.parentAsset = child.parentAsset;
    if (!child.defaultPawnClass.empty()) parent.defaultPawnClass = child.defaultPawnClass;
    if (!child.playerControllerClass.empty()) parent.playerControllerClass = child.playerControllerClass;
    for (const std::string& tag : child.defaultTags)
        if (std::find(parent.defaultTags.begin(), parent.defaultTags.end(), tag) == parent.defaultTags.end()) parent.defaultTags.push_back(tag);
    for (const BPVarDef& value : child.vars) {
        auto it = std::find_if(parent.vars.begin(), parent.vars.end(), [&](const BPVarDef& inherited) { return _stricmp(inherited.name, value.name) == 0; });
        if (it == parent.vars.end()) parent.vars.push_back(value); else *it = value;
    }
    for (const BPFunc& value : child.funcs) {
        auto it = std::find_if(parent.funcs.begin(), parent.funcs.end(), [&](const BPFunc& inherited) { return _stricmp(inherited.name, value.name) == 0; });
        if (it == parent.funcs.end()) parent.funcs.push_back(value); else *it = value;
    }
    for (const BPEventDef& value : child.events) {
        auto it = std::find_if(parent.events.begin(), parent.events.end(), [&](const BPEventDef& inherited) { return _stricmp(inherited.name, value.name) == 0; });
        if (it == parent.events.end()) parent.events.push_back(value); else *it = value;
    }
    for (BPFunc graphCanvas : child.graphs) {
        std::string name = std::string("Child::") + graphCanvas.name;
        snprintf(graphCanvas.name, sizeof(graphCanvas.name), "%s", name.c_str());
        parent.graphs.push_back(std::move(graphCanvas));
    }
    auto mergeStrings = [](std::vector<std::string>& dst, const std::vector<std::string>& src) {
        for (const std::string& value : src) if (std::find(dst.begin(), dst.end(), value) == dst.end()) dst.push_back(value);
    };
    mergeStrings(parent.interfaces, child.interfaces);
    mergeStrings(parent.interfaceAssets, child.interfaceAssets);
    for (const BPRequiredComponent& required : child.requiredComponents) parent.requiredComponents.push_back(required);
    parent.syncRequiredVariables();
    for(const BPDispatcherDef& value:child.dispatchers){
        auto it=std::find_if(parent.dispatchers.begin(),parent.dispatchers.end(),[&](const BPDispatcherDef& inherited){return _stricmp(inherited.name,value.name)==0;});
        if(it==parent.dispatchers.end())parent.dispatchers.push_back(value);else *it=value;
    }
}

bool bpResolveBlueprintAssetPath(const std::string& projectDir, const std::string& requestedPath,
                                 std::string& resolvedPath) {
    resolvedPath.clear();
    if (projectDir.empty() || requestedPath.empty()) return false;
    std::string requested = requestedPath;
    if (requested.rfind("blueprint:", 0) == 0) requested.erase(0, 10);
    std::replace(requested.begin(), requested.end(), '/', '\\');
    std::error_code ec;
    fs::path exact = fs::path(projectDir) / requested;
    if (fs::is_regular_file(exact, ec)) {
        resolvedPath = fs::relative(exact, projectDir, ec).string();
        if (ec) resolvedPath = requested;
        return true;
    }
    ec.clear();
    const std::string wantedName = fs::path(requested).filename().string();
    if (wantedName.empty()) return false;
    std::vector<std::string> matches;
    for (fs::recursive_directory_iterator it(projectDir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec) || _stricmp(it->path().extension().string().c_str(), ".bp") != 0) continue;
        if (_stricmp(it->path().filename().string().c_str(), wantedName.c_str()) != 0) continue;
        std::string relative = fs::relative(it->path(), projectDir, ec).string();
        if (!ec) matches.push_back(relative); else ec.clear();
    }
    // A moved class can be repaired safely only when its filename is unique.
    if (matches.size() != 1) return false;
    resolvedPath = matches.front();
    return true;
}

static bool bpLoadResolvedGraphRec(const std::string& projectDir, const std::string& relativePath,
                                   BPGraph& out, std::set<std::string>& visiting, int depth) {
    if (relativePath.empty() || depth > 16) return false;
    std::string resolvedPath;
    if (!bpResolveBlueprintAssetPath(projectDir, relativePath, resolvedPath)) return false;
    std::string key = resolvedPath;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
    if (!visiting.insert(key).second) return false;
    std::string data;
    BPGraph child;
    if (!bpReadTextFile(projectDir + "\\" + resolvedPath, data) || !child.deserialize(data)) { visiting.erase(key); return false; }
    if (child.parentAsset.empty()) out = std::move(child);
    else {
        BPGraph parent;
        if (!bpLoadResolvedGraphRec(projectDir, child.parentAsset, parent, visiting, depth + 1)) { visiting.erase(key); return false; }
        bpMergeInherited(parent, child);
        out = std::move(parent);
    }
    visiting.erase(key);
    return true;
}

bool bpLoadResolvedGraph(const std::string& projectDir, const std::string& relativePath, BPGraph& out) {
    std::set<std::string> visiting;
    return bpLoadResolvedGraphRec(projectDir, relativePath, out, visiting, 0);
}

static BPValue bpCoerce(const BPValue& v, PinKind k) {
    switch (k) {
    case PIN_NUM: return BPValue::N(v.asNum());
    case PIN_INT: return BPValue::I((int)floorf(v.asNum() + 0.5f));
    case PIN_BOOL: return BPValue::B(v.asBool());
    case PIN_VEC: return BPValue::V(v.asVec());
    case PIN_VEC2: { Vec3 x = v.asVec(); return BPValue::V2(x.x, x.y); }
    case PIN_ENT:
        // Object asset references (currently Animator Controller) carry their
        // path in str; preserve it when wiring one Object variable into another.
        return v.kind == PIN_ENT && !v.str.empty() ? v : BPValue::E(v.asEnt());
    case PIN_STR: {
        if (v.kind == PIN_STR) return v;
        char b[48];
        snprintf(b, sizeof(b), "%.4g", v.asNum());
        return BPValue::S(b);
    }
    case PIN_TRANSFORM:
        if (v.kind == PIN_TRANSFORM) return v;
        return BPValue::T(v.asVec(), {}, { 1, 1, 1 });
    case PIN_TIMER_HANDLE: return BPValue::H(v.asTimerHandle());
    case PIN_ENUM: return BPValue::En((int)v.asNum());
    case PIN_COLOR: return v.kind==PIN_COLOR?v:BPValue::C(v.asVec(),v.alpha);
    case PIN_ANIMATION_CLIP:
    case PIN_ANIMATOR_CONTROLLER: return BPValue::Asset(k, v.str);
    default: return v;
    }
}

// human-readable form (Print String)
static void bpFormat(const BPValue& v, char* out, int cap) {
    switch (v.kind) {
    case PIN_VEC: snprintf(out, cap, "(%.2f, %.2f, %.2f)", v.vec.x, v.vec.y, v.vec.z); break;
    case PIN_VEC2: snprintf(out, cap, "(%.2f, %.2f)", v.vec.x, v.vec.y); break;
    case PIN_BOOL: snprintf(out, cap, "%s", v.b ? "true" : "false"); break;
    case PIN_STR: snprintf(out, cap, "%s", v.str.c_str()); break;
    case PIN_INT: snprintf(out, cap, "%d", (int)v.num); break;
    case PIN_ENT:
        snprintf(out, cap, "%s", !v.str.empty() ? v.str.c_str() : (std::string("entity #") + std::to_string(v.ent)).c_str());
        break;
    case PIN_TIMER_HANDLE: snprintf(out, cap, "timer #%d", v.asTimerHandle()); break;
    case PIN_ENUM: snprintf(out, cap, "%d", (int)v.num); break;
    case PIN_COLOR: snprintf(out,cap,"RGBA(%.2f, %.2f, %.2f, %.2f)",v.vec.x,v.vec.y,v.vec.z,v.alpha);break;
    case PIN_ANIMATION_CLIP:
    case PIN_ANIMATOR_CONTROLLER: snprintf(out, cap, "%s", v.str.empty() ? "None" : v.str.c_str()); break;
    default: snprintf(out, cap, "%.3f", v.num); break;
    }
}

// wildcard math: vector op scalar broadcasts the scalar on every component
static BPValue bpMathOp(int op, const BPValue& a, const BPValue& b) {
    if (a.isVec() || b.isVec()) {
        Vec3 x = a.isVec() ? a.vec : Vec3{ a.asNum(), a.asNum(), a.asNum() };
        Vec3 y = b.isVec() ? b.vec : Vec3{ b.asNum(), b.asNum(), b.asNum() };
        Vec3 res;
        switch (op) {
        case 0: res = x + y; break;
        case 1: res = x - y; break;
        case 2: res = { x.x * y.x, x.y * y.y, x.z * y.z }; break;
        default:
            res = { y.x != 0 ? x.x / y.x : 0, y.y != 0 ? x.y / y.y : 0, y.z != 0 ? x.z / y.z : 0 };
            break;
        }
        bool v2 = (!a.isVec() || a.kind == PIN_VEC2) && (!b.isVec() || b.kind == PIN_VEC2);
        if (v2) return BPValue::V2(res.x, res.y);
        return BPValue::V(res);
    }
    float x = a.asNum(), y = b.asNum();
    float res = op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : (y != 0 ? x / y : 0);
    if (a.kind == PIN_INT && b.kind == PIN_INT && op != 3) return BPValue::I((int)res);
    return BPValue::N(res);
}

// the function whose body is `cv` (function Entry/Return live inside it), or null
static BPFunc* funcOwning(BPGraph* g, const BPCanvas& cv) {
    if (!g) return nullptr;
    for (auto& f : g->funcs) if (&f.body == &cv) return &f;
    return nullptr;
}

// closest point of a body's collider to `center`; true if within `radius`
static bool bpSphereVsBody(const RigidBody& b, const Vec3& center, float radius, Vec3& cp, Vec3& normal) {
    if (b.shape.kind == ShapeKind::Sphere) {
        Vec3 d = center - b.position;
        float dl = d.length();
        if (dl > radius + b.shape.radius) return false;
        normal = dl > 1e-5f ? d * (1.0f / dl) : Vec3{ 0, 1, 0 };
        cp = b.position + normal * b.shape.radius;
        return true;
    }
    Quat inv = b.quat.conjugate();
    Vec3 lo = inv.rotate(center - b.position);
    Vec3 h = b.shape.h;
    Vec3 cl = { clampf(lo.x, -h.x, h.x), clampf(lo.y, -h.y, h.y), clampf(lo.z, -h.z, h.z) };
    Vec3 diff = lo - cl;
    float dl = diff.length();
    if (dl > radius) return false;
    Vec3 nLocal = dl > 1e-5f ? diff * (1.0f / dl) : Vec3{ 0, 1, 0 };
    normal = b.quat.rotate(nLocal);
    cp = b.position + b.quat.rotate(cl);
    return true;
}

// bit i of the mask enables hitting layer i; mask 0 = all layers
static bool bpLayerAllowed(unsigned mask, int layer) {
    return mask == 0 || (layer >= 0 && layer < 32 && ((mask >> layer) & 1u));
}

// line trace: closest collider hit along Start→End, ignoring `skip` and disabled
// bodies; layerMask filters which collision layers can be hit (0 = all)
static void bpLineTrace(BPContext& ctx, RigidBody* skip, const Vec3& start, const Vec3& end, unsigned layerMask, BPTraceResult& out) {
    if (!ctx.scene) return;
    Vec3 d = end - start;
    float dist = d.length();
    if (dist < 1e-5f) return;
    Vec3 dir = d * (1.0f / dist);
    RayHit best;
    bool found = false;
    float bestT = dist;
    for (auto& bp : ctx.scene->world.bodies) {
        RigidBody* b = bp.get();
        if (b == skip || !b->enabled) continue;
        if (!bpLayerAllowed(layerMask, b->layer)) continue;
        RayHit h;
        if (raycastBody(*b, start, dir, dist, h) && (!found || h.t < bestT)) {
            best = h;
            best.body = b;
            bestT = h.t;
            found = true;
        }
    }
    if (found) {
        out.hit = true;
        out.point = best.point;
        out.normal = best.normal;
        Entity* e = ctx.scene->byBody(best.body);
        out.actor = e ? e->id : 0;
    }
}

// sphere trace: march a sphere of `radius` along Start→End, first overlap wins
static void bpSphereTrace(BPContext& ctx, RigidBody* skip, const Vec3& start, const Vec3& end, float radius, unsigned layerMask, BPTraceResult& out) {
    if (!ctx.scene) return;
    Vec3 d = end - start;
    float dist = d.length();
    Vec3 dir = dist > 1e-5f ? d * (1.0f / dist) : Vec3{ 0, 0, 1 };
    float step = radius * 0.5f;
    if (step < 0.05f) step = 0.05f;
    int nSteps = (int)(dist / step) + 1;
    for (int i = 0; i <= nSteps; i++) {
        float t = i * step;
        if (t > dist) t = dist;
        Vec3 c = start + dir * t;
        for (auto& bp : ctx.scene->world.bodies) {
            RigidBody* b = bp.get();
            if (b == skip || !b->enabled) continue;
            if (!bpLayerAllowed(layerMask, b->layer)) continue;
            Vec3 cp, nrm;
            if (bpSphereVsBody(*b, c, radius, cp, nrm)) {
                out.hit = true;
                out.point = cp;
                out.normal = nrm;
                Entity* e = ctx.scene->byBody(b);
                out.actor = e ? e->id : 0;
                return;
            }
        }
        if (t >= dist) break;
    }
}

// widget handle from a BP value: <= 0 (unconnected pin) means "this widget",
// which is only meaningful while a widget's own graph is running
static int bpWidgetHandle(const BPValue& v, const BPContext& ctx) {
    int handle = (int)v.asNum();
    return handle > 0 ? handle : ctx.selfWidget;
}

// entity from a BP object value: id <= 0 (unconnected pin) means "self"
static Entity* bpResolveEnt(BPContext& ctx, Entity* self, int id) {
    if (id <= 0) return self;
    return ctx.scene ? ctx.scene->byId(id) : nullptr;
}

// inverse of Quat::fromEulerDeg: returns { X = roll, Y = pitch, Z = yaw }
static Vec3 bpQuatToEulerDeg(const Quat& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float sinp = 2 * (w * x - y * z);
    float roll, pitch, yaw;
    if (fabsf(sinp) < 0.9999f) {
        pitch = asinf(sinp);
        yaw = atan2f(2 * (x * z + w * y), 1 - 2 * (x * x + y * y));
        roll = atan2f(2 * (x * y + w * z), 1 - 2 * (x * x + z * z));
    } else {
        pitch = sinp > 0 ? PI / 2 : -PI / 2;
        yaw = atan2f(-2 * (x * z - w * y), 1 - 2 * (y * y + z * z));
        roll = 0;
    }
    const float r = 180.0f / PI;
    return { roll * r, pitch * r, yaw * r };
}

// mirror of the app-side attachment rule: children the physics does not
// simulate follow their parent, so a transform set must refresh the offset
static bool bpFollowsParent(const Entity& e) {
    return !(e.hasPhysics && e.body->enabled && e.body->type == BodyType::Dynamic);
}

static void bpSyncAttach(BPContext& ctx, Entity* e) {
    if (!e || !e->parentId || !ctx.scene) return;
    Entity* p = ctx.scene->byId(e->parentId);
    if (!p || !bpFollowsParent(*e)) return;
    e->attachPos = p->body->quat.conjugate().rotate(e->body->position - p->body->position);
    e->attachRot = p->body->quat.conjugate() * e->body->quat;
}

static bool bpEntityHasAnyBlueprint(const Entity& e) {
    if (e.graphPath[0]) return true;
    for (const BlueprintComponentDef& component : e.additionalBlueprints)
        if (!component.graphPath.empty()) return true;
    return false;
}

static bool bpHasComponent(const Entity& e, int cls) {
    switch (cls % BP_NCOMPS) {
    case 0: return e.isCamera;
    case 1: return e.isLight;
    case 2: return e.hasMesh;
    case 3: return e.hasPhysics;
    case 4: return e.hasAudio;
    case 5: return e.body != nullptr;       // every scene object owns a Transform
    case 6: return bpEntityHasAnyBlueprint(e); // any Blueprint script component
    case 7: return e.hasReverb;
    case 8: return e.hasAIAgent;
    case 9: return e.hasTrigger;
    default: return e.hasAnimator;
    }
}

static std::string bpCanonicalTag(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        if (isalnum(c)) out.push_back((char)tolower(c));
        else if ((c == ' ' || c == '_' || c == '-') && !out.empty() && out.back() != '_') out.push_back('_');
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

static bool bpActorHasTag(const Entity& e, const std::string& tag) {
    std::string wanted = bpCanonicalTag(tag);
    if (wanted.empty()) return false;
    for (const std::string& candidate : e.tags)
        if (bpCanonicalTag(candidate) == wanted) return true;
    return false;
}

static bool bpBlueprintIsA(const std::string& actorClass, const std::string& requestedClass) {
    std::string current = actorClass;
    for (int depth = 0; depth < 16 && !current.empty(); depth++) {
        if (_stricmp(current.c_str(), requestedClass.c_str()) == 0) return true;
        std::string data;
        BPGraph graph;
        if (!bpReadTextFile(gBPProjectDir + "\\" + current, data) || !graph.deserialize(data)) break;
        current = graph.parentAsset;
    }
    return false;
}

static bool bpEntityHasBlueprintClass(const Entity& e, const std::string& requestedClass) {
    if (requestedClass.empty()) return false;
    if (e.graphPath[0] && bpBlueprintIsA(e.graphPath, requestedClass)) return true;
    for (const BlueprintComponentDef& component : e.additionalBlueprints)
        if (!component.graphPath.empty() && bpBlueprintIsA(component.graphPath, requestedClass)) return true;
    return false;
}

static bool bpSameAssetPath(std::string a,std::string b) {
    for(char& c:a)if(c=='/')c='\\';
    for(char& c:b)if(c=='/')c='\\';
    return _stricmp(a.c_str(),b.c_str())==0;
}

static bool bpGraphImplementsInterface(const BPGraph& graph,const char* interfaceAsset) {
    if(!interfaceAsset||!interfaceAsset[0])return false;
    for(const std::string& implemented:graph.interfaceAssets)
        if(bpSameAssetPath(implemented,interfaceAsset))return true;
    return false;
}

static bool bpLoadInterfaceFunction(const std::string& projectDir,const std::string& interfaceAsset,
                                    const char* functionName,BPFunc& out) {
    if(projectDir.empty()||interfaceAsset.empty()||!functionName||!functionName[0])return false;
    std::string data;BPGraph interfaceGraph;
    if(!bpReadTextFile(projectDir+"\\"+interfaceAsset,data)||!interfaceGraph.deserialize(data))return false;
    BPFunc* function=interfaceGraph.findFunc(functionName);
    if(!function)return false;
    out=*function;return true;
}

static bool bpActorMatchesClass(const Entity& e, const char* classSpec) {
    if (!classSpec || !classSpec[0]) return false;
    const char* component = "component:";
    const char* blueprint = "blueprint:";
    if (strncmp(classSpec, component, strlen(component)) == 0)
        return bpHasComponent(e, atoi(classSpec + strlen(component)));
    if (strncmp(classSpec, blueprint, strlen(blueprint)) == 0)
        return bpEntityHasBlueprintClass(e, classSpec + strlen(blueprint));
    return bpEntityHasBlueprintClass(e, classSpec);
}

static std::string bpClassSpecLabel(const char* classSpec) {
    if (!classSpec || !classSpec[0]) return "Class";
    const char* component = "component:";
    const char* blueprint = "blueprint:";
    if (strncmp(classSpec, component, strlen(component)) == 0) {
        int index = atoi(classSpec + strlen(component));
        return index >= 0 && index < BP_NCOMPS ? BP_COMP_NAMES[index] : "Componente";
    }
    const char* path = strncmp(classSpec, blueprint, strlen(blueprint)) == 0
        ? classSpec + strlen(blueprint) : classSpec;
    std::string stem = fs::path(path).stem().string();
    return stem.empty() ? "Blueprint" : stem;
}

// A member target is either an object's Blueprint or a Widget. The Widget half
// of a .wgt lives after the marker, and it has no parent chain to resolve.
bool bpMemberClassIsWidget(const std::string& classSpec) {
    return classSpec.rfind("widget:", 0) == 0;
}
bool bpLoadWidgetGraph(const std::string& projectDir, const std::string& classSpec, BPGraph& out) {
    std::string path = classSpec;
    if (bpMemberClassIsWidget(path)) path.erase(0, 7);
    if (projectDir.empty() || path.empty()) return false;
    std::string data;
    if (!bpReadTextFile(projectDir + "\\" + path, data)) return false;
    size_t split = data.find(WIDGET_GRAPH_MARKER);
    if (split == std::string::npos) return false;
    return out.deserialize(data.substr(split + strlen(WIDGET_GRAPH_MARKER)));
}

static bool bpLoadMemberGraph(const std::string& projectDir, const BPNode& node, BPGraph& out) {
    std::string path = node.slit[0];
    if (bpMemberClassIsWidget(path)) return bpLoadWidgetGraph(projectDir, path, out);
    if (path.rfind("blueprint:", 0) == 0) path.erase(0, 10);
    return !projectDir.empty() && !path.empty() && bpLoadResolvedGraph(projectDir, path, out);
}

bool bpPinKindsCompatible(PinKind a, PinKind b) { return pinsCompatible(a, b); }

// Load the graph a reference class names ("blueprint:…" / "widget:…").
bool bpLoadClassGraph(const std::string& projectDir, const std::string& classSpec, BPGraph& out) {
    if (bpMemberClassIsWidget(classSpec)) return bpLoadWidgetGraph(projectDir, classSpec, out);
    if (classSpec.rfind("blueprint:", 0) != 0) return false;
    std::string path = classSpec.substr(10), resolved;
    if (bpResolveBlueprintAssetPath(projectDir, path, resolved)) path = resolved;
    return bpLoadResolvedGraph(projectDir, path, out);
}

// The dispatcher a Bind node targets: the class wired into its Target pin when
// there is one, otherwise this Blueprint's own.
bool bpFindBindDispatcher(const std::string& projectDir, const BPGraph& local,
                          const BPCanvas& cv, const BPNode& bindNode, BPDispatcherDef& out) {
    if (!bindNode.sname[0]) return false;
    std::string cls = bpPinRefClass(cv, local, bindNode.id, 2, false);
    if (cls.empty()) cls = bpPinRefClass(cv, local, bindNode.id, 3, false);
    BPGraph target;
    if (!cls.empty() && bpLoadClassGraph(projectDir, cls, target)) {
        if (const BPDispatcherDef* d = target.findDispatcher(bindNode.sname)) { out = *d; return true; }
        return false;                     // the target class simply has no such dispatcher
    }
    if (const BPDispatcherDef* d = const_cast<BPGraph&>(local).findDispatcher(bindNode.sname)) { out = *d; return true; }
    return false;
}

// The event a delegate pin carries: a Create Event node or a Custom Event node's
// delegate output both stand for the Custom Event of that name.
static const BPEventDef* bpDelegateEvent(const BPGraph& graph, const BPCanvas& cv, int nodeId) {
    const BPNode* n = cv.byId(nodeId);
    if (!n) return nullptr;
    if (n->def != BP_CREATE_EVENT && n->def != BP_EV_CUSTOM) return nullptr;
    return const_cast<BPGraph&>(graph).findEvent(n->sname);
}

// One line describing a parameter list, for the refusal message
static std::string bpSignatureText(const std::vector<BPFuncPin>& params) {
    if (params.empty()) return "()";
    std::string s = "(";
    for (size_t i = 0; i < params.size(); i++) {
        if (i) s += ", ";
        s += BP_VARTYPE_NAMES[(int)params[i].kind];
    }
    return s + ")";
}

bool BPEditor::delegateLinkAllowed(const BPCanvas& cv, int fromNode, int fromPin,
                                   int toNode, int toPin, std::string& why) const {
    (void)fromPin;
    const BPNode* target = cv.byId(toNode);
    if (!target || target->def != BP_BIND_EVENT || toPin != 1) return true;   // only the Event pin
    const BPEventDef* event = bpDelegateEvent(graph, cv, fromNode);
    if (!event) {
        why = "Bind Event: connect a Custom Event (or a Create Event naming one).";
        return false;
    }
    BPDispatcherDef disp;
    if (!bpFindBindDispatcher(projectDir, graph, cv, *target, disp))
        return true;   // dispatcher unknown here: nothing to check against yet
    bool same = disp.params.size() == event->params.size();
    for (size_t i = 0; same && i < disp.params.size(); i++)
        if (disp.params[i].kind != event->params[i].kind) same = false;
    if (same) return true;
    why = std::string("Bind Event: '") + event->name + bpSignatureText(event->params) +
          "' does not match Dispatcher '" + disp.name + bpSignatureText(disp.params) + "'.";
    return false;
}

static BPNodeDef bpMemberNodeDef(const std::string& projectDir, const BPNode& node);
BPNodeDef bpNodeDefForTest(const std::string& projectDir, const BPNode& node) {
    return bpMemberNodeDef(projectDir, node);
}

static BPNodeDef bpMemberNodeDef(const std::string& projectDir, const BPNode& node) {
    BPNodeDef d = DEFS[BP_MEMBER_ACCESS];
    static thread_local char names[BP_MAX_PINS][40];
    BPGraph owner;
    const bool loaded = bpLoadMemberGraph(projectDir, node, owner);
    // A widget target is addressed by its instance handle, not by an entity.
    const PinKind targetKind = bpMemberClassIsWidget(node.slit[0]) ? PIN_WIDGET : PIN_ENT;
    if (node.choice == 0) { // get variable
        d.category = 3; d.nIns = 1; d.ins[0] = { "Target", targetKind }; d.nOuts = 1;
        BPVarDef* var = loaded ? owner.findVar(node.sname) : nullptr;
        d.outs[0] = { "Value", var ? var->type : PIN_ANY };
    } else if (node.choice == 1) { // set variable
        d.category = 3; d.nIns = 3; d.ins[0] = { "", PIN_EXEC }; d.ins[1] = { "Target", targetKind };
        BPVarDef* var = loaded ? owner.findVar(node.sname) : nullptr;
        d.ins[2] = { "Value", var ? var->type : PIN_ANY };
        d.nOuts = 2; d.outs[0] = { "", PIN_EXEC }; d.outs[1] = { "Value", var ? var->type : PIN_ANY };
    } else if (node.choice == 4) { // custom event
        d.category = 1; d.nIns = 2; d.ins[0] = { "", PIN_EXEC }; d.ins[1] = { "Target", targetKind };
        BPEventDef* event = loaded ? owner.findEvent(node.sname) : nullptr;
        if (event) for (const BPFuncPin& pin : event->params) {
            if (d.nIns >= BP_MAX_PINS) break;
            int index = d.nIns; snprintf(names[index], sizeof(names[index]), "%s", pin.name);
            d.ins[d.nIns++] = { names[index], pin.kind };
        }
        d.nOuts = 1; d.outs[0] = { "", PIN_EXEC };
    } else { // function: choice 2 impure, 3 pure
        const bool pure = node.choice == 3;
        d.category = pure ? 2 : 1; d.nIns = 0; d.nOuts = 0;
        if (!pure) d.ins[d.nIns++] = { "", PIN_EXEC };
        d.ins[d.nIns++] = { "Target", targetKind };
        BPFunc* function = loaded ? owner.findFunc(node.sname) : nullptr;
        if (function) for (const BPFuncPin& pin : function->ins) {
            if (d.nIns >= BP_MAX_PINS) break;
            int index = d.nIns; snprintf(names[index], sizeof(names[index]), "%s", pin.name);
            d.ins[d.nIns++] = { names[index], pin.kind };
        }
        if (!pure) d.outs[d.nOuts++] = { "", PIN_EXEC };
        if (function) for (const BPFuncPin& pin : function->outs) {
            if (d.nOuts >= BP_MAX_PINS) break;
            int index = d.nOuts; snprintf(names[index], sizeof(names[index]), "%s", pin.name);
            d.outs[d.nOuts++] = { names[index], pin.kind };
        }
    }
    return d;
}

static std::string mapKey(float k) {
    char b[24];
    snprintf(b, sizeof(b), "%.3f", k);
    return b;
}

void BPInstance::initVars(const std::map<std::string, Vec3>* overrides,const std::map<std::string,float>* alphaOverrides) {
    vars.clear();
    nodeState_.clear();
    lastRets_.clear();
    traceResults_.clear();
    spawnResults_.clear();
    loopEl_.clear();
    loopIdx_.clear();
    frames_.clear();
    curEventArgs_.clear();
    dispatchBindings_.clear();
    overlapBindings_.clear();
    delays_.clear();
    timers_.clear();
    timerNodeHandles_.clear();
    nextTimerHandle_ = 1;
    if (!graph) return;
    for (const auto& d : graph->vars) {
        BPVarStore st;
        Vec3 v = d.def;
        if (d.requiredGenerated && entity) {
            v.x = (float)entity->id;
        } else if (overrides && (d.expose || d.exposeOnSpawn) && d.container == VC_SINGLE) {
            auto it = overrides->find(d.name);
            if (it != overrides->end()) v = it->second;
        }
        switch (d.type) {
        case PIN_VEC: st.single = BPValue::V(v); break;
        case PIN_VEC2: st.single = BPValue::V2(v.x, v.y); break;
        case PIN_BOOL: st.single = BPValue::B(v.x != 0); break;
        case PIN_ENT:
            st.single = bpIsAnimatorControllerObject(d) ? BPValue::Asset(PIN_ENT, d.assetPath) : BPValue::E((int)v.x);
            break;
        case PIN_INT: st.single = BPValue::I((int)v.x); break;
        // a widget reference starts empty; only Create Widget can fill it
        case PIN_WIDGET: st.single = BPValue::W(0); break;
        case PIN_STR: st.single = BPValue::S(d.strDef); break;
        case PIN_TRANSFORM: {
            // explicit transform value: location = v (overridden above), rotation/scale
            // from their own per-instance override keys ("<name>#rot" / "<name>#scl")
            Vec3 rot = d.defRot, scl = d.defScl;
            if (overrides && (d.expose || d.exposeOnSpawn) && d.container == VC_SINGLE) {
                auto ir = overrides->find(std::string(d.name) + "#rot"); if (ir != overrides->end()) rot = ir->second;
                auto is = overrides->find(std::string(d.name) + "#scl"); if (is != overrides->end()) scl = is->second;
            }
            st.single = BPValue::T(v, rot, scl);
            break;
        }
        case PIN_TIMER_HANDLE: st.single = BPValue::H((int)v.x); break;
        case PIN_ANIMATION_CLIP:
        case PIN_ANIMATOR_CONTROLLER: st.single = BPValue::Asset(d.type, d.assetPath); break;
        case PIN_ENUM: st.single = BPValue::En((int)v.x); break;
        case PIN_COLOR: {
            float alpha=d.defAlpha;if(alphaOverrides){auto it=alphaOverrides->find(d.name);if(it!=alphaOverrides->end())alpha=it->second;}
            st.single=BPValue::C(v,alpha);break;
        }
        default: st.single = BPValue::N(v.x); break;
        }
        vars[d.name] = st;
    }
}

BPVarStore* BPInstance::store(const char* name) {
    auto it = vars.find(name);
    return it == vars.end() ? nullptr : &it->second;
}

int BPInstance::timerHandleFor(const BPCanvas& cv, int nodeId) {
    std::pair<const BPCanvas*, int> key{ &cv, nodeId };
    auto it = timerNodeHandles_.find(key);
    if (it != timerNodeHandles_.end()) return it->second;
    int handle = nextTimerHandle_++;
    if (nextTimerHandle_ <= 0) nextTimerHandle_ = 1;
    timerNodeHandles_[key] = handle;
    return handle;
}

void BPInstance::updateLatent(BPContext& ctx) {
    struct Due {
        const BPCanvas* canvas;
        int nodeId;
        int outPin;
        std::string eventName;
        std::string functionName;
    };
    std::vector<Due> due;
    float dt = ctx.dt > 0 ? ctx.dt : 0;

    for (auto it = delays_.begin(); it != delays_.end();) {
        it->second.remaining -= dt;
        if (it->second.remaining <= 0) {
            due.push_back({ it->second.canvas, it->second.nodeId, 0, {}, {} });
            it = delays_.erase(it);
        } else ++it;
    }
    for (auto it = timers_.begin(); it != timers_.end();) {
        BPTimerState& t = it->second;
        if (t.paused) { ++it; continue; }
        t.remaining -= dt;
        if (t.remaining <= 0) {
            due.push_back({ t.canvas, t.nodeId, 1, t.eventName, t.functionName });
            if (t.looping) {
                t.remaining += t.rate > 0.0001f ? t.rate : 0.0001f;
                if (t.remaining <= 0) t.remaining = t.rate;
                ++it;
            } else {
                it = timers_.erase(it);
            }
        } else ++it;
    }

    // Remove/advance the states before resuming their graphs: a completion
    // chain is free to start, clear or retrigger the same latent action.
    for (const Due& d : due) {
        if (dead || !d.canvas || !d.canvas->byId(d.nodeId)) continue;
        if (!d.eventName.empty()) fireCustom(d.eventName.c_str(), ctx);
        if (!dead && !d.functionName.empty() && graph) {
            if (BPFunc* fn = graph->findFunc(d.functionName.c_str())) callFunction(*fn, {}, ctx, 0);
            else if (ctx.log) ctx.log(2, "Blueprint Timer: function '%s' not found.", d.functionName.c_str());
        }
        if (dead) continue;
        execChain(*d.canvas, d.nodeId, d.outPin, ctx, 0);
    }
}

void BPInstance::applyRefOverrides(const std::map<std::string, Vec3>*, EditorScene*) {
    // Transform variables are now explicit Location/Rotation/Scale values, fully
    // initialised in initVars — they are no longer bound to a scene object here.
}

BPValue BPInstance::readIn(const BPCanvas& cv, const BPNode& n, int pinIdx, BPContext& ctx, int depth) {
    const BPLink* link = cv.linkInto(n.id, pinIdx);
    if (link && depth < 40) {
        const BPNode* src = cv.byId(link->fromNode);
        if (src) return evalOut(cv, *src, link->fromPin, ctx, depth + 1);
    }
    PinKind k;
    if (n.def == BP_FN_RETURN) {
        BPFunc* f = funcOwning(graph, cv);
        int off = f && f->pure ? 0 : 1;
        k = !off && f && pinIdx < (int)f->outs.size() ? f->outs[pinIdx].kind
          : off && pinIdx == 0 ? PIN_EXEC
          : f && pinIdx - off >= 0 && pinIdx - off < (int)f->outs.size() ? f->outs[pinIdx - off].kind : PIN_ANY;
    } else if (n.def == BP_CALL_FUNC) {
        BPFunc* f = graph ? graph->findFunc(n.sname) : nullptr;
        int off = f && f->pure ? 0 : 1;
        k = !off && f && pinIdx < (int)f->ins.size() ? f->ins[pinIdx].kind
          : off && pinIdx == 0 ? PIN_EXEC
          : f && pinIdx - off >= 0 && pinIdx - off < (int)f->ins.size() ? f->ins[pinIdx - off].kind : PIN_ANY;
    } else if (n.def == BP_CALL_EVENT) {
        BPEventDef* ed = graph ? graph->findEvent(n.sname) : nullptr;
        k = pinIdx == 0 ? PIN_EXEC : (ed && pinIdx - 1 < (int)ed->params.size() ? ed->params[pinIdx - 1].kind : PIN_ANY);
    } else if(n.def==BP_INTERFACE_MESSAGE){
        if(pinIdx==0)k=PIN_EXEC;
        else if(pinIdx==1)k=PIN_ENT;
        else{BPFunc function;k=bpLoadInterfaceFunction(gBPProjectDir,n.slit[0],n.sname,function)&&pinIdx-2<(int)function.ins.size()?function.ins[pinIdx-2].kind:PIN_ANY;}
    } else if(n.def==BP_CALL_DISPATCH){
        BPDispatcherDef* dispatcher=graph?graph->findDispatcher(n.sname):nullptr;
        k=pinIdx==0?PIN_EXEC:(dispatcher&&pinIdx-1<(int)dispatcher->params.size()?dispatcher->params[pinIdx-1].kind:PIN_ANY);
    } else if (n.def == BP_MEMBER_ACCESS) {
        BPNodeDef member = bpMemberNodeDef(gBPProjectDir, n);
        k = pinIdx >= 0 && pinIdx < member.nIns ? member.ins[pinIdx].kind : PIN_ANY;
    } else if (n.def == BP_SPAWN_PREFAB) {
        if (pinIdx == 0) k = PIN_EXEC;
        else if (pinIdx == 1) k = PIN_TRANSFORM;
        else {
            std::vector<BPSpawnPinInfo> pins = bpSpawnPinInfo(gBPProjectDir, n.sname);
            k = pinIdx - 2 >= 0 && pinIdx - 2 < (int)pins.size() ? pins[pinIdx - 2].kind : PIN_ANY;
        }
    } else if (n.def == BP_SELECT_ENUM) {
        k = pinIdx == 0 ? PIN_ENUM : PIN_ANY;
    } else if (n.def == BP_SWITCH_ENUM) {
        k = pinIdx == 0 ? PIN_EXEC : PIN_ENUM;
    } else {
        k = DEFS[n.def].ins[pinIdx].kind;
    }
    // il pin valore del Set porta il tipo della variabile: risolvi ANY→tipo reale
    PinKind lk = k;
    if (k == PIN_ANY && (n.def == BP_VAR_SET || n.def == BP_LOCAL_SET) && pinIdx == 1 && graph) {
        BPVarDef* vd = graph->findVar(n.sname);
        if (vd) lk = vd->type;
    }
    BPValue v;
    v.kind = lk == PIN_ANY ? PIN_NUM : lk;
    if (lk == PIN_VEC) v.vec = n.lit[pinIdx];
    else if (lk == PIN_VEC2) v.vec = { n.lit[pinIdx].x, n.lit[pinIdx].y, 0 };
    else if (lk == PIN_BOOL) v.b = n.lit[pinIdx].x != 0;
    else if (lk == PIN_ENT) v = BPValue::E((int)n.lit[pinIdx].x);
    // an unwired widget pin means "no widget"; bpWidgetHandle turns 0 into self
    else if (lk == PIN_WIDGET) v = BPValue::W((int)n.lit[pinIdx].x);
    else if (lk == PIN_STR) v = BPValue::S(n.slit[pinIdx]);
    else if (lk == PIN_TIMER_HANDLE) v = BPValue::H((int)n.lit[pinIdx].x);
    else if (lk == PIN_ENUM) v = BPValue::En((int)n.lit[pinIdx].x);
    else if (lk == PIN_COLOR) v = BPValue::C(n.lit[pinIdx],n.litAlpha[pinIdx]);
    else if (lk == PIN_TRANSFORM) v = BPValue::T(n.lit[pinIdx], {}, { 1, 1, 1 });
    else v.num = n.lit[pinIdx].x;
    return v;
}

BPValue BPInstance::evalOut(const BPCanvas& cv, const BPNode& n, int outPin, BPContext& ctx, int depth) {
    RigidBody* body = entity ? entity->body : nullptr;
    switch (n.def) {
    case BP_EV_TICK: return BPValue::N(ctx.dt);
    case BP_EV_W_TICK: return BPValue::N(ctx.dt);
    case BP_EV_W_MOUSE_ENTER: case BP_EV_W_MOUSE_LEAVE:
    case BP_EV_W_MOUSE_DOWN: case BP_EV_W_MOUSE_UP:
        return BPValue::S(ctx.eventWidgetElement ? ctx.eventWidgetElement : "");
    case BP_EV_HIT: return outPin == 2 ? BPValue::E(ctx.eventOther) : BPValue::N(ctx.eventImpulse);
    case BP_EV_BEGIN_OVERLAP:
    case BP_EV_END_OVERLAP:
        return BPValue::E(outPin == 1 ? (entity ? entity->id : 0) : ctx.eventOther);
    case BP_EV_KEY: {
        // "Value": 1/0 for keys; mouse delta for X/Y; Vector2 for XY
        int c = n.choice % BP_NBINDS;
        if (c >= BP_NKEYS) {
            if (!ctx.axisValues) return BPValue::N(0);
            int a = c - BP_NKEYS;
            if (a == 0) return BPValue::N(ctx.axisValues[0]);
            if (a == 1) return BPValue::N(ctx.axisValues[1]);
            return BPValue::V2(ctx.axisValues[0], ctx.axisValues[1]);
        }
        return BPValue::N(ctx.keysDown && ctx.keysDown[c] ? 1.0f : 0.0f);
    }
    case BP_VAL_NUM: return BPValue::N(n.prop);
    case BP_M_PI: return BPValue::N(3.14159265358979323846f);
    case BP_SPAWN_PREFAB:
    case BP_CREATE_SAVE_GAME: {
        auto it = spawnResults_.find(n.id);
        return BPValue::E(it != spawnResults_.end() ? it->second : 0);
    }
    case BP_CREATE_WIDGET: {
        auto it = spawnResults_.find(n.id);
        return BPValue::W(it != spawnResults_.end() ? it->second : 0);
    }
    // property getters: pure reads of the live widget tree
    case BP_GET_WIDGET_NUM: {
        float value = 0;
        int handle = bpWidgetHandle(readIn(cv, n, 0, ctx, depth), ctx);
        std::string element = readIn(cv, n, 1, ctx, depth).str;
        if (ctx.getWidgetNumber) ctx.getWidgetNumber(handle, element.c_str(), n.sname, value);
        return BPValue::N(value);
    }
    // ── direct readers: the node names the property, so there is no combo ──
    case BP_GET_WIDGET_PERCENT:
    case BP_GET_WIDGET_RANGE:
    case BP_GET_WIDGET_HALIGN:
    case BP_GET_WIDGET_VALIGN:
    case BP_GET_WIDGET_ANCHOR:
    case BP_GET_WIDGET_PIVOT:
    case BP_GET_WIDGET_TEXT:
    case BP_GET_WIDGET_VISIBLE:
    case BP_GET_WIDGET_ENABLED:
    case BP_GET_WIDGET_OPACITY:
    case BP_GET_WIDGET_SIZE:
    case BP_GET_WIDGET_POSITION:
    case BP_GET_WIDGET_COLOR_DIRECT: {
        const int handle = bpWidgetHandle(readIn(cv, n, 0, ctx, depth), ctx);
        const std::string element = readIn(cv, n, 1, ctx, depth).str;
        auto num = [&](const char* prop) {
            float v = 0;
            if (ctx.getWidgetNumber) ctx.getWidgetNumber(handle, element.c_str(), prop, v);
            return v;
        };
        auto boolean = [&](const char* prop) {
            bool v = false;
            if (ctx.getWidgetBool) ctx.getWidgetBool(handle, element.c_str(), prop, v);
            return v;
        };
        switch (n.def) {
        case BP_GET_WIDGET_PERCENT: return BPValue::N(num("Percent"));
        case BP_GET_WIDGET_RANGE:   return BPValue::N(num(outPin == 1 ? "Max" : "Min"));
        case BP_GET_WIDGET_HALIGN:  return BPValue::En((int)num("H Align"));
        case BP_GET_WIDGET_VALIGN:  return BPValue::En((int)num("V Align"));
        case BP_GET_WIDGET_ANCHOR:  return BPValue::En((int)num("Anchor"));
        case BP_GET_WIDGET_PIVOT:   return BPValue::V2(num("Alignment X"), num("Alignment Y"));
        case BP_GET_WIDGET_VISIBLE: return BPValue::B(boolean("Visible"));
        case BP_GET_WIDGET_ENABLED: return BPValue::B(boolean("Is Enabled"));
        case BP_GET_WIDGET_OPACITY: return BPValue::N(num("Render Opacity"));
        case BP_GET_WIDGET_SIZE:    return BPValue::N(num(outPin == 1 ? "Height" : "Width"));
        case BP_GET_WIDGET_POSITION:return BPValue::N(num(outPin == 1 ? "Y" : "X"));
        case BP_GET_WIDGET_TEXT: {
            std::string value;
            if (ctx.getWidgetString) ctx.getWidgetString(handle, element.c_str(), "Text", value);
            return BPValue::S(value);
        }
        default: {   // BP_GET_WIDGET_COLOR_DIRECT
            Vec3 rgb{ 1, 1, 1 }; float alpha = 1;
            if (ctx.getWidgetColor) ctx.getWidgetColor(handle, element.c_str(), "Color", rgb, alpha);
            return BPValue::C(rgb, alpha);
        }
        }
    }
    case BP_GET_WIDGET_STR: {
        std::string value;
        int handle = bpWidgetHandle(readIn(cv, n, 0, ctx, depth), ctx);
        std::string element = readIn(cv, n, 1, ctx, depth).str;
        if (ctx.getWidgetString) ctx.getWidgetString(handle, element.c_str(), n.sname, value);
        return BPValue::S(value);
    }
    case BP_GET_WIDGET_COLOR: {
        Vec3 rgb{}; float alpha = 1;
        int handle = bpWidgetHandle(readIn(cv, n, 0, ctx, depth), ctx);
        std::string element = readIn(cv, n, 1, ctx, depth).str;
        if (ctx.getWidgetColor) ctx.getWidgetColor(handle, element.c_str(), n.sname, rgb, alpha);
        return BPValue::C(rgb, alpha);
    }
    case BP_GET_WIDGET_BOOL: {
        bool value = false;
        int handle = bpWidgetHandle(readIn(cv, n, 0, ctx, depth), ctx);
        std::string element = readIn(cv, n, 1, ctx, depth).str;
        if (ctx.getWidgetBool) ctx.getWidgetBool(handle, element.c_str(), n.sname, value);
        return BPValue::B(value);
    }
    case BP_SELECT_ENUM: {
        BPEnumAsset en;
        int count = bpLoadEnumAsset(gBPProjectDir, n.sname, en) ? (int)en.values.size() : 2;
        int selected = (int)readIn(cv, n, 0, ctx, depth).asNum();
        if (selected < 0 || selected >= count) selected = 0;
        return readIn(cv, n, selected + 1, ctx, depth);
    }
    case BP_VAL_VEC:
        return BPValue::V({ readIn(cv, n, 0, ctx, depth).asNum(), readIn(cv, n, 1, ctx, depth).asNum(), readIn(cv, n, 2, ctx, depth).asNum() });
    case BP_VAL_POS: return BPValue::V(body ? body->position : Vec3{});
    case BP_VAL_VEL: return BPValue::V(body ? body->velocity : Vec3{});
    case BP_VAL_TIME: return BPValue::N((float)ctx.time);
    case BP_VAL_KEYDOWN:
        return BPValue::B(ctx.keysDown && n.choice >= 0 && n.choice < BP_NKEYS && ctx.keysDown[n.choice]);
    case BP_VAL_RANDOM: {
        float mn = readIn(cv, n, 0, ctx, depth).asNum(), mx = readIn(cv, n, 1, ctx, depth).asNum();
        return BPValue::N(mn + (mx - mn) * (rand() / (float)RAND_MAX));
    }
    case BP_FLOAT_TO_STRING: {
        char text[48];snprintf(text,sizeof(text),"%.7g",readIn(cv,n,0,ctx,depth).asNum());return BPValue::S(text);
    }
    case BP_INT_TO_STRING: {
        char text[32];snprintf(text,sizeof(text),"%d",(int)readIn(cv,n,0,ctx,depth).asNum());return BPValue::S(text);
    }
    case BP_BOOL_TO_STRING:
        return BPValue::S(readIn(cv,n,0,ctx,depth).asBool()?"true":"false");
    case BP_M_TRUNCATE:
        return BPValue::I((int)truncf(readIn(cv,n,0,ctx,depth).asNum()));
    case BP_CURVE_EVAL: {
        float t = readIn(cv, n, 0, ctx, depth).asNum();
        return BPValue::N(ctx.evalCurve && n.sname[0] ? ctx.evalCurve(n.sname, t) : 0.0f);
    }
    case BP_SELF: return BPValue::E(entity ? entity->id : 0);
    case BP_GET_GAME_MODE: return BPValue::E(ctx.gameModeEntity);
    case BP_GET_GAME_INSTANCE: return BPValue::E(ctx.gameInstanceEntity);
    case BP_GET_PLAYER_CONTROLLER: return BPValue::E(ctx.playerControllerEntity);
    case BP_GET_PLAYER_PAWN: return BPValue::E(ctx.playerPawnEntity);
    case BP_SAVE_GAME_SLOT:
    case BP_LOAD_GAME_SLOT:
        return BPValue::B(nodeState_[n.id] > 0.5f);
    case BP_SAVE_GAME_EXISTS: {
        std::string slot = readIn(cv, n, 0, ctx, depth).str;
        return BPValue::B(ctx.saveGameExists && ctx.saveGameExists(slot.c_str()));
    }
    case BP_GET_CURRENT_LEVEL:
        return BPValue::S(ctx.currentLevelName ? ctx.currentLevelName : "");
    case BP_FIND: {
        if (ctx.scene) {
            for (auto& e : ctx.scene->entities) {
                if (strcmp(e.name, n.sname) == 0) return BPValue::E(e.id);
            }
        }
        return BPValue::E(0);
    }
    case BP_FIND_BY_TAG: {
        std::string tag = readIn(cv, n, 0, ctx, depth).str;
        if (ctx.scene) for (const Entity& actor : ctx.scene->entities) {
            if (!bpActorHasTag(actor, tag)) continue;
            return outPin == 1 ? BPValue::B(true) : BPValue::E(actor.id);
        }
        return outPin == 1 ? BPValue::B(false) : BPValue::E(0);
    }
    case BP_ISVALID: {
        int id = readIn(cv, n, 0, ctx, depth).asEnt();
        return BPValue::B(ctx.scene && id > 0 && ctx.scene->byId(id) != nullptr);
    }
    case BP_DOES_IMPLEMENT_INTERFACE: {
        Entity* target=bpResolveEnt(ctx,entity,readIn(cv,n,0,ctx,depth).asEnt());
        if(!target||!n.sname[0])return BPValue::B(false);
        // The currently executing graph is already parent-resolved by the app,
        // so inherited interfaces work without another disk read.
        if(target==entity&&graph&&bpGraphImplementsInterface(*graph,n.sname))return BPValue::B(true);
        auto implements = [&](const std::string& path) {
            BPGraph resolved;
            return !path.empty() && bpLoadResolvedGraph(gBPProjectDir,path,resolved) &&
                   bpGraphImplementsInterface(resolved,n.sname);
        };
        if(target->graphPath[0]&&implements(target->graphPath))return BPValue::B(true);
        for(const BlueprintComponentDef& component:target->additionalBlueprints)
            if(implements(component.graphPath))return BPValue::B(true);
        return BPValue::B(false);
    }
    case BP_CAST_TO_CLASS: {
        if (outPin != 2) return BPValue{};
        Entity* target = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, depth).asEnt());
        return BPValue::E(target && bpActorMatchesClass(*target, n.sname) ? target->id : 0);
    }
    case BP_INTERFACE_MESSAGE: {
        if(outPin==0)return BPValue{};
        auto found=lastRets_.find(n.id);int index=outPin-1;
        return found!=lastRets_.end()&&index>=0&&index<(int)found->second.size()?found->second[index]:BPValue{};
    }
    case BP_MEMBER_ACCESS: {
        const int targetPin = n.choice == 3 ? 0 : (n.choice == 0 ? 0 : 1);
        const int target = readIn(cv, n, targetPin, ctx, depth).asEnt();
        const bool onWidget = bpMemberClassIsWidget(n.slit[0]);
        if (n.choice == 0) {
            if (onWidget) return ctx.getWidgetMember ? ctx.getWidgetMember(target, n.sname) : BPValue{};
            return ctx.getBlueprintMember ? ctx.getBlueprintMember(target, n.slit[0].c_str(), n.sname) : BPValue{};
        }
        if (n.choice == 1) return outPin == 1 ? readIn(cv, n, 2, ctx, depth) : BPValue{};
        if (n.choice == 3) {
            BPGraph owner; std::vector<BPValue> args;
            BPFunc* function = bpLoadMemberGraph(gBPProjectDir, n, owner) ? owner.findFunc(n.sname) : nullptr;
            if (function) for (int i = 0; i < (int)function->ins.size(); i++) args.push_back(readIn(cv, n, i + 1, ctx, depth));
            std::vector<BPValue> values =
                onWidget ? (ctx.callWidgetMember ? ctx.callWidgetMember(target, n.sname, args) : std::vector<BPValue>{})
                         : (ctx.callBlueprintMember ? ctx.callBlueprintMember(target, n.slit[0].c_str(), n.sname, args)
                                                    : std::vector<BPValue>{});
            return outPin >= 0 && outPin < (int)values.size() ? values[outPin] : BPValue{};
        }
        auto found = lastRets_.find(n.id);
        int index = outPin - 1;
        return found != lastRets_.end() && index >= 0 && index < (int)found->second.size() ? found->second[index] : BPValue{};
    }
    case BP_TIMER_IS_VALID: {
        int handle = readIn(cv, n, 0, ctx, depth).asTimerHandle();
        return BPValue::B(handle > 0 && timers_.find(handle) != timers_.end());
    }
    case BP_VAR_GET:
    case BP_VAR_SET: {   // Set: il pin di ritorno da' la variabile (appena) settata
        BPVarStore* s = store(n.sname);
        return s ? s->single : BPValue{};
    }
    case BP_LOCAL_GET:
    case BP_LOCAL_SET: {
        if (!frames_.empty()) {
            auto it = frames_.back().locals.find(n.sname);
            if (it != frames_.back().locals.end()) return it->second;
        }
        return BPValue{};
    }
    case BP_ARR_GET: {
        BPVarStore* s = store(n.sname);
        int idx = (int)readIn(cv, n, 0, ctx, depth).asNum();
        if (s && idx >= 0 && idx < (int)s->arr.size()) return s->arr[idx];
        return BPValue{};
    }
    case BP_ARR_LEN: {
        BPVarStore* s = store(n.sname);
        return BPValue::N(s ? (float)s->arr.size() : 0.0f);
    }
    case BP_MAP_GET: {
        BPVarStore* s = store(n.sname);
        if (s) {
            auto it = s->mapv.find(mapKey(readIn(cv, n, 0, ctx, depth).asNum()));
            if (it != s->mapv.end()) return outPin == 1 ? BPValue::B(true) : it->second;
        }
        return outPin == 1 ? BPValue::B(false) : BPValue{};
    }
    case BP_MAP_LEN: {
        BPVarStore* s = store(n.sname);
        return BPValue::N(s ? (float)s->mapv.size() : 0.0f);
    }
    case BP_CALL_FUNC: {
        BPFunc* fn = graph ? graph->findFunc(n.sname) : nullptr;
        if (fn && fn->pure) {
            std::vector<BPValue> args;
            for (size_t i = 0; i < fn->ins.size(); i++) args.push_back(readIn(cv, n, (int)i, ctx, depth));
            std::vector<BPValue> rets = callFunction(*fn, args, ctx, depth + 1);
            return outPin >= 0 && outPin < (int)rets.size() ? rets[outPin] : BPValue{};
        }
        auto it = lastRets_.find(n.id);
        int i = outPin - 1;   // outPin 0 is exec
        if (it != lastRets_.end() && i >= 0 && i < (int)it->second.size()) return it->second[i];
        return BPValue{};
    }
    case BP_TRACE_LINE:
    case BP_TRACE_SPHERE: {
        auto it = traceResults_.find(n.id);
        BPTraceResult r = it != traceResults_.end() ? it->second : BPTraceResult{};
        switch (outPin) {
        case 1: return BPValue::B(r.hit);
        case 2: return BPValue::V(r.point);
        case 3: return BPValue::V(r.normal);
        case 4: return BPValue::E(r.actor);
        default: return BPValue{};
        }
    }
    case BP_FN_ENTRY:
        if (!frames_.empty()) {
            BPFunc* fn = funcOwning(graph, cv);
            int i = outPin - (fn && fn->pure ? 0 : 1);
            if (i >= 0 && i < (int)frames_.back().params.size()) return frames_.back().params[i];
        }
        return BPValue{};
    case BP_FLOW_FOR: {
        auto it = loopIdx_.find(n.id);
        return BPValue::N(it != loopIdx_.end() ? it->second : 0.0f);
    }
    case BP_FLOW_FOREACH: {
        if (outPin == 1) {
            auto it = loopEl_.find(n.id);
            return it != loopEl_.end() ? it->second : BPValue{};
        }
        auto it = loopIdx_.find(n.id);
        return BPValue::N(it != loopIdx_.end() ? it->second : 0.0f);
    }
    case BP_GET_ALL_WITH_CLASS:
    case BP_GET_ALL_WITH_TAG: {
        if (outPin == 1) {
            auto it = loopEl_.find(n.id);
            return it != loopEl_.end() ? it->second : BPValue::E(0);
        }
        auto it = loopIdx_.find(n.id);
        return BPValue::I(it != loopIdx_.end() ? (int)it->second : 0);
    }
    case BP_FLOW_FLIPFLOP: return BPValue::B(nodeState_[n.id] < 0.5f); // eA: true if next is A
    case BP_M_ADD: return bpMathOp(0, readIn(cv, n, 0, ctx, depth), readIn(cv, n, 1, ctx, depth));
    case BP_M_SUB: return bpMathOp(1, readIn(cv, n, 0, ctx, depth), readIn(cv, n, 1, ctx, depth));
    case BP_M_MUL: return bpMathOp(2, readIn(cv, n, 0, ctx, depth), readIn(cv, n, 1, ctx, depth));
    case BP_M_DIV: return bpMathOp(3, readIn(cv, n, 0, ctx, depth), readIn(cv, n, 1, ctx, depth));
    case BP_BREAK_V3: {
        Vec3 v = readIn(cv, n, 0, ctx, depth).asVec();
        return BPValue::N(outPin == 0 ? v.x : outPin == 1 ? v.y : v.z);
    }
    case BP_BREAK_V2: {
        Vec3 v = readIn(cv, n, 0, ctx, depth).asVec();
        return BPValue::N(outPin == 0 ? v.x : v.y);
    }
    case BP_VAL_VEC2:
        return BPValue::V2(readIn(cv, n, 0, ctx, depth).asNum(), readIn(cv, n, 1, ctx, depth).asNum());
    case BP_REROUTE:
        return readIn(cv, n, 0, ctx, depth);   // pass-through
    case BP_CREATE_EVENT:
        return BPValue::S(n.sname);
    case BP_M_CLAMP_FLOAT: {
        float value = readIn(cv, n, 0, ctx, depth).asNum();
        float mn = readIn(cv, n, 1, ctx, depth).asNum();
        float mx = readIn(cv, n, 2, ctx, depth).asNum();
        if (mn > mx) std::swap(mn, mx);
        return BPValue::N(clampf(value, mn, mx));
    }
    case BP_EV_AXIS:
    case BP_VAL_AXIS:
        return BPValue::N(ctx.axisValues ? ctx.axisValues[n.choice % BP_NAXES] : 0.0f);
    case BP_GET_COMPONENT: {
        // Components are stored on their owning Entity; return that Entity as
        // the component reference used by the rest of the graph runtime.
        Entity* target = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        if (target && ctx.scene) {
            std::vector<int> ids;
            ctx.scene->collectSubtree(target->id, ids);
            for (int id : ids) {
                Entity* e = ctx.scene->byId(id);
                const bool matches = e && (!n.slit[0].empty()
                    ? bpEntityHasBlueprintClass(*e, n.slit[0])
                    : bpHasComponent(*e, n.choice));
                if (matches) {
                    return outPin == 1 ? BPValue::B(true) : BPValue::E(e->id);
                }
            }
        }
        return outPin == 1 ? BPValue::B(false) : BPValue::E(0);
    }
    case BP_AI_REMAINING_DISTANCE: {
        Entity* agent = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        return BPValue::N(agent ? (ctx.aiRemainingDistance ? ctx.aiRemainingDistance(agent) : agent->aiRemainingDistance) : 0.0f);
    }
    case BP_AI_HAS_PATH: {
        Entity* agent = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        return BPValue::B(agent && (ctx.aiHasPath ? ctx.aiHasPath(agent) : agent->aiHasPath));
    }
    case BP_DIR_FWD: case BP_DIR_RIGHT: case BP_DIR_UP: {
        // the object's own axes (its facing), expressed in world space
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        if (!e || !e->body) return BPValue::V({});
        Vec3 axis = n.def == BP_DIR_FWD ? Vec3{ 0, 0, -1 }
                  : n.def == BP_DIR_RIGHT ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 };
        return BPValue::V(e->body->quat.rotate(axis));
    }
    case BP_WLOC: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        return BPValue::V(e && e->body ? e->body->position : Vec3{});
    }
    case BP_WROT: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        return BPValue::V(e && e->body ? bpQuatToEulerDeg(e->body->quat) : Vec3{});
    }
    case BP_LLOC: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        if (!e || !e->body) return BPValue::V({});
        Entity* p = e->parentId && ctx.scene ? ctx.scene->byId(e->parentId) : nullptr;
        if (!p || !p->body) return BPValue::V(e->body->position);   // no parent: local == world
        return BPValue::V(p->body->quat.conjugate().rotate(e->body->position - p->body->position));
    }
    case BP_LROT: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        if (!e || !e->body) return BPValue::V({});
        Entity* p = e->parentId && ctx.scene ? ctx.scene->byId(e->parentId) : nullptr;
        if (!p || !p->body) return BPValue::V(bpQuatToEulerDeg(e->body->quat));
        return BPValue::V(bpQuatToEulerDeg(p->body->quat.conjugate() * e->body->quat));
    }
    case BP_M_SIN: return BPValue::N(sinf(readIn(cv, n, 0, ctx, depth).asNum()));
    case BP_M_DOT: return BPValue::N(readIn(cv, n, 0, ctx, depth).asVec().dot(readIn(cv, n, 1, ctx, depth).asVec()));
    case BP_M_MOD: {
        float a = readIn(cv, n, 0, ctx, depth).asNum(), b = readIn(cv, n, 1, ctx, depth).asNum();
        return BPValue::N(b != 0 ? fmodf(a, b) : 0.0f);
    }
    case BP_M_NORM: return BPValue::V(readIn(cv, n, 0, ctx, depth).asVec().normalized());
    case BP_M_DIST: return BPValue::N((readIn(cv, n, 0, ctx, depth).asVec() - readIn(cv, n, 1, ctx, depth).asVec()).length());
    case BP_M_LERP: {
        // linear interpolation; adapts to the operands (float / vec2 / vec3)
        BPValue a = readIn(cv, n, 0, ctx, depth), b = readIn(cv, n, 1, ctx, depth);
        float t = readIn(cv, n, 2, ctx, depth).asNum();
        if (a.isVec() || b.isVec()) {
            Vec3 x = a.asVec(), y = b.asVec();
            Vec3 res = x + (y - x) * t;
            bool v2 = (!a.isVec() || a.kind == PIN_VEC2) && (!b.isVec() || b.kind == PIN_VEC2);
            return v2 ? BPValue::V2(res.x, res.y) : BPValue::V(res);
        }
        return BPValue::N(a.asNum() + (b.asNum() - a.asNum()) * t);
    }
    case BP_M_FINTERP: {
        // Unreal FInterpTo: eases Current toward Target at Speed, framerate-aware
        float cur = readIn(cv, n, 0, ctx, depth).asNum(), tgt = readIn(cv, n, 1, ctx, depth).asNum();
        float dt = readIn(cv, n, 2, ctx, depth).asNum(), sp = readIn(cv, n, 3, ctx, depth).asNum();
        if (sp <= 0) return BPValue::N(tgt);
        float d = tgt - cur;
        if (fabsf(d) < 1e-5f) return BPValue::N(tgt);
        return BPValue::N(cur + d * clampf(dt * sp, 0.0f, 1.0f));
    }
    case BP_M_VINTERP: {
        Vec3 cur = readIn(cv, n, 0, ctx, depth).asVec(), tgt = readIn(cv, n, 1, ctx, depth).asVec();
        float dt = readIn(cv, n, 2, ctx, depth).asNum(), sp = readIn(cv, n, 3, ctx, depth).asNum();
        if (sp <= 0) return BPValue::V(tgt);
        Vec3 d = tgt - cur;
        if (d.length() < 1e-5f) return BPValue::V(tgt);
        return BPValue::V(cur + d * clampf(dt * sp, 0.0f, 1.0f));
    }
    case BP_M_SCALEV: return BPValue::V(readIn(cv, n, 0, ctx, depth).asVec() * readIn(cv, n, 1, ctx, depth).asNum());
    case BP_M_ADDV: return BPValue::V(readIn(cv, n, 0, ctx, depth).asVec() + readIn(cv, n, 1, ctx, depth).asVec());
    case BP_M_LEN: return BPValue::N(readIn(cv, n, 0, ctx, depth).asVec().length());
    case BP_L_CMP: {
        float a = readIn(cv, n, 0, ctx, depth).asNum(), b = readIn(cv, n, 1, ctx, depth).asNum();
        bool result = false;
        switch (n.choice) {
        case 0: result = a > b; break;
        case 1: result = a < b; break;
        case 2: result = fabsf(a - b) < 1e-6f; break;
        case 3: result = a <= b; break;
        case 4: result = a >= b; break;
        default: result = false; break;
        }
        return BPValue::B(result);
    }
    case BP_L_STREQ: {
        // case-insensitive, like every other name lookup in the widget runtime
        std::string a = readIn(cv, n, 0, ctx, depth).str, b = readIn(cv, n, 1, ctx, depth).str;
        return BPValue::B(_stricmp(a.c_str(), b.c_str()) == 0);
    }
    case BP_L_NOT: return BPValue::B(!readIn(cv, n, 0, ctx, depth).asBool());
    case BP_L_AND: return BPValue::B(readIn(cv, n, 0, ctx, depth).asBool() && readIn(cv, n, 1, ctx, depth).asBool());
    case BP_L_OR: return BPValue::B(readIn(cv, n, 0, ctx, depth).asBool() || readIn(cv, n, 1, ctx, depth).asBool());
    case BP_L_XOR: return BPValue::B(readIn(cv, n, 0, ctx, depth).asBool() != readIn(cv, n, 1, ctx, depth).asBool());
    case BP_M_ABS: {
        BPValue a = readIn(cv, n, 0, ctx, depth);
        if (a.isVec()) { Vec3 v = a.asVec(); return a.kind == PIN_VEC2 ? BPValue::V2(fabsf(v.x), fabsf(v.y)) : BPValue::V({ fabsf(v.x), fabsf(v.y), fabsf(v.z) }); }
        return BPValue::N(fabsf(a.asNum()));
    }
    case BP_M_POW: {
        float b = readIn(cv, n, 0, ctx, depth).asNum(), e = readIn(cv, n, 1, ctx, depth).asNum();
        return BPValue::N(powf(b, e));
    }
    case BP_M_CROSS:
        return BPValue::V(readIn(cv, n, 0, ctx, depth).asVec().cross(readIn(cv, n, 1, ctx, depth).asVec()));
    case BP_M_ACOS: return BPValue::N(acosf(clampf(readIn(cv, n, 0, ctx, depth).asNum(), -1.0f, 1.0f)));
    case BP_M_ATAN2: return BPValue::N(atan2f(readIn(cv, n, 0, ctx, depth).asNum(), readIn(cv, n, 1, ctx, depth).asNum()));
    case BP_M_CEIL: return BPValue::I((int)ceilf(readIn(cv, n, 0, ctx, depth).asNum()));
    case BP_M_FLOOR: return BPValue::I((int)floorf(readIn(cv, n, 0, ctx, depth).asNum()));
    case BP_M_FRAC: { float x = readIn(cv, n, 0, ctx, depth).asNum(); return BPValue::N(x - floorf(x)); }
    case BP_M_LOOKAT: {
        // rotation (euler degrees) whose forward (0,0,-1) points from Start to Target
        Vec3 dir = readIn(cv, n, 1, ctx, depth).asVec() - readIn(cv, n, 0, ctx, depth).asVec();
        float len = dir.length();
        if (len < 1e-5f) return BPValue::V({ 0, 0, 0 });
        dir = dir * (1.0f / len);
        Vec3 fwd = { 0, 0, -1 };
        float d = clampf(fwd.dot(dir), -1.0f, 1.0f);
        Vec3 axis = fwd.cross(dir);
        float alen = axis.length();
        Quat q;
        if (alen < 1e-5f) q = d > 0 ? Quat{} : Quat::axisAngle({ 0, 1, 0 }, 3.14159265f);
        else q = Quat::axisAngle(axis * (1.0f / alen), acosf(d));
        return BPValue::V(bpQuatToEulerDeg(q));
    }
    case BP_FLOW_DON: return BPValue::I((int)nodeState_[n.id]);   // Counter output
    case BP_TF_INV_DIR: case BP_TF_DIR: case BP_TF_INV_LOC: case BP_TF_LOC: {
        // trasforma un vettore/punto tra world e local, relativo all'oggetto (self se scollegato)
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        Vec3 d = readIn(cv, n, 1, ctx, depth).asVec();
        if (!e || !e->body) return BPValue::V(d);
        switch (n.def) {
        case BP_TF_INV_DIR: return BPValue::V(e->body->quat.conjugate().rotate(d));            // world dir → local
        case BP_TF_DIR:     return BPValue::V(e->body->quat.rotate(d));                         // local dir → world
        case BP_TF_INV_LOC: return BPValue::V(e->body->quat.conjugate().rotate(d - e->body->position)); // world point → local
        default:            return BPValue::V(e->body->position + e->body->quat.rotate(d));     // local point → world
        }
    }
    case BP_EV_CUSTOM: {
        // i pin dati dell'evento restituiscono gli argomenti passati da Chiama Evento;
        // il pin dopo i parametri e' il delegate (porta il nome dell'evento, per Bind)
        int i = outPin - 1;   // outPin 0 = exec
        BPEventDef* ed = graph ? graph->findEvent(n.sname) : nullptr;
        int np = ed ? (int)ed->params.size() : 0;
        if (i >= 0 && i < np) return i < (int)curEventArgs_.size() ? curEventArgs_[i] : BPValue{};
        if (outPin == np + 1) return BPValue::S(n.sname);
        return BPValue{};
    }
    case BP_MAKE_TF: {
        Vec3 loc = readIn(cv, n, 0, ctx, depth).asVec();
        Vec3 rot = readIn(cv, n, 1, ctx, depth).asVec();
        Vec3 scl = readIn(cv, n, 2, ctx, depth).asVec();
        if (scl.x == 0 && scl.y == 0 && scl.z == 0) scl = { 1, 1, 1 };   // scala non impostata = identita'
        return BPValue::T(loc, rot, scl);
    }
    case BP_BREAK_TF: {
        BPValue t = readIn(cv, n, 0, ctx, depth);
        if (outPin == 0) return BPValue::V(t.vec);   // Location
        if (outPin == 1) return BPValue::V(t.rot);   // Rotation
        return BPValue::V(t.scl);                     // Scale
    }
    case BP_GET_COLOR: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        return BPValue::V(e ? e->color : Vec3{});
    }
    case BP_GET_PHYSTYPE: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 0, ctx, depth).asEnt());
        return BPValue::I(e && e->body && e->body->type == BodyType::Static ? 1 : 0);
    }
    case BP_TF_INV_DIR_T: {
        // direzione world → local rispetto a un Transform (solo rotazione)
        BPValue t = readIn(cv, n, 0, ctx, depth);
        Vec3 d = readIn(cv, n, 1, ctx, depth).asVec();
        Quat q = Quat::fromEulerDeg(t.rot.x, t.rot.y, t.rot.z);
        return BPValue::V(q.conjugate().rotate(d));
    }
    case BP_TF_INV_LOC_T: {
        // punto world → local rispetto a un Transform (rotazione + scala inverse)
        BPValue t = readIn(cv, n, 0, ctx, depth);
        Vec3 p = readIn(cv, n, 1, ctx, depth).asVec();
        Quat q = Quat::fromEulerDeg(t.rot.x, t.rot.y, t.rot.z);
        Vec3 l = q.conjugate().rotate(p - t.vec);
        Vec3 s = t.scl;
        return BPValue::V({ s.x != 0 ? l.x / s.x : l.x, s.y != 0 ? l.y / s.y : l.y, s.z != 0 ? l.z / s.z : l.z });
    }
    case BP_TIMER_SET:
    case BP_TIMER_SET_FUNC:
        return outPin == 2 ? BPValue::H(timerHandleFor(cv, n.id)) : BPValue{};
    default: return BPValue{};
    }
}

std::vector<BPValue> BPInstance::callFunction(BPFunc& fn, const std::vector<BPValue>& args, BPContext& ctx, int depth) {
    if (frames_.size() > 16) return {};
    BPFrame fr;
    fr.params = args;
    fr.rets.resize(fn.outs.size());
    frames_.push_back(fr);
    if (fn.pure) {
        for (const BPNode& n : fn.body.nodes) {
            if (n.def != BP_FN_RETURN) continue;
            for (size_t i = 0; i < fn.outs.size(); i++) frames_.back().rets[i] = readIn(fn.body, n, (int)i, ctx, depth + 1);
            break;
        }
    } else {
        for (const auto& n : fn.body.nodes) {
            if (n.def == BP_FN_ENTRY) {
                execChain(fn.body, n.id, 0, ctx, depth + 1);
                break;
            }
        }
    }
    std::vector<BPValue> rets = frames_.back().rets;
    frames_.pop_back();
    return rets;
}

static bool bpEntitiesRelated(const EditorScene& scene, const Entity& a, const Entity& b) {
    for (int p = a.parentId, guard = 0; p && guard++ < 128;) {
        if (p == b.id) return true;
        const Entity* e = const_cast<EditorScene&>(scene).byId(p); p = e ? e->parentId : 0;
    }
    for (int p = b.parentId, guard = 0; p && guard++ < 128;) {
        if (p == a.id) return true;
        const Entity* e = const_cast<EditorScene&>(scene).byId(p); p = e ? e->parentId : 0;
    }
    return false;
}

// Collision-aware transform movement for static/kinematic character graphs.
// The regular physics solver cannot resolve a body whose Transform is teleported
// every Tick, so Set World/Local Location performs a short stepped sweep and
// resolves X/Z/Y separately. This produces wall sliding and also collides with
// Mesh Renderer colliders that do not own a dynamic Rigidbody.
static Vec3 bpSweepLocation(Entity& mover, const Vec3& target, EditorScene* scene) {
    if (!scene || !mover.body || !mover.body->enabled || mover.collision != 0) return target;
    RigidBody& body = *mover.body;
    Vec3 start = body.position;
    Vec3 wanted = target - start;
    auto blocksAt = [&](const Vec3& candidate, const Vec3& motion) {
        Vec3 saved = body.position;
        body.position = candidate; body.updateAABB();
        bool blocked = false;
        for (Entity& other : scene->entities) {
            if (other.id == mover.id || !other.body || !other.body->enabled || other.collision != 0 ||
                !scene->layers.collide(mover.layer, other.layer) || bpEntitiesRelated(*scene, mover, other)) continue;
            const AABB& a = body.aabb; const AABB& b = other.body->aabb;
            if (a.max.x < b.min.x || a.min.x > b.max.x || a.max.y < b.min.y || a.min.y > b.max.y ||
                a.max.z < b.min.z || a.min.z > b.max.z) continue;
            Contact contacts[4]; int count = collide(body, *other.body, contacts);
            for (int i = 0; i < count; i++) {
                if (motion.dot(contacts[i].normal) > 0.000001f) { blocked = true; break; }
            }
            if (blocked) break;
        }
        body.position = saved; body.updateAABB();
        return blocked;
    };
    auto moveAxis = [&](int axis) {
        float amount = (&wanted.x)[axis];
        if (fabsf(amount) < 0.000001f) return;
        float minExtent = (std::min)(body.shape.h.x, (std::min)(body.shape.h.y, body.shape.h.z));
        float maxStep = clampf(minExtent * 0.45f, 0.04f, 0.25f);
        int steps = (std::max)(1, (int)ceilf(fabsf(amount) / maxStep));
        float stepAmount = amount / steps;
        for (int s = 0; s < steps; s++) {
            Vec3 motion{}; (&motion.x)[axis] = stepAmount;
            Vec3 from = body.position, candidate = from + motion;
            if (!blocksAt(candidate, motion)) { body.position = candidate; body.updateAABB(); continue; }
            float lo = 0.0f, hi = 1.0f;
            for (int it = 0; it < 8; it++) {
                float mid = (lo + hi) * 0.5f;
                if (blocksAt(from + motion * mid, motion)) hi = mid; else lo = mid;
            }
            body.position = from + motion * (std::max)(0.0f, lo - 0.01f);
            body.updateAABB();
            break;
        }
    };
    moveAxis(0); moveAxis(2); moveAxis(1);
    return body.position;
}

bool BPInstance::execNode(const BPCanvas& cv, BPNode& n, BPContext& ctx, int depth, int& nextOut) {
    // returns false to stop the chain; nextOut = exec output to follow
    RigidBody* body = entity ? entity->body : nullptr;
    nextOut = 0;
    switch (n.def) {
    case BP_MEMBER_ACCESS: {
        // Target is an entity id for a Blueprint member, a widget instance
        // handle for a Widget one (asEnt() hands back the raw int either way).
        const int target = readIn(cv, n, 1, ctx, 0).asEnt();
        const bool onWidget = bpMemberClassIsWidget(n.slit[0]);
        if (n.choice == 1) {
            BPValue value = readIn(cv, n, 2, ctx, 0);
            if (onWidget) { if (ctx.setWidgetMember) ctx.setWidgetMember(target, n.sname, value); }
            else if (ctx.setBlueprintMember) ctx.setBlueprintMember(target, n.slit[0].c_str(), n.sname, value);
        } else {
            BPGraph owner;
            if (!bpLoadMemberGraph(gBPProjectDir, n, owner)) return true;
            if (n.choice == 4) {
                std::vector<BPValue> args;
                if (BPEventDef* event = owner.findEvent(n.sname))
                    for (int i = 0; i < (int)event->params.size(); i++) args.push_back(readIn(cv, n, i + 2, ctx, 0));
                if (onWidget) { if (ctx.fireWidgetMemberEvent) ctx.fireWidgetMemberEvent(target, n.sname, args); }
                else if (ctx.fireBlueprintMemberEvent) ctx.fireBlueprintMemberEvent(target, n.slit[0].c_str(), n.sname, args);
            } else if (n.choice == 2) {
                std::vector<BPValue> args;
                if (BPFunc* function = owner.findFunc(n.sname))
                    for (int i = 0; i < (int)function->ins.size(); i++) args.push_back(readIn(cv, n, i + 2, ctx, 0));
                if (onWidget)
                    lastRets_[n.id] = ctx.callWidgetMember ? ctx.callWidgetMember(target, n.sname, args)
                                                           : std::vector<BPValue>{};
                else
                    lastRets_[n.id] = ctx.callBlueprintMember ?
                        ctx.callBlueprintMember(target, n.slit[0].c_str(), n.sname, args) : std::vector<BPValue>{};
            }
        }
        return true;
    }
    case BP_ACT_IMPULSE: if (body) body->applyImpulse(readIn(cv, n, 1, ctx, 0).asVec()); return true;
    case BP_ACT_FORCE: if (body) body->applyForce(readIn(cv, n, 1, ctx, 0).asVec()); return true;
    case BP_ACT_SETVEL: if (body) { body->wake(); body->velocity = readIn(cv, n, 1, ctx, 0).asVec(); } return true;
    case BP_ACT_TORQUE: if (body) body->applyTorque(readIn(cv, n, 1, ctx, 0).asVec()); return true;
    case BP_ACT_COLOR: {
        Vec3 c = readIn(cv, n, 1, ctx, 0).asVec();
        if (entity) entity->color = { clampf(c.x, 0, 1), clampf(c.y, 0, 1), clampf(c.z, 0, 1) };
        return true;
    }
    case BP_SPAWN_PREFAB: {
        std::vector<BPSpawnPinInfo> pins = bpSpawnPinInfo(gBPProjectDir, n.sname);
        std::vector<BPValue> values;
        for (int i = 0; i < (int)pins.size() && i + 2 < BP_MAX_PINS; i++)
            values.push_back(readIn(cv, n, i + 2, ctx, 0));
        BPValue transform = readIn(cv, n, 1, ctx, 0);
        int spawned = ctx.spawnPrefab ? ctx.spawnPrefab(n.sname, transform, values) : 0;
        spawnResults_[n.id] = spawned;
        return true;
    }
    case BP_CREATE_SAVE_GAME:
        spawnResults_[n.id] = ctx.createSaveGame ? ctx.createSaveGame(n.sname) : 0;
        return true;
    case BP_CREATE_WIDGET:
        spawnResults_[n.id] = ctx.createWidget ? ctx.createWidget(n.sname) : 0;
        return true;
    // An unwired Widget pin (handle 0) means "the widget this graph belongs to",
    // so a Widget Blueprint can drive its own elements without plumbing a handle.
    case BP_ADD_WIDGET_VIEWPORT:
        if (ctx.addWidgetToViewport) ctx.addWidgetToViewport(bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx));
        return true;
    case BP_REMOVE_WIDGET_VIEWPORT:
        if (ctx.removeWidgetFromViewport) ctx.removeWidgetFromViewport(bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx));
        return true;
    case BP_SET_WIDGET_TEXT: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        std::string text = readIn(cv, n, 3, ctx, 0).str;
        if (ctx.setWidgetText) ctx.setWidgetText(handle, element.c_str(), text.c_str());
        return true;
    }
    case BP_SET_WIDGET_VALUE: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        float value = readIn(cv, n, 3, ctx, 0).asNum();
        if (ctx.setWidgetValue) ctx.setWidgetValue(handle, element.c_str(), value);
        return true;
    }
    // ── direct slot setters: the node names the property, so there is no combo.
    // They all land on setWidgetNumber, the same path Set Widget Number takes.
    case BP_SET_WIDGET_PERCENT:
    case BP_SET_WIDGET_HALIGN:
    case BP_SET_WIDGET_VALIGN:
    case BP_SET_WIDGET_ANCHOR: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        const char* prop = n.def == BP_SET_WIDGET_PERCENT ? "Percent"
                         : n.def == BP_SET_WIDGET_HALIGN  ? "H Align"
                         : n.def == BP_SET_WIDGET_VALIGN  ? "V Align" : "Anchor";
        if (ctx.setWidgetNumber) ctx.setWidgetNumber(handle, element.c_str(), prop, readIn(cv, n, 3, ctx, 0).asNum());
        return true;
    }
    case BP_SET_WIDGET_RANGE: {
        // Min before Max, so the value clamp that Percent applies sees the new
        // span rather than half of it
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        if (ctx.setWidgetNumber) {
            ctx.setWidgetNumber(handle, element.c_str(), "Min", readIn(cv, n, 3, ctx, 0).asNum());
            ctx.setWidgetNumber(handle, element.c_str(), "Max", readIn(cv, n, 4, ctx, 0).asNum());
        }
        return true;
    }
    case BP_SET_WIDGET_PIVOT: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        Vec3 pivot = readIn(cv, n, 3, ctx, 0).asVec();
        if (ctx.setWidgetNumber) {
            ctx.setWidgetNumber(handle, element.c_str(), "Alignment X", pivot.x);
            ctx.setWidgetNumber(handle, element.c_str(), "Alignment Y", pivot.y);
        }
        return true;
    }
    // property setters: `sname` is the property, the pins are widget/element/value
    case BP_SET_WIDGET_NUM: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        if (ctx.setWidgetNumber) ctx.setWidgetNumber(handle, element.c_str(), n.sname, readIn(cv, n, 3, ctx, 0).asNum());
        return true;
    }
    case BP_SET_WIDGET_STR: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        std::string value = readIn(cv, n, 3, ctx, 0).str;
        if (ctx.setWidgetString) ctx.setWidgetString(handle, element.c_str(), n.sname, value.c_str());
        return true;
    }
    case BP_SET_WIDGET_COLOR: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        BPValue value = readIn(cv, n, 3, ctx, 0);
        if (ctx.setWidgetColor) ctx.setWidgetColor(handle, element.c_str(), n.sname, value.asVec(), value.alpha);
        return true;
    }
    case BP_SET_WIDGET_BOOL: {
        int handle = bpWidgetHandle(readIn(cv, n, 1, ctx, 0), ctx);
        std::string element = readIn(cv, n, 2, ctx, 0).str;
        if (ctx.setWidgetBool) ctx.setWidgetBool(handle, element.c_str(), n.sname, readIn(cv, n, 3, ctx, 0).asBool());
        return true;
    }
    case BP_SET_MATCOLOR: {
        // come Set Color ma su un oggetto qualsiasi (renderer)
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        Vec3 c = readIn(cv, n, 2, ctx, 0).asVec();
        if (e) e->color = { clampf(c.x, 0, 1), clampf(c.y, 0, 1), clampf(c.z, 0, 1) };
        return true;
    }
    case BP_SET_PHYSTYPE: {
        // componente fisico: passa tra dinamica e statica (come i Dettagli editor)
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        if (e && e->body) {
            bool stat = n.choice != 0;   // 0 Dinamica, 1 Statica
            float keepMass = e->body->mass > 0 ? e->body->mass : 1.0f;
            e->body->type = stat ? BodyType::Static : BodyType::Dynamic;
            e->body->setMass(stat ? 0.0f : keepMass);
            e->body->wake();
        }
        return true;
    }
    case BP_AUDIO_PLAY: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        if (e && ctx.playAudio) ctx.playAudio(e);
        return true;
    }
    case BP_AUDIO_STOP: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        if (e && ctx.stopAudio) ctx.stopAudio(e);
        return true;
    }
    case BP_AUDIO_SET_VOLUME: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        float volume = clampf(readIn(cv, n, 2, ctx, 0).asNum(), 0.0f, 2.0f);
        if (e && ctx.setAudioVolume) ctx.setAudioVolume(e, volume);
        else if (e) e->audioVolume = volume;
        return true;
    }
    case BP_AUDIO_SET_CLIP: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        std::string clip = readIn(cv, n, 2, ctx, 0).str;
        if (e && ctx.setAudioClip) ctx.setAudioClip(e, clip.c_str());
        else if (e) {
            e->hasAudio = true;
            snprintf(e->audioClip, sizeof(e->audioClip), "%s", clip.c_str());
        }
        return true;
    }
    case BP_AUDIO_FADE_IN: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        float duration = readIn(cv, n, 2, ctx, 0).asNum();
        if (e && ctx.fadeInAudio) ctx.fadeInAudio(e, duration);
        return true;
    }
    case BP_AUDIO_FADE_OUT: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        float duration = readIn(cv, n, 2, ctx, 0).asNum();
        if (e && ctx.fadeOutAudio) ctx.fadeOutAudio(e, duration);
        return true;
    }
    case BP_ANIM_SET_FLOAT:
    case BP_ANIM_SET_BOOL:
    case BP_ANIM_SET_TRIGGER: {
        Entity* animator = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        int type = n.def == BP_ANIM_SET_FLOAT ? 0 : n.def == BP_ANIM_SET_BOOL ? 1 : 2;
        float value = n.def == BP_ANIM_SET_TRIGGER ? 1.0f :
                      n.def == BP_ANIM_SET_BOOL ? (readIn(cv,n,2,ctx,0).asBool()?1.0f:0.0f) : readIn(cv,n,2,ctx,0).asNum();
        if(animator&&ctx.setAnimatorParameter)ctx.setAnimatorParameter(animator,n.sname,type,value);
        return true;
    }
    case BP_ANIM_BIND_TRIGGER: {
        Entity* animator=bpResolveEnt(ctx,entity,readIn(cv,n,1,ctx,0).asEnt());
        std::string customEvent=readIn(cv,n,2,ctx,0).str;
        if(animator&&ctx.bindAnimationTrigger&&!customEvent.empty()&&n.sname[0])
            ctx.bindAnimationTrigger(entity,animator,n.sname,customEvent.c_str());
        return true;
    }
    case BP_AI_SET_TARGET: {
        Entity* agent = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        int target = readIn(cv, n, 2, ctx, 0).asEnt();
        if (agent && ctx.aiSetTarget) ctx.aiSetTarget(agent, target);
        return true;
    }
    case BP_AI_SET_DESTINATION: {
        Entity* agent = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        Vec3 destination = readIn(cv, n, 2, ctx, 0).asVec();
        if (agent && ctx.aiSetDestination) ctx.aiSetDestination(agent, destination);
        return true;
    }
    case BP_AI_SET_SPEED: {
        Entity* agent = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        float speed = readIn(cv, n, 2, ctx, 0).asNum();
        if (agent && ctx.aiSetSpeed) ctx.aiSetSpeed(agent, speed);
        else if (agent) agent->aiSpeed = (std::max)(0.0f, speed);
        return true;
    }
    case BP_AI_SET_STOPPED: {
        Entity* agent = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        bool stopped = readIn(cv, n, 2, ctx, 0).asBool();
        if (agent && ctx.aiSetStopped) ctx.aiSetStopped(agent, stopped);
        else if (agent) agent->aiStopped = stopped;
        return true;
    }
    case BP_ACT_DESTROY:
        if (ctx.requestDestroy && entity) ctx.requestDestroy(entity);
        dead = true;
        return false;
    case BP_ACT_PRINT: {
        BPValue text=readIn(cv,n,1,ctx,0);
        BPValue color=readIn(cv,n,2,ctx,0);
        if(ctx.printString)ctx.printString(entity,text.str.c_str(),color.vec,color.alpha);
        else if (ctx.log) ctx.log(1, "[BP %s] %s", entity ? entity->name : "?", text.str.c_str());
        return true;
    }
    case BP_TIMER_SET:
    case BP_TIMER_SET_FUNC: {
        float rate = readIn(cv, n, 1, ctx, 0).asNum();
        if (rate < 0.0001f) rate = 0.0001f;
        int handle = timerHandleFor(cv, n.id);
        BPTimerState t;
        t.canvas = &cv;
        t.nodeId = n.id;
        t.remaining = t.rate = rate;
        t.looping = readIn(cv, n, 2, ctx, 0).asBool();
        if (n.def == BP_TIMER_SET) t.eventName = readIn(cv, n, 3, ctx, 0).str;
        else t.functionName = n.sname;
        timers_[handle] = t;       // richiamarlo resetta lo stesso timer/handle
        nextOut = 0;               // Started: continua subito
        return true;
    }
    case BP_TIMER_PAUSE: {
        auto it = timers_.find(readIn(cv, n, 1, ctx, 0).asTimerHandle());
        if (it != timers_.end()) it->second.paused = true;
        return true;
    }
    case BP_TIMER_UNPAUSE: {
        auto it = timers_.find(readIn(cv, n, 1, ctx, 0).asTimerHandle());
        if (it != timers_.end()) it->second.paused = false;
        return true;
    }
    case BP_TIMER_CLEAR:
        timers_.erase(readIn(cv, n, 1, ctx, 0).asTimerHandle());
        return true;
    case BP_FLOW_DELAY:
    case BP_FLOW_RETRIGGER_DELAY: {
        std::pair<const BPCanvas*, int> key{ &cv, n.id };
        float duration = readIn(cv, n, 1, ctx, 0).asNum();
        if (duration < 0.0001f) duration = 0.0001f;
        auto it = delays_.find(key);
        if (n.def == BP_FLOW_DELAY && it != delays_.end()) return false; // Delay ignora i retrigger
        delays_[key] = { &cv, n.id, duration };                         // retrigger: riparte da zero
        return false;                                                   // la catena riprende a Completed
    }
    case BP_TRACE_LINE:
    case BP_TRACE_SPHERE: {
        Vec3 start = readIn(cv, n, 1, ctx, 0).asVec();
        Vec3 end = readIn(cv, n, 2, ctx, 0).asVec();
        BPTraceResult res;
        RigidBody* self = entity ? entity->body : nullptr;
        unsigned mask = (unsigned)n.choice;   // layer mask (0 = tutti)
        float radius = 0;
        if (n.def == BP_TRACE_SPHERE) {
            radius = readIn(cv, n, 3, ctx, 0).asNum();
            if (radius > 1e-4f) bpSphereTrace(ctx, self, start, end, radius, mask, res);
            else bpLineTrace(ctx, self, start, end, mask, res);
        } else {
            bpLineTrace(ctx, self, start, end, mask, res);
        }
        traceResults_[n.id] = res;
        if (n.prop != 0 && ctx.drawDebugTrace)   // "Show debug" attivo
            ctx.drawDebugTrace(start, end, radius, res.hit, res.hit ? res.point : end);
        return true;
    }
    case BP_CALL_EVENT: {
        // delegate-style: run the custom event chain synchronously (any event graph),
        // passando gli argomenti dei pin dati (letti prima di eseguire la catena)
        std::vector<BPValue> args;
        if (BPEventDef* ed = graph->findEvent(n.sname))
            for (size_t i = 0; i < ed->params.size(); i++) args.push_back(readIn(cv, n, (int)i + 1, ctx, 0));
        for (auto& gph : graph->graphs) {
            for (const auto& ev : gph.body.nodes) {
                if (ev.def == BP_EV_CUSTOM && strcmp(ev.sname, n.sname) == 0) {
                    std::vector<BPValue> saved = curEventArgs_;   // supporta eventi annidati
                    curEventArgs_ = args;
                    execChain(gph.body, ev.id, 0, ctx, depth + 1);
                    curEventArgs_ = saved;
                    return true;
                }
            }
        }
        return true;
    }
    case BP_SEND_MSG: {
        int target = readIn(cv, n, 1, ctx, 0).asEnt();
        if (ctx.sendMessage && target > 0) ctx.sendMessage(target, n.sname);
        return true; // safe no-op if not implemented (interface message)
    }
    case BP_INTERFACE_MESSAGE: {
        BPFunc signature;std::vector<BPValue> args;
        if(bpLoadInterfaceFunction(gBPProjectDir,n.slit[0],n.sname,signature))
            for(int i=0;i<(int)signature.ins.size();i++)args.push_back(readIn(cv,n,i+2,ctx,0));
        Entity* target=bpResolveEnt(ctx,entity,readIn(cv,n,1,ctx,0).asEnt());
        lastRets_[n.id]=target&&ctx.callInterfaceMessage?
            ctx.callInterfaceMessage(target->id,n.slit[0].c_str(),n.sname,args):std::vector<BPValue>{};
        return true; // Object nullo/non compatibile: no-op intenzionale, senza errore
    }
    case BP_INVOKE_INSPECTOR_EVENT: {
        Entity* target=bpResolveEnt(ctx,entity,readIn(cv,n,1,ctx,0).asEnt());
        std::string eventName=readIn(cv,n,2,ctx,0).str;
        if(target&&ctx.invokeInspectorEvent&&!eventName.empty())ctx.invokeInspectorEvent(target,eventName.c_str());
        return true;
    }
    case BP_BIND_EVENT: {
        // Bind the Custom Event named by the delegate pin to dispatcher n.sname.
        // With Target / Target Widget left alone the dispatcher is this graph's
        // own; otherwise the binding is registered on the target's instance and
        // fires back here when that instance calls the dispatcher.
        std::string evName = readIn(cv, n, 1, ctx, 0).str;
        if (evName.empty() || !n.sname[0]) return true;
        const int selfEntity = ctx.entity ? ctx.entity->id : 0;
        const int targetEntity = readIn(cv, n, 2, ctx, 0).asEnt();
        const int targetWidget = (int)readIn(cv, n, 3, ctx, 0).asNum();
        const bool external = targetWidget != 0 ||
                              (targetEntity != 0 && targetEntity != selfEntity);
        if (external) {
            // the host logs precisely what it searched, so only report the case
            // where there is no host at all — otherwise the user gets two lines,
            // the useful one and a vague one
            if (!ctx.bindDispatcher) {
                if (ctx.log) ctx.log(2, "Bind Dispatcher '%s': no host to resolve the target.", n.sname);
            } else {
                ctx.bindDispatcher(targetEntity, targetWidget, n.sname,
                                   selfEntity, ctx.selfWidget, evName.c_str());
            }
            return true;
        }
        std::string why;
        if (bindDispatcher(n.sname, 0, 0, evName.c_str(), graph, &why)) return true;
        // Not in this graph — but an actor can carry several Blueprint components
        // and the dispatcher may belong to a sibling (a Health Component, say).
        // Retry through the host, which searches every component on the actor.
        if (ctx.bindDispatcher && ctx.bindDispatcher(selfEntity, ctx.selfWidget, n.sname,
                                                     selfEntity, ctx.selfWidget, evName.c_str()))
            return true;
        if (ctx.log) ctx.log(2, "Bind Dispatcher '%s': %s", n.sname, why.c_str());
        return true;
    }
    case BP_BIND_BEGIN_OVERLAP:
    case BP_BIND_END_OVERLAP: {
        int componentId = readIn(cv, n, 1, ctx, 0).asEnt();
        if (componentId <= 0 && entity) componentId = entity->id;
        std::string eventName = readIn(cv, n, 2, ctx, 0).str;
        Entity* component = ctx.scene ? ctx.scene->byId(componentId) : nullptr;
        BPEventDef* signature = graph ? graph->findEvent(eventName.c_str()) : nullptr;
        bool correctSignature = signature && signature->params.size() == 2 &&
                                signature->params[0].kind == PIN_ENT && signature->params[1].kind == PIN_ENT;
        if (!component || (!component->hasMesh && !component->hasTrigger)) {
            if (ctx.log) ctx.log(2, "Bind Overlap: the reference has no Mesh Renderer or Trigger.");
        } else if (!correctSignature) {
            if (ctx.log) ctx.log(2, "Bind Overlap: the Custom Event needs 2 Object pins (Component, Other Actor).");
        } else {
            bool begin = n.def == BP_BIND_BEGIN_OVERLAP;
            bool duplicate = false;
            for (const BPOverlapBinding& binding : overlapBindings_)
                if (binding.begin == begin && binding.componentId == componentId && binding.eventName == eventName) duplicate = true;
            if (!duplicate) overlapBindings_.push_back({ begin, componentId, eventName });
        }
        return true;
    }
    case BP_CALL_DISPATCH: {
        // esegue tutti gli eventi bindati al dispatcher n.sname
        std::vector<BPValue> args;if(BPDispatcherDef* dispatcher=graph?graph->findDispatcher(n.sname):nullptr)
            for(int i=0;i<(int)dispatcher->params.size();i++)args.push_back(readIn(cv,n,i+1,ctx,0));
        auto it = dispatchBindings_.find(n.sname);
        if (it != dispatchBindings_.end()) {
            std::vector<BPDispatchBinding> evs = it->second;   // copia: il chain potrebbe modificare la mappa
            for (const BPDispatchBinding& b : evs) {
                if (b.listenerEntity || b.listenerWidget) {
                    // the listener lives in another Blueprint or Widget: its own
                    // instance runs the event, with its own variables
                    if (ctx.fireDispatcherEvent)
                        ctx.fireDispatcherEvent(b.listenerEntity, b.listenerWidget, b.eventName.c_str(), args);
                    if (dead) break;
                    continue;
                }
                for (auto& gph : graph->graphs) {
                    bool done = false;
                    for (const auto& ev : gph.body.nodes) {
                        if (ev.def == BP_EV_CUSTOM && strcmp(ev.sname, b.eventName.c_str()) == 0) {
                            std::vector<BPValue> saved=curEventArgs_;curEventArgs_=args;
                            execChain(gph.body, ev.id, 0, ctx, depth + 1);
                            curEventArgs_=saved;
                            done = true;
                            break;
                        }
                    }
                    if (done) break;
                }
                if (dead) break;
            }
        }
        return true;
    }
    case BP_CALL_FUNC: {
        BPFunc* fn = graph->findFunc(n.sname);
        if (fn) {
            if (fn->pure) return true; // i nodi puri vengono valutati tramite i pin dati
            std::vector<BPValue> args;
            for (size_t i = 0; i < fn->ins.size(); i++) args.push_back(readIn(cv, n, (int)i + 1, ctx, 0));
            lastRets_[n.id] = callFunction(*fn, args, ctx, depth);
        } else if (ctx.log) {
            ctx.log(2, "Blueprint: function '%s' not found.", n.sname);
        }
        return true;
    }
    case BP_FN_RETURN:
        if (!frames_.empty()) {
            BPFunc* fn = funcOwning(graph, cv);
            int off = fn && fn->pure ? 0 : 1;
            BPFrame& fr = frames_.back();
            for (size_t i = 0; i < fr.rets.size(); i++) fr.rets[i] = readIn(cv, n, (int)i + off, ctx, 0);
            fr.returned = true;
        }
        return false;
    case BP_SAVE_GAME_SLOT: {
        int objectId = readIn(cv, n, 1, ctx, 0).asEnt();
        std::string slot = readIn(cv, n, 2, ctx, 0).str;
        nodeState_[n.id] = ctx.saveGameSlot && ctx.saveGameSlot(objectId, slot.c_str()) ? 1.0f : 0.0f;
        return true;
    }
    case BP_LOAD_GAME_SLOT: {
        int objectId = readIn(cv, n, 1, ctx, 0).asEnt();
        std::string slot = readIn(cv, n, 2, ctx, 0).str;
        nodeState_[n.id] = ctx.loadGameSlot && ctx.loadGameSlot(objectId, slot.c_str()) ? 1.0f : 0.0f;
        return true;
    }
    case BP_OPEN_LEVEL: {
        std::string level = readIn(cv, n, 1, ctx, 0).str;
        if (ctx.openLevel && !level.empty()) ctx.openLevel(level.c_str());
        return true;
    }
    case BP_VAR_SET: {
        BPVarDef* definition = graph ? graph->findVar(n.sname) : nullptr;
        if (definition && (definition->requiredGenerated || definition->widgetGenerated)) return true;
        BPVarStore* s = store(n.sname);
        if (s) s->single = bpCoerce(readIn(cv, n, 1, ctx, 0), s->single.kind);
        return true;
    }
    case BP_LOCAL_SET:
        if (!frames_.empty()) frames_.back().locals[n.sname] = readIn(cv, n, 1, ctx, 0);
        return true;
    case BP_ARR_ADD: {
        BPVarStore* s = store(n.sname);
        if (s && s->arr.size() < 4096) s->arr.push_back(readIn(cv, n, 1, ctx, 0));
        return true;
    }
    case BP_ARR_REMOVE: {
        BPVarStore* s = store(n.sname);
        int idx = (int)readIn(cv, n, 1, ctx, 0).asNum();
        if (s && idx >= 0 && idx < (int)s->arr.size()) s->arr.erase(s->arr.begin() + idx);
        return true;
    }
    case BP_ARR_CLEAR: {
        BPVarStore* s = store(n.sname);
        if (s) s->arr.clear();
        return true;
    }
    case BP_MAP_SET: {
        BPVarStore* s = store(n.sname);
        if (s && s->mapv.size() < 4096) s->mapv[mapKey(readIn(cv, n, 1, ctx, 0).asNum())] = readIn(cv, n, 2, ctx, 0);
        return true;
    }
    case BP_MAP_REMOVE: {
        BPVarStore* s = store(n.sname);
        if (s) s->mapv.erase(mapKey(readIn(cv, n, 1, ctx, 0).asNum()));
        return true;
    }
    case BP_L_IF:
        nextOut = readIn(cv, n, 1, ctx, 0).asBool() ? 0 : 1;
        return true;
    case BP_CAST_TO_CLASS: {
        Entity* target = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        nextOut = target && bpActorMatchesClass(*target, n.sname) ? 0 : 1;
        return true;
    }
    case BP_FLOW_FOR: {
        int a = (int)readIn(cv, n, 1, ctx, 0).asNum();
        int b = (int)readIn(cv, n, 2, ctx, 0).asNum();
        if (b - a > 10000) b = a + 10000;
        for (int i = a; i <= b && !dead; i++) {
            loopIdx_[n.id] = (float)i;
            execChain(cv, n.id, 0, ctx, depth + 1);
            if (!frames_.empty() && frames_.back().returned) return false;
        }
        nextOut = 2;
        return true;
    }
    case BP_FLOW_FOREACH: {
        BPVarStore* s = store(n.sname);
        if (s) {
            std::vector<BPValue> copy = s->arr;
            for (size_t i = 0; i < copy.size() && !dead; i++) {
                loopEl_[n.id] = copy[i];
                loopIdx_[n.id] = (float)i;
                execChain(cv, n.id, 0, ctx, depth + 1);
                if (!frames_.empty() && frames_.back().returned) return false;
            }
        }
        nextOut = 3;
        return true;
    }
    case BP_GET_ALL_WITH_CLASS:
    case BP_GET_ALL_WITH_TAG: {
        std::vector<int> matches;
        std::string tag = n.def == BP_GET_ALL_WITH_TAG ? readIn(cv, n, 1, ctx, 0).str : std::string{};
        if (ctx.scene) for (const Entity& actor : ctx.scene->entities) {
            bool match = n.def == BP_GET_ALL_WITH_CLASS ? bpActorMatchesClass(actor, n.sname)
                                                        : bpActorHasTag(actor, tag);
            if (match) matches.push_back(actor.id);
            if (matches.size() >= 10000) break;
        }
        for (size_t i = 0; i < matches.size() && !dead; i++) {
            loopEl_[n.id] = BPValue::E(matches[i]);
            loopIdx_[n.id] = (float)i;
            execChain(cv, n.id, 0, ctx, depth + 1);
            if (!frames_.empty() && frames_.back().returned) return false;
        }
        nextOut = 3;
        return true;
    }
    case BP_REROUTE_EX:
        nextOut = 0;   // exec pass-through
        return true;
    case BP_SET_WLOC: case BP_SET_WROT: case BP_SET_LLOC: case BP_SET_LROT: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        if (!e || !e->body) return true;
        Entity* p = e->parentId && ctx.scene ? ctx.scene->byId(e->parentId) : nullptr;
        Vec3 v = readIn(cv, n, 2, ctx, 0).asVec();
        Vec3 oldPosition = e->body->position;
        switch (n.def) {
        case BP_SET_WLOC:
            e->body->position = bpSweepLocation(*e, v, ctx.scene);
            break;
        case BP_SET_WROT:
            e->body->quat = Quat::fromEulerDeg(v.x, v.y, v.z);
            break;
        case BP_SET_LLOC:
            // relative to the parent (== world when there is none)
            e->body->position = bpSweepLocation(*e, p && p->body ? p->body->position + p->body->quat.rotate(v) : v, ctx.scene);
            break;
        default:
            e->body->quat = p && p->body ? p->body->quat * Quat::fromEulerDeg(v.x, v.y, v.z)
                                         : Quat::fromEulerDeg(v.x, v.y, v.z);
            break;
        }
        e->body->updateAABB();
        if ((n.def == BP_SET_WLOC || n.def == BP_SET_LLOC) && e->body->type == BodyType::Static && ctx.dt > 0.000001f)
            e->body->velocity = (e->body->position - oldPosition) * (1.0f / ctx.dt);
        e->body->wake();
        bpSyncAttach(ctx, e);   // the new pose survives the parent-follow update
        return true;
    }
    case BP_SET_SCALE: {
        Entity* e = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        if (!e) return true;
        e->scale = readIn(cv, n, 2, ctx, 0).asVec();
        if (ctx.scene) ctx.scene->syncBodyShape(*e);   // rebuild the collider from the new scale
        if (e->body) e->body->wake();
        return true;
    }
    case BP_SET_CONSTRAINT_OBJECTS: {
        Entity* con = bpResolveEnt(ctx, entity, readIn(cv, n, 1, ctx, 0).asEnt());
        if (!con) return true;
        con->hasConstraint = true;
        int o1 = readIn(cv, n, 2, ctx, 0).asEnt();   // 0 (unwired) leaves that side unchanged
        int o2 = readIn(cv, n, 3, ctx, 0).asEnt();
        if (o1) con->constraintObjA = o1;
        if (o2) con->constraintObjB = o2;
        if (ctx.scene) ctx.scene->rebuildConstraints();
        return true;
    }
    case BP_FLOW_SEQ: {
        int cnt = seqCount(n);   // variable number of "Then" outputs
        for (int i = 0; i < cnt; i++) {
            execChain(cv, n.id, i, ctx, depth + 1);
            if (dead || (!frames_.empty() && frames_.back().returned)) return false;
        }
        return false;
    }
    case BP_SWITCH_ENUM: {
        BPEnumAsset en;
        int count = bpLoadEnumAsset(gBPProjectDir, n.sname, en) ? (int)en.values.size() : 2;
        int selected = (int)readIn(cv, n, 1, ctx, 0).asNum();
        nextOut = selected >= 0 && selected < count ? selected : count; // last pin = Default
        return true;
    }
    default:
        return false;
    }
}

void BPInstance::execChain(const BPCanvas& cv, int nodeId, int outPin, BPContext& ctx, int depth) {
    if (depth > 24 || dead) return;
    const BPLink* link = cv.linkFromExec(nodeId, outPin);
    int steps = 0;
    while (link && steps++ < 500 && !dead) {
        BPNode* n = const_cast<BPCanvas&>(cv).byId(link->toNode);
        if (!n) return;
        int inPin = link->toPin;
        int nextOut = 0;
        // stateful multi-entrance nodes need to know the input pin used
        if (n->def == BP_FLOW_DOONCE) {
            if (inPin == 1) { nodeState_[n->id] = 0; return; }       // reset
            if (nodeState_[n->id] >= 1) return;                       // already done
            nodeState_[n->id] = 1;
            nextOut = 0;
        } else if (n->def == BP_FLOW_GATE) {
            if (inPin == 1) { nodeState_[n->id] = 1; return; }       // apri
            if (inPin == 2) { nodeState_[n->id] = 0; return; }       // chiudi
            if (nodeState_[n->id] < 1) return;                        // closed
            nextOut = 0;
        } else if (n->def == BP_FLOW_FLIPFLOP) {
            float s = nodeState_[n->id];
            nextOut = s < 0.5f ? 0 : 1;
            nodeState_[n->id] = s < 0.5f ? 1.0f : 0.0f;
        } else if (n->def == BP_FLOW_MULTIGATE) {
            if (inPin == 1) { nodeState_[n->id] = 0; return; }        // Reset
            int outs = DEFS[n->def].nOuts;
            int idx = (int)nodeState_[n->id];
            if (idx < 0 || idx >= outs) idx = 0;
            nextOut = idx;
            nodeState_[n->id] = (float)((idx + 1) % outs);           // cycle through the outputs
        } else if (n->def == BP_FLOW_DON) {
            if (inPin == 1) { nodeState_[n->id] = 0; return; }        // Reset
            int limit = (int)readIn(cv, *n, 2, ctx, 0).asNum();
            int counter = (int)nodeState_[n->id];
            if (counter >= limit) return;                             // done until reset
            nodeState_[n->id] = (float)(counter + 1);                // Counter = 1..N
            nextOut = 0;                                              // Exit
        } else {
            if (!execNode(cv, *n, ctx, depth, nextOut)) return;
        }
        if (!frames_.empty() && frames_.back().returned) return;
        link = cv.linkFromExec(n->id, nextOut);
    }
}

void BPInstance::fire(int evType, BPContext& ctx, int outPin) {
    if (dead || !graph) return;
    // the widget tick is a widget graph's only per-frame beat, so it drives the
    // latent nodes (Delay / timers) exactly like the actor tick does
    if (evType == BP_EV_TICK || evType == BP_EV_W_TICK) {
        updateLatent(ctx);
        if (dead) return;
    }
    for (auto& gph : graph->graphs) {
        for (auto& n : gph.body.nodes) {
            if (n.def != evType) continue;
            if (evType == BP_EV_KEY && n.choice != ctx.eventKey) continue;
            execChain(gph.body, n.id, outPin, ctx, 0);
            if (dead) return;
        }
    }
}

void BPInstance::fireCustom(const char* name, BPContext& ctx) {
    if (dead || !graph) return;
    for (auto& gph : graph->graphs) {
        for (auto& n : gph.body.nodes) {
            if (n.def != BP_EV_CUSTOM || strcmp(n.sname, name) != 0) continue;
            execChain(gph.body, n.id, 0, ctx, 0);
            if (dead) return;
        }
    }
}

const BPDispatcherDef* BPInstance::findDispatcherDef(const char* name) const {
    if (!graph || !name || !name[0]) return nullptr;
    return const_cast<BPGraph*>(graph)->findDispatcher(name);
}

// Registers a listener against a dispatcher this instance owns. `listenerGraph`
// is where the Custom Event is declared — the same graph for a local bind, the
// caller's graph for a cross-Blueprint one — and is what the signature is
// checked against, so a mismatched event can never be wired up silently.
bool BPInstance::bindDispatcher(const char* dispatcher, int listenerEntity, int listenerWidget,
                                const char* eventName, const BPGraph* listenerGraph, std::string* why) {
    auto fail = [&](const char* msg) { if (why) *why = msg; return false; };
    if (!dispatcher || !dispatcher[0] || !eventName || !eventName[0]) return fail("no dispatcher or event named.");
    const BPDispatcherDef* def = findDispatcherDef(dispatcher);
    if (!def) return fail("the target owns no dispatcher with that name.");
    const BPEventDef* event = listenerGraph ? const_cast<BPGraph*>(listenerGraph)->findEvent(eventName) : nullptr;
    if (!event) return fail("the Custom Event does not exist on the listener.");
    if (event->params.size() != def->params.size()) return fail("the Custom Event must match the Dispatcher signature.");
    for (size_t i = 0; i < def->params.size(); i++)
        if (def->params[i].kind != event->params[i].kind) return fail("the Custom Event must match the Dispatcher signature.");
    auto& list = dispatchBindings_[dispatcher];
    for (const BPDispatchBinding& b : list)                    // binding twice is a no-op
        if (b.listenerEntity == listenerEntity && b.listenerWidget == listenerWidget &&
            b.eventName == eventName) return true;
    list.push_back({ eventName, listenerEntity, listenerWidget });
    return true;
}

void BPInstance::fireCustomWithArgs(const char* name,const std::vector<BPValue>& args,BPContext& ctx) {
    std::vector<BPValue> saved=curEventArgs_;
    curEventArgs_=args;
    fireCustom(name,ctx);
    curEventArgs_=std::move(saved);
}

// ═══ editor ═══
std::vector<BPValue> BPInstance::callNamedFunction(const char* name,const std::vector<BPValue>& args,BPContext& ctx) {
    if(dead||!graph||!name||!name[0])return {};
    BPFunc* function=graph->findFunc(name);
    return function?callFunction(*function,args,ctx,0):std::vector<BPValue>{};
}

void BPInstance::fireOverlapBinding(bool begin, int componentId, int otherActorId, BPContext& ctx) {
    if (dead || !graph) return;
    for (const BPOverlapBinding& binding : overlapBindings_) {
        if (binding.begin != begin || binding.componentId != componentId) continue;
        std::vector<BPValue> saved = curEventArgs_;
        curEventArgs_ = { BPValue::E(componentId), BPValue::E(otherActorId) };
        for (auto& graphCanvas : graph->graphs) {
            bool executed = false;
            for (const BPNode& eventNode : graphCanvas.body.nodes) {
                if (eventNode.def != BP_EV_CUSTOM || strcmp(eventNode.sname, binding.eventName.c_str()) != 0) continue;
                execChain(graphCanvas.body, eventNode.id, 0, ctx, 0);
                executed = true;
                break;
            }
            if (executed || dead) break;
        }
        curEventArgs_ = saved;
        if (dead) return;
    }
}

static const Vec3 CAT_COLORS[7] = {
    { 0.62f, 0.22f, 0.22f }, { 0.18f, 0.36f, 0.6f }, { 0.18f, 0.48f, 0.3f },
    { 0.55f, 0.4f, 0.14f }, { 0.42f, 0.3f, 0.6f }, { 0.66f, 0.45f, 0.16f }, { 0.3f, 0.5f, 0.55f },
};
static const Vec3 PIN_COLORS[PIN_KIND_COUNT] = {
    { 0.92f, 0.94f, 0.97f },  // exec   bianco
    { 0.36f, 0.85f, 0.45f },  // float  verde
    { 0.98f, 0.86f, 0.18f },  // vector3 giallo
    { 0.95f, 0.32f, 0.32f },  // bool   rosso
    { 0.35f, 0.72f, 0.98f },  // object azzurro
    { 0.72f, 0.72f, 0.78f },  // any    grigio (assume il colore del pin sorgente)
    { 0.30f, 0.95f, 0.85f },  // int    turchese
    { 0.30f, 0.55f, 0.95f },  // vector2 blu
    { 0.72f, 0.45f, 0.95f },  // string viola
    { 0.95f, 0.55f, 0.15f },  // transform arancione
    { 0.90f, 0.20f, 0.45f },  // delegate magenta
    { 0.38f, 0.88f, 0.92f },  // timer handle ciano
    { 0.74f, 0.36f, 0.92f },  // enum viola
    { 0.74f, 0.36f, 0.92f },  // animation clip viola
    { 0.35f, 0.72f, 0.98f },  // controller legacy: stesso azzurro di Object
    { 1.00f, 1.00f, 1.00f },  // color bianco
    { 0.55f, 0.85f, 0.65f },  // widget verde chiaro: riferimento, non un numero
};
static const Vec3 FN_NODE_COLOR = { 0.42f, 0.26f, 0.62f };   // viola: nodi nel grafo di una funzione
static const float NODE_W = 168;
static const float NTITLE_H = 22;
static const float NODE_STRIP_H = 22;
static const float BP_GRID = 14;   // world-space snap step for node movement
static float snapGrid(float v) { return floorf(v / BP_GRID + 0.5f) * BP_GRID; }
static const float PIN_STEP = 23;  // 17 px glyph + 3 px padding above and below
static const float LIT_W = 46;
static const float LIT_STRIDE = 50;

static float commentTitleHeight(const BPComment& c, float zoom) {
    return (std::max)(24.0f, 8.0f + 17.0f * clampf(c.fontSize, 0.6f, 5.0f)) * zoom;
}

// literal edit boxes: how many components, and whether the pin has one at all
static int litComps(PinKind k) { return k == PIN_VEC ? 3 : k == PIN_VEC2 ? 2 : 1; }
static bool litEditable(PinKind k) {
    // exec/oggetto/transform/delegate non hanno editor inline; string ha un box di testo, gli altri numerici/check
    // (il transform si costruisce con Make Transform o si modifica nei Dettagli della variabile)
    return k != PIN_EXEC && k != PIN_ENT && k != PIN_TRANSFORM && k != PIN_DELEGATE && k != PIN_TIMER_HANDLE &&
           k != PIN_ANIMATION_CLIP && k != PIN_ANIMATOR_CONTROLLER && k != PIN_WIDGET;
}

// effective type of a pin: PIN_ANY pins (wildcard math, reroute, Get variable)
// resolve to the concrete type they carry, so they render with that colour
PinKind bpEffKind(const BPCanvas& cv, const BPGraph& g, int nodeId, int pin, bool isOut, int depth) {
    const BPNode* n = cv.byId(nodeId);
    if (!n || depth > 24) return PIN_ANY;
    // dynamic function signatures (Entry outputs / Return inputs / Call both sides)
    if (n->def == BP_FN_ENTRY && isOut) {
        const BPFunc* f = funcOwning(const_cast<BPGraph*>(&g), cv);
        int off = f && f->pure ? 0 : 1;
        if (off && pin == 0) return PIN_EXEC;
        return f && pin - off >= 0 && pin - off < (int)f->ins.size() ? f->ins[pin - off].kind : PIN_ANY;
    }
    if (n->def == BP_FN_RETURN && !isOut) {
        const BPFunc* f = funcOwning(const_cast<BPGraph*>(&g), cv);
        int off = f && f->pure ? 0 : 1;
        if (off && pin == 0) return PIN_EXEC;
        return f && pin - off >= 0 && pin - off < (int)f->outs.size() ? f->outs[pin - off].kind : PIN_ANY;
    }
    if (n->def == BP_CALL_FUNC) {
        const BPFunc* f = const_cast<BPGraph&>(g).findFunc(n->sname);
        if (!f) return PIN_ANY;
        int off = f->pure ? 0 : 1;
        if (off && pin == 0) return PIN_EXEC;
        const std::vector<BPFuncPin>& v = isOut ? f->outs : f->ins;
        return pin - off >= 0 && pin - off < (int)v.size() ? v[pin - off].kind : PIN_ANY;
    }
    if (n->def == BP_MEMBER_ACCESS) {
        BPNodeDef member = bpMemberNodeDef(gBPProjectDir, *n);
        return isOut ? (pin >= 0 && pin < member.nOuts ? member.outs[pin].kind : PIN_ANY)
                     : (pin >= 0 && pin < member.nIns ? member.ins[pin].kind : PIN_ANY);
    }
    if(n->def==BP_INTERFACE_MESSAGE){
        if(pin==0)return PIN_EXEC;
        if(!isOut&&pin==1)return PIN_ENT;
        BPFunc function;
        if(!bpLoadInterfaceFunction(gBPProjectDir,n->slit[0],n->sname,function))return PIN_ANY;
        const std::vector<BPFuncPin>& pins=isOut?function.outs:function.ins;
        int index=pin-(isOut?1:2);
        return index>=0&&index<(int)pins.size()?pins[index].kind:PIN_ANY;
    }
    if(n->def==BP_CALL_DISPATCH&&!isOut){
        if(pin==0)return PIN_EXEC;BPDispatcherDef* dispatcher=const_cast<BPGraph&>(g).findDispatcher(n->sname);
        return dispatcher&&pin-1<(int)dispatcher->params.size()?dispatcher->params[pin-1].kind:PIN_ANY;
    }
    // custom event: i parametri sono pin dati in uscita sull'evento, in ingresso su Chiama Evento
    if (n->def == BP_EV_CUSTOM && isOut) {
        if (pin == 0) return PIN_EXEC;
        const BPEventDef* ed = const_cast<BPGraph&>(g).findEvent(n->sname);
        int np = ed ? (int)ed->params.size() : 0;
        if (pin - 1 < np) return ed->params[pin - 1].kind;
        return PIN_DELEGATE;   // il pin dopo i parametri e' il delegate
    }
    if (n->def == BP_CALL_EVENT && !isOut) {
        if (pin == 0) return PIN_EXEC;
        const BPEventDef* ed = const_cast<BPGraph&>(g).findEvent(n->sname);
        return ed && pin - 1 < (int)ed->params.size() ? ed->params[pin - 1].kind : PIN_ANY;
    }
    if (n->def == BP_SPAWN_PREFAB && !isOut) {
        if (pin == 0) return PIN_EXEC;
        if (pin == 1) return PIN_TRANSFORM;
        std::vector<BPSpawnPinInfo> pins = bpSpawnPinInfo(gBPProjectDir, n->sname);
        return pin - 2 >= 0 && pin - 2 < (int)pins.size() ? pins[pin - 2].kind : PIN_ANY;
    }
    if (n->def == BP_SELECT_ENUM) {
        if (!isOut) return pin == 0 ? PIN_ENUM : ([&]() {
            const BPLink* l = cv.linkInto(nodeId, pin);
            return l ? bpEffKind(cv, g, l->fromNode, l->fromPin, true, depth + 1) : PIN_ANY;
        })();
        BPEnumAsset en;
        int count = bpLoadEnumAsset(gBPProjectDir, n->sname, en) ? (int)en.values.size() : 2;
        PinKind result = PIN_ANY;
        for (int i = 0; i < count; i++) {
            PinKind k = bpEffKind(cv, g, nodeId, i + 1, false, depth + 1);
            if (k == PIN_VEC) return PIN_VEC;
            if (k == PIN_VEC2) result = PIN_VEC2;
            else if (result == PIN_ANY && k != PIN_ANY) result = k;
        }
        return result;
    }
    if (n->def == BP_SWITCH_ENUM) return isOut ? PIN_EXEC : (pin == 0 ? PIN_EXEC : PIN_ENUM);
    const BPNodeDef& d = DEFS[n->def];
    PinKind decl = isOut ? (pin < d.nOuts ? d.outs[pin].kind : PIN_ANY)
                         : (pin < d.nIns ? d.ins[pin].kind : PIN_ANY);
    // variables carry their declared type on the ANY pin
    if ((n->def == BP_VAR_GET || n->def == BP_LOCAL_GET) && isOut) {
        const BPVarDef* v = const_cast<BPGraph&>(g).findVar(n->sname);
        return v ? v->type : PIN_ANY;
    }
    // Set: sia il pin valore in ingresso (1) sia il pin di ritorno in uscita (1)
    // portano il tipo della variabile
    if ((n->def == BP_VAR_SET || n->def == BP_LOCAL_SET) && pin == 1) {
        const BPVarDef* v = const_cast<BPGraph&>(g).findVar(n->sname);
        if (v) return v->type;
    }
    // InputAction "Value": Mouse XY porta un Vector2, gli altri assi/tasti un Float
    if (n->def == BP_EV_KEY && isOut && pin == 3) {
        int c = n->choice % BP_NBINDS;
        if (c >= BP_NKEYS) return (c - BP_NKEYS) == 2 ? PIN_VEC2 : PIN_NUM;
        return PIN_NUM;
    }
    if (decl != PIN_ANY) return decl;
    if (!isOut) {
        // an ANY input takes the colour of whatever feeds it
        const BPLink* l = cv.linkInto(nodeId, pin);
        if (l) return bpEffKind(cv, g, l->fromNode, l->fromPin, true, depth + 1);
        return PIN_ANY;
    }
    // ANY output: wildcard math / reroute derive from their inputs
    if (n->def == BP_REROUTE) return bpEffKind(cv, g, nodeId, 0, false, depth + 1);
    if (n->def == BP_M_ADD || n->def == BP_M_SUB || n->def == BP_M_MUL || n->def == BP_M_DIV || n->def == BP_M_LERP) {
        PinKind a = bpEffKind(cv, g, nodeId, 0, false, depth + 1);
        PinKind b = bpEffKind(cv, g, nodeId, 1, false, depth + 1);
        if (a == PIN_VEC || b == PIN_VEC) return PIN_VEC;
        if (a == PIN_VEC2 || b == PIN_VEC2) return PIN_VEC2;
        if (a == PIN_INT && b == PIN_INT) return PIN_INT;
        if (a == PIN_ANY && b != PIN_ANY) return b;
        if (b == PIN_ANY && a != PIN_ANY) return a;
        return a;
    }
    return PIN_ANY;
}

std::string bpPinRefClass(const BPCanvas& cv, const BPGraph& graph,
                          int nodeId, int pin, bool isOut, int depth) {
    if (depth > 20) return {};
    const BPNode* node = cv.byId(nodeId);
    if (!node) return {};
    if (!isOut) {
        const BPLink* link = cv.linkInto(nodeId, pin);
        return link ? bpPinRefClass(cv, graph, link->fromNode, link->fromPin, true, depth + 1) : std::string{};
    }
    if (node->def == BP_GET_COMPONENT && pin == 0)
        return !node->slit[0].empty() ? "blueprint:" + node->slit[0] : "component:" + std::to_string(node->choice);
    if (node->def == BP_CAST_TO_CLASS && pin == 2) return node->sname;
    // A variable exposes its class so dragging off it can offer that class's
    // members: Object variables (PIN_ENT) and Widget references (PIN_WIDGET)
    // both answer here, which is what makes them behave the same in the editor.
    auto varClass = [](const BPVarDef* var) {
        if (!var) return std::string{};
        if (var->type == PIN_ENT || var->type == PIN_WIDGET) return std::string(var->refClass);
        return std::string{};
    };
    if ((node->def == BP_VAR_GET || node->def == BP_LOCAL_GET) && pin == 0)
        return varClass(const_cast<BPGraph&>(graph).findVar(node->sname));
    if ((node->def == BP_VAR_SET || node->def == BP_LOCAL_SET) && pin == 1)
        return varClass(const_cast<BPGraph&>(graph).findVar(node->sname));
    if (node->def == BP_MEMBER_ACCESS && node->choice == 0 && pin == 0) {
        BPGraph owner;
        return varClass(bpLoadMemberGraph(gBPProjectDir, *node, owner) ? owner.findVar(node->sname) : nullptr);
    }
    // Create Widget hands out the instance: dragging off it offers that .wgt's API
    if (node->def == BP_CREATE_WIDGET && pin == 1 && node->sname[0])
        return std::string("widget:") + node->sname;
    if (node->def == BP_REROUTE && pin == 0) {
        const BPLink* link = cv.linkInto(nodeId, 0);
        return link ? bpPinRefClass(cv, graph, link->fromNode, link->fromPin, true, depth + 1) : std::string{};
    }
    return {};
}

static bool icontains(const char* hay, const char* needle) {
    if (!needle[0]) return true;
    size_t nl = strlen(needle);
    for (const char* h = hay; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return true;
    }
    return false;
}

static bool isVarGet(int def) { return def == BP_VAR_GET || def == BP_LOCAL_GET; }
static bool isVarSet(int def) { return def == BP_VAR_SET || def == BP_LOCAL_SET; }

static int nodeStrips(const BPNode& n) {
    if (n.def == BP_EV_KEY) return 0;   // InputAction: solo i 3 exec, tasto nel titolo
    if (n.def == BP_EV_CUSTOM) return n.slit[0].empty() ? 0 : 1; // interface origin under the event title
    if (isVarSet(n.def)) return 0;      // Set: nome nell'header, niente striscia "name:"
    if (n.def == BP_TIMER_SET_FUNC) return 1; // riferimento funzione assegnato via drag & drop
    if (n.def == BP_MEMBER_ACCESS) return 1;
    const BPNodeDef& d = DEFS[n.def];
    return (d.propKind ? 1 : 0) + (d.usesName ? 1 : 0);
}

// node title; InputAction embeds its binding
static void bpNodeTitle(const BPNode& n, char* out, int cap) {
    if (n.def == BP_EV_KEY) snprintf(out, cap, "InputAction  [%s]", bpBindName(n.choice));
    else if (n.def == BP_EV_CUSTOM) snprintf(out, cap, "%s", n.sname[0] ? n.sname : "Custom Event");
    else if (n.def == BP_CAST_TO_CLASS) snprintf(out, cap, "Cast To %s", bpClassSpecLabel(n.sname).c_str());
    else if (n.def == BP_MEMBER_ACCESS) {
        const char* action = n.choice == 0 ? "Get" : n.choice == 1 ? "Set" : n.choice == 4 ? "Event" : "Call";
        snprintf(out, cap, "%s %s", action, n.sname[0] ? n.sname : "Member");
    }
    else if (n.def == BP_INTERFACE_MESSAGE)
        snprintf(out, cap, "%s (Message)", n.sname[0] ? n.sname : "Interface Function");
    else snprintf(out, cap, "%s", DEFS[n.def].title);
}

BPCanvas& BPEditor::canvas() {
    if (curFunc >= 0 && curFunc < (int)graph.funcs.size()) return graph.funcs[curFunc].body;
    graph.ensureDefaults();
    if (curGraph < 0 || curGraph >= (int)graph.graphs.size()) curGraph = 0;
    return graph.graphs[curGraph].body;
}

const BPCanvas& BPEditor::canvas() const {
    static const BPCanvas empty;
    if (curFunc >= 0 && curFunc < (int)graph.funcs.size()) return graph.funcs[curFunc].body;
    if (curGraph >= 0 && curGraph < (int)graph.graphs.size()) return graph.graphs[curGraph].body;
    return graph.graphs.empty() ? empty : graph.graphs[0].body;
}

void BPEditor::switchCanvas(int graphIdx, int funcIdx) {
    curGraph = graphIdx;
    curFunc = funcIdx;
    selNode = 0;
    selSet.clear();
    if (curFunc >= 0 && curFunc < (int)graph.funcs.size()) {
        for (const BPNode& n : graph.funcs[curFunc].body.nodes) {
            if (n.def == BP_FN_ENTRY) { selNode = n.id; selSet.insert(n.id); break; }
        }
    }
    selComment = -1;
    dragComment = resizeComment = -1;
    panX = 40;
    panY = 40;
    wiring = false;
    paletteOpen = false;
    ctxKind = 0;
    varMenuOpen = false;
}

float BPEditor::litOffset(const BPNodeDef& d, int p) const {
    // where the literal boxes start; usa la label piu' larga fra TUTTI i pin d'ingresso
    // cosi' le colonne dei box restano allineate in verticale (es. Make Transform)
    (void)p;
    float o = 52;
    if (r_) {
        for (int i = 0; i < d.nIns; i++) {
            float t = 16 + r_->textWidth(d.ins[i].name) + 6;
            if (t > o) o = t;
        }
    }
    return o;
}

// Built-in widget enums (alignment, anchors) show their NAME on the node body
// instead of a bare index, and clicking cycles them. Returns nullptr when the
// pin is not one of them, so every other literal keeps its normal editing.
static const char* bpWidgetEnumLabel(int def, PinKind kind, int value) {
    if (kind != PIN_ENUM) return nullptr;
    if (def == BP_SET_WIDGET_HALIGN) return widgetHAlignName(value);
    if (def == BP_SET_WIDGET_VALIGN) return widgetVAlignName(value);
    if (def == BP_SET_WIDGET_ANCHOR) return widgetAnchorName(value < 0 || value >= WANCH_COUNT ? 0 : value);
    return nullptr;
}
static int bpWidgetEnumCount(int def) {
    return def == BP_SET_WIDGET_ANCHOR ? WANCH_COUNT : 4;
}

float BPEditor::literalBoxWidth(const BPNode& n, int pin, PinKind kind) const {
    if (const char* en = bpWidgetEnumLabel(n.def, kind, (int)n.lit[pin].x)) {
        float textW = r_ ? r_->textWidth(en) : (float)strlen(en) * 8.0f;
        return clampf(textW + 12.0f, LIT_W, 180.0f);
    }
    if (kind == PIN_BOOL) return 14.0f;
    if (kind == PIN_COLOR) return 34.0f;
    if (kind == PIN_STR) {
        float textW = r_ ? r_->textWidth(n.slit[pin]) : (float)n.slit[pin].size() * 8.0f;
        return clampf(textW + 12.0f, 100.0f, 440.0f);
    }
    float width = LIT_W;
    int comps = litComps(kind);
    for (int c = 0; c < comps; c++) {
        char value[64];
        if (kind == PIN_INT || kind == PIN_ENUM) snprintf(value, sizeof(value), "%d", (int)n.lit[pin].x);
        else snprintf(value, sizeof(value), "%.7g", (&n.lit[pin].x)[c]);
        float textW = r_ ? r_->textWidth(value) : (float)strlen(value) * 8.0f;
        if (textW + 12.0f > width) width = textW + 12.0f;
    }
    return clampf(width, LIT_W, 180.0f);
}


// dynamic pins for Function Entry/Return/Call and variable-length Sequence
BPNodeDef BPEditor::effDef(const BPNode& n) const {
    BPNodeDef d = DEFS[n.def];
    static thread_local char dynamicNames[BP_MAX_PINS][40];
    if (n.def == BP_FLOW_SEQ) { d.nOuts = seqCount(n); return d; }
    if (n.def == BP_MEMBER_ACCESS) return bpMemberNodeDef(projectDir, n);
    if (n.def == BP_CAST_TO_CLASS) {
        snprintf(dynamicNames[2], sizeof(dynamicNames[2]), "As %s", bpClassSpecLabel(n.sname).c_str());
        d.outs[2] = { dynamicNames[2], PIN_ENT };
        return d;
    }
    if (n.def == BP_SPAWN_PREFAB) {
        int c = 2;
        for (const BPSpawnPinInfo& pin : bpSpawnPinInfo(projectDir, n.sname)) {
            if (c >= BP_MAX_PINS) break;
            snprintf(dynamicNames[c], sizeof(dynamicNames[c]), "%s", pin.name.c_str());
            d.ins[c] = { dynamicNames[c], pin.kind };
            c++;
        }
        d.nIns = c;
        return d;
    }
    if (n.def == BP_SELECT_ENUM || n.def == BP_SWITCH_ENUM) {
        BPEnumAsset en;
        if (bpLoadEnumAsset(projectDir, n.sname, en)) {
            if (n.def == BP_SELECT_ENUM) {
                int c = 1;
                for (const std::string& value : en.values) {
                    if (c >= BP_MAX_PINS) break;
                    snprintf(dynamicNames[c], sizeof(dynamicNames[c]), "%s", value.c_str());
                    d.ins[c] = { dynamicNames[c], PIN_ANY };
                    c++;
                }
                d.nIns = c;
            } else {
                int c = 0;
                for (const std::string& value : en.values) {
                    if (c >= BP_MAX_PINS - 1) break;
                    snprintf(dynamicNames[c], sizeof(dynamicNames[c]), "%s", value.c_str());
                    d.outs[c] = { dynamicNames[c], PIN_EXEC };
                    c++;
                }
                d.outs[c++] = { "Default", PIN_EXEC };
                d.nOuts = c;
            }
        }
        return d;
    }
    if(n.def==BP_INTERFACE_MESSAGE){
        BPFunc function;int input=2,output=1;
        if(bpLoadInterfaceFunction(projectDir,n.slit[0],n.sname,function)){
            for(const BPFuncPin& pin:function.ins){if(input>=BP_MAX_PINS)break;snprintf(dynamicNames[input],sizeof(dynamicNames[input]),"%s",pin.name);d.ins[input]={dynamicNames[input],pin.kind};input++;}
            for(const BPFuncPin& pin:function.outs){if(output>=BP_MAX_PINS)break;snprintf(dynamicNames[output],sizeof(dynamicNames[output]),"%s",pin.name);d.outs[output]={dynamicNames[output],pin.kind};output++;}
        }
        d.nIns=input;d.nOuts=output;return d;
    }
    if(n.def==BP_CALL_DISPATCH){
        int input=1;if(BPDispatcherDef* dispatcher=const_cast<BPGraph&>(graph).findDispatcher(n.sname))
            for(const BPFuncPin& pin:dispatcher->params){if(input>=BP_MAX_PINS)break;snprintf(dynamicNames[input],sizeof(dynamicNames[input]),"%s",pin.name);d.ins[input]={dynamicNames[input],pin.kind};input++;}
        d.nIns=input;return d;
    }
    // custom event: parametri come pin dati in uscita; Chiama Evento: come pin in ingresso
    if (n.def == BP_EV_CUSTOM || n.def == BP_CALL_EVENT) {
        const BPEventDef* ed = const_cast<BPGraph&>(graph).findEvent(n.sname);
        bool out = (n.def == BP_EV_CUSTOM);
        BPPinDef* arr = out ? d.outs : d.ins;
        arr[0] = { "", PIN_EXEC };
        int c = 1;
        if (ed) for (const auto& p : ed->params) { if (c >= BP_MAX_PINS) break; arr[c++] = { p.name, p.kind }; }
        if (out && c < BP_MAX_PINS) arr[c++] = { "delegate", PIN_DELEGATE };   // pin delegate (per Bind)
        if (out) d.nOuts = c; else d.nIns = c;
        return d;
    }
    const BPFunc* f = nullptr;
    if (n.def == BP_FN_ENTRY || n.def == BP_FN_RETURN) {
        if (curFunc >= 0 && curFunc < (int)graph.funcs.size()) f = &graph.funcs[curFunc];
    } else if (n.def == BP_CALL_FUNC) {
        f = const_cast<BPGraph&>(graph).findFunc(n.sname);
    } else {
        return d;
    }
    if (n.def == BP_FN_ENTRY) {
        d.nIns = 0;
        int c = 0;
        if (!f || !f->pure) d.outs[c++] = { "", PIN_EXEC };
        if (f) for (const auto& p : f->ins) { if (c >= BP_MAX_PINS) break; d.outs[c++] = { p.name, p.kind }; }
        d.nOuts = c;
    } else if (n.def == BP_FN_RETURN) {
        d.nOuts = 0;
        int c = 0;
        if (!f || !f->pure) d.ins[c++] = { "", PIN_EXEC };
        if (f) for (const auto& p : f->outs) { if (c >= BP_MAX_PINS) break; d.ins[c++] = { p.name, p.kind }; }
        d.nIns = c;
    } else {   // BP_CALL_FUNC
        int ci = 0;
        if (!f || !f->pure) d.ins[ci++] = { "", PIN_EXEC };
        if (f) for (const auto& p : f->ins) { if (ci >= BP_MAX_PINS) break; d.ins[ci++] = { p.name, p.kind }; }
        d.nIns = ci;
        int co = 0;
        if (!f || !f->pure) d.outs[co++] = { "", PIN_EXEC };
        if (f) for (const auto& p : f->outs) { if (co >= BP_MAX_PINS) break; d.outs[co++] = { p.name, p.kind }; }
        d.nOuts = co;
    }
    return d;
}

PinKind BPEditor::editorKind(const BPNode& n, const BPNodeDef& d, int pin) const {
    if (pin < 0 || pin >= d.nIns) return PIN_ANY;
    // il pin valore del Set assume il tipo della variabile (bool→check, string→testo...)
    if (isVarSet(n.def) && pin == 1) {
        const BPVarDef* v = const_cast<BPGraph&>(graph).findVar(n.sname);
        if (v) return v->type;
    }
    return d.ins[pin].kind;
}

void BPEditor::nodeRect(const BPNode& n, float* w, float* h) const {
    // returns SCREEN size (world layout * zoom)
    if (isReroute(n.def)) { *w = 30 * zoom; *h = 20 * zoom; return; }
    if (isVarGet(n.def)) {
        // compact rounded pill: just the variable name + output pin
        float nameW = r_ ? r_->textWidth(n.sname) : 60;
        float pillW = nameW + 40;
        if (pillW > 360) pillW = 360;
        *w = pillW * zoom;
        *h = 29 * zoom;   // 6 px above and below the 17 px text
        return;
    }
    BPNodeDef d = effDef(n);
    const BPCanvas* current = nullptr;
    if (curFunc >= 0 && curFunc < (int)graph.funcs.size()) current = &graph.funcs[curFunc].body;
    else if (curGraph >= 0 && curGraph < (int)graph.graphs.size()) current = &graph.graphs[curGraph].body;
    int nOut = d.nOuts;
    int layoutOut = n.def == BP_EV_CUSTOM && nOut > 0 ? nOut - 1 : nOut;
    int rows = d.nIns > layoutOut ? d.nIns : layoutOut;
    float wid = NODE_W;
    if (r_) {
        // standard minimum, stretched by whatever text the node carries
        wid = 120;
        char title[160];
        bpNodeTitle(n, title, sizeof(title));
        float tw = r_->textWidth(title) + 22;
        if (tw > wid) wid = tw;
        if (d.usesName) {
            const char* pref = isVarSet(n.def) ? "SET  " : "name: ";
            float nw = r_->textWidth(pref + std::string(n.sname)) + 26;
            if (nw > wid) wid = nw;
        }
        if (n.def == BP_EV_CUSTOM && !n.slit[0].empty()) {
            std::string origin = std::string("Interface: ") + fs::path(n.slit[0]).stem().string();
            float nw = r_->textWidth(origin) + 26;
            if (nw > wid) wid = nw;
        }
        if (n.def == BP_TIMER_SET_FUNC) {
            std::string label = std::string("funzione: ") + (n.sname[0] ? n.sname : "drag here");
            float nw = r_->textWidth(label) + 26;
            if (nw > wid) wid = nw;
        }
        for (int i = 0; i < rows; i++) {
            float inW = 14, outW = 12;
            if (i < d.nIns) {
                inW = 18 + r_->textWidth(d.ins[i].name);
                PinKind ek = editorKind(n, d, i);
                if (litEditable(ek) && (!current || !current->linkInto(n.id, i))) {
                    int comps = ek == PIN_STR ? 1 : litComps(ek);
                    float boxW = literalBoxWidth(n, i, ek);
                    float lw = litOffset(d, i) + comps * boxW + (comps - 1) * 4.0f + 6;
                    if (lw > inW) inW = lw;
                }
            }
            if (i < layoutOut && n.def != BP_FLOW_SEQ) outW = 22 + r_->textWidth(d.outs[i].name);
            if (inW + outW + 6 > wid) wid = inW + outW + 6;
        }
        if (wid > 680) wid = 680;
    }
    float ht = NTITLE_H + nodeStrips(n) * NODE_STRIP_H + rows * PIN_STEP;
    if (n.def == BP_FLOW_SEQ) ht += PIN_STEP + 8;   // room for the "+" row
    *w = wid * zoom;
    *h = ht * zoom;
}

bool BPEditor::rerouteFlipped(const BPNode& n) const {
    if (!isReroute(n.def)) return false;
    const BPCanvas& C = canvas();
    float w, h;
    nodeRect(n, &w, &h);
    float centerX = n.x + (w / zoom) * 0.5f;
    for (const BPLink& link : C.links) {
        if (link.toNode != n.id || link.toPin != 0) continue;
        const BPNode* source = C.byId(link.fromNode);
        if (!source || source->id == n.id) continue;
        float sw, sh;
        nodeRect(*source, &sw, &sh);
        float sourceOutX = source->x + sw / zoom;
        return centerX < sourceOutX;
    }
    for (const BPLink& link : C.links) {
        if (link.fromNode != n.id || link.fromPin != 0) continue;
        const BPNode* target = C.byId(link.toNode);
        if (!target || target->id == n.id) continue;
        float targetInX = target->x;
        return targetInX < centerX;
    }
    return false;
}

float BPEditor::pinTangentDir(const BPNode& n, bool out) const {
    if (isReroute(n.def)) {
        bool flipped = rerouteFlipped(n);
        bool onRight = out ? !flipped : flipped;
        return onRight ? 1.0f : -1.0f;
    }
    return out ? 1.0f : -1.0f;
}

void BPEditor::pinPos(const BPNode& n, float ox, float oy, int pin, bool out, float* px, float* py) const {
    float w, h;
    nodeRect(n, &w, &h);   // screen size
    if (isReroute(n.def)) {
        bool flipped = rerouteFlipped(n);
        bool onRight = out ? !flipped : flipped;
        *px = ox + n.x * zoom + (onRight ? w : 0);
        *py = oy + n.y * zoom + h * 0.5f;
        return;
    }
    if (isVarGet(n.def)) {
        *px = ox + n.x * zoom + (out ? w : 0);
        *py = oy + n.y * zoom + h * 0.5f;
        return;
    }
    if (n.def == BP_EV_CUSTOM && out) {
        BPNodeDef d = effDef(n);
        if (pin == d.nOuts - 1 && d.outs[pin].kind == PIN_DELEGATE) {
            *px = ox + n.x * zoom + w;
            *py = oy + n.y * zoom + NTITLE_H * zoom * 0.5f;
            return;
        }
    }
    *px = ox + n.x * zoom + (out ? w : 0);
    *py = oy + n.y * zoom + (NTITLE_H + nodeStrips(n) * NODE_STRIP_H + pin * PIN_STEP + PIN_STEP * 0.5f) * zoom;
}

// screen rect of the Sequence "+" button (adds an exec output), centered at bottom
static bool seqPlusRect(const BPNode& n, float ox, float oy, float w, float z,
                        float* bx, float* by, float* bw, float* bh) {
    if (n.def != BP_FLOW_SEQ || seqCount(n) >= BP_MAX_PINS) return false;
    int cnt = seqCount(n);
    float baseY = (NTITLE_H + cnt * PIN_STEP + 13) * z;   // centered in its own row
    *bw = 22 * z; *bh = 18 * z;
    *bx = ox + n.x * z + w * 0.5f - *bw * 0.5f;
    *by = oy + n.y * z + baseY - *bh * 0.5f;
    return true;
}

void BPEditor::disconnectPin(int nodeId, int pin, bool out) {
    BPCanvas& C = canvas();
    size_t before = C.links.size();
    C.links.erase(std::remove_if(C.links.begin(), C.links.end(),
        [&](const BPLink& l) {
            return out ? (l.fromNode == nodeId && l.fromPin == pin)
                       : (l.toNode == nodeId && l.toPin == pin);
        }), C.links.end());
    if (C.links.size() != before) dirty = true;
}

bool BPEditor::isInterfaceAsset() const {
    if (curPath.empty()) return false;
    std::string ext = fs::path(curPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
    return ext == ".bpi";
}

static void bpNormalizeInterfaceFunction(BPFunc& function) {
    // A Blueprint Interface stores declarations, never executable bodies.
    // No outputs means an Event declaration; one or more outputs means a
    // Function declaration with an immutable Entry -> Return exec wire.
    function.pure = false;
    function.scope = VS_PUBLIC;
    function.body.clear();
    int entry = function.body.addNode(BP_FN_ENTRY, 60, 100);
    if (!function.outs.empty()) {
        int result = function.body.addNode(BP_FN_RETURN, 390, 100);
        function.body.connect(entry, 0, result, 0);
    }
}

void BPEditor::normalizeInterfaceAsset() {
    graph.classKind = BP_CLASS_ACTOR;
    graph.parentAsset.clear();
    graph.defaultPawnClass.clear();
    graph.playerControllerClass.clear();
    graph.defaultTags.clear();
    graph.vars.clear();
    graph.events.clear();
    graph.interfaces.clear();
    graph.interfaceAssets.clear();
    graph.dispatchers.clear();

    // Keep one empty internal canvas because the common serializer owns a
    // mandatory main canvas. It is hidden by the BPI editor and contains no
    // user graph or Construction Script.
    BPFunc hidden;
    snprintf(hidden.name, sizeof(hidden.name), "EventGraph");
    hidden.ins.clear();
    hidden.outs.clear();
    hidden.body.clear();
    graph.graphs.clear();
    graph.graphs.push_back(std::move(hidden));
    for (BPFunc& function : graph.funcs) bpNormalizeInterfaceFunction(function);
}

void BPEditor::newGraph() {
    graph.clear();
    curPath.clear();
    switchCanvas(0, -1);
    if (widgetMode) {
        // a Widget Blueprint starts from Construct, not from an actor input
        graph.main().addNode(BP_EV_W_CONSTRUCT, 40, 60);
    } else {
        int ev = graph.main().addNode(BP_EV_KEY, 40, 60);
        int act = graph.main().addNode(BP_ACT_IMPULSE, 320, 60);
        graph.main().connect(ev, 1, act, 0);   // Started → impulse
    }
    selVar = -1;
    selDispatcher = -1;
    dirty = false;
    clearHistory();
}

void BPEditor::clearHistory() {
    undoHistory.clear();
    redoHistory.clear();
    historyGestureActive = false;
    historyGestureBefore.clear();
}

void BPEditor::pushUndoState(const std::string& state) {
    if (!undoHistory.empty() && undoHistory.back() == state) return;
    undoHistory.push_back(state);
    if (undoHistory.size() > 50) undoHistory.erase(undoHistory.begin());
    redoHistory.clear();
}

void BPEditor::finishHistoryFrame(const std::string& before, bool mouseDown) {
    const std::string after = graph.serialize();
    if (after != before) {
        if (mouseDown) {
            if (!historyGestureActive) {
                historyGestureActive = true;
                historyGestureBefore = before;
            }
        } else {
            pushUndoState(before);
        }
    }
    if (historyGestureActive && !mouseDown) {
        if (after != historyGestureBefore) pushUndoState(historyGestureBefore);
        historyGestureActive = false;
        historyGestureBefore.clear();
    }
}

void BPEditor::undo() {
    if (undoHistory.empty()) return;
    std::string current = graph.serialize();
    std::string previous = undoHistory.back();
    undoHistory.pop_back();
    if (!graph.deserialize(previous)) return;
    redoHistory.push_back(std::move(current));
    if (redoHistory.size() > 50) redoHistory.erase(redoHistory.begin());
    if (curFunc >= (int)graph.funcs.size()) curFunc = -1;
    if (curGraph >= (int)graph.graphs.size()) curGraph = 0;
    if (selNode && !canvas().byId(selNode)) selNode = 0;
    dirty = true;
    historyGestureActive = false;
}

void BPEditor::redo() {
    if (redoHistory.empty()) return;
    std::string current = graph.serialize();
    std::string next = redoHistory.back();
    redoHistory.pop_back();
    if (!graph.deserialize(next)) return;
    undoHistory.push_back(std::move(current));
    if (undoHistory.size() > 50) undoHistory.erase(undoHistory.begin());
    if (curFunc >= (int)graph.funcs.size()) curFunc = -1;
    if (curGraph >= (int)graph.graphs.size()) curGraph = 0;
    if (selNode && !canvas().byId(selNode)) selNode = 0;
    dirty = true;
    historyGestureActive = false;
}

bool BPEditor::doSaveDialog(char* out) {
    char file[MAX_PATH] = "grafo.bp";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFilter = "Pulse Engine Blueprint (*.bp)\0*.bp\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "bp";
    ofn.lpstrInitialDir = projectDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) return false;
    strcpy(out, file);
    return true;
}

bool BPEditor::saveTo(const std::string& absPath) {
    std::string ext = fs::path(absPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
    if (ext == ".bpi") normalizeInterfaceAsset();
    FILE* f = fopen(absPath.c_str(), "wb");
    if (!f) return false;
    std::string data = graph.serialize();
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    if (absPath.rfind(projectDir, 0) == 0 && absPath.size() > projectDir.size() + 1) {
        curPath = absPath.substr(projectDir.size() + 1);
    } else {
        curPath = absPath;
    }
    dirty = false;
    return true;
}

bool BPEditor::loadFrom(const std::string& absPath, const std::string& rel) {
    FILE* f = fopen(absPath.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string data(size, 0);
    fread(data.data(), 1, size, f);
    fclose(f);
    if (!graph.deserialize(data)) return false;
    curPath = rel;
    if (isInterfaceAsset()) {
        normalizeInterfaceAsset();
        switchCanvas(0, graph.funcs.empty() ? -1 : 0);
    } else {
        syncImplementedInterfaces();
        switchCanvas(0, -1);
    }
    selVar = -1;
    selDispatcher = -1;
    dirty = false;
    clearHistory();
    return true;
}

void BPEditor::cycleName(BPNode& n, int dir) {
    // cycle among valid names for the node kind
    std::vector<const char*> options;
    switch (n.def) {
    case BP_VAR_GET:
        for (auto& v : graph.vars) if (v.container == VC_SINGLE) options.push_back(v.name);
        break;
    case BP_VAR_SET:
        for (auto& v : graph.vars)
            if (v.container == VC_SINGLE && !v.requiredGenerated && !v.widgetGenerated) options.push_back(v.name);
        break;
    case BP_ARR_GET: case BP_ARR_ADD: case BP_ARR_LEN: case BP_ARR_REMOVE: case BP_ARR_CLEAR:
    case BP_FLOW_FOREACH:
        for (auto& v : graph.vars) if (v.container == VC_ARRAY) options.push_back(v.name);
        break;
    case BP_MAP_GET: case BP_MAP_SET: case BP_MAP_REMOVE: case BP_MAP_LEN:
        for (auto& v : graph.vars) if (v.container == VC_MAP) options.push_back(v.name);
        break;
    case BP_CALL_FUNC:
        for (auto& f : graph.funcs) options.push_back(f.name);
        break;
    case BP_CALL_EVENT: case BP_SEND_MSG: case BP_CREATE_EVENT:
        for (auto& gph : graph.graphs)
            for (auto& ev : gph.body.nodes)
                if (ev.def == BP_EV_CUSTOM) options.push_back(ev.sname);
        break;
    case BP_BIND_EVENT: case BP_CALL_DISPATCH:
        for (auto& s : graph.dispatchers) options.push_back(s.name);
        break;
    default: return; // free-typed names (find / ev_custom / locals) — use the rename field
    }
    if (options.empty()) return;
    int cur = -1;
    for (int i = 0; i < (int)options.size(); i++) if (strcmp(options[i], n.sname) == 0) cur = i;
    cur = (cur + dir + (int)options.size() + 1 - 1) % (int)options.size();
    if (cur < 0) cur = 0;
    snprintf(n.sname, sizeof(n.sname), "%s", options[(cur) % options.size()]);
    dirty = true;
}

namespace {
struct BPSharedNodeClipboard {
    std::vector<BPNode> nodes;
    std::vector<BPLink> links;
    std::vector<BPEventDef> events;
};

BPSharedNodeClipboard gBPNodeClipboard;

static std::string bpLowerName(const char* value) {
    std::string result = value ? value : "";
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return result;
}

static bool bpNodeReferencesCustomEvent(const BPNode& node) {
    return node.def == BP_EV_CUSTOM || node.def == BP_CALL_EVENT ||
           node.def == BP_CREATE_EVENT || node.def == BP_SEND_MSG;
}
}

bool bpNodeClipboardEmpty() { return gBPNodeClipboard.nodes.empty(); }

void bpCopyNodesToClipboard(const BPGraph& sourceGraph, const BPCanvas& source,
                            const std::set<int>& nodeIds) {
    gBPNodeClipboard = {};
    for (int id : nodeIds) {
        const BPNode* node = source.byId(id);
        if (node) gBPNodeClipboard.nodes.push_back(*node);
    }
    for (const BPLink& link : source.links)
        if (nodeIds.count(link.fromNode) && nodeIds.count(link.toNode))
            gBPNodeClipboard.links.push_back(link);

    std::set<std::string> copiedEvents;
    for (const BPNode& node : gBPNodeClipboard.nodes) {
        if (!bpNodeReferencesCustomEvent(node) || !node.sname[0]) continue;
        const std::string key = bpLowerName(node.sname);
        if (!copiedEvents.insert(key).second) continue;
        for (const BPEventDef& event : sourceGraph.events) {
            if (_stricmp(event.name, node.sname) != 0) continue;
            gBPNodeClipboard.events.push_back(event);
            break;
        }
    }
}

std::vector<int> bpPasteNodesFromClipboard(BPGraph& targetGraph, BPCanvas& target,
                                           float worldX, float worldY) {
    std::vector<int> pastedIds;
    if (gBPNodeClipboard.nodes.empty()) return pastedIds;

    // A copied Custom Event carries its signature. If the destination already
    // defines that event, give the pasted definition a unique name and remap
    // calls copied in the same group to it.
    std::map<std::string, std::string> eventNames;
    for (const BPEventDef& sourceEvent : gBPNodeClipboard.events) {
        const std::string key = bpLowerName(sourceEvent.name);
        bool includesDefinition = false;
        for (const BPNode& node : gBPNodeClipboard.nodes)
            if (node.def == BP_EV_CUSTOM && _stricmp(node.sname, sourceEvent.name) == 0) {
                includesDefinition = true;
                break;
            }
        std::string targetName = sourceEvent.name;
        if (includesDefinition)
            targetName = bpUniqueMemberName(targetGraph, targetName, 2);
        eventNames[key] = targetName;
        if (!targetGraph.findEvent(targetName.c_str())) {
            BPEventDef copied = sourceEvent;
            snprintf(copied.name, sizeof(copied.name), "%s", targetName.c_str());
            targetGraph.events.push_back(std::move(copied));
        }
    }

    float centerX = 0, centerY = 0;
    for (const BPNode& node : gBPNodeClipboard.nodes) { centerX += node.x; centerY += node.y; }
    centerX /= (float)gBPNodeClipboard.nodes.size();
    centerY /= (float)gBPNodeClipboard.nodes.size();
    std::map<int, int> remap;
    for (const BPNode& sourceNode : gBPNodeClipboard.nodes) {
        BPNode node = sourceNode;
        node.id = target.nextId++;
        node.x = sourceNode.x - centerX + worldX;
        node.y = sourceNode.y - centerY + worldY;
        if (bpNodeReferencesCustomEvent(node)) {
            auto renamed = eventNames.find(bpLowerName(node.sname));
            if (renamed != eventNames.end())
                snprintf(node.sname, sizeof(node.sname), "%s", renamed->second.c_str());
        }
        remap[sourceNode.id] = node.id;
        pastedIds.push_back(node.id);
        target.nodes.push_back(std::move(node));
    }
    for (const BPLink& link : gBPNodeClipboard.links) {
        auto from = remap.find(link.fromNode), to = remap.find(link.toNode);
        if (from != remap.end() && to != remap.end())
            target.links.push_back({ from->second, link.fromPin, to->second, link.toPin });
    }
    return pastedIds;
}

void BPEditor::copySelection(BPCanvas& C) {
    std::set<int> ids = selSet;
    if (ids.empty() && selNode) ids.insert(selNode);
    bpCopyNodesToClipboard(graph, C, ids);
    if (!bpNodeClipboardEmpty() && logFn) logFn(0, "Copied %d nodes.", (int)ids.size());
}

void BPEditor::pasteClipboard(BPCanvas& C, float wx, float wy) {
    std::vector<int> pasted = bpPasteNodesFromClipboard(graph, C, wx, wy);
    if (pasted.empty()) return;
    selSet.clear();
    selSet.insert(pasted.begin(), pasted.end());
    selNode = pasted.size() == 1 ? pasted[0] : 0;
    dirty = true;
}

void BPEditor::deleteSelection(BPCanvas& C) {
    std::set<int> ids = selSet;
    if (ids.empty() && selNode) ids.insert(selNode);
    bool constructionCanvas = curFunc < 0 && curGraph >= 0 && curGraph < (int)graph.graphs.size() &&
                              strcmp(graph.graphs[curGraph].name, "ConstructionScript") == 0;
    std::vector<std::string> removedEvents;      // Custom Events that lost their node
    for (int id : ids) {
        BPNode* node = C.byId(id);
        if (constructionCanvas && node && node->def == BP_EV_CONSTRUCT) continue;
        // The Function Entry node is the function's permanent starting point and
        // must never be removable (like Unreal's function entry).
        if (node && node->def == BP_FN_ENTRY) continue;
        if (node && node->def == BP_EV_CUSTOM && node->sname[0]) removedEvents.push_back(node->sname);
        C.removeNode(id);
    }
    for (const std::string& name : removedEvents) pruneCustomEvent(name.c_str());
    if (!ids.empty()) dirty = true;
    selSet.clear();
    selNode = 0;
}

// Deleting a Custom Event node used to leave its BPEventDef behind, so the name
// stayed "taken" for ever: it kept showing up in the Custom Event lists and any
// attempt to reuse it was renamed away. Drop the declaration once no node in the
// whole Blueprint defines that event any more — interface events are exempt,
// they belong to the interface, not to the node.
void BPEditor::pruneCustomEvent(const char* name) {
    if (!name || !name[0]) return;
    auto stillDefined = [&](const BPCanvas& body) {
        for (const BPNode& n : body.nodes)
            if (n.def == BP_EV_CUSTOM && bpSameName(n.sname, name)) return true;
        return false;
    };
    for (const BPFunc& gph : graph.graphs) if (stillDefined(gph.body)) return;
    for (const BPFunc& fn : graph.funcs) if (stillDefined(fn.body)) return;
    for (const std::string& asset : graph.interfaceAssets) {
        BPGraph iface; std::string data;
        if (!bpReadTextFile(projectDir + "\\" + asset, data) || !iface.deserialize(data)) continue;
        if (iface.findFunc(name)) return;         // implemented interface event: keep it
    }
    for (size_t i = 0; i < graph.events.size(); i++)
        if (bpSameName(graph.events[i].name, name)) { graph.events.erase(graph.events.begin() + i); break; }
}

static int remapMovedIndex(int index, int from, int to) {
    if (index < 0 || from == to) return index;
    if (index == from) return to;
    if (from < to && index > from && index <= to) return index - 1;
    if (to < from && index >= to && index < from) return index + 1;
    return index;
}

template <typename T>
static int moveListItem(std::vector<T>& items, int from, int to) {
    if (from < 0 || from >= (int)items.size() || to < 0 || to >= (int)items.size() || from == to) return from;
    T moved = std::move(items[from]);
    items.erase(items.begin() + from);
    if (to > (int)items.size()) to = (int)items.size();
    items.insert(items.begin() + to, std::move(moved));
    return to;
}

static BPNode* bpFindInterfaceEventNode(BPGraph& graph, const char* name);
static std::string bpInterfaceOriginForFunction(const std::string& projectDir, BPGraph& graph, const char* functionName);

// ── My Blueprint panel: sections like Unreal (graphs, functions, vars, ...) ──
void BPEditor::drawMyBlueprint(UI& ui) {
    const UIInput& in = ui.input();
    const Vec3 dim = { 0.5f, 0.54f, 0.6f };
    char idb[48];
    // The My Blueprint list occupies the left column. A release anywhere outside
    // it (e.g. over the canvas) must never be treated as a list reorder — that
    // drop belongs to the canvas Get/Set chooser handled in draw().
    const UIRect mbPanel = ui.panelInner();
    const bool mouseInList = in.mouseX >= mbPanel.x && in.mouseX < mbPanel.x + mbPanel.w &&
                             in.mouseY >= mbPanel.y && in.mouseY < mbPanel.y + mbPanel.h;

    if (isInterfaceAsset()) {
        ui.label("BLUEPRINT INTERFACE", { .36f, .72f, .98f });
        ui.label("Signatures only: no executable graph.", dim);
        ui.spacing(5);
        bool openFunctions = (secOpen & 2) != 0;
        if (ui.treeItem("bpi_sec_funcs", "INTERFACE FUNCTIONS", 0, true, openFunctions, false, false) & UI::TREE_CLICKED)
            secOpen ^= 2;
        if (secOpen & 2) {
            for (int i = 0; i < (int)graph.funcs.size(); i++) {
                snprintf(idb, sizeof(idb), "bpifn%d", i);
                std::string kind = graph.funcs[i].outs.empty() ? "[Event] " : "[Function] ";
                int fl = ui.treeItem(idb, kind + graph.funcs[i].name, 1, false, false, curFunc == i, false);
                if (fl & UI::TREE_CLICKED) {
                    if (in.keyAlt) {
                        graph.funcs.erase(graph.funcs.begin() + i);
                        if (graph.funcs.empty()) switchCanvas(0, -1);
                        else switchCanvas(0, (std::min)(i, (int)graph.funcs.size() - 1));
                        dirty = true;
                        break;
                    }
                    switchCanvas(0, i);
                }
            }
            if (ui.button("+ Interface function")) {
                BPFunc function;
                std::string name = bpUniqueMemberName(graph, "NewFunction", 1);
                snprintf(function.name, sizeof(function.name), "%s", name.c_str());
                function.ins.clear();
                function.outs.clear();
                bpNormalizeInterfaceFunction(function);
                graph.funcs.push_back(std::move(function));
                switchCanvas(0, (int)graph.funcs.size() - 1);
                dirty = true;
            }
        }
        ui.spacing(6);
        ui.label("Without outputs: implemented as an Interface Event.", dim);
        ui.label("With outputs: implemented as an Interface Function.", dim);
        ui.label("Alt+click deletes a declaration.", dim);
        return;
    }

    // Detect a list drag before drawing the rows. Releasing on the canvas is
    // still handled later by draw(); releasing on another row reorders here.
    if (dragFuncIdx >= 0 && in.mouseDown && !dragFuncActive &&
        (fabsf(in.mouseX - dragFuncX) > 7 || fabsf(in.mouseY - dragFuncY) > 7)) dragFuncActive = true;
    if (dragVarIdx >= 0 && in.mouseDown && !dragVarActive &&
        (fabsf(in.mouseX - dragVarX) > 7 || fabsf(in.mouseY - dragVarY) > 7)) dragVarActive = true;

    // 0: WIDGET (only in a Widget Blueprint) ─────────────────
    // The Designer owns these rows: they mirror every component flagged
    // "Is Variable" and can only be dragged out as a Get.
    bool hasWidgetVars = false;
    for (const BPVarDef& v : graph.vars) if (v.widgetGenerated) { hasWidgetVars = true; break; }
    if (widgetMode && hasWidgetVars) {
        bool openWidgets = (secOpen & 32) != 0;
        if (ui.treeItem("sec_widgets", "WIDGETS", 0, true, openWidgets, false, false) & UI::TREE_CLICKED) secOpen ^= 32;
        if (secOpen & 32) {
            for (int i = 0; i < (int)graph.vars.size(); i++) {
                BPVarDef& v = graph.vars[i];
                if (!v.widgetGenerated) continue;
                // a name shared with another member makes Get ambiguous — say so
                int sameName = 0;
                for (const BPVarDef& other : graph.vars) if (bpSameName(other.name, v.name)) sameName++;
                snprintf(idb, sizeof(idb), "bpwvar%d", i);
                std::string label = std::string("[") + (v.widgetType[0] ? v.widgetType : "Widget") + "] " + v.name;
                if (sameName > 1) label += "  (duplicate name)";
                int fl = ui.treeItem(idb, label, 1, false, false, selVar == i, false);
                if (fl & UI::TREE_PRESSED) {
                    dragVarIdx = i; dragVarActive = false; dragVarOver = -1;
                    dragVarX = in.mouseX; dragVarY = in.mouseY;
                }
                if ((fl & UI::TREE_CLICKED) && !dragVarActive) {
                    selVar = i; selDispatcher = -1;
                    selNode = 0; selComment = -1; selSet.clear();
                }
            }
            ui.label("Drag one into the graph for a Get. Add or rename them in the Designer.", dim);
        }
    }

    // 1: GRAFICI ─────────────────────────────────────────────
    bool open = (secOpen & 1) != 0;
    if (ui.treeItem("sec_graphs", "GRAPHS", 0, true, open, false, false) & UI::TREE_CLICKED) secOpen ^= 1;
    if (secOpen & 1) {
        for (int i = 0; i < (int)graph.graphs.size(); i++) {
            snprintf(idb, sizeof(idb), "bpgraph%d", i);
            int fl = ui.treeItem(idb, graph.graphs[i].name, 1, false, false, curFunc < 0 && curGraph == i, false);
            if (fl & UI::TREE_CLICKED) {
                bool construction = strcmp(graph.graphs[i].name, "ConstructionScript") == 0;
                if (in.keyAlt && i > 0 && !construction) {
                    graph.graphs.erase(graph.graphs.begin() + i);
                    if (curFunc < 0 && curGraph >= i) switchCanvas(0, -1);
                    dirty = true;
                    break;
                }
                switchCanvas(i, -1);
            }
        }
        if (ui.button("+ Graph")) {
            BPFunc gph;
            snprintf(gph.name, sizeof(gph.name), "EventGraph%d", (int)graph.graphs.size() + 1);
            graph.graphs.push_back(gph);
            switchCanvas((int)graph.graphs.size() - 1, -1);
            dirty = true;
        }
    }

    // 1: FUNZIONI (grouped by subcategory) ───────────────────
    open = (secOpen & 2) != 0;
    if (ui.treeItem("sec_funcs", "FUNCTIONS", 0, true, open, false, false) & UI::TREE_CLICKED) secOpen ^= 2;
    if (secOpen & 2) {
        int hoverFunc = -1;
        dragOverCat.clear(); dragOverCatValid = false;
        auto funcRow = [&](int i, int depth) {
            snprintf(idb, sizeof(idb), "bpfn%d", i);
            bool dropHi = dragFuncActive && dragFuncOver == i && dragFuncIdx != i;
            int fl = ui.treeItem(idb, graph.funcs[i].name, depth, false, false, curFunc == i, dropHi);
            if ((fl & UI::TREE_HOVERED) && dragFuncActive) hoverFunc = i;
            if (fl & UI::TREE_PRESSED) {
                dragFuncIdx = i; dragFuncActive = false; dragFuncOver = -1;
                dragFuncX = in.mouseX; dragFuncY = in.mouseY;
            }
            if (fl & UI::TREE_RCLICKED) { mbMenuKind = 2; mbMenuIdx = i; mbMenuX = in.mouseX; mbMenuY = in.mouseY; mbMenuConfirmDelete = false; }
            if ((fl & UI::TREE_CLICKED) && !dragFuncActive) switchCanvas(curGraph, i);
        };
        std::vector<std::string> cats;
        for (const BPFunc& f : graph.funcs)
            if (f.category[0] && std::find(cats.begin(), cats.end(), f.category) == cats.end()) cats.push_back(f.category);
        for (int i = 0; i < (int)graph.funcs.size(); i++) if (!graph.funcs[i].category[0]) funcRow(i, 1);
        for (const std::string& cat : cats) {
            bool collapsed = mbCatCollapsed.count(cat) != 0;
            snprintf(idb, sizeof(idb), "fncat_%s", cat.c_str());
            bool catDrop = dragFuncActive && dragOverCatValid && dragOverCat == cat;
            int cfl = ui.treeItem(idb, cat, 1, true, !collapsed, false, catDrop);
            if (cfl & UI::TREE_TOGGLED) { if (collapsed) mbCatCollapsed.erase(cat); else mbCatCollapsed.insert(cat); }
            if ((cfl & UI::TREE_HOVERED) && dragFuncActive) { dragOverCat = cat; dragOverCatValid = true; }
            if (!collapsed)
                for (int i = 0; i < (int)graph.funcs.size(); i++) if (cat == graph.funcs[i].category) funcRow(i, 2);
        }
        if (dragFuncActive) dragFuncOver = hoverFunc;
        if (in.mouseReleased && dragFuncActive && dragFuncIdx >= 0 && dragFuncIdx < (int)graph.funcs.size()) {
            if (dragOverCatValid) {
                snprintf(graph.funcs[dragFuncIdx].category, sizeof(graph.funcs[dragFuncIdx].category), "%s", dragOverCat.c_str());
                dirty = true;
            } else if (hoverFunc >= 0) {
                std::string targetCat = graph.funcs[hoverFunc].category;
                int from = dragFuncIdx;
                int to = moveListItem(graph.funcs, from, hoverFunc);
                snprintf(graph.funcs[to].category, sizeof(graph.funcs[to].category), "%s", targetCat.c_str());
                curFunc = remapMovedIndex(curFunc, from, to);
                dirty = true;
            }
            dragFuncIdx = dragFuncOver = -1;
            dragFuncActive = false;
        }
        if (ui.button("+ Function")) {
            BPFunc f;
            std::string name = bpUniqueMemberName(graph, "Function", 1);
            snprintf(f.name, sizeof(f.name), "%s", name.c_str());
            f.body.addNode(BP_FN_ENTRY, 40, 80);
            f.ins.clear();   // nuova funzione: solo pin di esecuzione
            f.outs.clear();
            graph.funcs.push_back(f);
            switchCanvas(curGraph, (int)graph.funcs.size() - 1);
            dirty = true;
        }
    }

    // 2: VARIABILI (grouped by subcategory) ──────────────────
    open = (secOpen & 4) != 0;
    if (ui.treeItem("sec_vars", "VARIABLES", 0, true, open, false, false) & UI::TREE_CLICKED) secOpen ^= 4;
    if (secOpen & 4) {
        int hoverVar = -1;
        dragOverCat.clear(); dragOverCatValid = false;
        auto varRow = [&](int i, int depth) {
            BPVarDef& v = graph.vars[i];
            snprintf(idb, sizeof(idb), "bpvar%d", i);
            std::string typeLabel = BP_VARTYPE_NAMES[v.type];
            if (bpIsAnimatorControllerObject(v)) {
                typeLabel = "Animator Controller";
            } else if (bpIsWidgetObject(v)) {
                const char* cls = v.refClass + 7;                 // past "widget:"
                typeLabel = "Widget";
                if (cls[0]) {                                     // a specific .wgt subclass
                    std::string name = cls;
                    size_t slash = name.find_last_of("\\/");
                    if (slash != std::string::npos) name = name.substr(slash + 1);
                    size_t dot = name.rfind('.');
                    if (dot != std::string::npos) name.resize(dot);
                    typeLabel = "Widget: " + name;
                }
            } else if (v.type == PIN_ENT && v.refClass[0]) {
                const char* c = strrchr(v.refClass, ':');
                typeLabel = c ? c + 1 : v.refClass;
                size_t slash = typeLabel.find_last_of("\\/");
                if (slash != std::string::npos) typeLabel = typeLabel.substr(slash + 1);
                size_t dot = typeLabel.rfind('.');
                if (dot != std::string::npos) typeLabel.resize(dot);
            }
            std::string label = std::string("[") + typeLabel + "] " + v.name;
            bool dropHi = dragVarActive && dragVarOver == i && dragVarIdx != i;
            int fl = ui.treeItem(idb, label, depth, false, false, selVar == i, dropHi);
            if ((fl & UI::TREE_HOVERED) && dragVarActive) hoverVar = i;
            if (fl & UI::TREE_PRESSED) {
                dragVarIdx = i; dragVarActive = false; dragVarOver = -1;
                dragVarX = in.mouseX; dragVarY = in.mouseY;
            }
            if (fl & UI::TREE_RCLICKED) { mbMenuKind = 1; mbMenuIdx = i; mbMenuX = in.mouseX; mbMenuY = in.mouseY; mbMenuConfirmDelete = false; }
            if ((fl & UI::TREE_CLICKED) && !dragVarActive) {
                selVar = i; selDispatcher = -1;
                selNode = 0;   // selezionare una variabile toglie il focus dal nodo
                selComment = -1; selSet.clear();
            }
        };
        // widget components live in their own section above, not here
        auto ownRow = [&](const BPVarDef& v) { return !v.requiredGenerated && !v.widgetGenerated; };
        std::vector<std::string> cats;
        for (const BPVarDef& v : graph.vars)
            if (ownRow(v) && v.category[0] &&
                std::find(cats.begin(), cats.end(), v.category) == cats.end()) cats.push_back(v.category);
        for (int i = 0; i < (int)graph.vars.size(); i++)
            if (ownRow(graph.vars[i]) && !graph.vars[i].category[0]) varRow(i, 1);
        for (const std::string& cat : cats) {
            bool collapsed = mbCatCollapsed.count(cat) != 0;
            snprintf(idb, sizeof(idb), "varcat_%s", cat.c_str());
            bool catDrop = dragVarActive && dragOverCatValid && dragOverCat == cat;
            int cfl = ui.treeItem(idb, cat, 1, true, !collapsed, false, catDrop);
            if (cfl & UI::TREE_TOGGLED) { if (collapsed) mbCatCollapsed.erase(cat); else mbCatCollapsed.insert(cat); }
            if ((cfl & UI::TREE_HOVERED) && dragVarActive) { dragOverCat = cat; dragOverCatValid = true; }
            if (!collapsed)
                for (int i = 0; i < (int)graph.vars.size(); i++)
                    if (ownRow(graph.vars[i]) && cat == graph.vars[i].category) varRow(i, 2);
        }
        if (dragVarActive) dragVarOver = hoverVar;
        // Only consume the release when it lands on a list target (a category
        // heading or another row). A drop on the canvas leaves the drag state
        // intact so the canvas handler can open the Get/Set chooser.
        if (in.mouseReleased && dragVarActive && dragVarIdx >= 0 && dragVarIdx < (int)graph.vars.size() &&
            ownRow(graph.vars[dragVarIdx]) && mouseInList && (dragOverCatValid || hoverVar >= 0)) {
            if (dragOverCatValid) {
                snprintf(graph.vars[dragVarIdx].category, sizeof(graph.vars[dragVarIdx].category), "%s", dragOverCat.c_str());
                dirty = true;
            } else if (hoverVar >= 0) {
                std::string targetCat = graph.vars[hoverVar].category;
                int from = dragVarIdx;
                int to = moveListItem(graph.vars, from, hoverVar);
                snprintf(graph.vars[to].category, sizeof(graph.vars[to].category), "%s", targetCat.c_str());
                selVar = remapMovedIndex(selVar, from, to);
                dirty = true;
            }
            dragVarIdx = dragVarOver = -1;
            dragVarActive = false;
        }
        if (ui.button("+ Variable")) {
            BPVarDef v;
            std::string name = bpUniqueMemberName(graph, "var", 0);
            snprintf(v.name, sizeof(v.name), "%s", name.c_str());
            graph.vars.push_back(v);
            selVar = (int)graph.vars.size() - 1;
            selDispatcher = -1;
            dirty = true;
        }
        if (!graph.requiredComponents.empty()) {
            ui.spacing(5);
            ui.label("REQUIRED", { .94f, .69f, .30f });
            for (int i = 0; i < (int)graph.vars.size(); i++) {
                BPVarDef& v = graph.vars[i];
                if (!v.requiredGenerated) continue;
                snprintf(idb, sizeof(idb), "bprequired%d", i);
                std::string label = std::string("[Object] ") + v.name + "  [sola lettura]";
                int fl = ui.treeItem(idb, label, 1, false, false, selVar == i, false);
                if (fl & UI::TREE_PRESSED) {
                    dragVarIdx = i;
                    dragVarActive = false;
                    dragVarOver = -1;
                    dragVarX = in.mouseX;
                    dragVarY = in.mouseY;
                }
                if ((fl & UI::TREE_CLICKED) && !dragVarActive) {
                    selVar = i;
                    selDispatcher = -1;
                    selNode = 0;
                    selComment = -1;
                    selSet.clear();
                }
            }
        }
    }

    // 3: INTERFACCE ──────────────────────────────────────────
    open = (secOpen & 8) != 0;
    if (ui.treeItem("sec_ifaces", "INTERFACES", 0, true, open, false, false) & UI::TREE_CLICKED) secOpen ^= 8;
    if (secOpen & 8) {
        for (int i = 0; i < (int)graph.interfaces.size(); i++) {
            snprintf(idb, sizeof(idb), "bpif%d", i);
            BPFunc* interfaceFunction = graph.findFunc(graph.interfaces[i].c_str());
            BPNode* interfaceEvent = interfaceFunction ? nullptr : bpFindInterfaceEventNode(graph, graph.interfaces[i].c_str());
            std::string origin = interfaceFunction
                ? bpInterfaceOriginForFunction(projectDir, graph, graph.interfaces[i].c_str())
                : (interfaceEvent ? interfaceEvent->slit[0] : std::string{});
            std::string label = interfaceFunction ? "[Function] " : "[Event] ";
            label += graph.interfaces[i];
            if (!origin.empty()) label += "  (" + fs::path(origin).stem().string() + ")";
            int fl = ui.treeItem(idb, label, 1, false, false, false, false);
            if (fl & UI::TREE_CLICKED) {
                // The relation is managed only from Blueprint Settings. Here
                // the exact functions declared by implemented assets are shown.
                int fidx = -1;
                for (int k = 0; k < (int)graph.funcs.size(); k++) {
                    if (graph.interfaces[i] == graph.funcs[k].name) fidx = k;
                }
                if (fidx >= 0) switchCanvas(curGraph, fidx);
                else {
                    for (int graphIndex = 0; graphIndex < (int)graph.graphs.size(); graphIndex++) {
                        BPNode* eventNode = nullptr;
                        for (BPNode& node : graph.graphs[graphIndex].body.nodes)
                            if (node.def == BP_EV_CUSTOM && _stricmp(node.sname, graph.interfaces[i].c_str()) == 0) {
                                eventNode = &node; break;
                            }
                        if (!eventNode) continue;
                        int eventId = eventNode->id;
                        switchCanvas(graphIndex, -1);
                        selNode = eventId;
                        selSet.insert(eventId);
                        break;
                    }
                }
            }
        }
        if (graph.interfaces.empty()) ui.label("Add interfaces from Settings", dim);
    }

    // 4: EVENT DISPATCHERS (grouped by subcategory) ──────────
    open = (secOpen & 16) != 0;
    if (ui.treeItem("sec_disp", "EVENT DISPATCHERS", 0, true, open, false, false) & UI::TREE_CLICKED) secOpen ^= 16;
    if (secOpen & 16) {
        auto dispRow = [&](int i, int depth) {
            snprintf(idb, sizeof(idb), "bpdsp%d", i);
            int fl = ui.treeItem(idb, graph.dispatchers[i].name, depth, false, false, selDispatcher==i, false);
            if (fl & UI::TREE_RCLICKED) { mbMenuKind = 3; mbMenuIdx = i; mbMenuX = in.mouseX; mbMenuY = in.mouseY; mbMenuConfirmDelete = false; }
            if (fl & UI::TREE_CLICKED) { selDispatcher=i;selNode=0;selVar=-1;selComment=-1; }
        };
        std::vector<std::string> cats;
        for (const BPDispatcherDef& d : graph.dispatchers)
            if (d.category[0] && std::find(cats.begin(), cats.end(), d.category) == cats.end()) cats.push_back(d.category);
        for (int i = 0; i < (int)graph.dispatchers.size(); i++) if (!graph.dispatchers[i].category[0]) dispRow(i, 1);
        for (const std::string& cat : cats) {
            bool collapsed = mbCatCollapsed.count(cat) != 0;
            snprintf(idb, sizeof(idb), "dspcat_%s", cat.c_str());
            int cfl = ui.treeItem(idb, cat, 1, true, !collapsed, false, false);
            if (cfl & UI::TREE_TOGGLED) { if (collapsed) mbCatCollapsed.erase(cat); else mbCatCollapsed.insert(cat); }
            if (!collapsed)
                for (int i = 0; i < (int)graph.dispatchers.size(); i++) if (cat == graph.dispatchers[i].category) dispRow(i, 2);
        }
        if (ui.button("+ Dispatcher")) {
            BPDispatcherDef dispatcher;snprintf(dispatcher.name,sizeof(dispatcher.name),"Dispatcher%d",(int)graph.dispatchers.size()+1);
            graph.dispatchers.push_back(dispatcher);selDispatcher=(int)graph.dispatchers.size()-1;selNode=0;selVar=-1;
            dirty = true;
        }
        ui.label("Select to edit the signature and create a Call Dispatcher.", { 0.5f, 0.54f, 0.6f });
    }

    ui.spacing(4);
    ui.label("Drag: reorder / change category / drop into the graph", dim);
    ui.label("Right-click: rename / delete", dim);
}

// Rename text field with deferred validation. The user types freely; while the field
// holds a duplicate/empty name it is outlined in red; only on commit (Enter or click
// away) does it either keep the new name (calling onCommit with the previous name, e.g.
// to rename references) or, if invalid, revert to the last committed name.
bool BPEditor::nameField(UI& ui, const char* id, char* buf, int cap,
                         const std::function<bool(const char*)>& isDuplicate,
                         const std::function<void(const char* oldName)>& onCommit) {
    bool wasFocused = ui.inputFocused(id);
    if (!wasFocused) renameBaseline_[id] = buf;    // idle: buf is a valid committed name
    ui.textInput(id, buf, cap);
    UIRect fr = ui.lastItemRect();
    for (char* c = buf; *c; c++) if (*c == ' ') *c = '_';   // names carry no spaces
    bool isFocused = ui.inputFocused(id);
    bool invalid = buf[0] == 0 || isDuplicate(buf);
    if (isFocused && invalid) {   // red outline while the pending name is not acceptable
        Vec3 red = { 0.92f, 0.26f, 0.24f };
        ui.r->drawRectPx(fr.x, fr.y, fr.w, 2, red, 1);
        ui.r->drawRectPx(fr.x, fr.y + fr.h - 2, fr.w, 2, red, 1);
        ui.r->drawRectPx(fr.x, fr.y, 2, fr.h, red, 1);
        ui.r->drawRectPx(fr.x + fr.w - 2, fr.y, 2, fr.h, red, 1);
    }
    if (wasFocused && !isFocused) {   // committing this frame (Enter or focus lost)
        std::string base = renameBaseline_.count(id) ? renameBaseline_[id] : std::string(buf);
        if (invalid) { snprintf(buf, cap, "%s", base.c_str()); }   // reject: restore previous
        else if (base != buf) { onCommit(base.c_str()); renameBaseline_[id] = buf; return true; }
    }
    return false;
}

// My Blueprint right-click menu (Rinomina / Elimina→conferma) + the inline rename
// popup it launches. Drawn last so it floats over the panel. mbMenuKind/mbRenameKind:
// 1 = variable, 2 = function, 3 = dispatcher; mbMenuIdx is the index into that list.
void BPEditor::drawMyBlueprintMenus(UI& ui) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    auto listSize = [&](int kind) {
        return kind == 1 ? (int)graph.vars.size() : kind == 2 ? (int)graph.funcs.size() : (int)graph.dispatchers.size();
    };
    auto applyRename = [&]() {
        if (mbRenameBuf[0] == 0) return;
        if (mbRenameKind == 1 && mbRenameIdx < (int)graph.vars.size()) {
            BPVarDef& v = graph.vars[mbRenameIdx];
            if (v.requiredGenerated || v.widgetGenerated) return;
            std::string oldName = v.name;
            std::string unique = bpUniqueMemberName(graph, mbRenameBuf, 0, mbRenameIdx);
            if (unique == oldName) return;
            snprintf(v.name, sizeof(v.name), "%s", unique.c_str());
            auto ren = [&](BPCanvas& c) { for (BPNode& n : c.nodes) if ((n.def == BP_VAR_GET || n.def == BP_VAR_SET) && bpSameName(n.sname, oldName.c_str())) snprintf(n.sname, sizeof(n.sname), "%s", v.name); };
            for (BPFunc& g : graph.graphs) ren(g.body);
            for (BPFunc& f : graph.funcs) ren(f.body);
            dirty = true;
        } else if (mbRenameKind == 2 && mbRenameIdx < (int)graph.funcs.size()) {
            BPFunc& f = graph.funcs[mbRenameIdx];
            std::string oldName = f.name;
            std::string unique = bpUniqueMemberName(graph, mbRenameBuf, 1, mbRenameIdx);
            if (unique == oldName) return;
            snprintf(f.name, sizeof(f.name), "%s", unique.c_str());
            renameFunctionReferences(oldName.c_str(), f.name);
            dirty = true;
        } else if (mbRenameKind == 3 && mbRenameIdx < (int)graph.dispatchers.size()) {
            BPDispatcherDef& d = graph.dispatchers[mbRenameIdx];
            std::string oldName = d.name;
            if (oldName == mbRenameBuf) return;
            snprintf(d.name, sizeof(d.name), "%s", mbRenameBuf);
            auto ren = [&](BPCanvas& c) { for (BPNode& n : c.nodes) if ((n.def == BP_BIND_EVENT || n.def == BP_CALL_DISPATCH) && strcmp(n.sname, oldName.c_str()) == 0) snprintf(n.sname, sizeof(n.sname), "%s", d.name); };
            for (BPFunc& g : graph.graphs) ren(g.body);
            for (BPFunc& f : graph.funcs) ren(f.body);
            dirty = true;
        }
    };
    auto applyDelete = [&]() {
        if (mbMenuKind == 1 && mbMenuIdx < (int)graph.vars.size()) {
            if (graph.vars[mbMenuIdx].requiredGenerated || graph.vars[mbMenuIdx].widgetGenerated) return;
            graph.vars.erase(graph.vars.begin() + mbMenuIdx);
            if (selVar == mbMenuIdx) selVar = -1; else if (selVar > mbMenuIdx) selVar--;
            dirty = true;
        } else if (mbMenuKind == 2 && mbMenuIdx < (int)graph.funcs.size()) {
            bool interfaceFunction = std::find(graph.interfaces.begin(), graph.interfaces.end(), graph.funcs[mbMenuIdx].name) != graph.interfaces.end();
            if (interfaceFunction) { if (logFn) logFn(2, "Interface functions are removed from the Blueprint Settings."); return; }
            graph.funcs.erase(graph.funcs.begin() + mbMenuIdx);
            if (curFunc == mbMenuIdx) switchCanvas(0, -1);
            else if (curFunc > mbMenuIdx) curFunc--;
            dirty = true;
        } else if (mbMenuKind == 3 && mbMenuIdx < (int)graph.dispatchers.size()) {
            graph.dispatchers.erase(graph.dispatchers.begin() + mbMenuIdx);
            if (selDispatcher == mbMenuIdx) selDispatcher = -1; else if (selDispatcher > mbMenuIdx) selDispatcher--;
            dirty = true;
        }
    };

    // ── inline rename popup (takes precedence over the menu) ──
    if (mbRenameKind != 0) {
        if (mbRenameIdx < 0 || mbRenameIdx >= listSize(mbRenameKind)) { mbRenameKind = mbMenuKind = 0; return; }
        const float MW = 210, MH = 54;
        UIRect pf = ui.panelInner();
        float mx = mbMenuX, my = mbMenuY;
        if (mx + MW > pf.x + pf.w) mx = pf.x + pf.w - MW;
        if (my + MH > pf.y + pf.h) my = pf.y + pf.h - MH;
        r->setUIScissor(0, 0, 0, 0, false);
        r->drawRectPx(mx + 3, my + 4, MW, MH, { 0, 0, 0 }, 0.35f);
        r->drawRectPx(mx, my, MW, MH, { 0.13f, 0.145f, 0.17f }, 0.99f);
        r->drawRectPx(mx, my, MW, 2, { 0.30f, 0.62f, 0.99f }, 1);
        r->drawTextLine(mx + 10, my + 6, "Rename (Enter confirms):", { 0.68f, 0.76f, 0.88f }, 1);
        ui.registerBlockingRect({ mx, my, MW, MH });
        UIRect fld = { mx + 10, my + 26, MW - 20, 22 };
        ui.textInputRect("mb_rename_field", mbRenameBuf, sizeof(mbRenameBuf), fld, mbRenameFocus);
        mbRenameFocus = false;
        for (char* c = mbRenameBuf; *c; c++) if (*c == ' ') *c = '_';
        bool inPopup = in.mouseX >= mx && in.mouseX < mx + MW && in.mouseY >= my && in.mouseY < my + MH;
        if (in.keyEscape) { mbRenameKind = mbMenuKind = 0; }
        else if (in.keyEnter || (in.mousePressed && !inPopup)) { applyRename(); mbRenameKind = mbMenuKind = 0; }
        return;
    }

    if (mbMenuKind == 0) return;
    if (mbMenuIdx < 0 || mbMenuIdx >= listSize(mbMenuKind)) { mbMenuKind = 0; return; }
    const float MW = 178, IH = 22;
    const int count = 2;
    float mh = count * IH + 8;
    UIRect pf = ui.panelInner();
    float mx = mbMenuX, my = mbMenuY;
    if (mx + MW > pf.x + pf.w) mx = pf.x + pf.w - MW;
    if (my + mh > pf.y + pf.h) my = pf.y + pf.h - mh;
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(mx + 3, my + 4, MW, mh, { 0, 0, 0 }, 0.35f);
    r->drawRectPx(mx, my, MW, mh, { 0.13f, 0.145f, 0.17f }, 0.99f);
    ui.registerBlockingRect({ mx, my, MW, mh });
    bool inMenu = in.mouseX >= mx && in.mouseX < mx + MW && in.mouseY >= my && in.mouseY < my + mh;
    const char* mainItems[2] = { "Rename", "Delete" };
    const char* confItems[2] = { "Confirm deletion", "Cancel" };
    const char* const* items = mbMenuConfirmDelete ? confItems : mainItems;
    for (int i = 0; i < count; i++) {
        float iy = my + 4 + i * IH;
        bool hov = inMenu && in.mouseY >= iy && in.mouseY < iy + IH;
        bool danger = mbMenuConfirmDelete && i == 0;
        if (hov) r->drawRectPx(mx + 2, iy, MW - 4, IH, danger ? Vec3{ 0.5f, 0.18f, 0.18f } : Vec3{ 0.2f, 0.32f, 0.5f }, 1);
        r->drawTextLine(mx + 12, iy + 4, items[i], danger ? Vec3{ 1, 0.7f, 0.7f } : Vec3{ 0.87f, 0.9f, 0.95f }, 1);
        if (hov && in.mousePressed) {
            if (!mbMenuConfirmDelete) {
                if (i == 0) {
                    const char* nm = mbMenuKind == 1 ? graph.vars[mbMenuIdx].name
                                   : mbMenuKind == 2 ? graph.funcs[mbMenuIdx].name
                                   : graph.dispatchers[mbMenuIdx].name;
                    snprintf(mbRenameBuf, sizeof(mbRenameBuf), "%s", nm);
                    mbRenameKind = mbMenuKind; mbRenameIdx = mbMenuIdx; mbRenameFocus = true;
                } else {
                    mbMenuConfirmDelete = true;   // second confirmation dropdown
                }
            } else {
                if (i == 0) { applyDelete(); mbMenuKind = 0; }
                else mbMenuConfirmDelete = false;
            }
        }
    }
    if (in.mousePressed && !inMenu) mbMenuKind = 0;
}

// Details-panel subcategory picker: a free text field (type a new name — per keystroke,
// empty = no category) plus a dropdown of the categories already used by that member type.
// A category "exists" only while at least one member references it, so clearing the last
// user makes it disappear automatically.
static bool bpCategoryField(UI& ui, const char* idPrefix, char* category, int cap,
                            const std::vector<std::string>& existing) {
    bool changed = false;
    ui.label("Category (subcategory):", { 0.55f, 0.59f, 0.66f });
    char id[56];
    snprintf(id, sizeof(id), "%s_cat_txt", idPrefix);
    if (ui.textInput(id, category, cap)) { for (char* c = category; *c; c++) if (*c == ' ') *c = '_'; changed = true; }
    std::vector<const char*> opts; opts.push_back("(nessuna)");
    int sel = 0;
    for (int i = 0; i < (int)existing.size(); i++) { opts.push_back(existing[i].c_str()); if (existing[i] == category) sel = i + 1; }
    snprintf(id, sizeof(id), "%s_cat_sel", idPrefix);
    if (ui.combo(id, &sel, opts.data(), (int)opts.size())) {
        if (sel == 0) category[0] = 0;
        else snprintf(category, cap, "%s", opts[sel]);
        changed = true;
    }
    return changed;
}

// distinct categories currently used by members of a list (first-appearance order)
template <class Vec>
static std::vector<std::string> bpCategoriesInUse(const Vec& items) {
    std::vector<std::string> cats;
    for (const auto& it : items)
        if (it.category[0] && std::find(cats.begin(), cats.end(), it.category) == cats.end()) cats.push_back(it.category);
    return cats;
}

void BPEditor::drawVarDetails(UI& ui) {
    if (selVar < 0 || selVar >= (int)graph.vars.size()) return;
    BPVarDef& v = graph.vars[selVar];
    ui.label("VARIABLE DETAILS", { 0.30f, 0.62f, 0.99f });
    if (v.widgetGenerated) {
        // owned by the Designer: no editable name field at all
        ui.label(v.name, { .87f, .91f, .97f });
        ui.label("WIDGET COMPONENT", { .36f, .72f, .98f });
        ui.label("Designer component flagged \"Is Variable\".", { .62f, .68f, .76f });
        ui.label("Get only: rename it from the Designer, not from here.", { .62f, .68f, .76f });
        ui.label(std::string("Type: ") + (v.widgetType[0] ? v.widgetType : "Widget"), { .55f, .59f, .66f });
        ui.label("Its value is the element name for Set Widget Text/Value.", { .55f, .59f, .66f });
        return;
    }
    char vnameId[40]; snprintf(vnameId, sizeof(vnameId), "bpvdname%d", selVar);
    if (nameField(ui, vnameId, v.name, sizeof(v.name),
        [&](const char* nm) {
            std::string u = v.requiredGenerated ? bpUniqueRequiredName(graph, nm, v.requiredIndex)
                                                : bpUniqueMemberName(graph, nm, 0, selVar);
            return !bpSameName(u.c_str(), nm);
        },
        [&](const char* oldName) {
            if (v.requiredGenerated && v.requiredIndex >= 0 && v.requiredIndex < (int)graph.requiredComponents.size())
                snprintf(graph.requiredComponents[v.requiredIndex].variableName,
                         sizeof(graph.requiredComponents[v.requiredIndex].variableName), "%s", v.name);
            auto renameReferences = [&](BPCanvas& canvas) {
                for (BPNode& node : canvas.nodes)
                    if ((node.def == BP_VAR_GET || node.def == BP_VAR_SET) && bpSameName(node.sname, oldName))
                        snprintf(node.sname, sizeof(node.sname), "%s", v.name);
            };
            for (BPFunc& graphCanvas : graph.graphs) renameReferences(graphCanvas.body);
            for (BPFunc& function : graph.funcs) renameReferences(function.body);
        })) dirty = true;
    if (v.requiredGenerated) {
        ui.label("REQUIRED COMPONENT", { .94f, .69f, .30f });
        ui.label("Reference generated automatically by the Blueprint Settings.", { .62f, .68f, .76f });
        ui.label("Readable with Get; Set, default value and deletion are locked.", { .62f, .68f, .76f });
        ui.label(std::string("Type: Object (") + (v.refClass[0] ? v.refClass : "component") + ")", { .55f, .59f, .66f });
        return;
    }
    // Convert legacy controller variables even when this graph was assembled in
    // memory rather than read through deserialize().
    if (v.type == PIN_ANIMATOR_CONTROLLER) {
        v.type = PIN_ENT;
        snprintf(v.refClass, sizeof(v.refClass), "%s", "asset:AnimatorController");
        dirty = true;
    }
    // "Widget" is a reference kind of its own (PIN_WIDGET), not an Int carrying a
    // handle: it gets its own pin colour, refuses numeric connections and holds a
    // class the same way an Object variable does.
    static const char* TYPES[] = { "Float", "Int", "Bool", "Vector2", "Vector3", "String", "Object", "Transform", "Timer Handle", "Enum", "Animation Clip", "Color", "Widget" };
    static const PinKind TMAP[] = { PIN_NUM, PIN_INT, PIN_BOOL, PIN_VEC2, PIN_VEC, PIN_STR, PIN_ENT, PIN_TRANSFORM, PIN_TIMER_HANDLE, PIN_ENUM, PIN_ANIMATION_CLIP, PIN_COLOR, PIN_WIDGET };
    const int TYPE_COUNT = (int)(sizeof(TMAP) / sizeof(TMAP[0]));
    int ti = 0;
    for (int j = 0; j < TYPE_COUNT; j++) if (TMAP[j] == v.type) { ti = j; break; }
    if (ui.combo("Type", &ti, TYPES, TYPE_COUNT)) {
        v.type = TMAP[ti];
        if (v.type == PIN_WIDGET) { if (!bpMemberClassIsWidget(v.refClass)) snprintf(v.refClass, sizeof(v.refClass), "widget:"); }
        else if (v.type != PIN_ENT) v.refClass[0] = 0;
        if (v.type != PIN_ENUM) v.enumAsset[0] = 0;
        if (v.type != PIN_ANIMATION_CLIP && !bpIsAnimatorControllerObject(v)) v.assetPath[0] = 0;
        dirty = true;
    }
    if (bpIsWidgetObject(v)) {
        // class picker: the generic Widget, or one of the project's own .wgt
        std::vector<std::string> assets = bpFindProjectAssets(projectDir, ".wgt");
        std::vector<std::string> values = { "widget:" };
        std::vector<std::string> labels = { "Widget (any)" };
        for (const std::string& rel : assets) {
            values.push_back("widget:" + rel);
            labels.push_back("Widget: " + rel);
        }
        int ci = 0;
        for (int i = 0; i < (int)values.size(); i++) if (values[i] == v.refClass) ci = i;
        if (v.refClass[0] && values[ci] != v.refClass) {   // asset moved or deleted
            values.push_back(v.refClass);
            labels.push_back(std::string("Missing class: ") + (v.refClass + 7));
            ci = (int)values.size() - 1;
        }
        std::vector<const char*> opts;
        for (const std::string& l : labels) opts.push_back(l.c_str());
        if (ui.combo("Widget class", &ci, opts.data(), (int)opts.size())) {
            snprintf(v.refClass, sizeof(v.refClass), "%s", values[ci].c_str());
            dirty = true;
        }
        ui.label("Holds what Create Widget returns; feeds Add to Viewport.", { .55f, .59f, .66f });
        ui.label("A class narrows which .wgt the variable is meant to hold.", { .55f, .59f, .66f });
    }
    if (v.type == PIN_ENT) {
        std::string savedClass = v.refClass;
        if (savedClass.rfind("blueprint:", 0) == 0) {
            std::string resolvedClass;
            if (bpResolveBlueprintAssetPath(projectDir, savedClass, resolvedClass)) {
                std::string repaired = "blueprint:" + resolvedClass;
                if (_stricmp(repaired.c_str(), v.refClass) != 0) {
                    snprintf(v.refClass, sizeof(v.refClass), "%s", repaired.c_str());
                    dirty = true;
                }
            }
        }
        bool rescan = refClassValues.empty() || refClassScanProject != projectDir || frame_ - refClassScanFrame >= 120;
        if (rescan) {
            refClassValues = { "", "component:Camera", "component:Light", "component:Mesh", "component:Physics", "component:AudioSource", "component:ReverbZone", "component:AIAgent", "component:Animator", "asset:AnimatorController" };
            refClassLabels = { "GameObject (any)", "Camera", "Luce", "Mesh Renderer", "Rigid Body", "Audio Source", "Audio Reverb Zone", "AI Agent", "Animator (component)", "Animator Controller" };
            std::error_code ec;
            if (!projectDir.empty() && fs::exists(projectDir, ec)) {
                for (fs::recursive_directory_iterator it(projectDir, fs::directory_options::skip_permission_denied, ec), end;
                     it != end; it.increment(ec)) {
                    if (ec) { ec.clear(); continue; }
                    if (!it->is_regular_file(ec) || it->path().extension() != ".bp") continue;
                    std::string rel = fs::relative(it->path(), projectDir, ec).string();
                    if (ec) { ec.clear(); continue; }
                    refClassValues.push_back(std::string("blueprint:") + rel);
                    refClassLabels.push_back(std::string("Blueprint: ") + rel);
                }
            }
            refClassScanProject = projectDir;
            refClassScanFrame = frame_;
        }
        int ci = 0;
        for (int i = 0; i < (int)refClassValues.size(); i++) if (refClassValues[i] == v.refClass) ci = i;
        if (v.refClass[0] && refClassValues[ci] != v.refClass) {
            refClassValues.push_back(v.refClass);
            refClassLabels.push_back(std::string("Classe mancante: ") + v.refClass);
            ci = (int)refClassValues.size() - 1;
        }
        std::vector<const char*> opts;
        for (const auto& label : refClassLabels) opts.push_back(label.c_str());
        if (ui.combo("Classe oggetto", &ci, opts.data(), (int)opts.size())) {
            snprintf(v.refClass, sizeof(v.refClass), "%s", refClassValues[ci].c_str());
            if (!bpIsAnimatorControllerObject(v)) v.assetPath[0] = 0;
            dirty = true;
        }
        if (bpIsAnimatorControllerObject(v)) {
            std::vector<std::string> assets = bpFindProjectAssets(projectDir, ".animctrl");
            if (assets.empty()) {
                ui.label("Create an Animator Controller asset first.", { .95f, .58f, .28f });
            } else {
                int ai = 0;
                for (int i = 0; i < (int)assets.size(); i++) if (assets[i] == v.assetPath) ai = i;
                std::vector<const char*> labels;
                for (const std::string& asset : assets) labels.push_back(asset.c_str());
                if (ui.combo("Animator Controller", &ai, labels.data(), (int)labels.size())) {
                    snprintf(v.assetPath, sizeof(v.assetPath), "%s", assets[ai].c_str());
                    dirty = true;
                }
                if (!v.assetPath[0]) snprintf(v.assetPath, sizeof(v.assetPath), "%s", assets[ai].c_str());
            }
            ui.label("Object reference to the Animator Controller asset.", { 0.5f, 0.54f, 0.6f });
        } else {
            ui.label("The drop only accepts compatible objects.", { 0.5f, 0.54f, 0.6f });
        }
    } else if (v.type == PIN_ENUM) {
        std::vector<std::string> assets = bpFindProjectAssets(projectDir, ".enum");
        if (assets.empty()) {
            ui.label("Create an Enum asset in the Content Drawer first.", { 0.95f, 0.58f, 0.28f });
        } else {
            int ei = 0;
            for (int i = 0; i < (int)assets.size(); i++) if (assets[i] == v.enumAsset) ei = i;
            std::vector<const char*> labels;
            for (const std::string& asset : assets) labels.push_back(asset.c_str());
            if (ui.combo("Asset Enum", &ei, labels.data(), (int)labels.size())) {
                snprintf(v.enumAsset, sizeof(v.enumAsset), "%s", assets[ei].c_str());
                v.def.x = 0;
                dirty = true;
            }
            if (!v.enumAsset[0]) snprintf(v.enumAsset, sizeof(v.enumAsset), "%s", assets[ei].c_str());
        }
    } else if (v.type == PIN_ANIMATION_CLIP) {
        std::vector<std::string> assets = bpFindProjectAssets(projectDir, ".anim");
        if (assets.empty()) {
            ui.label("Create an Animation Clip asset first.", { .95f, .58f, .28f });
        } else {
            int ai = 0;
            for (int i = 0; i < (int)assets.size(); i++) if (assets[i] == v.assetPath) ai = i;
            std::vector<const char*> labels;
            for (const std::string& asset : assets) labels.push_back(asset.c_str());
            if (ui.combo("Asset", &ai, labels.data(), (int)labels.size())) {
                snprintf(v.assetPath, sizeof(v.assetPath), "%s", assets[ai].c_str());
                dirty = true;
            }
            if (!v.assetPath[0]) snprintf(v.assetPath, sizeof(v.assetPath), "%s", assets[ai].c_str());
        }
    }
    static const char* CONTS[] = { "Single", "Array", "Mappa" };
    int ci = (int)v.container;
    if (ui.combo("Container", &ci, CONTS, 3)) { v.container = (VarContainer)ci; dirty = true; }
    static const char* SCOPES[] = { "Public", "Protected", "Private" };
    int si = (int)v.scope;
    if (ui.combo("Accesso", &si, SCOPES, 3)) { v.scope = (VarScope)si; dirty = true; }
    bool ex = v.expose;
    if (ui.checkbox("Exposed in Details", &ex)) { v.expose = ex; dirty = true; }
    bool spawnEx = v.exposeOnSpawn;
    if (ui.checkbox("Exposed on Spawn", &spawnEx)) { v.exposeOnSpawn = spawnEx; dirty = true; }
    if (v.exposeOnSpawn && (v.scope != VS_PUBLIC || v.container != VC_SINGLE))
        ui.label("Requires a Public variable with a Single container.", { 0.95f, 0.58f, 0.28f });
    ui.spacing(4);
    {
        std::vector<std::string> cats;
        for (const BPVarDef& other : graph.vars)
            if (!other.requiredGenerated && other.category[0] &&
                std::find(cats.begin(), cats.end(), other.category) == cats.end()) cats.push_back(other.category);
        if (bpCategoryField(ui, "bpvar", v.category, sizeof(v.category), cats)) dirty = true;
    }
    ui.spacing(4);
    ui.label("Default value:", { 0.55f, 0.59f, 0.66f });
    if (v.container != VC_SINGLE) {
        ui.label("(empty at startup)", { 0.5f, 0.54f, 0.6f });
    } else {
        switch (v.type) {
        // a widget reference has no authorable default: it is empty until some
        // Create Widget fills it, exactly like an Object reference
        case PIN_WIDGET:
            ui.label("(none until Create Widget)", { 0.5f, 0.54f, 0.6f });
            break;
        case PIN_INT: {
            int iv = (int)v.def.x;
            if (ui.dragInt("#bpdefi", &iv, 0.1f, -1000000, 1000000)) { v.def.x = (float)iv; dirty = true; }
            break;
        }
        case PIN_ENUM: {
            BPEnumAsset en;
            if (bpLoadEnumAsset(projectDir, v.enumAsset, en)) {
                int value = (int)v.def.x;
                if (value < 0 || value >= (int)en.values.size()) value = 0;
                std::vector<const char*> names;
                for (const std::string& item : en.values) names.push_back(item.c_str());
                if (ui.combo("Enum value", &value, names.data(), (int)names.size())) { v.def.x = (float)value; dirty = true; }
            } else ui.label("Asset Enum non assegnato.", { 0.95f, 0.58f, 0.28f });
            break;
        }
        case PIN_BOOL: {
            bool b = v.def.x != 0;
            if (ui.checkbox(b ? "vero" : "falso", &b)) { v.def.x = b ? 1.0f : 0.0f; dirty = true; }
            break;
        }
        case PIN_VEC2:
            ui.row(2);
            dirty |= ui.dragFloat("#bpd2x", &v.def.x, 0.05f, -100000, 100000);
            dirty |= ui.dragFloat("#bpd2y", &v.def.y, 0.05f, -100000, 100000);
            break;
        case PIN_VEC:
            ui.row(3);
            dirty |= ui.dragFloat("#bpd3x", &v.def.x, 0.05f, -100000, 100000);
            dirty |= ui.dragFloat("#bpd3y", &v.def.y, 0.05f, -100000, 100000);
            dirty |= ui.dragFloat("#bpd3z", &v.def.z, 0.05f, -100000, 100000);
            break;
        case PIN_COLOR:
            dirty|=ui.colorEditRGBA("Colore",&v.def,&v.defAlpha);
            break;
        case PIN_STR:
            if (ui.textInput("bpdefs", v.strDef, sizeof(v.strDef))) dirty = true;
            break;
        case PIN_ANIMATION_CLIP:
            ui.label(v.assetPath[0] ? v.assetPath : "No asset", v.assetPath[0] ? Vec3{ .72f, .84f, 1.0f } : Vec3{ .55f, .59f, .66f });
            break;
        case PIN_ENT: {
            if (bpIsAnimatorControllerObject(v)) {
                ui.label(v.assetPath[0] ? v.assetPath : "No Animator Controller",
                         v.assetPath[0] ? Vec3{ .35f, .72f, .98f } : Vec3{ .55f, .59f, .66f });
                break;
            }
            int iv = (int)v.def.x;
            if (ui.dragInt("id oggetto##bpdefe", &iv, 0.1f, 0, 100000)) { v.def.x = (float)iv; dirty = true; }
            break;
        }
        case PIN_TRANSFORM:
            // dall'alto in basso: Location, Rotation, Scale (come richiesto)
            ui.label("Location", { 0.6f, 0.64f, 0.7f });
            ui.row(3);
            dirty |= ui.dragFloat("#bptfLx", &v.def.x, 0.05f, -100000, 100000);
            dirty |= ui.dragFloat("#bptfLy", &v.def.y, 0.05f, -100000, 100000);
            dirty |= ui.dragFloat("#bptfLz", &v.def.z, 0.05f, -100000, 100000);
            ui.label("Rotation", { 0.6f, 0.64f, 0.7f });
            ui.row(3);
            dirty |= ui.dragFloat("#bptfRx", &v.defRot.x, 0.1f, -100000, 100000);
            dirty |= ui.dragFloat("#bptfRy", &v.defRot.y, 0.1f, -100000, 100000);
            dirty |= ui.dragFloat("#bptfRz", &v.defRot.z, 0.1f, -100000, 100000);
            ui.label("Scale", { 0.6f, 0.64f, 0.7f });
            ui.row(3);
            dirty |= ui.dragFloat("#bptfSx", &v.defScl.x, 0.02f, -100000, 100000);
            dirty |= ui.dragFloat("#bptfSy", &v.defScl.y, 0.02f, -100000, 100000);
            dirty |= ui.dragFloat("#bptfSz", &v.defScl.z, 0.02f, -100000, 100000);
            break;
        case PIN_TIMER_HANDLE:
            v.def.x = 0;
            ui.label("None (assigned by Set Timer during Play)", { 0.5f, 0.54f, 0.6f });
            break;
        default:
            dirty |= ui.dragFloat("#bpdeff", &v.def.x, 0.05f, -100000, 100000);
            break;
        }
    }
    ui.spacing(6);
    if (ui.buttonColored("Delete variable", { 0.45f, 0.14f, 0.14f }, { 1, 0.85f, 0.85f })) {
        graph.vars.erase(graph.vars.begin() + selVar);
        selVar = -1;
        dirty = true;
    }
}

void BPEditor::drawDispatcherDetails(UI& ui){
    if(selDispatcher<0||selDispatcher>=(int)graph.dispatchers.size())return;
    BPDispatcherDef& dispatcher=graph.dispatchers[selDispatcher];ui.label("EVENT DISPATCHER",{.30f,.62f,.99f});
    char oldName[32];snprintf(oldName,sizeof(oldName),"%s",dispatcher.name);
    if(ui.textInput("dispatcher_name",dispatcher.name,sizeof(dispatcher.name))){for(char*c=dispatcher.name;*c;c++)if(*c==' ')*c='_';
        auto rename=[&](BPCanvas& canvas){for(BPNode& node:canvas.nodes)if((node.def==BP_BIND_EVENT||node.def==BP_CALL_DISPATCH)&&strcmp(node.sname,oldName)==0)snprintf(node.sname,sizeof(node.sname),"%s",dispatcher.name);};
        for(BPFunc& gph:graph.graphs)rename(gph.body);for(BPFunc& function:graph.funcs)rename(function.body);dirty=true;}
    if (bpCategoryField(ui, "bpdisp", dispatcher.category, sizeof(dispatcher.category), bpCategoriesInUse(graph.dispatchers))) dirty = true;
    ui.label("INPUTS PASSED TO CUSTOM EVENTS",{.55f,.68f,.84f});
    static const char* TYPES[]={"Float","Int","Bool","Vector2","Vector3","String","Object","Transform","Timer Handle","Color"};
    static const PinKind TMAP[]={PIN_NUM,PIN_INT,PIN_BOOL,PIN_VEC2,PIN_VEC,PIN_STR,PIN_ENT,PIN_TRANSFORM,PIN_TIMER_HANDLE,PIN_COLOR};
    int remove=-1;
    for(int i=0;i<(int)dispatcher.params.size();i++){
        BPFuncPin& pin=dispatcher.params[i];char id[48];snprintf(id,sizeof(id),"dispatcher_pin_name_%d",i);
        if(ui.textInput(id,pin.name,sizeof(pin.name))){for(char*c=pin.name;*c;c++)if(*c==' ')*c='_';dirty=true;}
        ui.row(2);int type=0;for(int t=0;t<10;t++)if(TMAP[t]==pin.kind)type=t;snprintf(id,sizeof(id),"dispatcher_pin_type_%d",i);
        if(ui.combo(id,&type,TYPES,10)){pin.kind=TMAP[type];dirty=true;}snprintf(id,sizeof(id),"Remove##dispatcher_pin_%d",i);
        if(ui.buttonColored(id,{.4f,.14f,.14f},{1,.85f,.85f}))remove=i;
    }
    if(remove>=0){
        dispatcher.params.erase(dispatcher.params.begin()+remove);int removedPin=remove+1;
        auto repair=[&](BPCanvas& canvas){for(size_t i=0;i<canvas.links.size();){BPLink& link=canvas.links[i];BPNode* target=canvas.byId(link.toNode);if(target&&target->def==BP_CALL_DISPATCH&&strcmp(target->sname,dispatcher.name)==0){if(link.toPin==removedPin){canvas.links.erase(canvas.links.begin()+i);continue;}if(link.toPin>removedPin)link.toPin--;}i++;}};
        for(BPFunc& gph:graph.graphs)repair(gph.body);for(BPFunc& function:graph.funcs)repair(function.body);dirty=true;
    }
    if((int)dispatcher.params.size()<BP_MAX_FUNC_PINS&&ui.button("+ Add input")){BPFuncPin pin;snprintf(pin.name,sizeof(pin.name),"Input%d",(int)dispatcher.params.size()+1);dispatcher.params.push_back(pin);dirty=true;}
    if(ui.button("Create Call Dispatcher in the graph")){BPCanvas& canvas= this->canvas();int id=canvas.addNode(BP_CALL_DISPATCH,snapGrid(-panX+120),snapGrid(-panY+120));snprintf(canvas.byId(id)->sname,sizeof(canvas.byId(id)->sname),"%s",dispatcher.name);selNode=id;selDispatcher=-1;selSet.clear();selSet.insert(id);dirty=true;}
    ui.label("The Custom Event bound here must have the same pins.",{.55f,.59f,.66f});
}

void BPEditor::drawCommentDetails(UI& ui) {
    BPCanvas& C = canvas();
    if (selComment < 0 || selComment >= (int)C.comments.size()) return;
    BPComment& c = C.comments[selComment];
    ui.label("COMMENT DETAILS", { 0.30f, 0.62f, 0.99f });
    if (ui.textInput("cmttext", c.text, sizeof(c.text))) dirty = true;
    ui.spacing(4);
    ui.label("Text size:", { 0.55f, 0.59f, 0.66f });
    dirty |= ui.dragFloat("#cmtfs", &c.fontSize, 0.01f, 0.6f, 5.0f);
    ui.spacing(4);
    ui.label("Colore (R G B):", { 0.55f, 0.59f, 0.66f });
    ui.row(3);
    dirty |= ui.dragFloat("#cmtr", &c.color.x, 0.004f, 0, 1);
    dirty |= ui.dragFloat("#cmtg", &c.color.y, 0.004f, 0, 1);
    dirty |= ui.dragFloat("#cmtb", &c.color.z, 0.004f, 0, 1);
    ui.spacing(6);
    if (ui.buttonColored("Delete comment", { 0.45f, 0.14f, 0.14f }, { 1, 0.85f, 0.85f })) {
        C.comments.erase(C.comments.begin() + selComment);
        selComment = -1;
        dirty = true;
    }
}

// the valid names a name-referencing node can pick from (variables of the matching
// container, functions, or custom events). Returns false for free-typed names.
static bool bpNameOptions(BPGraph& g, int def, std::vector<const char*>& out) {
    out.clear();
    switch (def) {
    case BP_VAR_GET:
        for (auto& v : g.vars) if (v.container == VC_SINGLE) out.push_back(v.name);
        return true;
    case BP_VAR_SET:
        for (auto& v : g.vars)
            if (v.container == VC_SINGLE && !v.requiredGenerated && !v.widgetGenerated) out.push_back(v.name);
        return true;
    case BP_ARR_GET: case BP_ARR_ADD: case BP_ARR_LEN: case BP_ARR_REMOVE: case BP_ARR_CLEAR:
    case BP_FLOW_FOREACH:
        for (auto& v : g.vars) if (v.container == VC_ARRAY) out.push_back(v.name);
        return true;
    case BP_MAP_GET: case BP_MAP_SET: case BP_MAP_REMOVE: case BP_MAP_LEN:
        for (auto& v : g.vars) if (v.container == VC_MAP) out.push_back(v.name);
        return true;
    case BP_CALL_FUNC:
        for (auto& f : g.funcs) out.push_back(f.name);
        return true;
    case BP_CALL_EVENT: case BP_SEND_MSG: case BP_CREATE_EVENT:
        for (auto& gph : g.graphs)
            for (auto& ev : gph.body.nodes)
                if (ev.def == BP_EV_CUSTOM) out.push_back(ev.sname);
        return true;
    case BP_BIND_EVENT: case BP_CALL_DISPATCH:
        for (auto& s : g.dispatchers) out.push_back(s.name);
        return true;
    default:
        return false;   // find / ev_custom / locals: free text
    }
}

void BPEditor::renameFunctionReferences(const char* oldName, const char* newName) {
    if (!oldName || !newName || !oldName[0] || strcmp(oldName, newName) == 0) return;
    auto update = [&](BPCanvas& c) {
        for (BPNode& n : c.nodes) {
            if ((n.def == BP_CALL_FUNC || n.def == BP_TIMER_SET_FUNC) && strcmp(n.sname, oldName) == 0)
                snprintf(n.sname, sizeof(n.sname), "%s", newName);
        }
    };
    for (BPFunc& gph : graph.graphs) update(gph.body);
    for (BPFunc& fn : graph.funcs) update(fn.body);
}

void BPEditor::setFunctionPure(bool makePure) {
    if (curFunc < 0 || curFunc >= (int)graph.funcs.size()) return;
    BPFunc& fn = graph.funcs[curFunc];
    if (fn.pure == makePure) return;
    auto shiftNode = [&](BPCanvas& c, int nodeId, bool output) {
        for (size_t i = 0; i < c.links.size();) {
            BPLink& l = c.links[i];
            bool match = output ? l.fromNode == nodeId : l.toNode == nodeId;
            int& pin = output ? l.fromPin : l.toPin;
            if (!match) { i++; continue; }
            if (makePure && pin == 0) { c.links.erase(c.links.begin() + i); continue; }
            pin += makePure ? -1 : 1;
            i++;
        }
    };
    for (BPNode& n : fn.body.nodes) {
        if (n.def == BP_FN_ENTRY) shiftNode(fn.body, n.id, true);
        else if (n.def == BP_FN_RETURN) shiftNode(fn.body, n.id, false);
    }
    auto shiftCalls = [&](BPCanvas& c) {
        for (BPNode& n : c.nodes) {
            if (n.def != BP_CALL_FUNC || strcmp(n.sname, fn.name) != 0) continue;
            shiftNode(c, n.id, false);
            shiftNode(c, n.id, true);
        }
    };
    for (BPFunc& gph : graph.graphs) shiftCalls(gph.body);
    for (BPFunc& other : graph.funcs) shiftCalls(other.body);
    fn.pure = makePure;
    dirty = true;
}

// remove a function input/output pin and repair the wires on every Entry / Return
// / Call node that referenced it (drop the pin's links, shift the ones after it)
void BPEditor::removeFuncPin(bool input, int idx) {
    if (curFunc < 0 || curFunc >= (int)graph.funcs.size()) return;
    BPFunc& fn = graph.funcs[curFunc];
    std::vector<BPFuncPin>& pins = input ? fn.ins : fn.outs;
    if (idx < 0 || idx >= (int)pins.size()) return;
    pins.erase(pins.begin() + idx);
    int dataPin = idx + (fn.pure ? 0 : 1);
    auto fixNode = [&](BPCanvas& C, int nodeId, bool out) {
        for (size_t i = 0; i < C.links.size();) {
            BPLink& l = C.links[i];
            int& p = out ? l.fromPin : l.toPin;
            bool match = out ? (l.fromNode == nodeId) : (l.toNode == nodeId);
            if (match && p == dataPin) { C.links.erase(C.links.begin() + i); continue; }
            if (match && p > dataPin) p -= 1;
            i++;
        }
    };
    char fname[32];
    snprintf(fname, sizeof(fname), "%s", fn.name);
    auto fixCalls = [&](BPCanvas& C, bool out) {
        for (auto& n : C.nodes)
            if (n.def == BP_CALL_FUNC && strcmp(n.sname, fname) == 0) fixNode(C, n.id, out);
    };
    if (input) {
        for (auto& n : fn.body.nodes) if (n.def == BP_FN_ENTRY) fixNode(fn.body, n.id, true);
        for (auto& g : graph.graphs) fixCalls(g.body, false);
        for (auto& f : graph.funcs) fixCalls(f.body, false);
    } else {
        for (auto& n : fn.body.nodes) if (n.def == BP_FN_RETURN) fixNode(fn.body, n.id, false);
        for (auto& g : graph.graphs) fixCalls(g.body, true);
        for (auto& f : graph.funcs) fixCalls(f.body, true);
    }
    dirty = true;
}

static std::string bpInterfaceOriginForFunction(const std::string& projectDir, BPGraph& graph, const char* functionName) {
    if (!functionName || !functionName[0]) return {};
    for (const std::string& asset : graph.interfaceAssets) {
        std::string data;
        BPGraph interfaceGraph;
        if (!bpReadTextFile(projectDir + "\\" + asset, data) || !interfaceGraph.deserialize(data)) continue;
        BPFunc* signature = interfaceGraph.findFunc(functionName);
        if (signature && !signature->outs.empty()) return asset;
    }
    return {};
}

// signature editor for the current function (Entry / Return nodes): edit the input
// and output pins (name + type), add / remove them
void BPEditor::drawFuncSignature(UI& ui) {
    if (curFunc < 0 || curFunc >= (int)graph.funcs.size()) return;
    BPFunc& fn = graph.funcs[curFunc];
    const bool interfaceDeclaration = isInterfaceAsset();
    const std::string implementedOrigin = interfaceDeclaration ? std::string{} : bpInterfaceOriginForFunction(projectDir, graph, fn.name);
    if (!implementedOrigin.empty()) {
        ui.label("INTERFACE FUNCTION", { .36f, .72f, .98f });
        ui.label(std::string("Origin: ") + fs::path(implementedOrigin).stem().string(), { .68f, .82f, 1.0f });
        for (const BPFuncPin& pin : fn.ins)
            ui.label(std::string("Input  ") + pin.name + " : " + BP_VARTYPE_NAMES[(int)pin.kind], { .58f, .67f, .78f });
        for (const BPFuncPin& pin : fn.outs)
            ui.label(std::string("Output ") + pin.name + " : " + BP_VARTYPE_NAMES[(int)pin.kind], { .58f, .67f, .78f });
        ui.label("Name and signature come from the .bpi asset.", { .55f, .59f, .66f });
        return;
    }
    ui.label(interfaceDeclaration ? "INTERFACE SIGNATURE" : "FUNCTION DETAILS", { 0.42f, 0.30f, 0.72f });
    char fnNameId[40]; snprintf(fnNameId, sizeof(fnNameId), "bpfnsigname%d", curFunc);
    if (nameField(ui, fnNameId, fn.name, sizeof(fn.name),
        [&](const char* nm) { std::string u = bpUniqueMemberName(graph, nm, 1, curFunc); return !bpSameName(u.c_str(), nm); },
        [&](const char* oldName) { renameFunctionReferences(oldName, fn.name); })) dirty = true;
    if (interfaceDeclaration) {
        ui.label(fn.outs.empty() ? "Implementazione: Interface Event" : "Implementazione: Interface Function",
                 fn.outs.empty() ? Vec3{ .82f, .38f, .32f } : Vec3{ .52f, .72f, 1.0f });
        ui.label("The canvas is generated from the signature and cannot be edited.", { 0.5f, 0.54f, 0.6f });
    } else {
        int scope = (int)fn.scope;
        if (ui.combo("Accesso", &scope, BP_SCOPE_NAMES, 3)) { fn.scope = (VarScope)scope; dirty = true; }
        bool pure = fn.pure;
        if (ui.checkbox("Pure function (no exec pins)", &pure)) setFunctionPure(pure);
        ui.label(fn.pure ? "Evaluated when an output is requested." : "Impure function: uses the execution flow.",
                 { 0.5f, 0.54f, 0.6f });
        if (bpCategoryField(ui, "bpfn", fn.category, sizeof(fn.category), bpCategoriesInUse(graph.funcs))) dirty = true;
    }
    static const char* TYPES[] = { "Float", "Int", "Bool", "Vector2", "Vector3", "String", "Object", "Transform", "Timer Handle", "Color" };
    static const PinKind TMAP[] = { PIN_NUM, PIN_INT, PIN_BOOL, PIN_VEC2, PIN_VEC, PIN_STR, PIN_ENT, PIN_TRANSFORM, PIN_TIMER_HANDLE, PIN_COLOR };
    auto pinSection = [&](const char* title, std::vector<BPFuncPin>& pins, bool input) {
        ui.spacing(4);
        ui.label(title, { 0.55f, 0.59f, 0.66f });
        int removeAt = -1;
        for (int i = 0; i < (int)pins.size(); i++) {
            char id[40];
            snprintf(id, sizeof(id), "%s_nm%d", input ? "in" : "out", i);
            if (ui.textInput(id, pins[i].name, sizeof(pins[i].name))) {
                for (char* c = pins[i].name; *c; c++) if (*c == ' ') *c = '_';
                dirty = true;
            }
            ui.row(2);
            int ti = 0;
            for (int j = 0; j < 10; j++) if (TMAP[j] == pins[i].kind) ti = j;
            snprintf(id, sizeof(id), "#%s_ty%d", input ? "in" : "out", i);
            if (ui.combo(id, &ti, TYPES, 10)) { pins[i].kind = TMAP[ti]; dirty = true; }
            snprintf(id, sizeof(id), "Remove##%s%d", input ? "in" : "out", i);
            if (ui.buttonColored(id, { 0.4f, 0.14f, 0.14f }, { 1, 0.85f, 0.85f })) removeAt = i;
        }
        if (removeAt >= 0) {
            removeFuncPin(input, removeAt);
            if (interfaceDeclaration) bpNormalizeInterfaceFunction(fn);
        }
        if ((int)pins.size() < BP_MAX_FUNC_PINS) {
            if (ui.button(input ? "+ Add input" : "+ Add output")) {
                BPFuncPin p;
                snprintf(p.name, sizeof(p.name), input ? "in%d" : "out%d", (int)pins.size() + 1);
                p.kind = PIN_NUM;
                pins.push_back(p);
                if (interfaceDeclaration) {
                    bpNormalizeInterfaceFunction(fn);
                } else if (!input) {
                    bool hasRet = false;
                    float ex = 360, ey = 40;
                    for (auto& nn : fn.body.nodes) {
                        if (nn.def == BP_FN_RETURN) hasRet = true;
                        if (nn.def == BP_FN_ENTRY) { ex = nn.x + 320; ey = nn.y; }
                    }
                    if (!hasRet) fn.body.addNode(BP_FN_RETURN, ex, ey);
                }
                dirty = true;
            }
        }
    };
    pinSection("INPUT (Entry pins)", fn.ins, true);
    pinSection("OUTPUT (Return pins)", fn.outs, false);
    ui.spacing(5);
    if (interfaceDeclaration) {
        ui.label("Adding the first output creates Return automatically.", { 0.5f, 0.54f, 0.6f });
        ui.label("Removing the last output turns the signature into an Event.", { 0.5f, 0.54f, 0.6f });
    } else {
        ui.label("The pins appear on Entry, Return", { 0.5f, 0.54f, 0.6f });
        ui.label("and on Call Function nodes.", { 0.5f, 0.54f, 0.6f });
    }
}

void BPEditor::drawNodeInputValues(UI& ui, BPNode& n, const BPNodeDef& d) {
    BPCanvas& C = canvas();
    bool headerDrawn = false;
    auto ensureHeader = [&]() {
        if (!headerDrawn) { ui.spacing(7); ui.label("INPUT VALUES", { 0.55f, 0.68f, 0.84f }); headerDrawn = true; }
    };
    // object variables (single) that can be bound to an Object input pin
    std::vector<int> objVars;
    for (int i = 0; i < (int)graph.vars.size(); i++)
        if (graph.vars[i].type == PIN_ENT && graph.vars[i].container == VC_SINGLE) objVars.push_back(i);
    for (int p = 0; p < d.nIns; p++) {
        PinKind kind = editorKind(n, d, p);
        char visible[48];
        snprintf(visible, sizeof(visible), "%s", d.ins[p].name[0] ? d.ins[p].name : "Value");
        char id[96];
        snprintf(id, sizeof(id), "%s##node%d_pin%d", visible, n.id, p);
        // ── Object / reference / class pin: bind a variable of that type (Unreal-style
        //    dropdown). Selecting one drops a Get node and wires it into this pin. ──
        if (kind == PIN_ENT) {
            if (!objVars.empty()) {
                ensureHeader();
                if (C.linkInto(n.id, p)) { ui.disabledField(id, "Collegato"); continue; }
                ui.label(visible, { 0.65f, 0.70f, 0.78f });
                std::vector<std::string> labelStore; labelStore.push_back("(connect a variable)");
                for (int vi : objVars) labelStore.push_back(std::string(graph.vars[vi].name));
                std::vector<const char*> opts; for (auto& s : labelStore) opts.push_back(s.c_str());
                int sel = 0;
                char comboId[64]; snprintf(comboId, sizeof(comboId), "#objbind_%d_%d", n.id, p);
                if (ui.combo(comboId, &sel, opts.data(), (int)opts.size()) && sel > 0) {
                    int vi = objVars[sel - 1];
                    int gid = C.addNode(BP_VAR_GET, snapGrid(n.x - 190), snapGrid(n.y + p * 24));
                    if (BPNode* gn = C.byId(gid)) {
                        snprintf(gn->sname, sizeof(gn->sname), "%s", graph.vars[vi].name);
                        C.connect(gid, 0, n.id, p);
                        dirty = true;
                    }
                }
            }
            continue;
        }
        if (!litEditable(kind)) continue;
        ensureHeader();
        if (C.linkInto(n.id, p)) {
            ui.disabledField(id, "Collegato");
            continue;
        }
        bool changed = false;
        if (kind == PIN_BOOL) {
            bool value = n.lit[p].x != 0.0f;
            if (ui.checkbox(id, &value)) { n.lit[p].x = value ? 1.0f : 0.0f; changed = true; }
        } else if (kind == PIN_COLOR) {
            changed|=ui.colorEditRGBA(visible,&n.lit[p],&n.litAlpha[p]);
        } else if (kind == PIN_STR) {
            ui.label(visible, { 0.65f, 0.70f, 0.78f });
            char text[512];
            snprintf(text, sizeof(text), "%s", n.slit[p].c_str());
            char textId[64]; snprintf(textId, sizeof(textId), "node_string_%d_%d", n.id, p);
            if (ui.textInput(textId, text, sizeof(text))) { n.slit[p] = text; changed = true; }
        } else if (kind == PIN_VEC || kind == PIN_VEC2) {
            int comps = kind == PIN_VEC ? 3 : 2;
            ui.label(visible, { 0.65f, 0.70f, 0.78f });
            ui.row(comps);
            static const char* AXIS[] = { "X", "Y", "Z" };
            for (int c = 0; c < comps; c++) {
                char cid[64]; snprintf(cid, sizeof(cid), "%s##node%d_pin%d_c%d", AXIS[c], n.id, p, c);
                changed |= ui.dragFloat(cid, &(&n.lit[p].x)[c], 0.02f, -1000000000.0f, 1000000000.0f);
            }
        } else if (kind == PIN_ENUM && (n.def == BP_SET_WIDGET_HALIGN || n.def == BP_SET_WIDGET_VALIGN ||
                                        n.def == BP_SET_WIDGET_ANCHOR)) {
            // built-in widget enums: the same names the designer's drop-downs use
            std::vector<const char*> values;
            if (n.def == BP_SET_WIDGET_ANCHOR) { for (int i = 0; i < WANCH_COUNT; i++) values.push_back(widgetAnchorName(i)); }
            else for (int i = 0; i < 4; i++)
                values.push_back(n.def == BP_SET_WIDGET_HALIGN ? widgetHAlignName(i) : widgetVAlignName(i));
            int value = (int)n.lit[p].x;
            if (value < 0 || value >= (int)values.size()) value = 0;
            if (ui.combo(id, &value, values.data(), (int)values.size())) { n.lit[p].x = (float)value; changed = true; }
        } else if (kind == PIN_ENUM && (n.def == BP_SELECT_ENUM || n.def == BP_SWITCH_ENUM || n.def == BP_SPAWN_PREFAB)) {
            BPEnumAsset en;
            const char* enumPath = n.sname;
            std::vector<BPSpawnPinInfo> spawnPins;
            if (n.def == BP_SPAWN_PREFAB) {
                spawnPins = bpSpawnPinInfo(projectDir, n.sname);
                int spawnPin = p - 2;
                enumPath = spawnPin >= 0 && spawnPin < (int)spawnPins.size() ? spawnPins[spawnPin].enumAsset.c_str() : "";
            }
            if (bpLoadEnumAsset(projectDir, enumPath, en)) {
                int value = (int)n.lit[p].x;
                if (value < 0 || value >= (int)en.values.size()) value = 0;
                std::vector<const char*> values;
                for (const std::string& item : en.values) values.push_back(item.c_str());
                if (ui.combo(id, &value, values.data(), (int)values.size())) { n.lit[p].x = (float)value; changed = true; }
            }
        } else if (kind == PIN_INT || kind == PIN_ENUM) {
            int value = (int)n.lit[p].x;
            if (ui.dragInt(id, &value, 0.2f, -1000000000, 1000000000)) { n.lit[p].x = (float)value; changed = true; }
        } else { // Float and an untyped wildcard literal both store a scalar.
            changed |= ui.dragFloat(id, &n.lit[p].x, 0.02f, -1000000000.0f, 1000000000.0f);
        }
        if (changed) dirty = true;
    }
}

static std::vector<std::string> animationTriggersForController(const std::string& projectDir,const std::string& controllerPath) {
    std::vector<std::string> result;std::string data;AnimatorControllerAsset controller;
    if(controllerPath.empty()||!bpReadTextFile(projectDir+"\\"+controllerPath,data)||!controller.deserialize(data))return result;
    for(const AnimatorState& state:controller.states){
        if(state.clip.empty())continue;AnimationClipAsset clip;
        if(!bpReadTextFile(projectDir+"\\"+state.clip,data)||!clip.deserialize(data))continue;
        for(const AnimationEventKey& event:clip.events)
            if(!event.name.empty()&&std::find(result.begin(),result.end(),event.name)==result.end())result.push_back(event.name);
    }
    std::sort(result.begin(),result.end());return result;
}

// details of the selected node: key binding (listener or combo), choices, name
void BPEditor::drawNodeDetails(UI& ui) {
    BPCanvas& C = canvas();
    BPNode* n = C.byId(selNode);
    if (!n) return;

    if(n->def==BP_BIND_EVENT){
        ui.label("BIND EVENT DISPATCHER",{.30f,.62f,.99f});
        // The dispatcher can belong to this Blueprint or to another one — the
        // runtime matches by name on whatever the Target pins point at — so the
        // picker offers every dispatcher declared anywhere in the project and
        // says where each one comes from.
        // The class wired into Target decides which dispatchers exist — listing
        // every dispatcher in the project let you pick one the target does not
        // own, which then failed at runtime. Unwired Target = this Blueprint.
        BPGraph targetGraph;
        bool haveTargetClass = false;
        std::string targetClass = bpPinRefClass(C, graph, n->id, 2, false);
        if (targetClass.empty()) targetClass = bpPinRefClass(C, graph, n->id, 3, false);
        if (bpMemberClassIsWidget(targetClass))
            haveTargetClass = bpLoadWidgetGraph(projectDir, targetClass, targetGraph);
        else if (targetClass.rfind("blueprint:", 0) == 0) {
            std::string path = targetClass.substr(10), resolved;
            if (bpResolveBlueprintAssetPath(projectDir, path, resolved)) path = resolved;
            haveTargetClass = bpLoadResolvedGraph(projectDir, path, targetGraph);
        }
        const BPGraph& owner = haveTargetClass ? targetGraph : graph;
        std::vector<std::string> names;
        for (const BPDispatcherDef& d : owner.dispatchers) names.push_back(d.name);
        int di = 0;
        for (int i = 0; i < (int)names.size(); i++) if (bpSameName(names[i].c_str(), n->sname)) di = i;
        bool known = !names.empty() && bpSameName(names[di].c_str(), n->sname);
        if (n->sname[0] && !known) { names.push_back(n->sname); di = (int)names.size() - 1; }
        if (names.empty()) {
            ui.label(haveTargetClass ? "The target class declares no Dispatcher."
                                     : "This Blueprint declares no Dispatcher.", { .95f, .58f, .28f });
        } else {
            std::vector<const char*> opts;
            for (const std::string& nm : names) opts.push_back(nm.c_str());
            if (ui.combo("Dispatcher", &di, opts.data(), (int)opts.size())) {
                snprintf(n->sname, sizeof(n->sname), "%s", names[di].c_str());
                dirty = true;
            }
        }
        const BPDispatcherDef* dispatcher = const_cast<BPGraph&>(owner).findDispatcher(n->sname);
        if(dispatcher){
            for(const BPFuncPin& pin:dispatcher->params)
                ui.label(std::string("  ")+pin.name+" : "+BP_VARTYPE_NAMES[(int)pin.kind],{.58f,.67f,.78f});
            if(ui.button("Create matching Custom Event")){   // short: the column is narrow
                std::string eventName=bpUniqueMemberName(graph,std::string(dispatcher->name)+"_Event",2);
                BPEventDef event;snprintf(event.name,sizeof(event.name),"%s",eventName.c_str());event.params=dispatcher->params;graph.events.push_back(event);
                int eventNode=C.addNode(BP_EV_CUSTOM,n->x-280,n->y);snprintf(C.byId(eventNode)->sname,sizeof(C.byId(eventNode)->sname),"%s",event.name);
                C.connect(eventNode,(int)event.params.size()+1,n->id,1);
                selSet.clear();selSet.insert(eventNode);selNode=eventNode;dirty=true;
            }
            ui.label("The delegate only accepts Custom Events with this signature.",{.55f,.59f,.66f});
        } else if (n->sname[0]) {
            ui.label("The connected target does not declare this Dispatcher.",{.95f,.58f,.28f});
        }
        if (haveTargetClass) ui.label("Dispatchers of the connected target.", { .55f, .59f, .66f });
        else ui.label("Target unwired: this Blueprint's own Dispatchers.", { .55f, .59f, .66f });
        BPNodeDef bindDef = effDef(*n);
        drawNodeInputValues(ui, *n, bindDef);
        return;
    }

    if (n->def == BP_CAST_TO_CLASS) {
        ui.label("CAST TO CLASS", { .30f, .62f, .99f });
        std::vector<std::string> values, labels;
        values.reserve(BP_NCOMPS + 16);
        labels.reserve(BP_NCOMPS + 16);
        for (int i = 0; i < BP_NCOMPS; i++) {
            values.push_back("component:" + std::to_string(i));
            labels.push_back(std::string("Componente: ") + BP_COMP_NAMES[i]);
        }
        for (const std::string& asset : bpFindProjectAssets(projectDir, ".bp")) {
            values.push_back("blueprint:" + asset);
            labels.push_back("Blueprint: " + asset);
        }
        int selected = 0;
        for (int i = 0; i < (int)values.size(); i++)
            if (bpSameAssetPath(values[i], n->sname)) selected = i;
        bool knownClass = !values.empty() && bpSameAssetPath(values[selected], n->sname);
        if (n->sname[0] && !knownClass) {
            values.push_back(n->sname);
            labels.push_back(std::string("Classe mancante: ") + n->sname);
            selected = (int)values.size() - 1;
        }
        std::vector<const char*> options;
        for (const std::string& label : labels) options.push_back(label.c_str());
        if (!options.empty() && ui.combo("Target class", &selected, options.data(), (int)options.size())) {
            snprintf(n->sname, sizeof(n->sname), "%s", values[selected].c_str());
            dirty = true;
        }
        ui.label("Success only follows if the Object belongs to the chosen class.", { .55f, .59f, .66f });
        ui.label("Child Blueprints are compatible with the parent class.", { .55f, .59f, .66f });
        ui.label("An unconnected Object uses Self; on Failed, As Object is null.", { .55f, .59f, .66f });
        drawNodeInputValues(ui, *n, effDef(*n));
        return;
    }

    if(n->def==BP_INTERFACE_MESSAGE){
        ui.label("INTERFACE MESSAGE",{.30f,.62f,.99f});
        std::vector<std::string> assets=bpFindProjectAssets(projectDir,".bpi");
        if(assets.empty())ui.label("No Blueprint Interface in the project.",{.95f,.58f,.28f});
        else{
            int selectedAsset=0;for(int i=0;i<(int)assets.size();i++)if(bpSameAssetPath(assets[i],n->slit[0]))selectedAsset=i;
            std::vector<std::string> labels;std::vector<const char*> options;
            for(const std::string& asset:assets){fs::path path(asset);labels.push_back(path.stem().string()+"  ("+asset+")");}
            for(const std::string& label:labels)options.push_back(label.c_str());
            if(ui.combo("Interface",&selectedAsset,options.data(),(int)options.size())){n->slit[0]=assets[selectedAsset];n->sname[0]=0;dirty=true;}
            if(n->slit[0].empty())n->slit[0]=assets[selectedAsset];
            std::string data;BPGraph interfaceGraph;
            if(bpReadTextFile(projectDir+"\\"+n->slit[0],data)&&interfaceGraph.deserialize(data)&&!interfaceGraph.funcs.empty()){
                int selectedFunction=0;for(int i=0;i<(int)interfaceGraph.funcs.size();i++)if(strcmp(interfaceGraph.funcs[i].name,n->sname)==0)selectedFunction=i;
                std::vector<const char*> functions;for(const BPFunc& function:interfaceGraph.funcs)functions.push_back(function.name);
                if(ui.combo("Function (Message)",&selectedFunction,functions.data(),(int)functions.size())){snprintf(n->sname,sizeof(n->sname),"%s",interfaceGraph.funcs[selectedFunction].name);dirty=true;}
                if(!n->sname[0])snprintf(n->sname,sizeof(n->sname),"%s",interfaceGraph.funcs[selectedFunction].name);
            }else ui.label("The interface has no functions.",{.72f,.58f,.38f});
        }
        ui.label("Calls every component of the Target that implements the interface.",{.55f,.59f,.66f});
        ui.label("If no component matches, the Message does nothing.",{.55f,.59f,.66f});
        BPNodeDef inputDef=effDef(*n);drawNodeInputValues(ui,*n,inputDef);return;
    }

    if(n->def==BP_DOES_IMPLEMENT_INTERFACE){
        ui.label("DOES IMPLEMENT INTERFACE",{.30f,.62f,.99f});
        std::vector<std::string> assets=bpFindProjectAssets(projectDir,".bpi");
        if(assets.empty())ui.label("No Blueprint Interface in the project.",{.95f,.58f,.28f});
        else{
            int selected=0;for(int i=0;i<(int)assets.size();i++)if(bpSameAssetPath(assets[i],n->sname))selected=i;
            std::vector<std::string> labels;std::vector<const char*> options;
            for(const std::string& asset:assets){fs::path path(asset);labels.push_back(path.stem().string()+"  ("+asset+")");}
            for(const std::string& label:labels)options.push_back(label.c_str());
            if(ui.combo("Interface",&selected,options.data(),(int)options.size())){snprintf(n->sname,sizeof(n->sname),"%s",assets[selected].c_str());dirty=true;}
            if(!n->sname[0])snprintf(n->sname,sizeof(n->sname),"%s",assets[selected].c_str());
        }
        ui.label("Controls the given Object; unconnected uses Self.",{.55f,.59f,.66f});
        ui.label("Includes interfaces inherited from parent Blueprints.",{.55f,.59f,.66f});
        BPNodeDef inputDef=effDef(*n);drawNodeInputValues(ui,*n,inputDef);return;
    }

    if(n->def==BP_ANIM_BIND_TRIGGER){
        ui.label("ANIMATION EVENT TRIGGER",{.30f,.62f,.99f});
        std::vector<std::string> assets=bpFindProjectAssets(projectDir,".animctrl");
        if(assets.empty())ui.label("No Animator Controller in the project.",{.95f,.58f,.28f});
        else{
            int controller=0;for(int i=0;i<(int)assets.size();i++)if(assets[i]==n->slit[0])controller=i;
            std::vector<const char*> labels;for(const std::string&asset:assets)labels.push_back(asset.c_str());
            if(ui.combo("Animator Controller",&controller,labels.data(),(int)labels.size())){n->slit[0]=assets[controller];n->sname[0]=0;dirty=true;}
            if(n->slit[0].empty())n->slit[0]=assets[controller];
            std::vector<std::string> triggers=animationTriggersForController(projectDir,n->slit[0]);
            if(triggers.empty())ui.label("No Event Trigger in the Controller clips.",{.72f,.58f,.38f});
            else{
                int selected=0;for(int i=0;i<(int)triggers.size();i++)if(triggers[i]==n->sname)selected=i;
                std::vector<const char*> names;for(const std::string&trigger:triggers)names.push_back(trigger.c_str());
                if(ui.combo("Key Trigger",&selected,names.data(),(int)names.size())){snprintf(n->sname,sizeof(n->sname),"%s",triggers[selected].c_str());dirty=true;}
                if(!n->sname[0])snprintf(n->sname,sizeof(n->sname),"%s",triggers[selected].c_str());
            }
        }
        ui.label("Animator: the Actor that owns the Animator component.",{.55f,.59f,.66f});
        ui.label("Event: connect the delegate from Create Event.",{.55f,.59f,.66f});
        BPNodeDef inputDef=effDef(*n);drawNodeInputValues(ui,*n,inputDef);return;
    }

    if(n->def==BP_ANIM_SET_FLOAT||n->def==BP_ANIM_SET_BOOL||n->def==BP_ANIM_SET_TRIGGER){
        int wanted=n->def==BP_ANIM_SET_FLOAT?ANIM_PARAM_FLOAT:n->def==BP_ANIM_SET_BOOL?ANIM_PARAM_BOOL:ANIM_PARAM_TRIGGER;
        ui.label("ANIMATOR PARAMETER",{.30f,.62f,.99f});
        std::vector<std::string> assets=bpFindProjectAssets(projectDir,".animctrl");
        if(assets.empty())ui.label("No Animator Controller in the project.",{.95f,.58f,.28f});
        else{
            int controller=0;for(int i=0;i<(int)assets.size();i++)if(assets[i]==n->slit[0])controller=i;
            std::vector<const char*> labels;for(const std::string&asset:assets)labels.push_back(asset.c_str());
            if(ui.combo("Animator Controller",&controller,labels.data(),(int)labels.size())){n->slit[0]=assets[controller];n->sname[0]=0;dirty=true;}
            if(n->slit[0].empty())n->slit[0]=assets[controller];
            std::string data;AnimatorControllerAsset asset;
            if(bpReadTextFile(projectDir+"\\"+n->slit[0],data)&&asset.deserialize(data)){
                std::vector<std::string> vars;for(const AnimatorParameter&p:asset.parameters)if(p.type==wanted)vars.push_back(p.name);
                if(vars.empty())ui.label("No compatible variable in the controller.",{.72f,.58f,.38f});
                else{int selected=0;for(int i=0;i<(int)vars.size();i++)if(vars[i]==n->sname)selected=i;std::vector<const char*>names;for(const std::string&v:vars)names.push_back(v.c_str());if(ui.combo("Variable",&selected,names.data(),(int)names.size())){snprintf(n->sname,sizeof(n->sname),"%s",vars[selected].c_str());dirty=true;}if(!n->sname[0])snprintf(n->sname,sizeof(n->sname),"%s",vars[selected].c_str());}
            }
        }
        ui.label("The Animator pin accepts the Actor holding the Animator component.",{.55f,.59f,.66f});
        BPNodeDef inputDef=effDef(*n);drawNodeInputValues(ui,*n,inputDef);return;
    }

    // ── component property nodes: pick the property, see who supports it ──
    {
        int propKind = -1;
        bool isGetter = false;
        switch (n->def) {
        case BP_GET_WIDGET_NUM:   propKind = WPK_NUM;   isGetter = true;  break;
        case BP_SET_WIDGET_NUM:   propKind = WPK_NUM;                     break;
        case BP_GET_WIDGET_STR:   propKind = WPK_STR;   isGetter = true;  break;
        case BP_SET_WIDGET_STR:   propKind = WPK_STR;                     break;
        case BP_GET_WIDGET_COLOR: propKind = WPK_COLOR; isGetter = true;  break;
        case BP_SET_WIDGET_COLOR: propKind = WPK_COLOR;                   break;
        case BP_GET_WIDGET_BOOL:  propKind = WPK_BOOL;  isGetter = true;  break;
        case BP_SET_WIDGET_BOOL:  propKind = WPK_BOOL;                    break;
        default: break;
        }
        if (propKind >= 0) {
            ui.label(isGetter ? "GET WIDGET PROPERTY" : "SET WIDGET PROPERTY", { 0.30f, 0.62f, 0.99f });
            std::vector<const char*> names;
            std::vector<const WidgetProperty*> props;
            for (int i = 0; i < widgetPropertyCount(); i++) {
                const WidgetProperty& p = widgetPropertyAt(i);
                if (p.kind != propKind) continue;
                props.push_back(&p);
                names.push_back(p.name);
            }
            int selected = 0;
            for (int i = 0; i < (int)props.size(); i++) if (_stricmp(props[i]->name, n->sname) == 0) selected = i;
            if (ui.combo("Property", &selected, names.data(), (int)names.size())) {
                snprintf(n->sname, sizeof(n->sname), "%s", props[selected]->name);
                dirty = true;
            }
            if (!props.empty()) {
                if (!n->sname[0]) snprintf(n->sname, sizeof(n->sname), "%s", props[selected]->name);
                ui.label(props[selected]->hint, { .55f, .59f, .66f });
                // which components advertise this property, so the choice is informed
                std::string owners;
                for (int t = 0; t < WT_TYPE_COUNT; t++) {
                    if (t == WT_PANEL) continue;               // legacy type, not in the palette
                    for (const WidgetProperty* p : widgetPropertiesFor(t))
                        if (p == props[selected]) { if (!owners.empty()) owners += ", "; owners += widgetTypeName(t); break; }
                }
                if (!owners.empty()) ui.label("Components: " + owners, { .62f, .68f, .76f });
            }
            if (widgetMode) ui.label("Leave the Widget pin unwired to target this widget.", { .55f, .59f, .66f });
            ui.label("Element: the component's name (drag one from WIDGETS).", { .55f, .59f, .66f });
            BPNodeDef inputDef = effDef(*n);
            drawNodeInputValues(ui, *n, inputDef);
            return;
        }
    }

    // ── direct slot setters: no property to pick, but the slot rules matter ──
    // direct readers: nothing to configure, just say what they read
    if (n->def >= BP_GET_WIDGET_PERCENT && n->def <= BP_GET_WIDGET_COLOR_DIRECT) {
        ui.label("READ COMPONENT PROPERTY", { 0.30f, 0.62f, 0.99f });
        if (n->def == BP_GET_WIDGET_PERCENT)
            ui.labelWrapped("The bar's current value. Compare it against Get Bar Range "
                            "to work out a fraction.", { .55f, .59f, .66f });
        if (n->def == BP_GET_WIDGET_RANGE)
            ui.label("The bar's Min and Max, as set in the Designer.", { .55f, .59f, .66f });
        if (n->def == BP_GET_WIDGET_ANCHOR || n->def == BP_GET_WIDGET_PIVOT)
            ui.label("Canvas slots only.", { .82f, .72f, .45f });
        if (widgetMode) ui.label("Leave the Widget pin unwired to read this widget.", { .55f, .59f, .66f });
        ui.label("Element: the component's name (drag one from WIDGETS).", { .55f, .59f, .66f });
        BPNodeDef readDef = effDef(*n);
        drawNodeInputValues(ui, *n, readDef);
        return;
    }

    if (n->def == BP_SET_WIDGET_PERCENT || n->def == BP_SET_WIDGET_HALIGN ||
        n->def == BP_SET_WIDGET_VALIGN || n->def == BP_SET_WIDGET_ANCHOR ||
        n->def == BP_SET_WIDGET_PIVOT || n->def == BP_SET_WIDGET_RANGE) {
        bool slotOnCanvas = n->def == BP_SET_WIDGET_ANCHOR || n->def == BP_SET_WIDGET_PIVOT;
        bool slotAligned = n->def == BP_SET_WIDGET_HALIGN || n->def == BP_SET_WIDGET_VALIGN;
        ui.label(n->def == BP_SET_WIDGET_PERCENT ? "SET BAR VALUE"
               : n->def == BP_SET_WIDGET_RANGE   ? "SET BAR RANGE" : "SET SLOT PROPERTY", { 0.30f, 0.62f, 0.99f });
        if (n->def == BP_SET_WIDGET_PERCENT) {
            ui.labelWrapped("The bar's value, held inside its Min..Max range. With the "
                            "default range (0..1) this is a plain percentage.", { .55f, .59f, .66f });
            ui.label("Feeding raw health? Set the range to 0..MaxHealth.", { .82f, .72f, .45f });
        }
        if (n->def == BP_SET_WIDGET_RANGE)
            ui.labelWrapped("Min empties the bar, Max fills it. The value is clamped "
                            "into this span.", { .55f, .59f, .66f });
        // the parent decides which slot properties a component actually has
        if (slotOnCanvas)
            ui.labelWrapped("Canvas slot only: the component's parent must be a Canvas "
                            "(or the screen). Inside a box or a grid it has no effect.", { .82f, .72f, .45f });
        if (slotAligned)
            ui.labelWrapped("Applies when the parent lays its children out (Vertical/Horizontal "
                            "Box, Overlay, grids, Button, Scroll/Stack/Wrap Box, Safe Zone, "
                            "Size/Scale Box). A Canvas child is placed by anchors instead.", { .82f, .72f, .45f });
        if (widgetMode) ui.label("Leave the Widget pin unwired to target this widget.", { .55f, .59f, .66f });
        ui.label("Element: the component's name (drag one from WIDGETS).", { .55f, .59f, .66f });
        BPNodeDef inputDef = effDef(*n);
        drawNodeInputValues(ui, *n, inputDef);
        return;
    }

    if (widgetMode && (n->def == BP_ADD_WIDGET_VIEWPORT || n->def == BP_REMOVE_WIDGET_VIEWPORT ||
                       n->def == BP_SET_WIDGET_TEXT || n->def == BP_SET_WIDGET_VALUE)) {
        ui.label("WIDGET ACTION", { 0.30f, 0.62f, 0.99f });
        ui.label("Leave the Widget pin unwired to target this widget.", { .55f, .59f, .66f });
        if (n->def == BP_SET_WIDGET_TEXT || n->def == BP_SET_WIDGET_VALUE)
            ui.label("Element: drag a component from WIDGETS for its name.", { .55f, .59f, .66f });
        BPNodeDef inputDef = effDef(*n);
        drawNodeInputValues(ui, *n, inputDef);
        return;
    }

    if (n->def == BP_BIND_BEGIN_OVERLAP || n->def == BP_BIND_END_OVERLAP) {
        bool begin = n->def == BP_BIND_BEGIN_OVERLAP;
        ui.label(begin ? "BIND BEGIN OVERLAP" : "BIND END OVERLAP", { 0.30f, 0.62f, 0.99f });
        ui.label("Mesh Renderer: reference del componente da osservare.", { .55f, .59f, .66f });
        ui.label("Event: delegate of a compatible Custom Event.", { .55f, .59f, .66f });
        ui.label("Required signature: Component (Object), Other Actor (Object).", { .82f, .72f, .45f });
        if (ui.button("+ Create compatible Custom Event")) {
            std::string base = begin ? "OnMeshBeginOverlap" : "OnMeshEndOverlap";
            std::string name = bpUniqueMemberName(graph, base, 2);
            BPEventDef signature;
            snprintf(signature.name, sizeof(signature.name), "%s", name.c_str());
            BPFuncPin component; snprintf(component.name, sizeof(component.name), "Component"); component.kind = PIN_ENT;
            BPFuncPin other; snprintf(other.name, sizeof(other.name), "OtherActor"); other.kind = PIN_ENT;
            signature.params = { component, other };
            graph.events.push_back(signature);
            int eventId = C.addNode(BP_EV_CUSTOM, n->x + 360, n->y + 80);
            snprintf(C.byId(eventId)->sname, sizeof(C.byId(eventId)->sname), "%s", name.c_str());
            selNode = eventId;
            dirty = true;
        }
        return;
    }

    if (n->def == BP_CREATE_SAVE_GAME) {
        ui.label("CREATE SAVE GAME", { 0.30f, 0.62f, 0.99f });
        std::vector<std::string> assets;
        for (const std::string& asset : bpFindProjectAssets(projectDir, ".bp")) {
            std::string data;
            BPGraph candidate;
            if (bpReadTextFile(projectDir + "\\" + asset, data) && candidate.deserialize(data) &&
                candidate.classKind == BP_CLASS_SAVEGAME) assets.push_back(asset);
        }
        if (assets.empty()) ui.label("Nessuna classe SaveGame nel progetto.", { .95f, .58f, .28f });
        else {
            int selected = 0;
            for (int i = 0; i < (int)assets.size(); i++) if (assets[i] == n->sname) selected = i;
            std::vector<const char*> labels;
            for (const std::string& asset : assets) labels.push_back(asset.c_str());
            if (ui.combo("SaveGame Class", &selected, labels.data(), (int)labels.size())) {
                snprintf(n->sname, sizeof(n->sname), "%s", assets[selected].c_str());
                dirty = true;
            }
            if (!n->sname[0]) snprintf(n->sname, sizeof(n->sname), "%s", assets[selected].c_str());
        }
        ui.label("The object becomes active when the current event finishes.", { .55f, .59f, .66f });
        return;
    }

    if (n->def == BP_CREATE_WIDGET) {
        ui.label("CREATE WIDGET", { 0.30f, 0.62f, 0.99f });
        std::vector<std::string> assets = bpFindProjectAssets(projectDir, ".wgt");
        if (assets.empty()) ui.label("No widget (.wgt) in the project.", { .95f, .58f, .28f });
        else {
            int selected = 0;
            for (int i = 0; i < (int)assets.size(); i++) if (assets[i] == n->sname) selected = i;
            std::vector<const char*> labels;
            for (const std::string& asset : assets) labels.push_back(asset.c_str());
            if (ui.combo("Widget (.wgt)", &selected, labels.data(), (int)labels.size())) {
                snprintf(n->sname, sizeof(n->sname), "%s", assets[selected].c_str());
                dirty = true;
            }
            if (!n->sname[0]) snprintf(n->sname, sizeof(n->sname), "%s", assets[selected].c_str());
        }
        ui.label("Connect 'Add to Viewport' to show it on screen.", { .55f, .59f, .66f });
        return;
    }

    if (n->def == BP_GET_ALL_WITH_CLASS) {
        ui.label("SCENE QUERY", { 0.30f, 0.62f, 0.99f });
        std::vector<std::string> values, labels;
        for (int i = 0; i < BP_NCOMPS; i++) {
            values.push_back("component:" + std::to_string(i));
            labels.push_back(std::string("Componente: ") + BP_COMP_NAMES[i]);
        }
        for (const std::string& asset : bpFindProjectAssets(projectDir, ".bp")) {
            values.push_back("blueprint:" + asset);
            labels.push_back("Blueprint: " + asset);
        }
        int selected = 0;
        for (int i = 0; i < (int)values.size(); i++) if (values[i] == n->sname) selected = i;
        std::vector<const char*> options;
        for (const std::string& label : labels) options.push_back(label.c_str());
        if (!options.empty() && ui.combo("Required class", &selected, options.data(), (int)options.size())) {
            snprintf(n->sname, sizeof(n->sname), "%s", values[selected].c_str());
            dirty = true;
        }
        ui.label("Loop Body runs once for every Actor found.", { .55f, .59f, .66f });
        ui.label("Actor and Index are valid during the loop; then Completed fires.", { .55f, .59f, .66f });
        return;
    }

    if (n->def == BP_SPAWN_PREFAB || n->def == BP_SELECT_ENUM || n->def == BP_SWITCH_ENUM) {
        bool spawn = n->def == BP_SPAWN_PREFAB;
        std::vector<std::string> assets = bpFindProjectAssets(projectDir, spawn ? ".pfb" : ".enum");
        ui.label(spawn ? "SPAWN PREFAB" : (n->def == BP_SELECT_ENUM ? "SELECT ENUM" : "SWITCH ENUM"),
                 { 0.30f, 0.62f, 0.99f });
        if (assets.empty()) {
            ui.label(spawn ? "No prefab in the project." : "No Enum asset in the project.", { .95f, .58f, .28f });
        } else {
            int selected = 0;
            for (int i = 0; i < (int)assets.size(); i++) if (assets[i] == n->sname) selected = i;
            std::vector<const char*> labels;
            for (const std::string& asset : assets) labels.push_back(asset.c_str());
            if (ui.combo(spawn ? "Prefab" : "Enum", &selected, labels.data(), (int)labels.size())) {
                snprintf(n->sname, sizeof(n->sname), "%s", assets[selected].c_str());
                dirty = true;
            }
            if (!n->sname[0]) snprintf(n->sname, sizeof(n->sname), "%s", assets[selected].c_str());
        }
        if (spawn) {
            int exposed = (int)bpSpawnPinInfo(projectDir, n->sname).size();
            char info[96]; snprintf(info, sizeof(info), "%d Exposed on Spawn variables from the root Blueprint.", exposed);
            ui.label(info, { .55f, .59f, .66f });
            ui.label("Connect Make Transform to the Spawn Transform pin.", { .55f, .59f, .66f });
        }
        BPNodeDef inputDef = effDef(*n);
        drawNodeInputValues(ui, *n, inputDef);
        return;
    }

    // function Entry / Return nodes: edit the function's input & output pins
    if ((n->def == BP_FN_ENTRY || n->def == BP_FN_RETURN) && curFunc >= 0) {
        drawFuncSignature(ui);
        if (n->def == BP_FN_RETURN) {
            BPNodeDef inputDef = effDef(*n);
            drawNodeInputValues(ui, *n, inputDef);
        }
        return;
    }

    // Line / Sphere Trace: multi-select layer mask (dropdown of available layers)
    if (n->def == BP_TRACE_LINE || n->def == BP_TRACE_SPHERE) {
        ui.label("TRACE DETAILS", { 0.30f, 0.62f, 0.99f });
        ui.label(DEFS[n->def].title, { 0.85f, 0.9f, 0.97f });
        ui.spacing(2);
        int lc = gBPLayers ? gBPLayers->count : 0;
        unsigned mask = (unsigned)n->choice;
        char summ[64];
        int nsel = 0, one = -1;
        for (int i = 0; i < lc; i++) if ((mask >> i) & 1u) { nsel++; one = i; }
        if (mask == 0 || lc == 0 || nsel == 0) snprintf(summ, sizeof(summ), "All");
        else if (nsel == 1) snprintf(summ, sizeof(summ), "%s", gBPLayers->names[one]);
        else snprintf(summ, sizeof(summ), "%d layer", nsel);
        char btn[110];
        snprintf(btn, sizeof(btn), "Layer: %s   %s", summ, traceMaskOpen ? "^" : "v");
        if (ui.buttonColored(btn, traceMaskOpen ? Vec3{ 0.12f, 0.32f, 0.56f } : Vec3{ 0.16f, 0.18f, 0.22f },
                             traceMaskOpen ? Vec3{ 0.8f, 0.92f, 1.0f } : Vec3{ 0.85f, 0.88f, 0.93f }))
            traceMaskOpen = !traceMaskOpen;
        if (traceMaskOpen && lc > 0) {
            bool all = (mask == 0);
            if (ui.checkbox("All layers", &all)) {
                n->choice = all ? 0 : (int)((1u << lc) - 1u);
                dirty = true;
            }
            if (n->choice != 0) {
                mask = (unsigned)n->choice;
                for (int i = 0; i < lc; i++) {
                    bool on = ((mask >> i) & 1u) != 0;
                    char cid[48];
                    snprintf(cid, sizeof(cid), "%s##tl%d", gBPLayers->names[i][0] ? gBPLayers->names[i] : "-", i);
                    if (ui.checkbox(cid, &on)) {
                        if (on) mask |= (1u << i); else mask &= ~(1u << i);
                        n->choice = (int)mask;
                        dirty = true;
                    }
                }
            }
        }
        ui.spacing(2);
        ui.label("Layers the trace can hit.", { 0.5f, 0.54f, 0.6f });
        ui.spacing(3);
        bool dbg = n->prop != 0;
        if (ui.checkbox("Show debug (in Play)", &dbg)) { n->prop = dbg ? 1.0f : 0.0f; dirty = true; }
        ui.label("Draws the ray and the hit point.", { 0.5f, 0.54f, 0.6f });
        BPNodeDef inputDef = effDef(*n);
        drawNodeInputValues(ui, *n, inputDef);
        return;
    }

    const BPNodeDef& d = DEFS[n->def];
    ui.label("NODE DETAILS", { 0.30f, 0.62f, 0.99f });
    char title[64];
    bpNodeTitle(*n, title, sizeof(title));
    ui.label(title, { 0.85f, 0.9f, 0.97f });
    ui.spacing(2);

    if (d.usesName && !(n->def == BP_EV_CUSTOM && !n->slit[0].empty())) {
        std::vector<const char*> opts;
        std::vector<std::string> curveNames;
        bool fixed = false;
        if (n->def == BP_CURVE_EVAL) {
            std::error_code ec;
            for (const auto& e : std::filesystem::recursive_directory_iterator(projectDir, ec)) {
                if (ec) break;
                if (!e.is_regular_file() || e.path().extension() != ".curve") continue;
                std::filesystem::path rel = std::filesystem::relative(e.path(), projectDir, ec);
                if (!ec) curveNames.push_back(rel.string());
            }
            std::sort(curveNames.begin(), curveNames.end());
            for (const std::string& path : curveNames) opts.push_back(path.c_str());
            fixed = true;
        } else {
            fixed = bpNameOptions(graph, n->def, opts);
        }
        if (fixed) {
            // searchable dropdown of the available variables / functions / events
            bool isVarNode = n->def == BP_VAR_GET || n->def == BP_VAR_SET;
            ui.label(isVarNode ? "Variable:" : "Reference:", { 0.55f, 0.59f, 0.66f });
            if (renameFor != selNode) { nameSearch[0] = 0; renameFor = selNode; }
            ui.textInput("bpnamesearch", nameSearch, sizeof(nameSearch));
            if (opts.empty()) {
                ui.label(isVarNode ? "(no variables: create one in the panel)" : "(no items)",
                         { 0.5f, 0.54f, 0.6f });
            }
            int shown = 0;
            for (const char* opt : opts) {
                if (nameSearch[0] && !icontains(opt, nameSearch)) continue;
                if (++shown > 40) break;
                char rid[48];
                snprintf(rid, sizeof(rid), "bpnamepick%d", shown);
                if (ui.selectable(rid, opt, strcmp(opt, n->sname) == 0)) {
                    snprintf(n->sname, sizeof(n->sname), "%s", opt);
                    dirty = true;
                }
            }
            ui.spacing(3);
        } else {
            // Deferred rename, the same contract as the variable and function
            // fields: the typed text lives in renameBuf and the node keeps its
            // committed name until Enter (or a click away). Applying every
            // keystroke made an emptied field snap straight back to a generated
            // "CustomEvent", and it also blanked the event's parameter pins for
            // as long as the half-typed name matched no event definition.
            const bool isEvent = n->def == BP_EV_CUSTOM;
            bool wasFocused = ui.inputFocused("bprename");
            if (!wasFocused) {                    // idle: mirror the committed name
                snprintf(renameBuf, sizeof(renameBuf), "%s", n->sname);
                renameFor = selNode;
            }
            ui.textInput("bprename", renameBuf, sizeof(renameBuf));
            UIRect fr = ui.lastItemRect();
            for (char* c = renameBuf; *c; c++) if (*c == ' ') *c = '_';
            const bool isFocused = ui.inputFocused("bprename");
            // an event name has to be unique; a plain object name is free-form
            int eventIndex = -1;
            if (isEvent)
                for (int i = 0; i < (int)graph.events.size(); i++)
                    if (bpSameName(graph.events[i].name, n->sname)) { eventIndex = i; break; }
            const bool taken = isEvent &&
                !bpSameName(bpUniqueMemberName(graph, renameBuf, 2, eventIndex).c_str(), renameBuf);
            const bool invalid = renameBuf[0] == 0 || taken;
            if (isFocused && invalid) {           // warn, but let the user keep typing
                Vec3 red = { 0.92f, 0.26f, 0.24f };
                ui.r->drawRectPx(fr.x, fr.y, fr.w, 2, red, 1);
                ui.r->drawRectPx(fr.x, fr.y + fr.h - 2, fr.w, 2, red, 1);
                ui.r->drawRectPx(fr.x, fr.y, 2, fr.h, red, 1);
                ui.r->drawRectPx(fr.x + fr.w - 2, fr.y, 2, fr.h, red, 1);
            }
            if (wasFocused && !isFocused) {       // committing this frame
                std::string oldEventName = isEvent ? n->sname : "";
                if (invalid) {                    // rejected: put the old name back
                    snprintf(renameBuf, sizeof(renameBuf), "%s", n->sname);
                } else if (!bpSameName(renameBuf, n->sname)) {
                    snprintf(n->sname, sizeof(n->sname), "%s", renameBuf);
                    if (!oldEventName.empty()) {
                        if (BPEventDef* eventDef = graph.findEvent(oldEventName.c_str()))
                            snprintf(eventDef->name, sizeof(eventDef->name), "%s", n->sname);
                        auto renameRefs = [&](BPCanvas& cvx) {
                            for (BPNode& ref : cvx.nodes)
                                if ((ref.def == BP_CALL_EVENT || ref.def == BP_CREATE_EVENT) &&
                                    bpSameName(ref.sname, oldEventName.c_str()))
                                    snprintf(ref.sname, sizeof(ref.sname), "%s", n->sname);
                        };
                        for (BPFunc& gph : graph.graphs) renameRefs(gph.body);
                        for (BPFunc& fn : graph.funcs) renameRefs(fn.body);
                    }
                    dirty = true;
                }
            }
            ui.label("name (event / object)", { 0.5f, 0.54f, 0.6f });
            ui.spacing(3);
        }
    }

    // Custom Event: input (compaiono come pin dati sull'evento e su Chiama Evento)
    if (n->def == BP_EV_CUSTOM) {
        if (!n->slit[0].empty()) {
            ui.label("INTERFACE EVENT", { .36f, .72f, .98f });
            ui.label(std::string("Origin: ") + fs::path(n->slit[0]).stem().string(), { .68f, .82f, 1.0f });
            if (BPEventDef* signature = graph.findEvent(n->sname)) {
                for (const BPFuncPin& pin : signature->params)
                    ui.label(std::string("  ") + pin.name + " : " + BP_VARTYPE_NAMES[(int)pin.kind], { .58f, .67f, .78f });
            }
            ui.label("Name and signature come from the .bpi asset.", { .55f, .59f, .66f });
            return;
        }
        if (n->sname[0] == 0) {
            ui.label("Name the event to add inputs.", { 0.5f, 0.54f, 0.6f });
            return;
        }
        BPEventDef* ed = graph.findEvent(n->sname);
        if (!ed) {
            BPEventDef nd;
            snprintf(nd.name, sizeof(nd.name), "%s", n->sname);
            graph.events.push_back(nd);
            ed = &graph.events.back();
        }
        int eventScope = (int)ed->scope;
        if (ui.combo("Accesso", &eventScope, BP_SCOPE_NAMES, 3)) {
            ed->scope = (VarScope)eventScope;
            dirty = true;
        }
        ui.label(ed->scope == VS_PUBLIC ? "Callable from other Blueprints."
                                        : ed->scope == VS_PROTECTED ? "Not exposed to other Blueprints; reserved for the hierarchy."
                                                                    : "Callable only inside this Blueprint.",
                 { 0.5f, 0.54f, 0.6f });
        static const char* TYPES[] = { "Float", "Int", "Bool", "Vector2", "Vector3", "String", "Object", "Transform", "Timer Handle" };
        static const PinKind TMAP[] = { PIN_NUM, PIN_INT, PIN_BOOL, PIN_VEC2, PIN_VEC, PIN_STR, PIN_ENT, PIN_TRANSFORM, PIN_TIMER_HANDLE };
        ui.spacing(3);
        ui.label("INPUT (event pins)", { 0.55f, 0.59f, 0.66f });
        int removeAt = -1;
        for (int i = 0; i < (int)ed->params.size(); i++) {
            char id[40];
            snprintf(id, sizeof(id), "evp_nm%d", i);
            if (ui.textInput(id, ed->params[i].name, sizeof(ed->params[i].name))) {
                for (char* c = ed->params[i].name; *c; c++) if (*c == ' ') *c = '_';
                dirty = true;
            }
            ui.row(2);
            int ti = 0;
            for (int j = 0; j < 9; j++) if (TMAP[j] == ed->params[i].kind) ti = j;
            snprintf(id, sizeof(id), "#evp_ty%d", i);
            if (ui.combo(id, &ti, TYPES, 9)) { ed->params[i].kind = TMAP[ti]; dirty = true; }
            snprintf(id, sizeof(id), "Remove##evp%d", i);
            if (ui.buttonColored(id, { 0.4f, 0.14f, 0.14f }, { 1, 0.85f, 0.85f })) removeAt = i;
        }
        if (removeAt >= 0) {
            int dp = removeAt + 1;   // il pin 0 e' l'exec
            auto fix = [&](BPCanvas& cvx) {
                for (size_t li = 0; li < cvx.links.size();) {
                    BPLink& l = cvx.links[li];
                    BPNode* fn = cvx.byId(l.fromNode);
                    BPNode* tn = cvx.byId(l.toNode);
                    bool evOut = fn && fn->def == BP_EV_CUSTOM && strcmp(fn->sname, n->sname) == 0;
                    bool callIn = tn && tn->def == BP_CALL_EVENT && strcmp(tn->sname, n->sname) == 0;
                    if ((evOut && l.fromPin == dp) || (callIn && l.toPin == dp)) { cvx.links.erase(cvx.links.begin() + li); continue; }
                    if (evOut && l.fromPin > dp) l.fromPin--;
                    if (callIn && l.toPin > dp) l.toPin--;
                    li++;
                }
            };
            for (auto& g : graph.graphs) fix(g.body);
            for (auto& f : graph.funcs) fix(f.body);
            ed->params.erase(ed->params.begin() + removeAt);
            dirty = true;
        }
        if ((int)ed->params.size() < BP_MAX_FUNC_PINS) {
            if (ui.button("+ Add input")) {
                // The delegate is always the last output. Appending a parameter
                // moves it one slot to the right, so preserve existing binds.
                int oldDelegatePin = 1 + (int)ed->params.size();
                auto shiftDelegate = [&](BPCanvas& cvx) {
                    for (auto& l : cvx.links) {
                        BPNode* fn = cvx.byId(l.fromNode);
                        if (fn && fn->def == BP_EV_CUSTOM && strcmp(fn->sname, n->sname) == 0 &&
                            l.fromPin >= oldDelegatePin) l.fromPin++;
                    }
                };
                for (auto& g : graph.graphs) shiftDelegate(g.body);
                for (auto& f : graph.funcs) shiftDelegate(f.body);
                BPFuncPin p;
                snprintf(p.name, sizeof(p.name), "in%d", (int)ed->params.size() + 1);
                p.kind = PIN_NUM;
                ed->params.push_back(p);
                dirty = true;
            }
        }
        ui.spacing(4);
        ui.label("The pins appear on the event and", { 0.5f, 0.54f, 0.6f });
        ui.label("on Call Event nodes.", { 0.5f, 0.54f, 0.6f });
        return;
    }

    if (n->def == BP_GET_COMPONENT) {
        std::vector<std::string> labels;
        std::vector<int> nativeClasses;
        std::vector<std::string> blueprintAssets;
        labels.reserve(BP_NCOMPS + 16);
        nativeClasses.reserve(BP_NCOMPS + 16);
        blueprintAssets.reserve(BP_NCOMPS + 16);

        for (int i = 0; i < BP_NCOMPS; i++) {
            labels.push_back(i == 6 ? "Blueprint (any)" : BP_COMP_NAMES[i]);
            nativeClasses.push_back(i);
            blueprintAssets.emplace_back();
        }
        const std::vector<std::string> assets = bpFindProjectAssets(projectDir, ".bp");
        for (const std::string& asset : assets) {
            fs::path path(asset);
            std::string label = path.stem().string();
            const std::string folder = path.parent_path().string();
            if (!folder.empty()) label += "  (" + folder + ")";
            labels.push_back(label);
            nativeClasses.push_back(6);
            blueprintAssets.push_back(asset);
        }

        int selected = n->choice % BP_NCOMPS;
        if (!n->slit[0].empty()) {
            selected = -1;
            for (int i = BP_NCOMPS; i < (int)blueprintAssets.size(); i++) {
                if (bpSameAssetPath(blueprintAssets[i], n->slit[0])) { selected = i; break; }
            }
            if (selected < 0) {
                fs::path missing(n->slit[0]);
                labels.push_back(missing.stem().string() + "  (asset non trovato)");
                nativeClasses.push_back(6);
                blueprintAssets.push_back(n->slit[0]);
                selected = (int)labels.size() - 1;
            }
        }

        std::vector<const char*> options;
        options.reserve(labels.size());
        for (const std::string& label : labels) options.push_back(label.c_str());
        if (ui.combo("Classe", &selected, options.data(), (int)options.size())) {
            n->choice = nativeClasses[selected];
            n->slit[0] = blueprintAssets[selected];
            dirty = true;
        }
        if (assets.empty())
            ui.label("No Blueprint asset found in the project.", { .95f, .58f, .28f });
        else
            ui.label("Blueprints are listed by name, without the .bp extension.", { .55f, .59f, .66f });
        drawNodeInputValues(ui, *n, effDef(*n));
        return;
    }

    switch (d.propKind) {
    case 1: {
        float v = n->prop;
        if (ui.dragFloat("Value", &v, 0.05f, -100000, 100000)) { n->prop = v; dirty = true; }
        break;
    }
    case 2: {
        // InputAction can also bind mouse axes; Is Key Down only lists keys
        bool withAxes = n->def == BP_EV_KEY;
        int nb = withAxes ? BP_NBINDS : BP_NKEYS;
        int ki = n->choice % nb;
        if (ui.combo(withAxes ? "Binding" : "Key", &ki, withAxes ? bpBindNames() : BP_KEY_NAMES, nb)) {
            n->choice = ki;
            dirty = true;
        }
        bool listening = keyListenNode == selNode;
        if (listening) {
            if (ui.buttonColored("Press a key...  (ESC cancels)", { 0.13f, 0.30f, 0.50f }, { 0.9f, 0.95f, 1.0f })) {
                keyListenNode = 0;
            }
        } else if (ui.button("Listen for key")) {
            keyListenNode = selNode;
        }
        ui.label("letters, numbers, arrows, SPACE,", { 0.5f, 0.54f, 0.6f });
        ui.label("SHIFT e CTRL", { 0.5f, 0.54f, 0.6f });
        break;
    }
    case 3: {
        int oi = n->choice % 5;
        if (ui.combo("Operatore", &oi, BP_CMP_OPS, 5)) { n->choice = oi; dirty = true; }
        break;
    }
    case 4: {
        int ci = n->choice % BP_NCOMPS;
        if (ui.combo("Classe", &ci, BP_COMP_NAMES, BP_NCOMPS)) { n->choice = ci; dirty = true; }
        break;
    }
    case 5: {
        int ai = n->choice % BP_NAXES;
        if (ui.combo("Asse", &ai, BP_AXIS_NAMES, BP_NAXES)) { n->choice = ai; dirty = true; }
        break;
    }
    }
    BPNodeDef inputDef = effDef(*n);
    drawNodeInputValues(ui, *n, inputDef);
}

void BPEditor::scanInterfaceAssets() {
    if (projectDir.empty()) return;
    if (settingsScanProject == projectDir && frame_ - settingsScanFrame < 120) return;
    settingsScanProject = projectDir;
    settingsScanFrame = frame_;
    settingsInterfaceAssets.clear();
    settingsInterfaceLabels.clear();
    std::error_code ec;
    for (fs::recursive_directory_iterator it(projectDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
        if (ext != ".bpi") continue;
        std::string rel = fs::relative(it->path(), projectDir, ec).string();
        if (ec) { ec.clear(); continue; }
        settingsInterfaceAssets.push_back(rel);
    }
    std::sort(settingsInterfaceAssets.begin(), settingsInterfaceAssets.end());
    for (const std::string& rel : settingsInterfaceAssets) {
        std::string label = fs::path(rel).stem().string();
        std::string parent = fs::path(rel).parent_path().string();
        if (!parent.empty()) label += "  (" + parent + ")";
        settingsInterfaceLabels.push_back(label);
    }
    if (settingsInterfacePick >= (int)settingsInterfaceAssets.size()) settingsInterfacePick = 0;
}

bool BPEditor::implementInterfaceAsset(const std::string& relativePath) {
    if (relativePath.empty()) return false;
    if (std::find(graph.interfaceAssets.begin(), graph.interfaceAssets.end(), relativePath) != graph.interfaceAssets.end()) {
        syncImplementedInterfaces();
        if (logFn) logFn(0, "Interface already implemented: %s", relativePath.c_str());
        return false;
    }
    std::string abs = projectDir + "\\" + relativePath;
    FILE* f = fopen(abs.c_str(), "rb");
    if (!f) {
        if (logFn) logFn(2, "Could not read the interface: %s", relativePath.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string data(size > 0 ? (size_t)size : 0, 0);
    if (size > 0) fread(data.data(), 1, (size_t)size, f);
    fclose(f);
    BPGraph iface;
    if (!iface.deserialize(data)) {
        if (logFn) logFn(2, "Invalid interface asset: %s", relativePath.c_str());
        return false;
    }

    graph.interfaceAssets.push_back(relativePath);
    syncImplementedInterfaces();
    dirty = true;
    if (logFn) logFn(1, "Interface implemented: %s (%d declarations)", relativePath.c_str(), (int)iface.funcs.size());
    return true;
}

static BPNode* bpFindInterfaceEventNode(BPGraph& graph, const char* name) {
    for (BPFunc& graphCanvas : graph.graphs)
        for (BPNode& node : graphCanvas.body.nodes)
            if (node.def == BP_EV_CUSTOM && _stricmp(node.sname, name) == 0) return &node;
    return nullptr;
}

static void bpMigrateFunctionBodyToEvent(BPGraph& graph, int functionIndex, BPNode& eventNode) {
    if (functionIndex < 0 || functionIndex >= (int)graph.funcs.size()) return;
    BPFunc source = std::move(graph.funcs[functionIndex]);
    BPCanvas& destination = graph.main();
    const int interfaceEventId = eventNode.id;
    const float interfaceEventX = eventNode.x;
    const float interfaceEventY = eventNode.y;
    const BPNode* entry = nullptr;
    for (const BPNode& node : source.body.nodes) if (node.def == BP_FN_ENTRY) { entry = &node; break; }
    const float dx = entry ? interfaceEventX - entry->x : 0.0f;
    const float dy = entry ? interfaceEventY - entry->y : 0.0f;
    std::map<int, int> remap;
    for (const BPNode& original : source.body.nodes) {
        if (original.def == BP_FN_ENTRY || original.def == BP_FN_RETURN) continue;
        BPNode copy = original;
        copy.id = destination.nextId++;
        copy.x += dx;
        copy.y += dy;
        remap[original.id] = copy.id;
        destination.nodes.push_back(std::move(copy));
    }
    const int entryId = entry ? entry->id : 0;
    for (const BPLink& link : source.body.links) {
        const BPNode* target = source.body.byId(link.toNode);
        if (target && target->def == BP_FN_RETURN) continue;
        const bool fromEntry = link.fromNode == entryId;
        int from = fromEntry ? interfaceEventId : (remap.count(link.fromNode) ? remap[link.fromNode] : 0);
        int to = remap.count(link.toNode) ? remap[link.toNode] : 0;
        int fromPin = fromEntry && source.pure ? link.fromPin + 1 : link.fromPin;
        if (from && to) destination.connect(from, fromPin, to, link.toPin);
    }
    for (BPComment comment : source.body.comments) {
        comment.x += dx;
        comment.y += dy;
        destination.comments.push_back(std::move(comment));
    }
    graph.funcs.erase(graph.funcs.begin() + functionIndex);
}

void BPEditor::syncImplementedInterfaces() {
    std::vector<std::string> declared;
    std::set<std::string> activeAssets;
    for (const std::string& relativePath : graph.interfaceAssets) {
        std::string assetKey = relativePath;
        std::replace(assetKey.begin(), assetKey.end(), '/', '\\');
        std::transform(assetKey.begin(), assetKey.end(), assetKey.begin(), [](unsigned char c) { return (char)tolower(c); });
        activeAssets.insert(assetKey);
        std::string data;
        if (!bpReadTextFile(projectDir + "\\" + relativePath, data)) continue;
        BPGraph iface;
        if (!iface.deserialize(data)) continue;
        for (const BPFunc& sig : iface.funcs) {
            if (std::find(declared.begin(), declared.end(), sig.name) == declared.end())
                declared.push_back(sig.name);
            if (sig.outs.empty()) {
                BPEventDef* event = graph.findEvent(sig.name);
                if (!event) {
                    BPEventDef created;
                    snprintf(created.name, sizeof(created.name), "%s", sig.name);
                    graph.events.push_back(std::move(created));
                    event = &graph.events.back();
                }
                event->params = sig.ins;
                BPNode* eventNode = bpFindInterfaceEventNode(graph, sig.name);
                if (!eventNode) {
                    BPCanvas& mainCanvas = graph.main();
                    int id = mainCanvas.addNode(BP_EV_CUSTOM, 80, 120 + (float)mainCanvas.nodes.size() * 28.0f);
                    eventNode = mainCanvas.byId(id);
                    snprintf(eventNode->sname, sizeof(eventNode->sname), "%s", sig.name);
                }
                eventNode->slit[0] = relativePath;

                int oldFunction = -1;
                for (int i = 0; i < (int)graph.funcs.size(); i++)
                    if (_stricmp(graph.funcs[i].name, sig.name) == 0) { oldFunction = i; break; }
                if (oldFunction >= 0) {
                    int eventId = eventNode->id;
                    bpMigrateFunctionBodyToEvent(graph, oldFunction, *eventNode);
                    eventNode = graph.main().byId(eventId);
                    if (eventNode) eventNode->slit[0] = relativePath;
                }
            } else {
                BPFunc* implementation = graph.findFunc(sig.name);
                if (!implementation) {
                    BPFunc created;
                    snprintf(created.name, sizeof(created.name), "%s", sig.name);
                    created.ins = sig.ins;
                    created.outs = sig.outs;
                    created.pure = false;
                    created.body.clear();
                    int entry = created.body.addNode(BP_FN_ENTRY, 40, 80);
                    int result = created.body.addNode(BP_FN_RETURN, 350, 80);
                    created.body.connect(entry, 0, result, 0);
                    graph.funcs.push_back(std::move(created));
                    implementation = &graph.funcs.back();
                }
                implementation->pure = false;
                implementation->ins = sig.ins;
                implementation->outs = sig.outs;

                if (BPNode* oldEvent = bpFindInterfaceEventNode(graph, sig.name)) oldEvent->slit[0].clear();
            }
        }
    }
    for (BPFunc& graphCanvas : graph.graphs) for (BPNode& node : graphCanvas.body.nodes) {
        if (node.def != BP_EV_CUSTOM || node.slit[0].empty()) continue;
        std::string key = node.slit[0];
        std::replace(key.begin(), key.end(), '/', '\\');
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
        if (!activeAssets.count(key)) node.slit[0].clear();
    }
    graph.interfaces = std::move(declared);
}

void BPEditor::drawBlueprintSettings(UI& ui) {
    const Vec3 accent = { 0.45f, 0.72f, 1.0f };
    const Vec3 dim = { 0.5f, 0.54f, 0.6f };
    const UIInput settingsInput = ui.input();
    const bool settingsWasBlocked = ui.interactionBlocked();
    if (settingsInterfaceContext >= 0) ui.setInteractionBlocked(true, false);
    ui.label("BLUEPRINT SETTINGS", accent);
    ui.separator();
    ui.label(std::string("Asset: ") + (curPath.empty() ? "unsaved" : curPath), { 0.84f, 0.88f, 0.95f });
    bool isInterface = !curPath.empty() && fs::path(curPath).extension() == ".bpi";
    ui.label(isInterface ? "Type: Blueprint Interface" : "Type: Blueprint", dim);
    if (!isInterface) {
        int kind = (int)graph.classKind;
        if (ui.combo("Blueprint class", &kind, BP_CLASS_KIND_NAMES, 5)) {
            graph.classKind = (BPClassKind)kind;
            dirty = true;
        }
        if (!graph.parentAsset.empty()) {
            ui.label(std::string("Parent: ") + graph.parentAsset, { .68f, .82f, 1.0f });
            ui.label("Parent variables, functions and graphs are inherited at runtime.", dim);
        } else ui.label("Parent: nessuno", dim);

        if (graph.classKind == BP_CLASS_ACTOR) {
            ui.spacing(5);
            ui.label("DEFAULT ACTOR TAGS", accent);
            int remove = -1;
            for (int i = 0; i < (int)graph.defaultTags.size(); i++) {
                char value[48]; snprintf(value, sizeof(value), "%s", graph.defaultTags[i].c_str());
                char id[48]; snprintf(id, sizeof(id), "bp_default_tag_%d", i);
                ui.row(2);
                if (ui.textInput(id, value, sizeof(value))) { graph.defaultTags[i] = value; dirty = true; }
                char rid[48]; snprintf(rid, sizeof(rid), "-##bp_default_tag_remove_%d", i);
                if (ui.button(rid)) remove = i;
            }
            if (remove >= 0) { graph.defaultTags.erase(graph.defaultTags.begin() + remove); dirty = true; }
            if (graph.defaultTags.size() < 16 && ui.button("+ Actor Tag")) { graph.defaultTags.push_back("Player"); dirty = true; }
        } else if (graph.classKind == BP_CLASS_GAMEMODE) {
            ui.spacing(5);
            ui.label("GAME MODE", accent);
            char pawn[128]; snprintf(pawn, sizeof(pawn), "%s", graph.defaultPawnClass.c_str());
            if (ui.textInput("default_pawn_class", pawn, sizeof(pawn))) { graph.defaultPawnClass = pawn; dirty = true; }
            ui.label("Default Pawn Class (.bp path; finds an existing actor of that class).", dim);
            char controller[128]; snprintf(controller, sizeof(controller), "%s", graph.playerControllerClass.c_str());
            if (ui.textInput("player_controller_class", controller, sizeof(controller))) { graph.playerControllerClass = controller; dirty = true; }
            ui.label("PlayerController Class (percorso .bp).", dim);
        } else if (graph.classKind == BP_CLASS_GAMEINSTANCE) {
            ui.label("Lives for the whole session and keeps its variables across Play runs.", dim);
        } else if (graph.classKind == BP_CLASS_PLAYERCONTROLLER) {
            ui.label("Use Get Player Pawn to control the Player without direct references.", dim);
        } else if (graph.classKind == BP_CLASS_SAVEGAME) {
            ui.label("Persistent data class; public variables define the save format.", dim);
        }

        ui.spacing(8);
        ui.label("BLUEPRINT COMPONENT", accent);
        bool unique = graph.uniquePerObject;
        if (ui.checkbox("One instance per object", &unique)) {
            graph.uniquePerObject = unique;
            dirty = true;
        }
        ui.label(graph.uniquePerObject
                     ? "If required or added several times, only one instance stays on the object."
                     : "The same Blueprint can be added several times to the same object.",
                 dim);

        ui.spacing(7);
        ui.label("REQUIRED COMPONENTS", accent);
        ui.label("They are added automatically together with this Blueprint.", dim);
        int removeRequired = -1;
        for (int i = 0; i < (int)graph.requiredComponents.size(); i++) {
            const BPRequiredComponent& required = graph.requiredComponents[i];
            std::string componentLabel = required.kind == BP_REQ_BLUEPRINT
                ? std::string("Blueprint: ") + fs::path(required.blueprintAsset).stem().string()
                : bpRequiredBaseName(required.kind);
            std::string row = componentLabel + "  ->  " + required.variableName;
            ui.row(2);
            ui.label(row, { .82f, .86f, .93f });
            char removeId[48]; snprintf(removeId, sizeof(removeId), "-##remove_required_%d", i);
            if (ui.button(removeId)) removeRequired = i;
        }
        if (removeRequired >= 0) {
            graph.requiredComponents.erase(graph.requiredComponents.begin() + removeRequired);
            graph.syncRequiredVariables();
            selVar = -1;
            dirty = true;
        }

        static const char* REQUIRED_TYPES[] = {
            "Mesh Renderer", "Rigid Body", "Trigger", "Luce", "Camera", "Audio Source",
            "Audio Reverb Zone", "AI Agent", "Navigation Occluder", "Animator", "Blueprint"
        };
        ui.combo("Required type", &settingsRequiredKindPick, REQUIRED_TYPES, BP_REQ_COUNT);
        std::vector<std::string> blueprintAssets = bpFindProjectAssets(projectDir, ".bp");
        blueprintAssets.erase(std::remove_if(blueprintAssets.begin(), blueprintAssets.end(),
            [&](const std::string& path) { return !curPath.empty() && _stricmp(path.c_str(), curPath.c_str()) == 0; }),
            blueprintAssets.end());
        if (settingsRequiredBlueprintPick >= (int)blueprintAssets.size()) settingsRequiredBlueprintPick = 0;
        if (settingsRequiredKindPick == BP_REQ_BLUEPRINT) {
            if (blueprintAssets.empty()) {
                ui.label("No other Blueprint available.", { .95f, .58f, .28f });
            } else {
                std::vector<const char*> blueprintLabels;
                for (const std::string& asset : blueprintAssets) blueprintLabels.push_back(asset.c_str());
                ui.combo("Required Blueprint", &settingsRequiredBlueprintPick,
                         blueprintLabels.data(), (int)blueprintLabels.size());
            }
        }
        bool canAddRequired = settingsRequiredKindPick != BP_REQ_BLUEPRINT || !blueprintAssets.empty();
        if (canAddRequired && ui.button("+ Add Required Component")) {
            BPRequiredComponent required;
            required.kind = (BPRequiredKind)settingsRequiredKindPick;
            std::string base = bpRequiredBaseName(required.kind);
            bool allowed = true;
            if (required.kind == BP_REQ_BLUEPRINT) {
                required.blueprintAsset = blueprintAssets[settingsRequiredBlueprintPick];
                base = fs::path(required.blueprintAsset).stem().string();
                BPGraph target;
                bool targetUnique = bpLoadResolvedGraph(projectDir, required.blueprintAsset, target) &&
                                    target.uniquePerObject;
                if (targetUnique) {
                    for (const BPRequiredComponent& existing : graph.requiredComponents)
                        if (existing.kind == BP_REQ_BLUEPRINT &&
                            _stricmp(existing.blueprintAsset.c_str(), required.blueprintAsset.c_str()) == 0)
                            allowed = false;
                }
            } else {
                for (const BPRequiredComponent& existing : graph.requiredComponents)
                    if (existing.kind == required.kind) allowed = false;
            }
            if (allowed) {
                std::string variableName = bpUniqueRequiredName(graph, base);
                snprintf(required.variableName, sizeof(required.variableName), "%s", variableName.c_str());
                graph.requiredComponents.push_back(std::move(required));
                graph.syncRequiredVariables();
                dirty = true;
            } else if (logFn) {
                logFn(2, "This Required component is single-instance and is already present.");
            }
        }
    }
    ui.spacing(8);
    ui.label("IMPLEMENTED INTERFACES", accent);

    if (isInterface) {
        ui.label("An Interface asset defines signatures; it does not implement other interfaces.", dim);
        ui.setInteractionBlocked(settingsWasBlocked, false);
        return;
    }

    if (graph.interfaceAssets.empty()) {
        ui.label("No interface configured.", dim);
    } else {
        if (settingsImplementedPick >= (int)graph.interfaceAssets.size()) settingsImplementedPick = 0;
        for (int index = 0; index < (int)graph.interfaceAssets.size(); index++) {
            const std::string& relativePath = graph.interfaceAssets[index];
            std::string label = fs::path(relativePath).stem().string();
            if (label.empty()) label = relativePath;
            std::string folder = fs::path(relativePath).parent_path().string();
            if (!folder.empty()) label += "  (" + folder + ")";
            const float rowTop = ui.panelCursorY();
            char rowId[48]; snprintf(rowId, sizeof(rowId), "implemented_interface_%d", index);
            if (ui.selectable(rowId, label, settingsImplementedPick == index)) settingsImplementedPick = index;
            const float rowBottom = ui.panelCursorY();
            UIRect panel = ui.panelInner();
            bool over = settingsInput.mouseX >= panel.x && settingsInput.mouseX < panel.x + panel.w &&
                        settingsInput.mouseY >= rowTop && settingsInput.mouseY < rowBottom;
            if (over && settingsInput.rmbPressed) {
                settingsImplementedPick = index;
                settingsInterfaceContext = index;
                settingsInterfaceContextX = settingsInput.mouseX;
                settingsInterfaceContextY = settingsInput.mouseY;
            }
        }
        ui.label("Right-click an interface to remove it.", dim);
    }

    ui.spacing(10);
    scanInterfaceAssets();
    std::vector<const char*> labels;
    for (const std::string& label : settingsInterfaceLabels) labels.push_back(label.c_str());
    if (labels.empty()) {
        ui.label("No .bpi asset found in the project.", dim);
    } else {
        ui.combo("Add", &settingsInterfacePick, labels.data(), (int)labels.size());
        if (ui.button("+ Implement interface") && settingsInterfacePick < (int)settingsInterfaceAssets.size())
            implementInterfaceAsset(settingsInterfaceAssets[settingsInterfacePick]);
    }
    if (ui.button("Refresh interface list")) {
        settingsScanFrame = -10000;
        scanInterfaceAssets();
        syncImplementedInterfaces();
        dirty = true;
    }

    ui.setInteractionBlocked(settingsWasBlocked, false);
    if (settingsInterfaceContext >= (int)graph.interfaceAssets.size()) settingsInterfaceContext = -1;
    if (settingsInterfaceContext >= 0) {
        Renderer* renderer = ui.r;
        const float menuWidth = 210.0f, menuHeight = 30.0f;
        float menuX = clampf(settingsInterfaceContextX, 2.0f, (float)renderer->width() - menuWidth - 2.0f);
        float menuY = clampf(settingsInterfaceContextY, 2.0f, (float)renderer->height() - menuHeight - 2.0f);
        UIRect menu{menuX, menuY, menuWidth, menuHeight};
        bool inside = settingsInput.mouseX >= menu.x && settingsInput.mouseX < menu.x + menu.w &&
                      settingsInput.mouseY >= menu.y && settingsInput.mouseY < menu.y + menu.h;
        renderer->setUIScissor(0, 0, 0, 0, false);
        renderer->drawRectPx(menu.x + 3, menu.y + 4, menu.w, menu.h, { 0, 0, 0 }, .35f);
        renderer->drawRectPx(menu.x, menu.y, menu.w, menu.h, inside ? Vec3{ .18f, .29f, .45f } : Vec3{ .09f, .10f, .12f }, 1);
        renderer->drawTextLine(menu.x + 12, menu.y + 6, "Remove interface", inside ? Vec3{ 1, .88f, .88f } : Vec3{ .88f, .82f, .84f }, 1);
        ui.registerBlockingRect(menu);
        if (inside && settingsInput.mousePressed) {
            const std::string relativePath = graph.interfaceAssets[settingsInterfaceContext];
            std::string question = "Remove the implemented interface '" + relativePath + "'?\n\nThe implementation code will be preserved.";
            if (MessageBoxA((HWND)hwnd, question.c_str(), "Confirm interface removal",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                graph.interfaceAssets.erase(graph.interfaceAssets.begin() + settingsInterfaceContext);
                syncImplementedInterfaces();
                if (settingsImplementedPick >= (int)graph.interfaceAssets.size()) settingsImplementedPick = 0;
                dirty = true;
            }
            settingsInterfaceContext = -1;
        } else if ((settingsInput.mousePressed || settingsInput.rmbPressed) && !inside) {
            settingsInterfaceContext = -1;
        }
        ui.reclipPanel();
    }
}

static int bpTextIndexAt(Renderer* r,const char* text,float localX,float scale){
    if(localX<=0)return 0;float x=0;int len=(int)strlen(text);
    for(int i=0;i<len;i++){char one[2]={text[i],0};float w=r->textWidth(one,scale);if(localX<x+w*.5f)return i;x+=w;}
    return len;
}
static bool bpEraseSelection(char* text,int& len,int& cursor,int& anchor){
    if(cursor==anchor)return false;int a=std::min(cursor,anchor),b=std::max(cursor,anchor);
    memmove(text+a,text+b,(size_t)(len-b+1));len-=b-a;cursor=anchor=a;return true;
}

void BPEditor::draw(UI& ui) {
    const bool signatureOnly = isInterfaceAsset();
    if (signatureOnly) {
        normalizeInterfaceAsset();
        paletteOpen = false;
        palLinkMode = false;
        ctxKind = 0;
        wiring = false;
        dragNode = 0;
        litDragging = false;
        dragComment = resizeComment = -1;
        varMenuOpen = false;
    }
    const std::string historyBefore = graph.serialize();
    Renderer* r = ui.r;
    r_ = r;
    frame_++;
    const UIInput& in = ui.input();

    // "press a key...": bind the next supported key to the listening node
    if (keyListenNode) {
        if (keyListenNode != selNode || in.keyEscape) {
            keyListenNode = 0;
        } else if (in.keyPressedVK) {
            int idx = -1;
            for (int i = 0; i < BP_NKEYS; i++) {
                if (BP_KEY_VKS[i] == in.keyPressedVK) idx = i;
            }
            BPNode* ln = canvas().byId(keyListenNode);
            if (ln && idx >= 0) {
                ln->choice = idx;
                dirty = true;
                keyListenNode = 0;
                if (logFn) logFn(1, "InputAction bound to key %s.", BP_KEY_NAMES[idx]);
            } else if (idx < 0 && logFn) {
                logFn(2, "Unsupported key: use letters, numbers, arrows, SPACE, SHIFT or CTRL.");
            }
        }
    }

    // ── icon tab bar ──
    // A Widget Blueprint is not a standalone asset: the .wgt holds the designer
    // tree and this graph together, so saving (and assigning) belongs to the
    // widget editor's own toolbar — never write a separate .bp from here.
    // In a Widget the two toggles live on the widget editor's own tool bar, on the
    // same row as its Save button — no separate row here.
    if (!widgetMode) {
        ui.beginCenteredToolRow(5, 56.0f, 10.0f);
        if (ui.toolIconButton("bp_tool_save", 0, false, "Save the Blueprint", dirty)) {
            char path[MAX_PATH];
            std::string abs = curPath.empty() ? "" : projectDir + "\\" + curPath;
            if (!abs.empty() || doSaveDialog(path)) {
                if (abs.empty()) abs = path;
                if (saveTo(abs) && logFn) logFn(1, "Blueprint saved: %s", curPath.c_str());
            }
        }
        if (ui.toolIconButton("bp_tool_save_as", 1, false, "Save the Blueprint under a new name")) {
            char path[MAX_PATH];
            if (doSaveDialog(path) && saveTo(path) && logFn) logFn(1, "Blueprint saved: %s", curPath.c_str());
        }
        if (ui.toolIconButton("bp_tool_assign", 2, false, signatureOnly ? "Interfaces are not assigned to objects" : "Assign the Blueprint to the selected object") && !signatureOnly)
            assignRequested = true;
        if (ui.toolIconButton("bp_tool_panels", 3, showVars, "Shows or hides the Blueprint panels")) showVars = !showVars;
        if (ui.toolIconButton("bp_tool_settings", 4, settingsOpen, "Opens or closes the Blueprint Settings")) {
            settingsOpen = !settingsOpen;
            if (settingsOpen) showVars = true;
        }
        ui.endCenteredToolRow();
    }

    // header row for the current function / extra event graph. The function name is
    // no longer editable here: renaming happens from the My Blueprint right-click menu.
    if (curFunc >= 0 && curFunc < (int)graph.funcs.size()) {
        std::string interfaceOrigin = signatureOnly ? std::string{} :
            bpInterfaceOriginForFunction(projectDir, graph, graph.funcs[curFunc].name);
        if (!interfaceOrigin.empty()) {
            ui.label(std::string(graph.funcs[curFunc].name) + "  [Interface: " +
                     fs::path(interfaceOrigin).stem().string() + "]", { .68f, .82f, 1.0f });
        } else {
            ui.label(std::string("Function: ") + graph.funcs[curFunc].name +
                     "   (rename: right-click in the panel)", { .62f, .70f, .82f });
        }
    } else if (curGraph > 0 && curGraph < (int)graph.graphs.size() &&
               strcmp(graph.graphs[curGraph].name, "ConstructionScript") != 0) {
        ui.row(2);
        if (ui.textInput("grname", graph.graphs[curGraph].name, sizeof(graph.graphs[curGraph].name))) {
            for (char* c = graph.graphs[curGraph].name; *c; c++) if (*c == ' ') *c = '_';
            dirty = true;
        }
        ui.label("current graph name", { 0.5f, 0.54f, 0.6f });
    }

    char info[200];
    snprintf(info, sizeof(info), "%s%s | %s | nodes %d, vars %d, fns %d | RMB: palette | Del: delete",
             widgetMode ? "graph of the widget" : curPath.empty() ? "(unsaved)" : curPath.c_str(), dirty ? " *" : "",
             curFunc >= 0 && curFunc < (int)graph.funcs.size() ? graph.funcs[curFunc].name
                                                              : graph.graphs[curGraph < (int)graph.graphs.size() ? curGraph : 0].name,
             (int)canvas().nodes.size(), (int)graph.vars.size(), (int)graph.funcs.size());
    ui.label(info, { 0.55f, 0.59f, 0.66f });

    // ── layout: My Blueprint column + canvas + details column (right) ──
    // the details column shows the selected node's settings, or else the variable's
    BPNode* selPtr0 = selNode ? canvas().byId(selNode) : nullptr;
    bool hasInputValues = false;
    if (selPtr0 && !isReroute(selPtr0->def)) {
        bool anyObjVar = false;
        for (const BPVarDef& v : graph.vars) if (v.type == PIN_ENT && v.container == VC_SINGLE) { anyObjVar = true; break; }
        BPNodeDef selectedDef = effDef(*selPtr0);
        for (int p = 0; p < selectedDef.nIns; p++) {
            PinKind ek = editorKind(*selPtr0, selectedDef, p);
            if (litEditable(ek) || (ek == PIN_ENT && anyObjVar)) { hasInputValues = true; break; }
        }
    }
    bool nodeDetails = selPtr0 && !isReroute(selPtr0->def) &&
                       (DEFS[selPtr0->def].propKind != 0 || DEFS[selPtr0->def].usesName ||
                        selPtr0->def == BP_FN_ENTRY || selPtr0->def == BP_FN_RETURN ||
                        selPtr0->def == BP_TRACE_LINE || selPtr0->def == BP_TRACE_SPHERE ||
                        selPtr0->def == BP_SPAWN_PREFAB || selPtr0->def == BP_SELECT_ENUM ||
                        selPtr0->def == BP_SWITCH_ENUM || selPtr0->def == BP_CAST_TO_CLASS || hasInputValues);
    bool commentDetails = selComment >= 0 && selComment < (int)canvas().comments.size();
    bool dispatcherDetails=selDispatcher>=0&&selDispatcher<(int)graph.dispatchers.size();
    bool detailsOpen = showVars && (settingsOpen || commentDetails || nodeDetails || dispatcherDetails || (selVar >= 0 && selVar < (int)graph.vars.size()));
    UIRect cv;
    if (showVars) {
        // draggable splitters: left (My Blueprint | canvas) and right (canvas | details)
        UIRect pf = ui.panelInner();
        float topY = ui.panelCursorY();
        float lsx = pf.x + varColW + 2;
        float rsx = pf.x + pf.w - detColW - 4;
        bool ctxAny = paletteOpen || ctxKind != 0 || varMenuOpen || mbMenuKind != 0 || mbRenameKind != 0;
        bool overL = !ctxAny && !ui.interactionBlocked() &&
            in.mouseX >= lsx - 3 && in.mouseX <= lsx + 4 && in.mouseY >= topY && in.mouseY < pf.y + pf.h;
        bool overR = detailsOpen && !ctxAny && !ui.interactionBlocked() &&
            in.mouseX >= rsx - 3 && in.mouseX <= rsx + 4 && in.mouseY >= topY && in.mouseY < pf.y + pf.h;
        if (overL && in.mousePressed) dragVarCol = true;
        if (overR && in.mousePressed && !dragVarCol) dragDetCol = true;
        if (dragVarCol) {
            varColW = clampf(in.mouseX - pf.x - 2, 150, pf.w * 0.45f);
            if (!in.mouseDown) dragVarCol = false;
        }
        if (dragDetCol) {
            detColW = clampf(pf.x + pf.w - in.mouseX - 4, 170, pf.w * 0.45f);
            if (!in.mouseDown) dragDetCol = false;
        }

        ui.beginColumns(varColW, detailsOpen ? detColW : 0.0f);
        // Both side columns scroll on their own: the panel can only scroll as a
        // whole, which would drag the canvas along with them.
        {
            float colTop = ui.panelCursorY();
            UIRect colRc = { pf.x + 1, colTop, varColW + 2, pf.y + pf.h - colTop - 2 };
            ui.beginScrollRegion("bp_myblueprint", colRc);
            drawMyBlueprint(ui);
            ui.endScrollRegion();
        }
        ui.nextColumn();
        ui.reclipPanel();
        UIRect pin = ui.panelInner();  // during columns: current column rect
        float top = ui.panelCursorY();
        cv = { pin.x, top, pin.w - 8, pin.y + pin.h - top - 6 };
        ui.spacing(cv.h > 40 ? cv.h : 40);
        if (detailsOpen) {
            ui.nextColumn();
            float detTop = ui.panelCursorY();
            UIRect detRc = { rsx + 3, detTop, pf.x + pf.w - rsx - 4, pf.y + pf.h - detTop - 2 };
            ui.beginScrollRegion("bp_details", detRc);
            if (settingsOpen) drawBlueprintSettings(ui);
            else if (commentDetails) drawCommentDetails(ui);
            else if (nodeDetails) drawNodeDetails(ui);
            else if(dispatcherDetails)drawDispatcherDetails(ui);
            else drawVarDetails(ui);
            ui.endScrollRegion();
        }
        // canvas drawn below after endColumns (coordinates already fixed)
        UIRect cvCopy = cv;
        ui.endColumns();
        ui.reclipPanel();
        cv = cvCopy;

        if (overL || dragVarCol) r->drawRectPx(lsx - 1, topY, 3, pf.y + pf.h - topY, { 0.30f, 0.62f, 0.99f }, 0.8f);
        if (overR || dragDetCol) r->drawRectPx(rsx - 1, topY, 3, pf.y + pf.h - topY, { 0.30f, 0.62f, 0.99f }, 0.8f);
    } else {
        UIRect pin = ui.panelInner();
        float top = ui.panelCursorY();
        cv = { pin.x + 4, top, pin.w - 12, pin.y + pin.h - top - 6 };
        ui.extendContent(cv.y + cv.h);
    }
    if (cv.h < 60) { r_ = nullptr; finishHistoryFrame(historyBefore, in.mouseDown); return; }

    BPCanvas& C = canvas();
    bool mouseInCanvas = !ui.interactionBlocked() &&
        in.mouseX >= cv.x && in.mouseX < cv.x + cv.w && in.mouseY >= cv.y && in.mouseY < cv.y + cv.h;

    // ── mouse-wheel zoom, keeping the point under the cursor fixed ──
    if (mouseInCanvas && !paletteOpen && in.wheel != 0) {
        float oldZ = zoom;
        zoom = clampf(zoom * powf(1.12f, in.wheel), 0.35f, 2.5f);
        float wx = (in.mouseX - (cv.x + panX)) / oldZ;
        float wy = (in.mouseY - (cv.y + panY)) / oldZ;
        panX = in.mouseX - cv.x - wx * zoom;
        panY = in.mouseY - cv.y - wy * zoom;
        ui.consumeWheel();   // zoom only — don't also scroll the enclosing panel
    }
    float Z = zoom;
    float ox = cv.x + panX, oy = cv.y + panY;

    // clip everything the canvas draws (nodes, wires, menus) to its rectangle
    r->setUIScissor(cv.x, cv.y, cv.w, cv.h, true);
    r->drawRectPx(cv.x, cv.y, cv.w, cv.h, { 0.07f, 0.078f, 0.09f }, 1);
    float gstep = 28 * Z;
    for (float gy = cv.y + fmodf(panY, gstep); gy < cv.y + cv.h; gy += gstep) {
        for (float gx = cv.x + fmodf(panX, gstep); gx < cv.x + cv.w; gx += gstep) {
            if (gx >= cv.x && gy >= cv.y) r->drawRectPx(gx, gy, 2, 2, { 0.14f, 0.155f, 0.18f }, 1);
        }
    }

    // ── interactions ──
    bool menuBlocked = paletteOpen || ctxKind != 0 || varMenuOpen || mbMenuKind != 0 || mbRenameKind != 0;
    bool litClickInside = false;
    float activeLitX=0,activeLitY=0,activeLitW=0;
    BPNode* activeLitNode=litEditNode?C.byId(litEditNode):nullptr;
    if(activeLitNode){
        BPNodeDef ad=effDef(*activeLitNode);PinKind ak=editorKind(*activeLitNode,ad,litEditPin);float px,py;
        pinPos(*activeLitNode,ox,oy,litEditPin,false,&px,&py);float bw=literalBoxWidth(*activeLitNode,litEditPin,ak)*Z;
        activeLitX=px+litOffset(ad,litEditPin)*Z+(ak==PIN_STR?0:litEditComp*(bw+4*Z));activeLitY=py;activeLitW=bw;
        litClickInside=in.mouseX>=activeLitX&&in.mouseX<activeLitX+activeLitW&&fabsf(in.mouseY-activeLitY)<9*Z;
    }
    if (in.mousePressed && litEditNode) {
        BPNode* n = activeLitNode;
        if (n && litClickInside) {
            int len=(int)strlen(litEditBuf);int cursor=bpTextIndexAt(r,litEditBuf,in.mouseX-(activeLitX+3*Z),Z);
            bool dbl=litLastClickNode==litEditNode&&litLastClickPin==litEditPin&&litLastClickComp==litEditComp&&frame_-litLastClickFrame<=18;
            litLastClickNode=litEditNode;litLastClickPin=litEditPin;litLastClickComp=litEditComp;litLastClickFrame=frame_;
            if(dbl){litEditAnchor=0;litEditCursor=len;litEditSelecting=false;}else{litEditCursor=litEditAnchor=cursor;litEditSelecting=true;}
        } else if (n) {
            BPNodeDef dd = effDef(*n);
            PinKind ek = editorKind(*n, dd, litEditPin);
            if (ek == PIN_STR) {
                n->slit[litEditPin] = litEditBuf;
            } else {
                float v = (float)atof(litEditBuf);
                if (ek == PIN_INT || ek == PIN_ENUM) v = floorf(v + 0.5f);
                (&n->lit[litEditPin].x)[litEditComp] = v;
            }
            dirty = true;
            litEditNode = 0;
        }
    }
    if(litEditSelecting&&litEditNode&&(in.mouseDown||in.mouseReleased))
        litEditCursor=bpTextIndexAt(r,litEditBuf,in.mouseX-(activeLitX+3*Z),Z);
    if(in.mouseReleased)litEditSelecting=false;
    if (mouseInCanvas && in.mousePressed && !menuBlocked && !litClickInside) {
        bool hit = false;
        for (auto& n : C.nodes) {
            if (signatureOnly) break;
            BPNodeDef d = effDef(n);
            for (int side = 0; side < 2 && !hit; side++) {
                int count = side ? d.nOuts : d.nIns;
                for (int p = 0; p < count; p++) {
                    float px, py;
                    pinPos(n, ox, oy, p, side == 1, &px, &py);
                    if (fabsf(in.mouseX - px) < 8 && fabsf(in.mouseY - py) < 8) {
                        if (in.keyAlt) {
                            // Alt+click: break every wire on this pin
                            disconnectPin(n.id, p, side == 1);
                        } else {
                            BPLink moved;
                            bool detached = in.keyCtrl && C.detachLinkAtPin(n.id, p, side == 1, moved);
                            wiring = true;
                            if (detached) {
                                // Ctrl is only needed for this initial detach.
                                // Keep the opposite endpoint fixed and drag the
                                // released end to any compatible pin.
                                wireKind = bpEffKind(C, graph, moved.fromNode, moved.fromPin, true, 0);
                                if (side == 1) {
                                    wireNode = moved.toNode;
                                    wirePin = moved.toPin;
                                    wireFromOut = false; // choose a replacement output
                                } else {
                                    wireNode = moved.fromNode;
                                    wirePin = moved.fromPin;
                                    wireFromOut = true;  // choose a replacement input
                                }
                                dirty = true;
                            } else {
                                wireNode = n.id;
                                wirePin = p;
                                wireFromOut = side == 1;
                                wireKind = side ? d.outs[p].kind : d.ins[p].kind;
                            }
                        }
                        hit = true;
                        break;
                    }
                }
            }
            if (hit) break;
        }
        if (!hit && !signatureOnly) {
            for (auto& n : C.nodes) {
                BPNodeDef d = effDef(n);
                for (int p = 0; p < d.nIns && !hit; p++) {
                    PinKind ek = editorKind(n, d, p);
                    if (!litEditable(ek) || C.linkInto(n.id, p)) continue;
                    float px, py;
                    pinPos(n, ox, oy, p, false, &px, &py);
                    float off = litOffset(d, p) * Z;
                    if (ek == PIN_BOOL) {
                        // checkbox: click = toggle (nessun drag)
                        float cs = 14 * Z, cbx = px + off, cby = py - 7 * Z;
                        if (in.mouseX >= cbx && in.mouseX < cbx + cs && in.mouseY >= cby && in.mouseY < cby + cs) {
                            n.lit[p].x = n.lit[p].x != 0 ? 0.0f : 1.0f;
                            dirty = true;
                            hit = true;
                        }
                        continue;
                    }
                    if (ek == PIN_STR) {
                        // click sul box di testo = entra in modalita' scrittura
                        float bw = literalBoxWidth(n, p, ek) * Z, bx = px + off;
                        if (in.mouseX >= bx && in.mouseX < bx + bw && fabsf(in.mouseY - py) < 9) {
                            litEditNode = n.id;
                            litEditPin = p;
                            litEditComp = 0;
                            snprintf(litEditBuf, sizeof(litEditBuf), "%s", n.slit[p].c_str());
                            litEditCursor=litEditAnchor=bpTextIndexAt(r,litEditBuf,in.mouseX-(bx+3*Z),Z);
                            litEditSelecting=true;
                            litLastClickNode=n.id;litLastClickPin=p;litLastClickComp=0;litLastClickFrame=frame_;
                            hit = true;
                        }
                        continue;
                    }
                    if (bpWidgetEnumLabel(n.def, ek, (int)n.lit[p].x)) {
                        // built-in enum: click cycles to the next value (the full
                        // list is a drop-down in the node Details)
                        float bw = literalBoxWidth(n, p, ek) * Z, bx = px + off;
                        if (in.mouseX >= bx && in.mouseX < bx + bw && fabsf(in.mouseY - py) < 9) {
                            int count = bpWidgetEnumCount(n.def);
                            n.lit[p].x = (float)(((int)n.lit[p].x + 1) % count);
                            dirty = true;
                            hit = true;
                        }
                        continue;
                    }
                    int comps = litComps(ek);
                    float bw = literalBoxWidth(n, p, ek) * Z;
                    for (int c = 0; c < comps; c++) {
                        float bx = px + off + c * (bw + 4 * Z);
                        if (in.mouseX >= bx && in.mouseX < bx + bw && fabsf(in.mouseY - py) < 8) {
                            // press starts a drag; a click without dragging types
                            litDragging = true;
                            litMoved = 0;
                            litNode = n.id;
                            litPin = p;
                            litComp = c;
                            hit = true;
                        }
                    }
                }
                if (hit) break;
            }
        }
        if (!hit) {
            for (int i = (int)C.nodes.size() - 1; i >= 0; i--) {
                BPNode& n = C.nodes[i];
                float w, h;
                nodeRect(n, &w, &h);
                float nx = ox + n.x * Z, ny = oy + n.y * Z;
                // Sequence "+" button: add an exec output
                float bx, by, bw, bh;
                if (seqPlusRect(n, ox, oy, w, Z, &bx, &by, &bw, &bh) &&
                    in.mouseX >= bx && in.mouseX < bx + bw && in.mouseY >= by && in.mouseY < by + bh) {
                    n.choice = seqCount(n) + 1;
                    selNode = n.id;
                    selVar = -1;
                    selDispatcher = -1;
                    selComment = -1;
                    dirty = true;
                    hit = true;
                    break;
                }
                if (in.mouseX >= nx && in.mouseX < nx + w && in.mouseY >= ny && in.mouseY < ny + h) {
                    selNode = n.id;
                    selVar = -1;   // selezionare un nodo toglie il focus dalla variabile
                    selDispatcher = -1;
                    selComment = -1;
                    if (!selSet.count(n.id)) {
                        selSet.clear();
                        selSet.insert(n.id);
                    }
                    if (!signatureOnly) {
                        dragNode = n.id;
                        dragOffX = in.mouseX - nx;
                        dragOffY = in.mouseY - ny;
                    }
                    hit = true;
                    break;
                }
            }
        }
        if (!hit && !signatureOnly) {
            // commenti: barra titolo = seleziona/sposta, angolo = ridimensiona
            for (int i = (int)C.comments.size() - 1; i >= 0 && !hit; i--) {
                BPComment& c = C.comments[i];
                float cx = ox + c.x * Z, cy = oy + c.y * Z, cw = c.w * Z, ch = c.h * Z;
                float titleH = commentTitleHeight(c, Z), handle = 14 * Z;
                if (in.mouseX >= cx + cw - handle && in.mouseX < cx + cw && in.mouseY >= cy + ch - handle && in.mouseY < cy + ch) {
                    selComment = i; selNode = 0; selVar = -1; selDispatcher = -1; selSet.clear();
                    resizeComment = i;
                    hit = true;
                } else if (in.mouseX >= cx && in.mouseX < cx + cw && in.mouseY >= cy && in.mouseY < cy + titleH) {
                    selComment = i; selNode = 0; selVar = -1; selDispatcher = -1; selSet.clear();
                    dragComment = i;
                    dragCOffX = in.mouseX - cx;
                    dragCOffY = in.mouseY - cy;
                    hit = true;
                }
            }
        }
        if (!hit) {
            // double click on a wire inserts a reroute node
            bool wireHit = false;
            bool dbl = (frame_ - lastCanvasClickF) < 22 &&
                       fabsf(in.mouseX - lastCanvasClickX) < 8 && fabsf(in.mouseY - lastCanvasClickY) < 8;
            if (dbl && !signatureOnly) {
                for (size_t li = 0; li < C.links.size() && !wireHit; li++) {
                    const BPLink l = C.links[li];
                    const BPNode* a = C.byId(l.fromNode);
                    const BPNode* b = C.byId(l.toNode);
                    if (!a || !b) continue;
                    float x1, y1, x2, y2;
                    pinPos(*a, ox, oy, l.fromPin, true, &x1, &y1);
                    pinPos(*b, ox, oy, l.toPin, false, &x2, &y2);
                    float px = x1, py = y1;
                    float coff = clampf(fabsf(x2 - x1) * 0.5f + fabsf(y2 - y1) * 0.15f, 16.0f * Z, 220.0f * Z);
                    float d1 = pinTangentDir(*a, true);
                    float d2 = pinTangentDir(*b, false);
                    for (int s = 1; s <= 14 && !wireHit; s++) {
                        float t = s / 14.0f, mt = 1 - t;
                        float bx = mt * mt * mt * x1 + 3 * mt * mt * t * (x1 + d1 * coff) + 3 * mt * t * t * (x2 + d2 * coff) + t * t * t * x2;
                        float by = mt * mt * mt * y1 + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t * y2;
                        // distance from mouse to the [px,py]-[bx,by] segment
                        float dx = bx - px, dy = by - py;
                        float len2 = dx * dx + dy * dy;
                        float tt = len2 > 0 ? ((in.mouseX - px) * dx + (in.mouseY - py) * dy) / len2 : 0;
                        tt = clampf(tt, 0, 1);
                        float qx = px + dx * tt - in.mouseX, qy = py + dy * tt - in.mouseY;
                        if (qx * qx + qy * qy < 36) {
                            PinKind k = bpEffKind(C, graph, a->id, l.fromPin, true, 0);
                            int rid = C.addNode(k == PIN_EXEC ? BP_REROUTE_EX : BP_REROUTE,
                                                (in.mouseX - ox) / Z - 15, (in.mouseY - oy) / Z - 10);
                            C.links.erase(C.links.begin() + li);
                            C.connect(l.fromNode, l.fromPin, rid, 0);
                            C.connect(rid, 0, l.toNode, l.toPin);
                            selNode = rid;
                            selSet.clear();
                            selSet.insert(rid);
                            dirty = true;
                            wireHit = true;
                        }
                        px = bx;
                        py = by;
                    }
                }
            }
            if (!wireHit) {
                // empty canvas: start a marquee selection (pan is on the right button)
                selNode = 0;
                selSet.clear();
                selComment = -1;
                selecting = true;
                selX0 = in.mouseX;
                selY0 = in.mouseY;
            }
            lastCanvasClickF = frame_;
            lastCanvasClickX = in.mouseX;
            lastCanvasClickY = in.mouseY;
        }
        lastMX = in.mouseX;
        lastMY = in.mouseY;
    }
    if (paletteOpen && in.mousePressed) {
        float palH = cv.h < 480 ? cv.h - 10 : 480;
        bool insidePal = in.mouseX >= palX && in.mouseX < palX + 340 &&
                         in.mouseY >= palY && in.mouseY < palY + palH;
        if (!insidePal) { paletteOpen = false; palLinkMode = false; }
    }

    if (in.mouseDown && !signatureOnly) {
        if (dragNode) {
            // move on a grid: snap the dragged node, shift the rest of the selection
            // by the same (snapped) delta so their relative layout is preserved
            BPNode* dn = C.byId(dragNode);
            if (dn) {
                float wantX = snapGrid((in.mouseX - dragOffX - ox) / Z);
                float wantY = snapGrid((in.mouseY - dragOffY - oy) / Z);
                float dx = wantX - dn->x, dy = wantY - dn->y;
                if (dx != 0 || dy != 0) {
                    for (auto& n : C.nodes) {
                        if (selSet.count(n.id) || n.id == dragNode) {
                            n.x += dx;
                            n.y += dy;
                        }
                    }
                    dirty = true;
                }
            }
        } else if (litDragging) {
            BPNode* n = C.byId(litNode);
            if (n) {
                float dx = (in.mouseX - lastMX);
                litMoved += fabsf(dx);
                if (litPin < 0) n->prop += dx * 0.05f;
                else (&n->lit[litPin].x)[litComp] += dx * 0.05f;
                if (dx != 0) dirty = true;
            }
        } else if (dragComment >= 0 && dragComment < (int)C.comments.size()) {
            BPComment& c = C.comments[dragComment];
            c.x = snapGrid((in.mouseX - dragCOffX - ox) / Z);
            c.y = snapGrid((in.mouseY - dragCOffY - oy) / Z);
            dirty = true;
        } else if (resizeComment >= 0 && resizeComment < (int)C.comments.size()) {
            BPComment& c = C.comments[resizeComment];
            c.w = clampf((in.mouseX - ox) / Z - c.x, 80, 4000);
            c.h = clampf((in.mouseY - oy) / Z - c.y, 44, 4000);
            dirty = true;
        }
        lastMX = in.mouseX;
        lastMY = in.mouseY;
    }
    // panning with the RIGHT button (Unreal-style); a short click still opens menus
    if (mouseInCanvas && in.rmbPressed && !menuBlocked) panning = true;
    if (panning) {
        if (in.rmbDown) {
            panX += in.mouseX - rmbLastX;
            panY += in.mouseY - rmbLastY;
        } else {
            panning = false;
        }
    }
    rmbLastX = in.mouseX;
    rmbLastY = in.mouseY;
    if (in.mouseReleased) {
        if (wiring && !signatureOnly) {
            bool connected = false, nearAnyPin = false;
            for (auto& n : C.nodes) {
                BPNodeDef d = effDef(n);
                int side = wireFromOut ? 0 : 1;
                int count = side ? d.nOuts : d.nIns;
                for (int p = 0; p < count; p++) {
                    float px, py;
                    pinPos(n, ox, oy, p, side == 1, &px, &py);
                    if (fabsf(in.mouseX - px) < 9 && fabsf(in.mouseY - py) < 9) {
                        nearAnyPin = true;
                        PinKind k = side ? d.outs[p].kind : d.ins[p].kind;
                        if (pinsCompatible(wireKind, k) && n.id != wireNode) {
                            int fromN = wireFromOut ? wireNode : n.id, fromP = wireFromOut ? wirePin : p;
                            int toN = wireFromOut ? n.id : wireNode, toP = wireFromOut ? p : wirePin;
                            // A delegate is not just "a delegate": the Custom Event
                            // behind it has to match the Dispatcher's signature, or
                            // the bind silently does nothing at runtime.
                            std::string reject;
                            if (!delegateLinkAllowed(C, fromN, fromP, toN, toP, reject)) {
                                if (logFn) logFn(2, "%s", reject.c_str());
                                nearAnyPin = true;
                                continue;
                            }
                            C.connect(fromN, fromP, toN, toP);
                            connected = true;
                            dirty = true;
                        }
                    }
                }
            }
            // dropped on empty canvas: context palette with only the nodes that can
            // attach to this pin; picking one wires it up automatically
            if (!connected && !nearAnyPin && mouseInCanvas) {
                paletteOpen = true;
                palScroll = 0;
                palSearch[0] = 0;
                palCatOpen = 0;
                palLinkMode = true;
                palLinkNode = wireNode;
                palLinkPin = wirePin;
                palLinkOut = wireFromOut;
                palLinkKind = wireKind;
                palLinkRefClass = wireFromOut ? bpPinRefClass(C, graph, wireNode, wirePin, true) : std::string{};
                palX = in.mouseX;
                palY = in.mouseY;
                palWX = (in.mouseX - ox) / Z;
                palWY = (in.mouseY - oy) / Z;
                float palH = cv.h < 480 ? cv.h - 10 : 480;
                if (palY + palH > cv.y + cv.h) palY = cv.y + cv.h - palH;
                if (palX + 340 > cv.x + cv.w) palX = cv.x + cv.w - 340;
            }
        }
        if (selecting) {
            // marquee: select every node intersecting the rectangle
            float x0 = selX0 < in.mouseX ? selX0 : in.mouseX;
            float x1 = selX0 < in.mouseX ? in.mouseX : selX0;
            float y0 = selY0 < in.mouseY ? selY0 : in.mouseY;
            float y1 = selY0 < in.mouseY ? in.mouseY : selY0;
            selSet.clear();
            for (const auto& n : C.nodes) {
                float w, h;
                nodeRect(n, &w, &h);
                float nx = ox + n.x * Z, ny = oy + n.y * Z;
                if (nx + w >= x0 && nx <= x1 && ny + h >= y0 && ny <= y1) selSet.insert(n.id);
            }
            selNode = selSet.size() == 1 ? *selSet.begin() : 0;
            selecting = false;
        }
        if (litDragging && litMoved < 3 && litPin >= 0) {
            // click without drag → type the value with the keyboard
            BPNode* n = C.byId(litNode);
            if (n) {
                litEditNode = litNode;
                litEditPin = litPin;
                litEditComp = litComp;
                snprintf(litEditBuf, sizeof(litEditBuf), "%g", (&n->lit[litPin].x)[litComp]);
                BPNodeDef dd=effDef(*n);PinKind ek=editorKind(*n,dd,litPin);float px,py;pinPos(*n,ox,oy,litPin,false,&px,&py);
                float bw=literalBoxWidth(*n,litPin,ek)*Z,bx=px+litOffset(dd,litPin)*Z+litComp*(bw+4*Z);
                litEditCursor=litEditAnchor=bpTextIndexAt(r,litEditBuf,in.mouseX-(bx+3*Z),Z);
                litLastClickNode=n->id;litLastClickPin=litPin;litLastClickComp=litComp;litLastClickFrame=frame_;
            }
        }
        wiring = false;
        dragNode = 0;
        litDragging = false;
        dragComment = -1;
        resizeComment = -1;
    }
    // keyboard editing of a literal value box (commit on click handled above)
    if (litEditNode) {
        BPNode* n = C.byId(litEditNode);
        if (!n) {
            litEditNode = 0;
        } else {
            BPNodeDef dd = effDef(*n);
            PinKind ek = editorKind(*n, dd, litEditPin);
            int len = (int)strlen(litEditBuf);
            if(in.keySelectAll){litEditAnchor=0;litEditCursor=len;}
            if(in.keyLeft){if(!in.keyShift&&litEditCursor!=litEditAnchor)litEditCursor=std::min(litEditCursor,litEditAnchor);else litEditCursor=std::max(0,litEditCursor-1);if(!in.keyShift)litEditAnchor=litEditCursor;}
            if(in.keyRight){if(!in.keyShift&&litEditCursor!=litEditAnchor)litEditCursor=std::max(litEditCursor,litEditAnchor);else litEditCursor=std::min(len,litEditCursor+1);if(!in.keyShift)litEditAnchor=litEditCursor;}
            // Ctrl+C: copy the selection to the OS clipboard; Ctrl+V: paste at the cursor
            if (in.keyCopy && litEditCursor != litEditAnchor) {
                int a = std::min(litEditCursor, litEditAnchor), b = std::max(litEditCursor, litEditAnchor);
                ui.requestCopyText(std::string(litEditBuf + a, litEditBuf + b));
            }
            if (in.keyPaste && !ui.pasteText().empty()) {
                bpEraseSelection(litEditBuf, len, litEditCursor, litEditAnchor);
                bool isInt = ek == PIN_INT || ek == PIN_ENUM;
                for (char ch : ui.pasteText()) {
                    if (ch < 32 || ch >= 127 || len >= (int)sizeof(litEditBuf) - 1) continue;
                    if (ek != PIN_STR) {
                        bool ok = (ch >= '0' && ch <= '9') || ch == '-' || (!isInt && (ch == '.' || ch == ','));
                        if (!ok) continue;
                        if (ch == ',') ch = '.';
                    }
                    memmove(litEditBuf + litEditCursor + 1, litEditBuf + litEditCursor, (size_t)(len - litEditCursor + 1));
                    litEditBuf[litEditCursor++] = ch; len++; litEditAnchor = litEditCursor;
                }
            }
            if (ek == PIN_STR) {
                // stringa: accetta qualsiasi carattere stampabile
                for (int i = 0; i < in.typedCount; i++) {
                    char ch = in.typed[i];
                    if (ch >= 32 && ch < 127 && len < (int)sizeof(litEditBuf) - 1) {
                        bpEraseSelection(litEditBuf,len,litEditCursor,litEditAnchor);
                        memmove(litEditBuf+litEditCursor+1,litEditBuf+litEditCursor,(size_t)(len-litEditCursor+1));
                        litEditBuf[litEditCursor++]=ch;len++;litEditAnchor=litEditCursor;
                    }
                }
                if(in.keyBackspace){if(!bpEraseSelection(litEditBuf,len,litEditCursor,litEditAnchor)&&litEditCursor>0){memmove(litEditBuf+litEditCursor-1,litEditBuf+litEditCursor,(size_t)(len-litEditCursor+1));litEditCursor--;litEditAnchor=litEditCursor;len--;}}
                if(in.keyDelete){if(!bpEraseSelection(litEditBuf,len,litEditCursor,litEditAnchor)&&litEditCursor<len){memmove(litEditBuf+litEditCursor,litEditBuf+litEditCursor+1,(size_t)(len-litEditCursor));len--;}}
                if (in.keyEnter) { n->slit[litEditPin] = litEditBuf; dirty = true; litEditNode = 0; }
                else if (in.keyEscape) litEditNode = 0;
            } else {
                bool isInt = ek == PIN_INT || ek == PIN_ENUM;
                for (int i = 0; i < in.typedCount; i++) {
                    char ch = in.typed[i];
                    bool ok = (ch >= '0' && ch <= '9') || ch == '-' || (!isInt && (ch == '.' || ch == ','));
                    if (ok && len < (int)sizeof(litEditBuf) - 1) {
                        bpEraseSelection(litEditBuf,len,litEditCursor,litEditAnchor);
                        memmove(litEditBuf+litEditCursor+1,litEditBuf+litEditCursor,(size_t)(len-litEditCursor+1));
                        litEditBuf[litEditCursor++]=ch==','?'.':ch;len++;litEditAnchor=litEditCursor;
                    }
                }
                if(in.keyBackspace){if(!bpEraseSelection(litEditBuf,len,litEditCursor,litEditAnchor)&&litEditCursor>0){memmove(litEditBuf+litEditCursor-1,litEditBuf+litEditCursor,(size_t)(len-litEditCursor+1));litEditCursor--;litEditAnchor=litEditCursor;len--;}}
                if(in.keyDelete){if(!bpEraseSelection(litEditBuf,len,litEditCursor,litEditAnchor)&&litEditCursor<len){memmove(litEditBuf+litEditCursor,litEditBuf+litEditCursor+1,(size_t)(len-litEditCursor));len--;}}
                if (in.keyEnter) {
                    float v = (float)atof(litEditBuf);
                    if (isInt) v = floorf(v + 0.5f);
                    (&n->lit[litEditPin].x)[litEditComp] = v;
                    dirty = true;
                    litEditNode = 0;
                } else if (in.keyEscape) {
                    litEditNode = 0;
                }
            }
        }
    }
    if (mouseInCanvas && in.rmbPressed) {
        rmbPressX = in.mouseX;
        rmbPressY = in.mouseY;
    }
    if (!signatureOnly && mouseInCanvas && in.rmbReleased && !menuBlocked &&
        fabsf(in.mouseX - rmbPressX) < 6 && fabsf(in.mouseY - rmbPressY) < 6) {
        // right click: pin menu > node menu > palette
        bool onPin = false, onNode = false;
        for (auto& n : C.nodes) {
            BPNodeDef d = effDef(n);
            for (int side = 0; side < 2 && !onPin; side++) {
                int count = side ? d.nOuts : d.nIns;
                for (int p = 0; p < count; p++) {
                    float px, py;
                    pinPos(n, ox, oy, p, side == 1, &px, &py);
                    if (fabsf(in.mouseX - px) < 8 && fabsf(in.mouseY - py) < 8) {
                        ctxKind = 2;
                        ctxNode = n.id;
                        ctxPin = p;
                        ctxPinOut = side == 1;
                        ctxX = in.mouseX;
                        ctxY = in.mouseY;
                        onPin = true;
                        break;
                    }
                }
            }
            if (onPin) break;
        }
        if (!onPin) {
            for (int i = (int)C.nodes.size() - 1; i >= 0; i--) {
                BPNode& n = C.nodes[i];
                float w, h;
                nodeRect(n, &w, &h);
                float nx = ox + n.x * Z, ny = oy + n.y * Z;
                if (in.mouseX >= nx && in.mouseX < nx + w && in.mouseY >= ny && in.mouseY < ny + h) {
                    ctxKind = 1;
                    ctxNode = n.id;
                    ctxX = in.mouseX;
                    ctxY = in.mouseY;
                    selNode = n.id;
                    selVar = -1;
                    selDispatcher = -1;
                    selComment = -1;
                    onNode = true;
                    break;
                }
            }
        }
        if (!onPin && !onNode) {
            paletteOpen = true;
            palScroll = 0;
            palSearch[0] = 0;
            palCatOpen = 0;
            palLinkMode = false;
            palX = in.mouseX;
            palY = in.mouseY;
            palWX = (in.mouseX - ox) / Z;
            palWY = (in.mouseY - oy) / Z;
            float palH = cv.h < 480 ? cv.h - 10 : 480;
            if (palY + palH > cv.y + cv.h) palY = cv.y + cv.h - palH;
            if (palX + 340 > cv.x + cv.w) palX = cv.x + cv.w - 340;
        }
    }
    if (!signatureOnly && in.keyDelete && !wantsTextInput() && (selNode || !selSet.empty())) {
        deleteSelection(C);
    }
    // C: crea una zona di commento (attorno alla selezione, o al mouse)
    if (!signatureOnly && in.keyPressedVK == 'C' && !in.keyCtrl && !wantsTextInput() && !listeningKey() && mouseInCanvas) {
        BPComment c;
        if (!selSet.empty()) {
            float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
            for (auto& n : C.nodes) if (selSet.count(n.id)) {
                float w, h; nodeRect(n, &w, &h);
                if (n.x < minx) minx = n.x;
                if (n.y < miny) miny = n.y;
                if (n.x + w / Z > maxx) maxx = n.x + w / Z;
                if (n.y + h / Z > maxy) maxy = n.y + h / Z;
            }
            c.x = minx - 16; c.y = miny - 34; c.w = (maxx - minx) + 32; c.h = (maxy - miny) + 50;
        } else {
            c.x = snapGrid((in.mouseX - ox) / Z - c.w * 0.5f);
            c.y = snapGrid((in.mouseY - oy) / Z - 10);
        }
        C.comments.push_back(c);
        selComment = (int)C.comments.size() - 1;
        selNode = 0; selVar = -1; selDispatcher = -1; selSet.clear();
        dirty = true;
    }
    // Canc su un commento selezionato lo elimina
    if (!signatureOnly && in.keyDelete && !wantsTextInput() && selComment >= 0 && selComment < (int)C.comments.size()) {
        C.comments.erase(C.comments.begin() + selComment);
        selComment = -1;
        dirty = true;
    }
    // Ctrl+C / Ctrl+V of nodes (skipped while a literal field is being edited: then
    // the copy/paste operates on the field's text instead, handled in the lit editor)
    if (!signatureOnly && in.keyCopy && !wantsTextInput()) copySelection(C);
    if (!signatureOnly && in.keyPaste && !wantsTextInput() && !bpNodeClipboardEmpty()) {
        float wx = (mouseInCanvas ? in.mouseX - ox : cv.x + cv.w * 0.5f - ox) / Z + 30;
        float wy = (mouseInCanvas ? in.mouseY - oy : cv.y + cv.h * 0.5f - oy) / Z + 30;
        pasteClipboard(C, wx, wy);
    }
    // variable drag from the left list → drop on the canvas → Get/Set chooser
    if (!signatureOnly && dragVarIdx >= 0 && in.mouseDown && !dragVarActive &&
        (fabsf(in.mouseX - dragVarX) > 7 || fabsf(in.mouseY - dragVarY) > 7)) {
        dragVarActive = true;
    }
    if (!signatureOnly && in.mouseReleased && dragVarIdx >= 0) {
        if (dragVarActive && mouseInCanvas && dragVarIdx < (int)graph.vars.size()) {
            const BPVarDef& dv = graph.vars[dragVarIdx];
            // dropped ON an existing variable node of the matching container →
            // replace its variable (Unreal-style), instead of opening the chooser
            BPNode* target = nullptr;
            for (int i = (int)C.nodes.size() - 1; i >= 0 && !target; i--) {
                BPNode& n = C.nodes[i];
                std::vector<const char*> opts;
                if (!bpNameOptions(graph, n.def, opts)) continue;
                VarContainer nc = (n.def >= BP_ARR_GET && n.def <= BP_ARR_CLEAR) || n.def == BP_FLOW_FOREACH ? VC_ARRAY
                                : (n.def >= BP_MAP_GET && n.def <= BP_MAP_LEN) ? VC_MAP
                                : (n.def == BP_VAR_GET || n.def == BP_VAR_SET) ? VC_SINGLE
                                : (VarContainer)-1;
                if (nc != dv.container) continue;
                float w, h;
                nodeRect(n, &w, &h);
                float nx = ox + n.x * Z, ny = oy + n.y * Z;
                if (in.mouseX >= nx && in.mouseX < nx + w && in.mouseY >= ny && in.mouseY < ny + h) target = &n;
            }
            if (target) {
                snprintf(target->sname, sizeof(target->sname), "%s", dv.name);
                selNode = target->id;
                dirty = true;
            } else {
                // Unreal-style modifiers: Ctrl → Get, Alt → Set directly; no
                // modifier opens the chooser. Set is skipped where not allowed
                // (pure function body / auto-generated required components).
                bool pureBody = curFunc >= 0 && curFunc < (int)graph.funcs.size() && graph.funcs[curFunc].pure;
                bool allowSet = !pureBody && !dv.requiredGenerated && !dv.widgetGenerated;
                int direct = in.keyCtrl ? 0 : (in.keyAlt && allowSet ? 1 : -1);
                if (direct >= 0) {
                    int id = C.addNode(direct == 0 ? BP_VAR_GET : BP_VAR_SET,
                                       snapGrid((in.mouseX - ox) / Z), snapGrid((in.mouseY - oy) / Z));
                    BPNode* nn = C.byId(id);
                    if (nn) {
                        snprintf(nn->sname, sizeof(nn->sname), "%s", dv.name);
                        if (direct == 1 && dv.type == PIN_COLOR) nn->litAlpha[1] = 1.0f;
                    }
                    selNode = id;
                    dirty = true;
                } else {
                    varMenuOpen = true;
                    varMenuIdx = dragVarIdx;
                    varMenuX = in.mouseX;
                    varMenuY = in.mouseY;
                    varMenuWX = (in.mouseX - ox) / Z;
                    varMenuWY = (in.mouseY - oy) / Z;
                }
            }
        }
        dragVarIdx = dragVarOver = -1;
        dragVarActive = false;
    }
    // ── drag di una funzione dal pannello → crea un nodo Chiama Funzione ──
    if (!signatureOnly && dragFuncIdx >= 0 && in.mouseDown && !dragFuncActive &&
        (fabsf(in.mouseX - dragFuncX) > 7 || fabsf(in.mouseY - dragFuncY) > 7)) {
        dragFuncActive = true;
    }
    if (!signatureOnly && in.mouseReleased && dragFuncIdx >= 0) {
        if (dragFuncActive && mouseInCanvas && dragFuncIdx < (int)graph.funcs.size() &&
            !(curFunc >= 0 && graph.funcs[curFunc].pure && !graph.funcs[dragFuncIdx].pure)) {
            BPNode* timerTarget = nullptr;
            for (int i = (int)C.nodes.size() - 1; i >= 0; i--) {
                BPNode& candidate = C.nodes[i];
                if (candidate.def != BP_TIMER_SET_FUNC) continue;
                float nw, nh; nodeRect(candidate, &nw, &nh);
                float nx = ox + candidate.x * Z, ny = oy + candidate.y * Z;
                if (in.mouseX >= nx && in.mouseX < nx + nw && in.mouseY >= ny && in.mouseY < ny + nh) {
                    timerTarget = &candidate;
                    break;
                }
            }
            int nid = 0;
            if (timerTarget) {
                snprintf(timerTarget->sname, sizeof(timerTarget->sname), "%s", graph.funcs[dragFuncIdx].name);
                nid = timerTarget->id;
            } else {
                nid = C.addNode(BP_CALL_FUNC, snapGrid((in.mouseX - ox) / Z), snapGrid((in.mouseY - oy) / Z));
                snprintf(C.byId(nid)->sname, sizeof(C.byId(nid)->sname), "%s", graph.funcs[dragFuncIdx].name);
            }
            selNode = nid;
            selVar = -1;
            selDispatcher = -1;
            selComment = -1;
            selSet.clear();
            selSet.insert(nid);
            dirty = true;
        }
        dragFuncIdx = dragFuncOver = -1;
        dragFuncActive = false;
    }
    if (in.keyEscape) { paletteOpen = false; palLinkMode = false; ctxKind = 0; varMenuOpen = false; if (mbRenameKind == 0) mbMenuKind = 0; }
    if (paletteOpen && in.wheel != 0) { palScroll += in.wheel * 30; ui.consumeWheel(); }

    // ── comments (dietro a fili e nodi) ──
    for (int ci = 0; ci < (int)C.comments.size(); ci++) {
        const BPComment& c = C.comments[ci];
        float cx = ox + c.x * Z, cy = oy + c.y * Z, cw = c.w * Z, ch = c.h * Z;
        float titleH = commentTitleHeight(c, Z);
        bool selc = selComment == ci;
        r->drawRectPx(cx, cy, cw, ch, c.color * 0.16f + Vec3{ 0.05f, 0.05f, 0.06f }, 0.5f);       // corpo translucido
        r->drawRectPx(cx, cy, cw, titleH, c.color * 0.55f + Vec3{ 0.04f, 0.04f, 0.05f }, 0.95f);  // barra titolo
        Vec3 bord = selc ? Vec3{ 1, 0.8f, 0.3f } : c.color;
        r->drawRectPx(cx, cy, cw, 1, bord, 1);
        r->drawRectPx(cx, cy + ch - 1, cw, 1, bord, 1);
        r->drawRectPx(cx, cy, 1, ch, bord, 1);
        r->drawRectPx(cx + cw - 1, cy, 1, ch, bord, 1);
        float textScale = clampf(c.fontSize, 0.6f, 5.0f) * Z;
        float textY = cy + (titleH - 17.0f * textScale) * 0.5f;
        // ellipsize works in unscaled text pixels: account for both graph zoom
        // and the user-selected comment font size so the title never overflows.
        float maxTextW = (cw / Z - 18.0f) / clampf(c.fontSize, 0.6f, 5.0f);
        r->drawTextLine(cx + 9 * Z, textY, ui.ellipsize(c.text, maxTextW),
                        { 0.96f, 0.97f, 1.0f }, 1, textScale);
        r->drawTriPx(cx + cw, cy + ch - 12 * Z, cx + cw, cy + ch, cx + cw - 12 * Z, cy + ch, bord, 0.9f);  // maniglia resize
    }

    // ── wires ──
    for (const auto& l : C.links) {
        const BPNode* a = C.byId(l.fromNode);
        const BPNode* b = C.byId(l.toNode);
        if (!a || !b) continue;
        float x1, y1, x2, y2;
        pinPos(*a, ox, oy, l.fromPin, true, &x1, &y1);
        pinPos(*b, ox, oy, l.toPin, false, &x2, &y2);
        Vec3 col = PIN_COLORS[bpEffKind(C, graph, a->id, l.fromPin, true, 0)];
        float px = x1, py = y1;
        // control-point offset grows with the gap between the pins: wider curves when
        // nodes are far apart, tighter when close
        float coff = clampf(fabsf(x2 - x1) * 0.5f + fabsf(y2 - y1) * 0.15f, 16.0f * Z, 220.0f * Z);
        float d1 = pinTangentDir(*a, true);
        float d2 = pinTangentDir(*b, false);
        for (int s = 1; s <= 14; s++) {
            float t = s / 14.0f;
            float mt = 1 - t;
            float bx = mt * mt * mt * x1 + 3 * mt * mt * t * (x1 + d1 * coff) + 3 * mt * t * t * (x2 + d2 * coff) + t * t * t * x2;
            float by = mt * mt * mt * y1 + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t * y2;
            r->drawLinePx(px, py, bx, by, 2.2f, col, 0.9f);
            px = bx;
            py = by;
        }
    }
    if (wiring) {
        const BPNode* a = C.byId(wireNode);
        if (a) {
            float x1, y1;
            pinPos(*a, ox, oy, wirePin, wireFromOut, &x1, &y1);
            r->drawLinePx(x1, y1, in.mouseX, in.mouseY, 2, PIN_COLORS[wireKind], 0.8f);
        }
    }

    // ── nodes ── (all offsets scaled by Z; text drawn at scale Z)
    for (auto& n : C.nodes) {
        BPNodeDef d = effDef(n);
        float w, h;
        nodeRect(n, &w, &h);          // screen size
        float nx = ox + n.x * Z, ny = oy + n.y * Z;
        if (nx + w < cv.x || nx > cv.x + cv.w || ny + h < cv.y || ny > cv.y + cv.h) continue;
        bool sel = n.id == selNode || selSet.count(n.id) != 0;
        float ps = 5 * Z;             // half pin size
        if (isReroute(n.def)) {
            Vec3 pc = PIN_COLORS[d.outs[0].kind];
            r->drawRectPx(nx + 2, ny + 3, w, h, { 0, 0, 0 }, 0.3f);
            r->drawRectPx(nx, ny, w, h, { 0.14f, 0.155f, 0.185f }, 1);
            if (sel) {
                r->drawRectPx(nx - 1, ny - 1, w + 2, 1, { 1, 0.8f, 0.3f }, 1);
                r->drawRectPx(nx - 1, ny + h, w + 2, 1, { 1, 0.8f, 0.3f }, 1);
                r->drawRectPx(nx - 1, ny, 1, h, { 1, 0.8f, 0.3f }, 1);
                r->drawRectPx(nx + w, ny, 1, h, { 1, 0.8f, 0.3f }, 1);
            }
            r->drawRectPx(nx + w * 0.5f - 3, ny + h * 0.5f - 3, 6, 6, pc, 1);
            float px, py;
            pinPos(n, ox, oy, 0, false, &px, &py);
            r->drawRectPx(px - 4, py - 4, 8, 8, pc, 1);
            pinPos(n, ox, oy, 0, true, &px, &py);
            r->drawRectPx(px - 4, py - 4, 8, 8, pc, 1);
            continue;
        }
        if (isVarGet(n.def)) {
            PinKind vk = bpEffKind(C, graph, n.id, 0, true, 0);
            Vec3 pc = PIN_COLORS[vk];
            Vec3 body = pc * 0.28f + Vec3{ 0.06f, 0.065f, 0.075f };
            const Vec3 bg = { 0.07f, 0.078f, 0.09f };
            r->drawRectPx(nx + 2, ny + 3, w, h, { 0, 0, 0 }, 0.3f);
            r->drawRectPx(nx, ny, w, h, body, 1);
            float ch = 3 * Z;
            for (int cx = 0; cx < 2; cx++)
                for (int cy = 0; cy < 2; cy++) {
                    float ex = cx ? nx + w : nx, ey = cy ? ny + h : ny;
                    r->drawTriPx(ex, ey, ex + (cx ? -ch : ch), ey, ex, ey + (cy ? -ch : ch), bg, 1);
                }
            Vec3 border = sel ? Vec3{ 1, 0.8f, 0.3f } : pc * 0.7f;
            r->drawRectPx(nx + ch, ny, w - 2 * ch, 1, border, 1);
            r->drawRectPx(nx + ch, ny + h - 1, w - 2 * ch, 1, border, 1);
            r->drawRectPx(nx, ny + ch, 1, h - 2 * ch, border, 1);
            r->drawRectPx(nx + w - 1, ny + ch, 1, h - 2 * ch, border, 1);
            r->drawTextLine(nx + 12 * Z, ny + 6 * Z, ui.ellipsize(n.sname, w / Z - 28), { 0.95f, 0.96f, 1.0f }, 1, Z);
            float px, py;
            pinPos(n, ox, oy, 0, true, &px, &py);
            r->drawRectPx(px - ps, py - ps, 2 * ps, 2 * ps, pc, 1);
            continue;
        }
        float NT = NTITLE_H * Z;
        if (isVarSet(n.def)) {
            // stessa grafica del Get: pill smussata colorata dal tipo della variabile,
            // ma con header "SET name" e le righe pin (exec+valore in / exec+ritorno out)
            PinKind vk = bpEffKind(C, graph, n.id, 1, true, 0);   // tipo del pin di ritorno = tipo variabile
            Vec3 pc = PIN_COLORS[vk];
            Vec3 body = pc * 0.28f + Vec3{ 0.06f, 0.065f, 0.075f };
            const Vec3 bg = { 0.07f, 0.078f, 0.09f };
            r->drawRectPx(nx + 2, ny + 3, w, h, { 0, 0, 0 }, 0.3f);
            r->drawRectPx(nx, ny, w, h, body, 1);
            r->drawRectPx(nx, ny, w, NT, pc * 0.5f + Vec3{ 0.05f, 0.05f, 0.06f }, 1);   // header piu' acceso
            float ch = 3 * Z;
            for (int cx = 0; cx < 2; cx++)
                for (int cy = 0; cy < 2; cy++) {
                    float ex = cx ? nx + w : nx, ey = cy ? ny + h : ny;
                    r->drawTriPx(ex, ey, ex + (cx ? -ch : ch), ey, ex, ey + (cy ? -ch : ch), bg, 1);
                }
            Vec3 border = sel ? Vec3{ 1, 0.8f, 0.3f } : pc * 0.7f;
            r->drawRectPx(nx + ch, ny, w - 2 * ch, 1, border, 1);
            r->drawRectPx(nx + ch, ny + h - 1, w - 2 * ch, 1, border, 1);
            r->drawRectPx(nx, ny + ch, 1, h - 2 * ch, border, 1);
            r->drawRectPx(nx + w - 1, ny + ch, 1, h - 2 * ch, border, 1);
            char htxt[64];
            snprintf(htxt, sizeof(htxt), "SET  %s", n.sname);
            r->drawTextLine(nx + 9 * Z, ny + 3 * Z, ui.ellipsize(htxt, w / Z - 18), { 0.95f, 0.96f, 1.0f }, 1, Z);
        } else {
            r->drawRectPx(nx + 3, ny + 4, w, h, { 0, 0, 0 }, 0.3f);
            r->drawRectPx(nx, ny, w, h, { 0.14f, 0.155f, 0.185f }, 0.98f);
            if (sel) {
                r->drawRectPx(nx - 1, ny - 1, w + 2, 1, { 1, 0.8f, 0.3f }, 1);
                r->drawRectPx(nx - 1, ny + h, w + 2, 1, { 1, 0.8f, 0.3f }, 1);
                r->drawRectPx(nx - 1, ny, 1, h, { 1, 0.8f, 0.3f }, 1);
                r->drawRectPx(nx + w, ny, 1, h, { 1, 0.8f, 0.3f }, 1);
            }
            // solo i nodi Entrata / Ritorna della funzione sono viola; gli altri
            // mantengono il colore della loro categoria
            bool fnNode = (n.def == BP_FN_ENTRY || n.def == BP_FN_RETURN);
            r->drawRectPx(nx, ny, w, NT, fnNode ? FN_NODE_COLOR : CAT_COLORS[d.category], 1);
            char title[160];
            bpNodeTitle(n, title, sizeof(title));
            r->drawTextLine(nx + 7 * Z, ny + 3 * Z, ui.ellipsize(title, w / Z - 14), { 0.95f, 0.96f, 1.0f }, 1, Z);
        }

        float sy = ny + NT;
        if (n.def == BP_EV_CUSTOM && !n.slit[0].empty()) {
            std::string origin = std::string("Interface: ") + fs::path(n.slit[0]).stem().string();
            r->drawRectPx(nx + 5 * Z, sy + 1 * Z, w - 10 * Z, 20 * Z, { 0.10f, 0.14f, 0.19f }, 1);
            r->drawTextLine(nx + 10 * Z, sy + 3 * Z, ui.ellipsize(origin, w / Z - 20), { .46f, .76f, 1.0f }, 1, Z);
            sy += NODE_STRIP_H * Z;
        }
        if (d.propKind && n.def != BP_EV_KEY) {
            char pb[48];
            if (d.propKind == 1) snprintf(pb, sizeof(pb), "value: %.2f", n.prop);
            else if (d.propKind == 2) snprintf(pb, sizeof(pb), "key: %s", BP_KEY_NAMES[n.choice % BP_NKEYS]);
            else if (d.propKind == 4) {
                if (n.def == BP_GET_COMPONENT && !n.slit[0].empty())
                    snprintf(pb, sizeof(pb), "classe: %s", fs::path(n.slit[0]).stem().string().c_str());
                else snprintf(pb, sizeof(pb), "classe: %s", BP_COMP_NAMES[n.choice % BP_NCOMPS]);
            }
            else if (d.propKind == 5) snprintf(pb, sizeof(pb), "asse: %s", BP_AXIS_NAMES[n.choice % BP_NAXES]);
            else if (d.propKind == 6) snprintf(pb, sizeof(pb), "type: %s", BP_PHYSTYPE_NAMES[n.choice % 2]);
            else snprintf(pb, sizeof(pb), "op: %s", BP_CMP_OPS[n.choice % 5]);
            r->drawRectPx(nx + 5 * Z, sy + 1 * Z, w - 10 * Z, 20 * Z, { 0.09f, 0.1f, 0.12f }, 1);
            r->drawTextLine(nx + 10 * Z, sy + 3 * Z, ui.ellipsize(pb, w / Z - 20), { 0.75f, 0.85f, 1.0f }, 1, Z);
            sy += NODE_STRIP_H * Z;
        }
        if (d.usesName && !isVarSet(n.def) && n.def != BP_EV_CUSTOM) {
            char pb[96];
            if(n.def==BP_INTERFACE_MESSAGE)
                snprintf(pb,sizeof(pb),"Interface: %s",
                         n.slit[0].empty() ? "non assegnata" : fs::path(n.slit[0]).stem().string().c_str());
            else snprintf(pb, sizeof(pb), "name: %s", n.sname);
            r->drawRectPx(nx + 5 * Z, sy + 1 * Z, w - 10 * Z, 20 * Z, { 0.11f, 0.13f, 0.1f }, 1);
            r->drawTextLine(nx + 10 * Z, sy + 3 * Z, ui.ellipsize(pb, w / Z - 20), { 0.72f, 0.95f, 0.75f }, 1, Z);
        }
        if (n.def == BP_TIMER_SET_FUNC) {
            char pb[128];
            snprintf(pb, sizeof(pb), "function: %s", n.sname[0] ? n.sname : "drag a function here");
            r->drawRectPx(nx + 5 * Z, sy + 1 * Z, w - 10 * Z, 20 * Z, { 0.16f, 0.10f, 0.20f }, 1);
            r->drawTextLine(nx + 10 * Z, sy + 3 * Z, ui.ellipsize(pb, w / Z - 20), n.sname[0] ? Vec3{ 0.85f, 0.76f, 1.0f } : Vec3{ 0.62f, 0.55f, 0.7f }, 1, Z);
        }

        for (int p = 0; p < d.nIns; p++) {
            float px, py;
            pinPos(n, ox, oy, p, false, &px, &py);
            Vec3 pc = PIN_COLORS[bpEffKind(C, graph, n.id, p, false, 0)];
            if (d.ins[p].kind == PIN_EXEC) r->drawTriPx(px - ps, py - 6 * Z, px + 6 * Z, py, px - ps, py + 6 * Z, pc, 1);
            else r->drawRectPx(px - ps, py - ps, 2 * ps, 2 * ps, pc, 1);
            r->drawTextLine(px + 10 * Z, py - 8 * Z, d.ins[p].name, { 0.78f, 0.82f, 0.88f }, 1, Z);
            PinKind ek = editorKind(n, d, p);
            if (litEditable(ek) && !C.linkInto(n.id, p)) {
                float off = litOffset(d, p) * Z;
                float bhh = 16 * Z;
                if (ek == PIN_COLOR) {
                    float bw=34*Z,bx=px+off,by=py-8*Z,hh=16*Z,hw=bw*.5f,halfH=hh*.5f;
                    r->drawRectPx(bx,by,hw,halfH,{.62f,.62f,.65f},1);r->drawRectPx(bx+hw,by,hw,halfH,{.28f,.29f,.32f},1);
                    r->drawRectPx(bx,by+halfH,hw,halfH,{.28f,.29f,.32f},1);r->drawRectPx(bx+hw,by+halfH,hw,halfH,{.62f,.62f,.65f},1);
                    r->drawRectPx(bx,by,bw,hh,n.lit[p],clampf(n.litAlpha[p],0,1));
                } else if (ek == PIN_BOOL) {
                    // checkbox con spunta
                    float cs = 14 * Z, cbx = px + off, cby = py - 7 * Z;
                    bool on = n.lit[p].x != 0;
                    r->drawRectPx(cbx, cby, cs, cs, { 0.09f, 0.1f, 0.12f }, 1);
                    Vec3 bd = { 0.32f, 0.36f, 0.42f };
                    r->drawRectPx(cbx, cby, cs, 1, bd, 1);
                    r->drawRectPx(cbx, cby + cs - 1, cs, 1, bd, 1);
                    r->drawRectPx(cbx, cby, 1, cs, bd, 1);
                    r->drawRectPx(cbx + cs - 1, cby, 1, cs, bd, 1);
                    if (on) {
                        Vec3 cc = { 0.4f, 0.92f, 0.5f };
                        r->drawLinePx(cbx + 3 * Z, cby + cs * 0.55f, cbx + cs * 0.42f, cby + cs - 3 * Z, 2, cc, 1);
                        r->drawLinePx(cbx + cs * 0.42f, cby + cs - 3 * Z, cbx + cs - 2 * Z, cby + 3 * Z, 2, cc, 1);
                    }
                } else if (ek == PIN_STR) {
                    // box di testo
                    bool editing = litEditNode == n.id && litEditPin == p;
                    float bw = literalBoxWidth(n, p, ek) * Z, bx = px + off;
                    r->drawRectPx(bx, py - 8 * Z, bw, bhh, editing ? Vec3{ 0.06f, 0.07f, 0.09f } : Vec3{ 0.09f, 0.1f, 0.12f }, 1);
                    if (editing) {
                        r->drawRectPx(bx, py - 8 * Z, bw, 1, { 0.30f, 0.62f, 0.99f }, 1);
                        r->drawRectPx(bx, py + 7 * Z, bw, 1, { 0.30f, 0.62f, 0.99f }, 1);
                    }
                    const char* txt = editing ? litEditBuf : n.slit[p].c_str();
                    std::string shown = ui.ellipsize(txt, bw / Z - 7);
                    if(editing&&litEditCursor!=litEditAnchor){int a=std::min(litEditCursor,litEditAnchor),b=std::max(litEditCursor,litEditAnchor);float x0=r->textWidth(std::string(litEditBuf,litEditBuf+a),Z),x1=r->textWidth(std::string(litEditBuf,litEditBuf+b),Z),sw=std::max(0.0f,std::min(x1-x0,bw-6*Z-x0));r->drawRectPx(bx+3*Z+x0,py-7*Z,sw,14*Z,{.12f,.24f,.40f},1);}
                    r->drawTextLine(bx + 3 * Z, py - 8 * Z, shown, { 0.86f, 0.82f, 0.99f }, 1, Z);
                    if (editing && (frame_ / 30) % 2 == 0) {
                        float tw = r->textWidth(std::string(litEditBuf,litEditBuf+litEditCursor), Z);
                        float caretX = bx + 4 * Z + tw;
                        if (caretX > bx + bw - 3 * Z) caretX = bx + bw - 3 * Z;
                        r->drawRectPx(caretX, py - 6 * Z, 2, 12 * Z, { 0.30f, 0.62f, 0.99f }, 1);
                    }
                } else {
                    int comps = litComps(ek);
                    float bw = literalBoxWidth(n, p, ek) * Z;
                    for (int c = 0; c < comps; c++) {
                        float bx = px + off + c * (bw + 4 * Z);
                        bool editing = litEditNode == n.id && litEditPin == p && litEditComp == c;
                        r->drawRectPx(bx, py - 8 * Z, bw, bhh, editing ? Vec3{ 0.06f, 0.07f, 0.09f } : Vec3{ 0.09f, 0.1f, 0.12f }, 1);
                        if (editing) {
                            r->drawRectPx(bx, py - 8 * Z, bw, 1, { 0.30f, 0.62f, 0.99f }, 1);
                            r->drawRectPx(bx, py + 7 * Z, bw, 1, { 0.30f, 0.62f, 0.99f }, 1);
                            std::string shown = ui.ellipsize(litEditBuf, bw / Z - 7);
                            if(litEditCursor!=litEditAnchor){int a=std::min(litEditCursor,litEditAnchor),b=std::max(litEditCursor,litEditAnchor);float x0=r->textWidth(std::string(litEditBuf,litEditBuf+a),Z),x1=r->textWidth(std::string(litEditBuf,litEditBuf+b),Z),sw=std::max(0.0f,std::min(x1-x0,bw-6*Z-x0));r->drawRectPx(bx+3*Z+x0,py-7*Z,sw,14*Z,{.12f,.24f,.40f},1);}
                            r->drawTextLine(bx + 3 * Z, py - 8 * Z, shown, { 0.9f, 0.95f, 1.0f }, 1, Z);
                            if ((frame_ / 30) % 2 == 0) {
                                float tw = r->textWidth(std::string(litEditBuf,litEditBuf+litEditCursor), Z);
                                float caretX = bx + 4 * Z + tw;
                                if (caretX > bx + bw - 3 * Z) caretX = bx + bw - 3 * Z;
                                r->drawRectPx(caretX, py - 6 * Z, 2, 12 * Z, { 0.30f, 0.62f, 0.99f }, 1);
                            }
                        } else {
                            char vb[64];
                            if (const char* en = bpWidgetEnumLabel(n.def, ek, (int)n.lit[p].x)) snprintf(vb, sizeof(vb), "%s", en);
                            else if (ek == PIN_INT || ek == PIN_ENUM) snprintf(vb, sizeof(vb), "%d", (int)n.lit[p].x);
                            else snprintf(vb, sizeof(vb), "%.7g", (&n.lit[p].x)[c]);
                            r->drawTextLine(bx + 3 * Z, py - 8 * Z, ui.ellipsize(vb, bw / Z - 7), { 0.65f, 0.9f, 0.7f }, 1, Z);
                        }
                    }
                }
            }
        }
        int nOut = d.nOuts;
        bool seq = n.def == BP_FLOW_SEQ;
        for (int p = 0; p < nOut; p++) {
            if (n.def == BP_EV_CUSTOM && p == nOut - 1 && d.outs[p].kind == PIN_DELEGATE) continue;
            float px, py;
            pinPos(n, ox, oy, p, true, &px, &py);
            Vec3 pc = PIN_COLORS[bpEffKind(C, graph, n.id, p, true, 0)];
            PinKind ok = seq ? PIN_EXEC : d.outs[p].kind;
            if (ok == PIN_EXEC) r->drawTriPx(px - 6 * Z, py - 6 * Z, px + ps, py, px - 6 * Z, py + 6 * Z, pc, 1);
            else r->drawRectPx(px - ps, py - ps, 2 * ps, 2 * ps, pc, 1);
            if (!seq) {                 // Sequence outputs carry no labels
                float tw = r->textWidth(d.outs[p].name, Z);
                r->drawTextLine(px - 12 * Z - tw, py - 8 * Z, d.outs[p].name, { 0.78f, 0.82f, 0.88f }, 1, Z);
            }
        }
        if (n.def == BP_EV_CUSTOM && nOut > 0 && d.outs[nOut - 1].kind == PIN_DELEGATE) {
            float px, py;
            pinPos(n, ox, oy, nOut - 1, true, &px, &py);
            Vec3 pc = PIN_COLORS[PIN_DELEGATE];
            r->drawRectPx(px - ps, py - ps, 2 * ps, 2 * ps, pc, 1);
        }
        // Sequence "+" button
        float bx, by, bw2, bh2;
        if (seqPlusRect(n, ox, oy, w, Z, &bx, &by, &bw2, &bh2)) {
            bool hov = in.mouseX >= bx && in.mouseX < bx + bw2 && in.mouseY >= by && in.mouseY < by + bh2;
            r->drawRectPx(bx, by, bw2, bh2, hov ? Vec3{ 0.25f, 0.4f, 0.6f } : Vec3{ 0.2f, 0.22f, 0.26f }, 1);
            float plusTw = r->textWidth("+", Z);
            r->drawTextLine(bx + (bw2 - plusTw) * 0.5f, by + bh2 * 0.5f - 7 * Z, "+", { 0.9f, 0.95f, 1.0f }, 1, Z);
        }
    }

    // ── palette: categories as submenus + search bar ──
    if (paletteOpen) {
        const float PW = 340;
        float palH = cv.h < 480 ? cv.h - 10 : 480;
        r->drawRectPx(palX + 3, palY + 4, PW, palH, { 0, 0, 0 }, 0.35f);
        r->drawRectPx(palX, palY, PW, palH, { 0.12f, 0.135f, 0.16f }, 0.99f);

        // search box (typed input is routed here while the palette is open)
        {
            int len = (int)strlen(palSearch);
            for (int i = 0; i < in.typedCount; i++) {
                char ch = in.typed[i];
                if (ch >= 32 && ch < 127 && len < (int)sizeof(palSearch) - 1) {
                    palSearch[len++] = ch;
                    palSearch[len] = 0;
                }
            }
            if (in.keyBackspace && len > 0) palSearch[len - 1] = 0;
        }
        const float searchW = 174;
        r->drawRectPx(palX + 8, palY + 8, searchW, 22, { 0.07f, 0.08f, 0.1f }, 1);
        if (palSearch[0]) {
            r->drawTextLine(palX + 14, palY + 12, palSearch, { 0.9f, 0.93f, 1.0f }, 1);
        } else {
            r->drawTextLine(palX + 14, palY + 12, "search node...", { 0.45f, 0.5f, 0.58f }, 1);
        }
        float tw0 = r->textWidth(palSearch);
        r->drawRectPx(palX + 15 + tw0, palY + 12, 2, 14, { 0.30f, 0.62f, 0.99f }, 1);
        const float csX = palX + 192, csY = palY + 10;
        bool csHover = in.mouseX >= csX && in.mouseX < palX + PW - 6 && in.mouseY >= palY + 7 && in.mouseY < palY + 31;
        r->drawRectPx(csX, csY, 16, 16, csHover ? Vec3{ .23f,.29f,.39f } : Vec3{ .08f,.09f,.11f }, 1);
        if (contextSensitive) {
            r->drawRectPx(csX + 3, csY + 3, 10, 10, { .30f,.62f,.99f }, 1);
            r->drawTextLine(csX + 4, csY, "v", { 1,1,1 }, 1, .78f);
        }
        r->drawTextLine(csX + 22, palY + 11, "Context Sensitive", { .76f,.82f,.90f }, 1, .86f);
        if (csHover && in.mousePressed) contextSensitive = !contextSensitive;

        float listTop = palY + 38, listBot = palY + palH - 6;
        struct InterfacePaletteAsset{std::string path,label;BPGraph graph;};
        std::vector<InterfacePaletteAsset> paletteInterfaces;
        for(const std::string& path:bpFindProjectAssets(projectDir,".bpi")){
            std::string data;InterfacePaletteAsset asset;asset.path=path;asset.label=fs::path(path).stem().string();
            if(bpReadTextFile(projectDir+"\\"+path,data)&&asset.graph.deserialize(data))paletteInterfaces.push_back(std::move(asset));
        }
        // def -1 = main category, -2 = nested Interface asset header.
        struct PalRow { int def; int cat; std::string name; int interfaceIndex=-1; int memberChoice=-1;
                        bool matchDelegate=false; };
        std::vector<PalRow> rows;
        const int functionsCat=BP_NCATS,customEventsCat=BP_NCATS+1,interfacesCat=BP_NCATS+2;
        auto catName = [&](int c) -> const char* {
            return c==functionsCat?"Funzioni":c==customEventsCat?"Custom Event":c==interfacesCat?"Interfaces":BP_CAT_NAMES[c];
        };
        auto catColor = [&](int c) -> Vec3 {
            return c==functionsCat?FN_NODE_COLOR:c==customEventsCat?CAT_COLORS[0]:c==interfacesCat?Vec3{.36f,.72f,.98f}:CAT_COLORS[c];
        };
        // link mode (wire dropped on the canvas): flat list of compatible nodes only
        bool searching = palSearch[0] != 0 || palLinkMode;
        bool pureBody = curFunc >= 0 && curFunc < (int)graph.funcs.size() && graph.funcs[curFunc].pure;
        auto usable = [&](int t) {
            if (t == BP_INTERFACE_MESSAGE || t == BP_MEMBER_ACCESS) return false;
            if (t == BP_FN_ENTRY) return false;
            // A Widget Blueprint offers the UMG events and none of the actor ones;
            // an actor Blueprint is the exact opposite.
            if (bpIsWidgetEvent(t)) return widgetMode && curFunc < 0;
            if (widgetMode && bpIsActorOnlyEvent(t)) return false;
            if (t == BP_EV_CONSTRUCT)
                return curFunc < 0 && curGraph >= 0 && curGraph < (int)graph.graphs.size() &&
                       strcmp(graph.graphs[curGraph].name, "ConstructionScript") == 0;
            if ((t == BP_FN_RETURN || t == BP_LOCAL_GET || t == BP_LOCAL_SET) && curFunc < 0) return false;
            // Come in Unreal, le azioni che sospendono/riprendono una catena non
            // sono ammesse nei corpi funzione (il loro frame e' sincrono).
            if (curFunc >= 0 && (t == BP_TIMER_SET || t == BP_TIMER_SET_FUNC || t == BP_FLOW_DELAY || t == BP_FLOW_RETRIGGER_DELAY)) return false;
            if (pureBody) {
                bool pureNode = DEFS[t].category == 2 || DEFS[t].category == 4 || DEFS[t].category == 5 ||
                                t == BP_VAR_GET || t == BP_LOCAL_GET || t == BP_ARR_GET || t == BP_ARR_LEN ||
                                t == BP_MAP_GET || t == BP_MAP_LEN || t == BP_REROUTE || t == BP_FN_RETURN;
                if (!pureNode) return false;
            }
            if (palLinkMode && contextSensitive && firstCompatiblePin(DEFS[t], palLinkKind, palLinkOut) < 0) return false;
            return true;
        };
        for (int cat = 0; cat < BP_NCATS + 3 && !searching; cat++) {
            rows.push_back({ -1, cat, {} });
            if (!(palCatOpen & (1u << cat))) continue;
            if (cat < BP_NCATS) {
                for (int t = 0; t < BP_TYPE_COUNT; t++) {
                    if (DEFS[t].category != cat) continue;
                    if (!usable(t)) continue;
                    rows.push_back({ t, cat, {} });
                }
            } else if (cat == functionsCat) {
                for (auto& f : graph.funcs) if (!pureBody || f.pure) rows.push_back({ BP_CALL_FUNC, cat, f.name });
            } else if (cat == customEventsCat) {
                if (!pureBody) for (auto& gph : graph.graphs)
                    for (auto& evn : gph.body.nodes)
                        if (evn.def == BP_EV_CUSTOM && evn.sname[0]) rows.push_back({ BP_CALL_EVENT, cat, evn.sname });
            } else if (!pureBody) {
                for (int ii=0;ii<(int)paletteInterfaces.size();ii++) {
                    const InterfacePaletteAsset& asset=paletteInterfaces[ii];
                    rows.push_back({-2,cat,asset.label.c_str(),ii});
                    if(palInterfaceOpen.find(asset.path)==palInterfaceOpen.end())continue;
                    for(const BPFunc& function:asset.graph.funcs)
                        rows.push_back({BP_INTERFACE_MESSAGE,cat,function.name,ii});
                }
            }
        }
        if (searching) {
            for (int t = 0; t < BP_TYPE_COUNT; t++) {
                if (!usable(t)) continue;
                if (icontains(DEFS[t].title, palSearch)) rows.push_back({ t, DEFS[t].category, {} });
            }
            if (!palLinkMode) {
                for (auto& f : graph.funcs)
                    if ((!pureBody || f.pure) && icontains(f.name, palSearch)) rows.push_back({ BP_CALL_FUNC, BP_NCATS, f.name });
                if (!pureBody) for (auto& gph : graph.graphs)
                    for (auto& evn : gph.body.nodes)
                        if (evn.def == BP_EV_CUSTOM && evn.sname[0] && icontains(evn.sname, palSearch))
                            rows.push_back({ BP_CALL_EVENT, BP_NCATS + 1, evn.sname });
            }
            bool interfaceCompatible = !palLinkMode ||
                (!contextSensitive || firstCompatiblePin(DEFS[BP_INTERFACE_MESSAGE], palLinkKind, palLinkOut) >= 0);
            if(!pureBody&&interfaceCompatible)
                for(int ii=0;ii<(int)paletteInterfaces.size();ii++)
                    for(const BPFunc& function:paletteInterfaces[ii].graph.funcs)
                        if(icontains(function.name,palSearch)||icontains(paletteInterfaces[ii].label.c_str(),palSearch)||
                           icontains("Message",palSearch))
                            rows.push_back({BP_INTERFACE_MESSAGE,interfacesCat,function.name,ii});
        }
        // A typed Blueprint reference contributes its public API to the same
        // contextual menu. These are real target-bound nodes, not local calls.
        // A Widget reference contributes its own graph's API the same way; the
        // class already names the .wgt, which has no parent chain to resolve.
        // Dragged the Bind node's Event pin onto empty canvas: offer a Custom Event
        // already shaped like the Dispatcher, so its signature cannot be wrong.
        BPDispatcherDef wantedDisp;
        bool wantsDelegate = false;
        if (palLinkMode && !palLinkOut && palLinkKind == PIN_DELEGATE && !pureBody) {
            const BPNode* bindNode = C.byId(palLinkNode);
            if (bindNode && bindNode->def == BP_BIND_EVENT && palLinkPin == 1 &&
                bpFindBindDispatcher(projectDir, graph, C, *bindNode, wantedDisp)) {
                wantsDelegate = true;
                std::string label = std::string("Add Custom Event matching ") + wantedDisp.name +
                                    bpSignatureText(wantedDisp.params);
                if (icontains(label.c_str(), palSearch))
                    rows.push_back({ BP_EV_CUSTOM, customEventsCat, label, -1, -1, true });
            }
        }
        (void)wantsDelegate;

        const bool memberIsWidget = bpMemberClassIsWidget(palLinkRefClass);
        std::string memberPath = palLinkRefClass;
        if (memberIsWidget) memberPath.erase(0, 7);
        else if (memberPath.rfind("blueprint:", 0) == 0) memberPath.erase(0, 10);
        std::string resolvedMemberPath;
        if (!memberIsWidget && !memberPath.empty() &&
            bpResolveBlueprintAssetPath(projectDir, memberPath, resolvedMemberPath))
            memberPath = resolvedMemberPath;
        BPGraph memberGraph;
        bool memberLoaded = palLinkMode && !memberPath.empty() &&
            (memberIsWidget ? bpLoadWidgetGraph(projectDir, palLinkRefClass, memberGraph)
                            : bpLoadResolvedGraph(projectDir, memberPath, memberGraph));
        if (memberLoaded) {
            auto matchesSearch = [&](const std::string& label) { return icontains(label.c_str(), palSearch); };
            for (const BPVarDef& var : memberGraph.vars) {
                if (var.scope != VS_PUBLIC || var.container != VC_SINGLE) continue;
                std::string getLabel = std::string("Get ") + var.name;
                std::string setLabel = std::string("Set ") + var.name;
                if (matchesSearch(getLabel)) rows.push_back({ BP_MEMBER_ACCESS, 3, getLabel, -1, 0 });
                if (!pureBody && matchesSearch(setLabel)) rows.push_back({ BP_MEMBER_ACCESS, 3, setLabel, -1, 1 });
            }
            for (const BPFunc& function : memberGraph.funcs) {
                if (function.scope != VS_PUBLIC || (pureBody && !function.pure)) continue;
                std::string label = std::string("Call ") + function.name;
                if (matchesSearch(label)) rows.push_back({ BP_MEMBER_ACCESS, function.pure ? 2 : 1, label, -1, function.pure ? 3 : 2 });
            }
            if (!pureBody) for (const BPEventDef& event : memberGraph.events) {
                if (event.scope != VS_PUBLIC) continue;
                std::string label = std::string("Event ") + event.name;
                if (matchesSearch(label)) rows.push_back({ BP_MEMBER_ACCESS, 1, label, -1, 4 });
            }
        }
        const float RH_CAT = 26, RH_ITEM = 21;
        float contentH = 0;
        for (const auto& row : rows) contentH += row.def < 0 ? RH_CAT : RH_ITEM;
        float minScroll = listBot - listTop - contentH;
        if (minScroll > 0) minScroll = 0;
        if (palScroll < minScroll) palScroll = minScroll;
        if (palScroll > 0) palScroll = 0;
        float iy = listTop + palScroll;
        for (int ri = 0; ri < (int)rows.size(); ri++) {
            float rh = rows[ri].def < 0 ? RH_CAT : RH_ITEM;
            float rowY = iy;
            iy += rh;
            if (rowY < listTop - rh || rowY > listBot - 4) continue;
            bool hov = in.mouseX >= palX && in.mouseX < palX + PW && in.mouseY >= rowY && in.mouseY < rowY + rh - 2;
            if (rows[ri].def == -1) {
                bool open = (palCatOpen & (1u << rows[ri].cat)) != 0;
                r->drawRectPx(palX + 4, rowY + 2, PW - 8, rh - 5,
                              hov ? Vec3{ 0.19f, 0.22f, 0.28f } : Vec3{ 0.155f, 0.175f, 0.21f }, 1);
                r->drawTextLine(palX + 12, rowY + 5, open ? "v" : ">", { 0.30f, 0.62f, 0.99f }, 1);
                r->drawRectPx(palX + 28, rowY + 8, 8, 8, catColor(rows[ri].cat), 1);
                r->drawTextLine(palX + 44, rowY + 5, catName(rows[ri].cat), { 0.82f, 0.86f, 0.92f }, 1);
                if (hov && in.mousePressed) palCatOpen ^= 1u << rows[ri].cat;
            } else if(rows[ri].def==-2&&rows[ri].interfaceIndex>=0&&rows[ri].interfaceIndex<(int)paletteInterfaces.size()) {
                const InterfacePaletteAsset& asset=paletteInterfaces[rows[ri].interfaceIndex];
                bool open=palInterfaceOpen.find(asset.path)!=palInterfaceOpen.end();
                r->drawRectPx(palX+16,rowY+2,PW-24,rh-5,hov?Vec3{.18f,.23f,.29f}:Vec3{.13f,.16f,.20f},1);
                r->drawTextLine(palX+26,rowY+5,open?"v":">",{.36f,.72f,.98f},1);
                r->drawTextLine(palX+44,rowY+5,asset.label,{.78f,.84f,.92f},1);
                if(hov&&in.mousePressed){if(open)palInterfaceOpen.erase(asset.path);else palInterfaceOpen.insert(asset.path);}
            } else {
                const BPNodeDef& d = DEFS[rows[ri].def];
                const char* label = !rows[ri].name.empty() ? rows[ri].name.c_str() : d.title;
                if (hov) r->drawRectPx(palX + 6, rowY + 1, PW - 12, rh - 3, { 0.2f, 0.32f, 0.5f }, 1);
                float ix = searching ? 14.0f : (rows[ri].def==BP_INTERFACE_MESSAGE?52.0f:34.0f);
                r->drawRectPx(palX + ix, rowY + 7, 7, 7, catColor(rows[ri].cat), 1);
                std::string displayLabel=rows[ri].def==BP_INTERFACE_MESSAGE?std::string(label)+" (Message)":std::string(label);
                r->drawTextLine(palX + ix + 15, rowY + 4, displayLabel,
                                hov ? Vec3{ 0.9f, 0.95f, 1.0f } : Vec3{ 0.75f, 0.79f, 0.85f }, 1, 0.92f);
                if (hov && in.mousePressed) {
                    int nid = C.addNode(rows[ri].def, snapGrid(palWX), snapGrid(palWY));
                    if (rows[ri].matchDelegate) {
                        // declare the event with the Dispatcher's exact parameters,
                        // then name the node after it — it can only match
                        BPEventDef created;
                        std::string evName = bpUniqueMemberName(graph, std::string(wantedDisp.name) + "_Event", 2);
                        snprintf(created.name, sizeof(created.name), "%s", evName.c_str());
                        created.params = wantedDisp.params;
                        graph.events.push_back(created);
                        snprintf(C.byId(nid)->sname, sizeof(C.byId(nid)->sname), "%s", created.name);
                    }
                    else if (!rows[ri].name.empty()) snprintf(C.byId(nid)->sname, sizeof(C.byId(nid)->sname), "%s", rows[ri].name.c_str());
                    if (rows[ri].def == BP_MEMBER_ACCESS && rows[ri].memberChoice >= 0) {
                        BPNode* member = C.byId(nid);
                        member->choice = rows[ri].memberChoice;
                        // keep the "widget:" prefix: it is what tells the node its
                        // Target is a widget handle rather than an entity
                        member->slit[0] = memberIsWidget ? palLinkRefClass : memberPath;
                        const char* prefix = member->choice == 0 ? "Get " : member->choice == 1 ? "Set " : member->choice == 4 ? "Event " : "Call ";
                        std::string memberName = rows[ri].name;
                        if (memberName.rfind(prefix, 0) == 0) memberName.erase(0, strlen(prefix));
                        snprintf(member->sname, sizeof(member->sname), "%s", memberName.c_str());
                    }
                    if(rows[ri].def==BP_INTERFACE_MESSAGE&&rows[ri].interfaceIndex>=0&&rows[ri].interfaceIndex<(int)paletteInterfaces.size())
                        C.byId(nid)->slit[0]=paletteInterfaces[rows[ri].interfaceIndex].path;
                    if (palLinkMode) {
                        // auto-wire the dragged pin to the first compatible pin
                        BPNodeDef createdDef = effDef(*C.byId(nid));
                        int p = firstCompatiblePin(createdDef, palLinkKind, palLinkOut);
                        if (p >= 0) {
                            if (palLinkOut) C.connect(palLinkNode, palLinkPin, nid, p);
                            else C.connect(nid, p, palLinkNode, palLinkPin);
                        }
                        palLinkMode = false;
                    }
                    selNode = nid;
                    selSet.clear();
                    selSet.insert(nid);
                    paletteOpen = false;
                    dirty = true;
                }
            }
        }
    }

    // ── node / pin context menus ──
    if (ctxKind != 0) {
        const char* nodeItems[3] = { "Copy  (Ctrl+C)", "Duplicate", "Delete" };
        const char* pinItems[2] = { "Remove connection", "Remove execute pin" };
        // il pin Then di una Sequence si puo' rimuovere (minimo 2 pin execute)
        BPNode* ctxSeq = C.byId(ctxNode);
        bool seqPin = ctxKind == 2 && ctxPinOut && ctxSeq && ctxSeq->def == BP_FLOW_SEQ && seqCount(*ctxSeq) > 2;
        // the function Entry node is permanent: offer only Copia (no Duplica/Elimina,
        // which would spawn a stray second entry or be a confusing no-op)
        bool ctxEntry = ctxKind == 1 && ctxSeq && ctxSeq->def == BP_FN_ENTRY;
        int count = ctxKind == 1 ? (ctxEntry ? 1 : 3) : (seqPin ? 2 : 1);
        const char* const* items = ctxKind == 1 ? nodeItems : pinItems;
        const float MW = 172, IH = 21;
        float mh = count * IH + 8;
        float mx = ctxX, my = ctxY;
        if (mx + MW > cv.x + cv.w) mx = cv.x + cv.w - MW;
        if (my + mh > cv.y + cv.h) my = cv.y + cv.h - mh;
        r->drawRectPx(mx + 3, my + 4, MW, mh, { 0, 0, 0 }, 0.35f);
        r->drawRectPx(mx, my, MW, mh, { 0.13f, 0.145f, 0.17f }, 0.99f);
        bool inMenu = in.mouseX >= mx && in.mouseX < mx + MW && in.mouseY >= my && in.mouseY < my + mh;
        for (int i = 0; i < count; i++) {
            float iy = my + 4 + i * IH;
            bool hov = inMenu && in.mouseY >= iy && in.mouseY < iy + IH;
            if (hov) r->drawRectPx(mx + 2, iy, MW - 4, IH, { 0.2f, 0.32f, 0.5f }, 1);
            r->drawTextLine(mx + 12, iy + 3, items[i], { 0.87f, 0.9f, 0.95f }, 1);
            if (hov && in.mousePressed) {
                BPNode* n = C.byId(ctxNode);
                if (ctxKind == 2) {
                    if (i == 0) {
                        disconnectPin(ctxNode, ctxPin, ctxPinOut);
                    } else if (seqPin && n) {
                        // rimuove l'output execute ctxPin dalla Sequence e ricuce i fili
                        int cnt = seqCount(*n);
                        for (size_t li = 0; li < C.links.size();) {
                            BPLink& l = C.links[li];
                            if (l.fromNode == ctxNode && l.fromPin == ctxPin) { C.links.erase(C.links.begin() + li); continue; }
                            if (l.fromNode == ctxNode && l.fromPin > ctxPin) l.fromPin--;
                            li++;
                        }
                        n->choice = cnt - 1;
                        dirty = true;
                    }
                } else if (n) {
                    // operate on the whole selection when the node is part of it
                    if (!selSet.count(ctxNode)) {
                        selSet.clear();
                        selSet.insert(ctxNode);
                        selNode = ctxNode;
                    }
                    if (i == 0) {
                        copySelection(C);
                    } else if (i == 1) {
                        float cx = 0, cy = 0;
                        int cnt = 0;
                        for (int selectedId : selSet) {
                            const BPNode* selected = C.byId(selectedId);
                            if (selected) { cx += selected->x; cy += selected->y; cnt++; }
                        }
                        copySelection(C);
                        if (cnt) pasteClipboard(C, cx / cnt + 30, cy / cnt + 30);
                    } else {
                        deleteSelection(C);
                    }
                }
                ctxKind = 0;
            }
        }
        if (in.mousePressed && !inMenu) ctxKind = 0;
    }

    // ── Get / Set chooser after dropping a variable ──
    if (varMenuOpen) {
        if (varMenuIdx < 0 || varMenuIdx >= (int)graph.vars.size()) {
            varMenuOpen = false;
        } else {
            const BPVarDef& v = graph.vars[varMenuIdx];
            char l0[48], l1[48];
            snprintf(l0, sizeof(l0), "Get %s", v.name);
            snprintf(l1, sizeof(l1), "Set %s", v.name);
            const char* items[2] = { l0, l1 };
            bool pureBody = curFunc >= 0 && curFunc < (int)graph.funcs.size() && graph.funcs[curFunc].pure;
            int itemCount = (pureBody || v.requiredGenerated || v.widgetGenerated) ? 1 : 2;
            const float MW = 160, IH = 21;
            float mh = itemCount * IH + 8;
            float mx = varMenuX, my = varMenuY;
            if (mx + MW > cv.x + cv.w) mx = cv.x + cv.w - MW;
            if (my + mh > cv.y + cv.h) my = cv.y + cv.h - mh;
            r->drawRectPx(mx + 3, my + 4, MW, mh, { 0, 0, 0 }, 0.35f);
            r->drawRectPx(mx, my, MW, mh, { 0.13f, 0.145f, 0.17f }, 0.99f);
            bool inMenu = in.mouseX >= mx && in.mouseX < mx + MW && in.mouseY >= my && in.mouseY < my + mh;
            for (int i = 0; i < itemCount; i++) {
                float iy = my + 4 + i * IH;
                bool hov = inMenu && in.mouseY >= iy && in.mouseY < iy + IH;
                if (hov) r->drawRectPx(mx + 2, iy, MW - 4, IH, { 0.2f, 0.32f, 0.5f }, 1);
                r->drawTextLine(mx + 12, iy + 3, items[i], { 0.87f, 0.9f, 0.95f }, 1);
                if (hov && in.mousePressed) {
                    int id = C.addNode(i == 0 ? BP_VAR_GET : BP_VAR_SET, snapGrid(varMenuWX), snapGrid(varMenuWY));
                    BPNode* nn = C.byId(id);
                    if (nn) {
                        snprintf(nn->sname, sizeof(nn->sname), "%s", v.name);
                        // A newly-created Set Color starts opaque. Alpha zero is
                        // still available explicitly through the RGBA picker.
                        if (i == 1 && v.type == PIN_COLOR) nn->litAlpha[1] = 1.0f;
                    }
                    selNode = id;
                    dirty = true;
                    varMenuOpen = false;
                }
            }
            if (in.mousePressed && !inMenu) varMenuOpen = false;
        }
    }

    // ── marquee selection rectangle ──
    if (selecting) {
        float x0 = selX0 < in.mouseX ? selX0 : in.mouseX;
        float x1 = selX0 < in.mouseX ? in.mouseX : selX0;
        float y0 = selY0 < in.mouseY ? selY0 : in.mouseY;
        float y1 = selY0 < in.mouseY ? in.mouseY : selY0;
        r->drawRectPx(x0, y0, x1 - x0, y1 - y0, { 0.30f, 0.62f, 0.99f }, 0.12f);
        r->drawRectPx(x0, y0, x1 - x0, 1, { 0.30f, 0.62f, 0.99f }, 0.8f);
        r->drawRectPx(x0, y1, x1 - x0, 1, { 0.30f, 0.62f, 0.99f }, 0.8f);
        r->drawRectPx(x0, y0, 1, y1 - y0, { 0.30f, 0.62f, 0.99f }, 0.8f);
        r->drawRectPx(x1, y0, 1, y1 - y0, { 0.30f, 0.62f, 0.99f }, 0.8f);
    }

    // ── variable / function drag ghost ──
    if (dragVarActive && dragVarIdx >= 0 && dragVarIdx < (int)graph.vars.size()) {
        const char* nm = graph.vars[dragVarIdx].name;
        float tw = r->textWidth(nm);
        r->drawRectPx(in.mouseX + 12, in.mouseY + 8, tw + 14, 19, { 0.1f, 0.11f, 0.13f }, 0.92f);
        r->drawTextLine(in.mouseX + 19, in.mouseY + 10, nm, { 0.85f, 0.9f, 1.0f }, 1);
    }
    if (dragFuncActive && dragFuncIdx >= 0 && dragFuncIdx < (int)graph.funcs.size()) {
        bool timerTarget = false;
        for (const BPNode& n : C.nodes) {
            if (n.def != BP_TIMER_SET_FUNC) continue;
            float nw, nh; nodeRect(n, &nw, &nh);
            float nx = ox + n.x * Z, ny = oy + n.y * Z;
            if (in.mouseX >= nx && in.mouseX < nx + nw && in.mouseY >= ny && in.mouseY < ny + nh) {
                timerTarget = true;
                break;
            }
        }
        std::string nm = timerTarget ? std::string("assign to timer: ") + graph.funcs[dragFuncIdx].name
                                     : std::string("f  ") + graph.funcs[dragFuncIdx].name;
        float tw = r->textWidth(nm.c_str());
        r->drawRectPx(in.mouseX + 12, in.mouseY + 8, tw + 14, 19,
                      timerTarget ? Vec3{ 0.16f, 0.32f, 0.22f } : Vec3{ 0.12f, 0.1f, 0.16f }, 0.92f);
        r->drawTextLine(in.mouseX + 19, in.mouseY + 10, nm.c_str(), { 0.85f, 0.8f, 1.0f }, 1);
    }

    drawMyBlueprintMenus(ui);   // My Blueprint right-click menu + rename popup (topmost)
    ui.reclipPanel();
    r_ = nullptr;
    finishHistoryFrame(historyBefore, in.mouseDown);
}