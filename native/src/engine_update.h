#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class EngineUpdater {
public:
    enum class State { Disabled, Idle, Checking, UpToDate, Available, Downloading, Ready, Error };

    EngineUpdater() = default;
    ~EngineUpdater();
    EngineUpdater(const EngineUpdater&) = delete;
    EngineUpdater& operator=(const EngineUpdater&) = delete;

    void initialize(const std::string& executableDirectory);
    void checkNow();
    void download();
    bool applyAndRestart();

    State state() const { return state_.load(); }
    std::string status() const;
    std::string availableVersion() const;
    std::string releaseNotes() const;
    uint64_t downloadedBytes() const { return downloaded_.load(); }
    uint64_t totalBytes() const { return total_.load(); }
    bool configured() const;

    static bool isNewerVersion(const std::string& candidate, const std::string& current);
    static bool validateManifestText(const std::string& text, std::string* version = nullptr);
    // Entry point used by the freshly downloaded executable. It waits for the
    // old process, replaces it with rollback protection, and starts the editor.
    static int runApplyMode(const std::string& targetExecutable, unsigned long oldProcessId);

private:
    struct Manifest { std::string version, url, sha256, notes; };
    std::atomic<State> state_{State::Disabled};
    std::atomic<uint64_t> downloaded_{0}, total_{0};
    mutable std::mutex mutex_;
    std::thread worker_;
    std::string baseDir_, manifestUrl_, status_, stagedPath_;
    Manifest manifest_;
    bool autoDownload_ = false;

    void joinWorker();
    void setStatus(State state, const std::string& text);
    void checkWorker();
    bool downloadWorker();
    static bool parseManifest(const std::string& text, Manifest& out);
};
