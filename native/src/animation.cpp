#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "animation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

static Vec3 lerp3(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
static Quat nlerpQuat(Quat a, Quat b, float t) {
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (dot < 0) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; }
    Quat q;
    q.x = a.x + (b.x-a.x)*t; q.y = a.y + (b.y-a.y)*t;
    q.z = a.z + (b.z-a.z)*t; q.w = a.w + (b.w-a.w)*t;
    return q.normalized();
}

void AnimationClipAsset::sortKeys() {
    std::sort(keys.begin(), keys.end(), [](const AnimationKey& a, const AnimationKey& b) {
        if (a.entityId != b.entityId) return a.entityId < b.entityId;
        if (a.objectName != b.objectName) return a.objectName < b.objectName;
        return a.time < b.time;
    });
    std::sort(events.begin(),events.end(),[](const AnimationEventKey&a,const AnimationEventKey&b){
        if(a.time!=b.time)return a.time<b.time;return a.name<b.name;
    });
    // La durata non e' un valore indipendente: coincide sempre con l'ultima
    // chiave della clip, come nella timeline di Unity.
    length = 0.01f;
    for (const AnimationKey& k : keys) length = std::max(length, k.time);
    for (const AnimationEventKey& e : events) length = std::max(length, e.time);
}

AnimationKey AnimationClipAsset::evaluate(float time) const {
    if (keys.empty()) return {};
    return evaluateTrack(time, keys.front().entityId, keys.front().objectName);
}

AnimationKey AnimationClipAsset::evaluateTrack(float time, int entityId, const std::string& objectName,
                                                bool allowLoop) const {
    std::vector<const AnimationKey*> track;
    for (const AnimationKey& k : keys) {
        bool same = entityId != 0 ? k.entityId == entityId
                                  : (k.entityId == 0 && k.objectName == objectName);
        if (same) track.push_back(&k);
    }
    if (track.empty()) return {};
    std::sort(track.begin(), track.end(), [](const AnimationKey* a, const AnimationKey* b) {
        return a->time < b->time;
    });
    float duration = std::max(length, 0.0001f);
    if (loop && allowLoop) { time = fmodf(time, duration); if (time < 0) time += duration; }
    else time = std::max(0.0f, std::min(duration, time));
    if (track.size() == 1 || time <= track.front()->time) return *track.front();
    if (time >= track.back()->time) return *track.back();
    size_t i = 0;
    while (i + 1 < track.size() && time > track[i + 1]->time) i++;
    const AnimationKey& a = *track[i]; const AnimationKey& b = *track[i + 1];
    float span = b.time - a.time;
    float u = span > 0.000001f ? (time - a.time) / span : 0;
    AnimationKey out;
    out.entityId = a.entityId;
    out.objectName = a.objectName;
    out.localSpace = a.localSpace;
    out.time = time;
    out.position = lerp3(a.position, b.position, u);
    out.rotation = nlerpQuat(a.rotation, b.rotation, u);
    out.scale = lerp3(a.scale, b.scale, u);
    return out;
}

std::string AnimationClipAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSOANIM 4\nlength " << length << "\nloop " << (loop ? 1 : 0) << "\n";
    for (const auto& k : keys)
        o << "key " << k.entityId << " " << std::quoted(k.objectName) << " " << (k.localSpace ? 1 : 0) << " " << k.time
          << " " << k.position.x << " " << k.position.y << " " << k.position.z
          << " " << k.rotation.x << " " << k.rotation.y << " " << k.rotation.z << " " << k.rotation.w
          << " " << k.scale.x << " " << k.scale.y << " " << k.scale.z << "\n";
    for(const AnimationEventKey& e:events)o<<"event "<<std::quoted(e.name)<<" "<<e.time<<"\n";
    return o.str();
}

bool AnimationClipAsset::deserialize(const std::string& text) {
    std::istringstream in(text); std::string line; int version = 0;
    if (!std::getline(in, line) || sscanf(line.c_str(), "IMPULSOANIM %d", &version) != 1) return false;
    keys.clear();events.clear();
    while (std::getline(in, line)) {
        int v = 0; AnimationKey k;
        if (sscanf(line.c_str(), "length %f", &length) == 1) continue;
        if (sscanf(line.c_str(), "loop %d", &v) == 1) { loop = v != 0; continue; }
        if(version>=4&&line.rfind("event ",0)==0){
            std::istringstream ls(line);std::string tag;AnimationEventKey event;
            if(ls>>tag>>std::quoted(event.name)>>event.time&&!event.name.empty())events.push_back(event);
            continue;
        }
        if (line.rfind("key ", 0) != 0) continue;
        if (version >= 3) {
            std::istringstream ls(line); std::string tag; int local = 0;
            if (ls >> tag >> k.entityId >> std::quoted(k.objectName) >> local >> k.time
                   >> k.position.x >> k.position.y >> k.position.z
                   >> k.rotation.x >> k.rotation.y >> k.rotation.z >> k.rotation.w
                   >> k.scale.x >> k.scale.y >> k.scale.z) { k.localSpace = local != 0; keys.push_back(k); }
        } else if (version >= 2) {
            std::istringstream ls(line); std::string tag;
            if (ls >> tag >> k.entityId >> std::quoted(k.objectName) >> k.time
                   >> k.position.x >> k.position.y >> k.position.z
                   >> k.rotation.x >> k.rotation.y >> k.rotation.z >> k.rotation.w
                   >> k.scale.x >> k.scale.y >> k.scale.z) keys.push_back(k);
        } else if (sscanf(line.c_str(), "key %f %f %f %f %f %f %f %f %f %f %f",
                          &k.time, &k.position.x, &k.position.y, &k.position.z,
                          &k.rotation.x, &k.rotation.y, &k.rotation.z, &k.rotation.w,
                          &k.scale.x, &k.scale.y, &k.scale.z) == 11) keys.push_back(k);
    }
    length = std::max(0.01f, length); sortKeys(); return true;
}

