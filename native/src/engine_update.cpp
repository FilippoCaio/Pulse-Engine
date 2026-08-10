#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include "engine_update.h"
#include "engine_version.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n); return out;
}

bool httpsGet(const std::string& url, std::string& out, uint64_t maxBytes,
              std::atomic<uint64_t>* progress = nullptr, std::atomic<uint64_t>* total = nullptr) {
    if (url.rfind("https://", 0) != 0) return false;
    std::wstring wide = widen(url);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) return false;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(L"ImpulsoEngineUpdater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, statusSize = sizeof(status);
    if (ok) ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status == 200;
    wchar_t lengthText[64] = {}; DWORD lengthSize = sizeof(lengthText);
    if (ok && WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                  lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
        uint64_t expected = _wcstoui64(lengthText, nullptr, 10);
        if (total) total->store(expected);
        if (expected > maxBytes) ok = false;
    }
    out.clear(); std::vector<char> buffer(64 * 1024);
    while (ok) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buffer.data(), (DWORD)buffer.size(), &got)) { ok = false; break; }
        if (!got) break;
        if ((uint64_t)out.size() + got > maxBytes) { ok = false; break; }
        out.append(buffer.data(), got); if (progress) progress->store(out.size());
    }
    if (request) WinHttpCloseHandle(request); if (connection) WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
    return ok;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); return s;
}

std::string jsonValue(const std::string& text, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t p = text.find(needle); if (p == std::string::npos) return {};
    p = text.find(':', p + needle.size()); if (p == std::string::npos) return {};
    p = text.find('"', p + 1); if (p == std::string::npos) return {};
    std::string out;
    for (++p; p < text.size(); p++) {
        char c = text[p]; if (c == '"') break;
        if (c == '\\' && p + 1 < text.size()) { char n = text[++p]; out += n == 'n' ? '\n' : n; }
        else out += c;
    }
    return out;
}

bool sha256File(const std::string& path, std::string& hex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0, hashSize = 0, cb = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectSize, sizeof(objectSize), &cb, 0) >= 0 &&
              BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hashSize, sizeof(hashSize), &cb, 0) >= 0;
    std::vector<unsigned char> object(objectSize), digest(hashSize);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    std::ifstream file(path, std::ios::binary); std::vector<unsigned char> buffer(64 * 1024);
    while (ok && file) { file.read((char*)buffer.data(), buffer.size()); std::streamsize n = file.gcount(); if (n > 0) ok = BCryptHashData(hash, buffer.data(), (ULONG)n, 0) >= 0; }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), hashSize, 0) >= 0;
    if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
    static const char digits[] = "0123456789abcdef"; hex.clear();
    if (ok) for (unsigned char b : digest) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    return ok;
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }
}

EngineUpdater::~EngineUpdater() { joinWorker(); }

void EngineUpdater::initialize(const std::string& executableDirectory) {
    baseDir_ = executableDirectory;
    std::ifstream cfg(fs::path(baseDir_) / "update.cfg", std::ios::binary);
    if (!cfg) {
        // Development builds live in native/build while update.cfg lives in native.
        cfg.open(fs::path(baseDir_).parent_path() / "update.cfg", std::ios::binary);
    }
    std::string line;
    while (std::getline(cfg, line)) {
        line = trim(line); if (line.empty() || line[0] == '#') continue;
        size_t space = line.find_first_of(" \t");
        std::string key = space == std::string::npos ? line : line.substr(0, space);
        std::string value = space == std::string::npos ? "" : trim(line.substr(space + 1));
        if (key == "manifest" && value != "-") manifestUrl_ = value;
        else if (key == "auto_download") autoDownload_ = value == "1" || value == "true";
    }
    if (manifestUrl_.empty()) setStatus(State::Disabled, "Update server is not configured.");
    else { setStatus(State::Idle, "Ready to check for updates."); checkNow(); }
}

void EngineUpdater::joinWorker() { if (worker_.joinable()) worker_.join(); }
void EngineUpdater::setStatus(State state, const std::string& text) { { std::lock_guard<std::mutex> lock(mutex_); status_ = text; } state_.store(state); }
std::string EngineUpdater::status() const { std::lock_guard<std::mutex> lock(mutex_); return status_; }
std::string EngineUpdater::availableVersion() const { std::lock_guard<std::mutex> lock(mutex_); return manifest_.version; }
std::string EngineUpdater::releaseNotes() const { std::lock_guard<std::mutex> lock(mutex_); return manifest_.notes; }
bool EngineUpdater::configured() const { return !manifestUrl_.empty(); }

bool EngineUpdater::isNewerVersion(const std::string& candidate, const std::string& current) {
    auto parts = [](const std::string& s) { std::vector<int> out; std::stringstream in(s); std::string p; while (std::getline(in, p, '.')) { size_t n = 0; while (n < p.size() && std::isdigit((unsigned char)p[n])) n++; out.push_back(n ? atoi(p.substr(0,n).c_str()) : 0); } return out; };
    auto a = parts(candidate), b = parts(current); size_t count = (std::max)(a.size(), b.size()); a.resize(count); b.resize(count);
    for (size_t i = 0; i < count; i++) if (a[i] != b[i]) return a[i] > b[i]; return false;
}

