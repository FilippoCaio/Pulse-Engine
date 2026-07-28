#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <sstream>

std::string AudioClassAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSO_AUDIO_CLASS 1\n";
    o << "volume " << volume << "\n";
    return o.str();
}

bool AudioClassAsset::deserialize(const std::string& text) {
    if (text.rfind("IMPULSO_AUDIO_CLASS", 0) != 0) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("volume ", 0) == 0) sscanf(line.c_str(), "volume %f", &volume);
    }
    volume = clampf(volume, 0.0f, 2.0f);
    return true;
}

std::string AudioAttenuationAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSO_AUDIO_ATTENUATION 1\n";
    o << "spatial " << (spatial ? 1 : 0) << "\n";
    o << "distance " << minDistance << " " << maxDistance << "\n";
    o << "falloff " << falloff << "\n";
    return o.str();
}

bool AudioAttenuationAsset::deserialize(const std::string& text) {
    if (text.rfind("IMPULSO_AUDIO_ATTENUATION", 0) != 0) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("spatial ", 0) == 0) { int v = 1; sscanf(line.c_str(), "spatial %d", &v); spatial = v != 0; }
        else if (line.rfind("distance ", 0) == 0) sscanf(line.c_str(), "distance %f %f", &minDistance, &maxDistance);
        else if (line.rfind("falloff ", 0) == 0) sscanf(line.c_str(), "falloff %d", &falloff);
    }
    minDistance = minDistance < 0.01f ? 0.01f : minDistance;
    maxDistance = maxDistance < minDistance + 0.01f ? minDistance + 0.01f : maxDistance;
    if (falloff < 0 || falloff > 2) falloff = 0;
    return true;
}

std::string AudioConcurrencyAsset::serialize() const {
    std::ostringstream o;
    o << "IMPULSO_AUDIO_CONCURRENCY 1\n";
    o << "max_voices " << maxVoices << "\n";
    o << "resolution " << resolution << "\n";
    return o.str();
}

bool AudioConcurrencyAsset::deserialize(const std::string& text) {
    if (text.rfind("IMPULSO_AUDIO_CONCURRENCY", 0) != 0) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("max_voices ", 0) == 0) sscanf(line.c_str(), "max_voices %d", &maxVoices);
        else if (line.rfind("resolution ", 0) == 0) sscanf(line.c_str(), "resolution %d", &resolution);
    }
    if (maxVoices < 1) maxVoices = 1;
    if (maxVoices > 64) maxVoices = 64;
    if (resolution < 0 || resolution > 1) resolution = 0;
    return true;
}

AudioSystem::AudioSystem() : worker_(&AudioSystem::workerLoop, this) {}

AudioSystem::~AudioSystem() {
    stopAll();
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        shuttingDown_ = true;
    }
    queueWake_.notify_one();
    if (worker_.joinable()) worker_.join();
}

bool AudioSystem::commandDirect(const std::string& text, bool reportError) {
    MCIERROR err = mciSendStringA(text.c_str(), nullptr, 0, nullptr);
    if (!err) return true;
    if (reportError) {
        char msg[256] = "Unknown audio error";
        mciGetErrorStringA(err, msg, sizeof(msg));
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = msg;
    }
    return false;
}

void AudioSystem::enqueueRaw(const std::string& text, bool reportError) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        Job job;job.kind=Job::Raw;job.command=text;job.reportError=reportError;
        queue_.push_back(std::move(job));
    }
    queueWake_.notify_one();
}

void AudioSystem::enqueuePlay(const Voice& voice, bool openDevice, int volume) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        Job job;job.kind=Job::OpenAndPlay;job.alias=voice.alias;job.path=voice.path;
        job.openDevice=openDevice;job.loop=voice.loop;job.volume=volume;job.reportError=true;
        queue_.push_back(std::move(job));
    }
    queueWake_.notify_one();
}

void AudioSystem::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueWake_.wait(lock,[&]{return shuttingDown_||!queue_.empty();});
            if(queue_.empty()){if(shuttingDown_)break;continue;}
            job=std::move(queue_.front());queue_.pop_front();
        }
        if(job.kind==Job::Raw){commandDirect(job.command,job.reportError);continue;}
        if(job.openDevice){
            std::string open="open \""+job.path+"\" alias "+job.alias;
            if(!commandDirect(open,false)){
                commandDirect("close "+job.alias,false);
                open="open \""+job.path+"\" type mpegvideo alias "+job.alias;
                if(!commandDirect(open,true))continue;
            }
        }
        commandDirect("seek "+job.alias+" to start",false);
        commandDirect("setaudio "+job.alias+" volume to "+std::to_string(job.volume),false);
        if(!commandDirect("play "+job.alias+(job.loop?" repeat":""),true))continue;
        if(paused_.load())commandDirect("pause "+job.alias,false);
    }
}

