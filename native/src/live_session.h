#pragma once

#include "math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Editor collaboration transport. The host owns the canonical scene and is the
// only participant that writes project files. Guests edit an in-memory replica
// and submit their scene to the host, which serializes accepted changes.
class LiveSession {
public:
    enum class Role { Offline, Host, Guest };
    enum class EventType { Joined, Left, SceneSnapshot, SceneProposal, ProjectReady, ProjectProgress, Error };

    struct Peer {
        uint32_t id = 0;
        std::string name;
        Vec3 color{0.25f, 0.65f, 1.0f};
        Vec3 cameraPosition;
        Vec3 cameraTarget;
        bool cameraValid = false;
    };

    struct Event {
        EventType type = EventType::Error;
        uint32_t peerId = 0;
        std::string text;
    };

    LiveSession();
    ~LiveSession();
    LiveSession(const LiveSession&) = delete;
    LiveSession& operator=(const LiveSession&) = delete;

    bool host(const std::string& displayName);
    bool join(const std::string& code, const std::string& displayName,
              const std::string& projectCacheRoot = {});
    // Register the host project for guests joining from the Hub. Files are
    // streamed incrementally; the scene continues over the normal live channel.
    bool shareProject(const std::string& projectRoot, const std::string& projectName,
                      const std::string& currentLevelRelative);
    void stop();
    void update();

    void sendPresence(const Vec3& cameraPosition, const Vec3& cameraTarget);
    void broadcastScene(const std::string& serializedScene);
    void sendSceneTo(uint32_t peerId, const std::string& serializedScene);
    void proposeScene(const std::string& serializedScene);
    bool pollEvent(Event& event);

    Role role() const;
    bool connected() const;
    const std::string& code() const;
    const std::string& status() const;
    const std::vector<Peer>& peers() const;
    uint32_t localId() const;
    const std::string& projectRoot() const;
    const std::string& remoteProjectName() const;
    const std::string& remoteCurrentLevel() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