bool EngineUpdater::parseManifest(const std::string& text, Manifest& out) {
    out = {};
    std::string source = text;
    if (source.size() >= 3 && (unsigned char)source[0] == 0xef && (unsigned char)source[1] == 0xbb && (unsigned char)source[2] == 0xbf)
        source.erase(0, 3);
    if (!source.empty() && source.find_first_not_of(" \t\r\n") != std::string::npos && source[source.find_first_not_of(" \t\r\n")] == '{') {
        out.version = jsonValue(source, "version"); out.url = jsonValue(source, "url");
        out.sha256 = jsonValue(source, "sha256"); out.notes = jsonValue(source, "notes");
    } else {
        std::istringstream input(source); std::string line;
        if (!std::getline(input, line) || trim(line) != "IMPULSO_UPDATE 1") return false;
        while (std::getline(input, line)) {
            size_t p = line.find(' '); std::string key = p == std::string::npos ? line : line.substr(0,p);
            std::string value = p == std::string::npos ? "" : trim(line.substr(p+1));
            if (key == "version") out.version = value; else if (key == "url") out.url = value;
            else if (key == "sha256") out.sha256 = value; else if (key == "notes") out.notes = value;
        }
    }
    std::transform(out.sha256.begin(), out.sha256.end(), out.sha256.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return !out.version.empty() && out.url.rfind("https://",0) == 0 && out.sha256.size() == 64 &&
           std::all_of(out.sha256.begin(), out.sha256.end(), [](unsigned char c){ return std::isxdigit(c) != 0; });
}

bool EngineUpdater::validateManifestText(const std::string& text, std::string* version) {
    Manifest manifest; bool ok = parseManifest(text, manifest);
    if (ok && version) *version = manifest.version;
    return ok;
}

void EngineUpdater::checkNow() {
    State s = state(); if (!configured() || s == State::Checking || s == State::Downloading) return;
    joinWorker(); worker_ = std::thread([this]{ checkWorker(); });
}

void EngineUpdater::checkWorker() {
    setStatus(State::Checking, "Checking for engine updates...");
    std::string text; Manifest found;
    if (!httpsGet(manifestUrl_, text, 1024 * 1024) || !parseManifest(text, found)) {
        setStatus(State::Error, "Could not read a valid HTTPS update manifest."); return;
    }
    { std::lock_guard<std::mutex> lock(mutex_); manifest_ = found; }
    if (!isNewerVersion(found.version, IMPULSO_ENGINE_VERSION)) {
        setStatus(State::UpToDate, std::string("Engine ") + IMPULSO_ENGINE_VERSION + " is up to date."); return;
    }
    setStatus(State::Available, "Engine " + found.version + " is available.");
    if (autoDownload_) downloadWorker();
}

void EngineUpdater::download() {
    if (state() != State::Available && state() != State::Error) return;
    joinWorker(); worker_ = std::thread([this]{ downloadWorker(); });
}

bool EngineUpdater::downloadWorker() {
    Manifest m; { std::lock_guard<std::mutex> lock(mutex_); m = manifest_; }
    if (m.url.empty()) { setStatus(State::Error, "No update package URL in the manifest."); return false; }
    setStatus(State::Downloading, "Downloading engine " + m.version + "..."); downloaded_.store(0); total_.store(0);
    std::string bytes;
    if (!httpsGet(m.url, bytes, (uint64_t)1024 * 1024 * 1024, &downloaded_, &total_)) {
        setStatus(State::Error, "Engine download failed."); return false;
    }
    std::error_code ec; fs::path dir = fs::temp_directory_path(ec) / "ImpulsoUpdates"; fs::create_directories(dir, ec);
    fs::path staged = dir / ("impulso_" + m.version + ".exe");
    std::ofstream file(staged, std::ios::binary); file.write(bytes.data(), (std::streamsize)bytes.size()); file.close();
    std::string digest;
    if (!file || !sha256File(staged.string(), digest) || digest != m.sha256) {
        fs::remove(staged, ec); setStatus(State::Error, "Downloaded engine failed SHA-256 verification."); return false;
    }
    { std::lock_guard<std::mutex> lock(mutex_); stagedPath_ = staged.string(); }
    setStatus(State::Ready, "Engine " + m.version + " is ready to install."); return true;
}

bool EngineUpdater::applyAndRestart() {
    if (state() != State::Ready) return false;
    joinWorker(); std::string staged; { std::lock_guard<std::mutex> lock(mutex_); staged = stagedPath_; }
    char target[MAX_PATH] = {}; GetModuleFileNameA(nullptr, target, MAX_PATH);
    std::string command = quote(staged) + " --apply-update " + quote(target) + " " + std::to_string(GetCurrentProcessId());
    STARTUPINFOA si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    std::vector<char> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(0);
    bool ok = CreateProcessA(staged.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    else setStatus(State::Error, "Could not start the engine installer.");
    return ok;
}

int EngineUpdater::runApplyMode(const std::string& target, unsigned long oldPid) {
    HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, oldPid); if (old) { WaitForSingleObject(old, 30000); CloseHandle(old); }
    char source[MAX_PATH] = {}; GetModuleFileNameA(nullptr, source, MAX_PATH);
    std::string backup = target + ".previous"; DeleteFileA(backup.c_str());
    bool moved = MoveFileExA(target.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    bool copied = false;
    for (int i = 0; i < 20 && !copied; i++) { copied = CopyFileA(source, target.c_str(), FALSE) != FALSE; if (!copied) Sleep(250); }
    if (!copied) { if (moved) MoveFileExA(backup.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING); return 2; }
    ShellExecuteA(nullptr, "open", target.c_str(), nullptr, fs::path(target).parent_path().string().c_str(), SW_SHOWNORMAL);
    return 0;
}
