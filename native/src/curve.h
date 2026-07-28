#pragma once
#include "ui.h"
#include <string>
#include <vector>

enum CurveInterp { CURVE_CONSTANT = 0, CURVE_LINEAR, CURVE_SMOOTH };

struct CurveKey {
    float time = 0;
    float value = 0;
    float arriveTangent = 0;
    float leaveTangent = 0;
    bool tangentUser = false;
};

struct CurveAsset {
    CurveInterp interp = CURVE_LINEAR;
    std::vector<CurveKey> keys{ { 0, 0 }, { 1, 1 } };

    void sortKeys();
    float evaluate(float time) const;
    std::string serialize() const;
    bool deserialize(const std::string& text);
};

class CurveEditor {
public:
    CurveAsset curve;
    std::string curPath;     // relative to projectDir
    std::string projectDir;
    bool dirty = false;
    void (*logFn)(int, const char*, ...) = nullptr;

    bool loadFrom(const std::string& absPath, const std::string& rel);
    bool save();
    void draw(UI& ui);

private:
    int selected_ = -1;
    bool dragging_ = false;
    bool panning_ = false;
    int tangentDrag_ = 0;       // -1 arrive, +1 leave
    float panMouseX_ = 0, panMouseY_ = 0;
    bool viewInitialized_ = false;
    float viewTime_ = 0.5f, viewValue_ = 0.5f;
    float viewTimeSpan_ = 1.2f, viewValueSpan_ = 1.4f;
    int frame_ = 0;
    int lastClickFrame_ = -1000;
    float lastClickX_ = 0, lastClickY_ = 0;

    void sortKeepingSelection(float time, float value);
    void fitView();
};
