#pragma once
#include "math.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

// Reusable project assets inspired by Unreal's Sound Class, Sound Attenuation
// and Sound Concurrency assets. Paths to these files are stored on Audio Source
// components, while an empty path keeps the component's legacy inline values.
struct AudioClassAsset {
    float volume = 1.0f;             // group multiplier, 0..2
    std::string serialize() const;
    bool deserialize(const std::string& text);
};

struct AudioAttenuationAsset {
    bool spatial = true;
    float minDistance = 1.0f;
    float maxDistance = 25.0f;
    int falloff = 0;                 // 0 linear, 1 inverse, 2 exponential
    std::string serialize() const;
    bool deserialize(const std::string& text);
};

struct AudioConcurrencyAsset {
    int maxVoices = 4;
    int resolution = 0;              // 0 prevent new, 1 stop oldest
    std::string serialize() const;
    bool deserialize(const std::string& text);
};

// Lightweight runtime audio backend.  Windows MCI selects the installed codec
// for WAV/MP3/OGG while the engine owns source lifetime, looping and attenuation.
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool play(int ownerId, const std::string& absolutePath, bool loop, float gain = 1.0f);
    void prepareClipChange(int ownerId);
    void stop(int ownerId);
    void stopAll();
    void setPaused(bool paused);
    void setGain(int ownerId, float gain);
    bool isPlaying(int ownerId);
    bool hasVoice(int ownerId) const { return voices_.find(ownerId) != voices_.end(); }
    std::string lastError() const;

private:
    struct Voice {
        std::string alias;
        std::string path;
        bool loop = false;
        int volume = -1;
    };
    std::map<int, Voice> voices_;
    // Voices previously used by the same source stay open and stopped. This
    // avoids synchronous codec/file reopen stalls for alternating footsteps.
    std::map<int, std::map<std::string, Voice>> idleVoices_;
    int serial_ = 1;
    std::atomic<bool> paused_{false};
    std::string lastError_;
    mutable std::mutex errorMutex_;

    struct Job {
        enum Kind { Raw, OpenAndPlay } kind = Raw;
        std::string command;
        std::string alias;
        std::string path;
        bool openDevice = false;
        bool loop = false;
        bool reportError = false;
        int volume = 500;
    };
    std::thread worker_;
    std::mutex queueMutex_;
    std::condition_variable queueWake_;
    std::deque<Job> queue_;
    bool shuttingDown_ = false;

    bool commandDirect(const std::string& text, bool reportError = true);
    void enqueueRaw(const std::string& text, bool reportError = false);
    void enqueuePlay(const Voice& voice, bool openDevice, int volume);
    void workerLoop();
};