std::string AudioSystem::lastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

bool AudioSystem::play(int ownerId, const std::string& absolutePath, bool loop, float gain) {
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_.clear();
    }
    DWORD attributes=GetFileAttributesA(absolutePath.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)){
        std::lock_guard<std::mutex> lock(errorMutex_);lastError_="Audio file not found.";return false;
    }
    auto active = voices_.find(ownerId);
    if (active != voices_.end()) {
        enqueueRaw(std::string("stop ") + active->second.alias, false);
        if (active->second.path != absolutePath) {
            idleVoices_[ownerId][active->second.path] = std::move(active->second);
            voices_.erase(active);
        } else if (active->second.loop != loop) {
            enqueueRaw(std::string("close ") + active->second.alias, false);
            voices_.erase(active);
        }
    }

    if (voices_.find(ownerId) == voices_.end()) {
        auto ownerIdle = idleVoices_.find(ownerId);
        if (ownerIdle != idleVoices_.end()) {
            auto cached = ownerIdle->second.find(absolutePath);
            if (cached != ownerIdle->second.end()) {
                if (cached->second.loop == loop) voices_[ownerId] = std::move(cached->second);
                else enqueueRaw(std::string("close ") + cached->second.alias, false);
                ownerIdle->second.erase(cached);
                if (ownerIdle->second.empty()) idleVoices_.erase(ownerIdle);
            }
        }
    }

    bool openDevice=false;
    if (voices_.find(ownerId) == voices_.end()) {
        char alias[48];
        snprintf(alias, sizeof(alias), "impulso_audio_%d", serial_++);
        Voice voice;
        voice.alias = alias;
        voice.path = absolutePath;
        voice.loop = loop;
        voices_[ownerId] = std::move(voice);
        openDevice=true;
    }

    Voice& voice = voices_[ownerId];
    gain=clampf(gain,0.0f,2.0f);voice.volume=(int)(gain*500.0f+0.5f);
    enqueuePlay(voice,openDevice,voice.volume);
    return true;
}

void AudioSystem::prepareClipChange(int ownerId) {
    auto it = voices_.find(ownerId);
    if (it == voices_.end()) return;
    enqueueRaw(std::string("stop ") + it->second.alias, false);
    auto& cache = idleVoices_[ownerId];
    auto old = cache.find(it->second.path);
    if (old != cache.end()) {
        enqueueRaw(std::string("close ") + old->second.alias, false);
        cache.erase(old);
    }
    cache[it->second.path] = std::move(it->second);
    voices_.erase(it);
}

void AudioSystem::stop(int ownerId) {
    auto it = voices_.find(ownerId);
    if (it != voices_.end()) {
        enqueueRaw(std::string("stop ") + it->second.alias, false);
        enqueueRaw(std::string("close ") + it->second.alias, false);
        voices_.erase(it);
    }
    auto idle = idleVoices_.find(ownerId);
    if (idle != idleVoices_.end()) {
        for (auto& [path, voice] : idle->second) {
            enqueueRaw(std::string("stop ") + voice.alias, false);
            enqueueRaw(std::string("close ") + voice.alias, false);
        }
        idleVoices_.erase(idle);
    }
}

void AudioSystem::stopAll() {
    while (!voices_.empty()) stop(voices_.begin()->first);
    while (!idleVoices_.empty()) stop(idleVoices_.begin()->first);
    paused_.store(false);
}

void AudioSystem::setPaused(bool paused) {
    if (paused_.load() == paused) return;
    paused_.store(paused);
    for (auto& [id, voice] : voices_)
        enqueueRaw(std::string(paused ? "pause " : "resume ") + voice.alias, false);
}

void AudioSystem::setGain(int ownerId, float gain) {
    auto it = voices_.find(ownerId);
    if (it == voices_.end()) return;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 2.0f) gain = 2.0f;
    // MCI exposes a device range of 0..1000.  Treat 500 as the normal 1x
    // level so the editor's 2x multiplier remains representable and portable.
    int volume = (int)(gain * 500.0f + 0.5f);
    int delta = volume - it->second.volume;
    if (delta < 0) delta = -delta;
    if (delta < 4) return;
    it->second.volume = volume;
    enqueueRaw("setaudio " + it->second.alias + " volume to " + std::to_string(volume), false);
}

bool AudioSystem::isPlaying(int ownerId) {
    // Never issue a synchronous status query from the game thread.
    return voices_.find(ownerId) != voices_.end();
}