AnimatorControllerAsset::AnimatorControllerAsset() { ensureDefaults(); }
void AnimatorControllerAsset::ensureDefaults() {
    if (states.empty()) {
        AnimatorState initial; initial.id = nextId++; initial.name = "State 1"; initial.x = 190; initial.y = 80;
        states.push_back(initial);
    }
    if (!byId(defaultState)) defaultState = states.front().id;
    for (const AnimatorState& state : states) nextId = std::max(nextId, state.id + 1);
}
AnimatorState* AnimatorControllerAsset::byId(int id) { for (auto& s : states) if (s.id == id) return &s; return nullptr; }
const AnimatorState* AnimatorControllerAsset::byId(int id) const { for (auto& s : states) if (s.id == id) return &s; return nullptr; }
int AnimatorControllerAsset::addState(const std::string& name, const std::string& clip, float x, float y) {
    AnimatorState s; s.id = nextId++; s.name = name; s.clip = clip; s.x = x; s.y = y; states.push_back(s);
    if (!defaultState) defaultState = s.id; return s.id;
}
bool AnimatorControllerAsset::connect(int from, int to) {
    if (from == to || !byId(from) || !byId(to)) return false;
    for (const auto& t : transitions) if (t.from == from && t.to == to) return false;
    transitions.push_back({from,to}); return true;
}
AnimatorTransition* AnimatorControllerAsset::transition(int from, int to) {
    for (auto& t : transitions) if (t.from == from && t.to == to) return &t;
    return nullptr;
}
void AnimatorControllerAsset::removeState(int id) {
    states.erase(std::remove_if(states.begin(), states.end(), [&](const AnimatorState& s){ return s.id == id; }), states.end());
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(), [&](const AnimatorTransition& t){ return t.from == id || t.to == id; }), transitions.end());
    if (defaultState == id) defaultState = states.empty() ? 0 : states.front().id;
    ensureDefaults();
}
std::string AnimatorControllerAsset::serialize() const {
    std::ostringstream o; o << "IMPULSOANIMCTRL 2\nnext " << nextId << "\ndefault " << defaultState << "\n";
    for (const auto& p : parameters)
        o << "parameter " << p.type << " " << p.defaultValue << " " << std::quoted(p.name) << "\n";
    for (const auto& s : states)
        o << "state " << s.id << " " << s.x << " " << s.y << " " << s.speed << " " << (s.mirror?1:0)
          << " " << std::quoted(s.clip) << " " << std::quoted(s.name) << "\n";
    for (const auto& t : transitions)
        o << "transition " << t.from << " " << t.to << " " << t.duration << " " << t.blendCurve
          << " " << (t.hasExitTime?1:0) << " " << t.exitTime << " " << t.condition << " " << t.threshold
          << " " << std::quoted(t.parameter) << "\n";
    return o.str();
}
bool AnimatorControllerAsset::deserialize(const std::string& text) {
    std::istringstream in(text); std::string line; int version = 0;
    if (!std::getline(in, line) || sscanf(line.c_str(), "IMPULSOANIMCTRL %d", &version) != 1) return false;
    states.clear(); transitions.clear(); parameters.clear(); nextId = 1; defaultState = 0;
    while (std::getline(in, line)) {
        if (sscanf(line.c_str(), "next %d", &nextId) == 1) continue;
        if (sscanf(line.c_str(), "default %d", &defaultState) == 1) continue;
        if (line.rfind("parameter ",0)==0 && version>=2) { std::istringstream ls(line);std::string tag;AnimatorParameter p;if(ls>>tag>>p.type>>p.defaultValue>>std::quoted(p.name))parameters.push_back(p);continue; }
        if (line.rfind("transition ",0)==0) { AnimatorTransition t;if(version>=2){std::istringstream ls(line);std::string tag;int exit=1;if(ls>>tag>>t.from>>t.to>>t.duration>>t.blendCurve>>exit>>t.exitTime>>t.condition>>t.threshold>>std::quoted(t.parameter)){t.hasExitTime=exit!=0;transitions.push_back(t);} }else if(sscanf(line.c_str(),"transition %d %d",&t.from,&t.to)==2)transitions.push_back(t);continue; }
        if (line.rfind("state ", 0) == 0) {
            std::istringstream ls(line); std::string tag; AnimatorState s;
            if(version>=2){int mirror=0;ls>>tag>>s.id>>s.x>>s.y>>s.speed>>mirror>>std::quoted(s.clip)>>std::quoted(s.name);s.mirror=mirror!=0;}
            else ls >> tag >> s.id >> s.x >> s.y >> std::quoted(s.clip) >> std::quoted(s.name);
            if (s.clip == "-") s.clip.clear();
            if (s.name.empty()) s.name = "State"; states.push_back(s);
            nextId = std::max(nextId, s.id + 1);
        }
    }
    ensureDefaults();
    return true;
}
