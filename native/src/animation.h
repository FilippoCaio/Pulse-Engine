#pragma once
#include "math.h"
#include <string>
#include <vector>

struct AnimationKey {
    int entityId = 0;
    std::string objectName;
    bool localSpace = false;     // relative to the object's parent
    float time = 0;
    Vec3 position;
    Quat rotation;
    Vec3 scale = { 1, 1, 1 };
};

struct AnimationEventKey {
    float time = 0;
    std::string name;
};

struct AnimationClipAsset {
    float length = 1.0f;
    bool loop = true;
    std::vector<AnimationKey> keys;
    std::vector<AnimationEventKey> events;

    void sortKeys();
    AnimationKey evaluate(float time) const;
    AnimationKey evaluateTrack(float time, int entityId, const std::string& objectName,
                               bool allowLoop = true) const;
    std::string serialize() const;
    bool deserialize(const std::string& text);
};

struct AnimatorState {
    int id = 0;
    std::string name = "State";
    std::string clip;
    float x = 80, y = 80;
    float speed = 1.0f;
    bool mirror = false;
};

struct AnimatorTransition {
    int from = 0, to = 0;
    float duration = 0.20f;
    int blendCurve = 1;          // 0 linear, 1 ease in/out, 2 ease in, 3 ease out
    bool hasExitTime = true;
    float exitTime = 1.0f;       // normalized source time
    std::string parameter;
    int condition = 0;           // 0 always, 1 true, 2 false, 3 >, 4 <, 5 trigger
    float threshold = 0.0f;
};

enum AnimatorParameterType { ANIM_PARAM_FLOAT = 0, ANIM_PARAM_BOOL, ANIM_PARAM_TRIGGER };
struct AnimatorParameter {
    std::string name = "Parameter";
    int type = ANIM_PARAM_FLOAT;
    float defaultValue = 0;
};

struct AnimatorControllerAsset {
    int nextId = 1;
    int defaultState = 0;
    std::vector<AnimatorState> states;
    std::vector<AnimatorTransition> transitions;
    std::vector<AnimatorParameter> parameters;

    AnimatorControllerAsset();
    void ensureDefaults();
    AnimatorState* byId(int id);
    const AnimatorState* byId(int id) const;
    int addState(const std::string& name, const std::string& clip, float x, float y);
    bool connect(int from, int to);
    AnimatorTransition* transition(int from, int to);
    void removeState(int id);
    std::string serialize() const;
    bool deserialize(const std::string& text);
};
