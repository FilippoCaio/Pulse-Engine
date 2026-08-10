#include <winsock2.h>
#include <ws2tcpip.h>
#include "live_session.h"
#include "engine_version.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <random>

namespace {
constexpr unsigned short DISCOVERY_PORT = 47830;
constexpr uint32_t MAX_FRAME = 32u * 1024u * 1024u;
constexpr uint8_t MSG_JOIN = 1;
constexpr uint8_t MSG_ACCEPT = 2;
constexpr uint8_t MSG_SCENE = 3;
constexpr uint8_t MSG_PROPOSAL = 4;
constexpr uint8_t MSG_PRESENCE = 5;
constexpr uint8_t MSG_PEER = 6;
constexpr uint8_t MSG_LEAVE = 7;
constexpr uint8_t MSG_PROJECT_INFO = 8;
constexpr uint8_t MSG_FILE_BEGIN = 9;
constexpr uint8_t MSG_FILE_CHUNK = 10;
constexpr uint8_t MSG_FILE_END = 11;
constexpr uint8_t MSG_PROJECT_END = 12;
constexpr uint8_t MSG_REJECT = 13;
constexpr size_t PROJECT_CHUNK = 64u * 1024u;
namespace fs = std::filesystem;

uint64_t nowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void nonBlocking(SOCKET s) { u_long one = 1; ioctlsocket(s, FIONBIO, &one); }
void closeSocket(SOCKET& s) { if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; } }

void putU32(std::string& out, uint32_t v) {
    uint32_t n = htonl(v); out.append((const char*)&n, sizeof(n));
}
bool getU32(const std::string& in, size_t& p, uint32_t& v) {
    if (p + 4 > in.size()) return false;
    uint32_t n; memcpy(&n, in.data() + p, 4); p += 4; v = ntohl(n); return true;
}
void putU64(std::string& out, uint64_t v) {
    putU32(out, (uint32_t)(v >> 32)); putU32(out, (uint32_t)(v & 0xffffffffu));
}
bool getU64(const std::string& in, size_t& p, uint64_t& v) {
    uint32_t hi, lo; if (!getU32(in, p, hi) || !getU32(in, p, lo)) return false;
    v = ((uint64_t)hi << 32) | lo; return true;
}
void putFloat(std::string& out, float v) { out.append((const char*)&v, sizeof(v)); }
bool getFloat(const std::string& in, size_t& p, float& v) {
    if (p + 4 > in.size()) return false; memcpy(&v, in.data() + p, 4); p += 4; return true;
}
void putString(std::string& out, const std::string& v) {
    putU32(out, (uint32_t)v.size()); out += v;
}
bool getString(const std::string& in, size_t& p, std::string& v, size_t maxLen = 256) {
    uint32_t n; if (!getU32(in, p, n) || n > maxLen || p + n > in.size()) return false;
    v.assign(in.data() + p, n); p += n; return true;
}
std::string frame(uint8_t type, const std::string& payload) {
    std::string out; out.reserve(payload.size() + 5);
    putU32(out, (uint32_t)payload.size() + 1); out.push_back((char)type); out += payload; return out;
}
std::string cleanName(std::string name) {
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }), name.end());
    if (name.empty()) name = "Developer";
    if (name.size() > 32) name.resize(32);
    return name;
}
Vec3 peerColor(uint32_t id) {
    static const Vec3 colors[] = {{.20f,.68f,1.f},{1.f,.40f,.32f},{.45f,.90f,.42f},{.93f,.55f,1.f},{1.f,.78f,.25f},{.25f,.90f,.83f}};
    return colors[id % (sizeof(colors) / sizeof(colors[0]))];
}
bool safeRelativePath(const std::string& rel) {
    if (rel.empty() || rel.size() > 1024 || rel.find(':') != std::string::npos) return false;
    fs::path p(rel);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p) if (part == "..") return false;
    return true;
}
}

struct LiveSession::Impl {
    struct Connection {
        SOCKET socket = INVALID_SOCKET;
        uint32_t peerId = 0;
        std::string rx;
        std::string tx;
        size_t txOffset = 0;
        bool joined = false;
        bool wantsProject = false;
        bool projectSending = false;
        bool projectFinished = false;
        bool rejectAfterSend = false;
        size_t projectFileIndex = 0;
        std::ifstream projectFile;
        uint64_t projectFileRemaining = 0;
    };

    struct HostFile { std::string absolute, relative; uint64_t size = 0; };

    Role role = Role::Offline;
    SOCKET discovery = INVALID_SOCKET;
    SOCKET listener = INVALID_SOCKET;
    std::vector<Connection> connections;
    std::vector<Peer> peers;
    std::deque<Event> events;
    std::string sessionCode;
    std::string displayName = "Developer";
    std::string statusText = "Offline";
    uint32_t ownId = 0;
    uint32_t nextPeerId = 1;
    unsigned short listenPort = 0;
    uint64_t lastDiscovery = 0;
    uint64_t lastPresence = 0;
    bool winsockReady = false;
    std::string hostProjectRoot, hostProjectName, hostCurrentLevel;
    std::vector<HostFile> hostFiles;
    uint64_t hostProjectBytes = 0;
    std::string receiveRoot, receivedProjectName, receivedCurrentLevel;
    std::ofstream incomingFile;
    uint64_t incomingRemaining = 0, projectBytesExpected = 0, projectBytesReceived = 0;
    uint32_t projectFilesExpected = 0, projectFilesReceived = 0;
    bool incomingProject = false;

    bool startWinsock() {
        if (winsockReady) return true;
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
        winsockReady = true; return true;
    }

    void queue(Connection& c, uint8_t type, const std::string& payload) {
        if (c.tx.size() - c.txOffset > MAX_FRAME * 2u) return;
        if (c.txOffset && c.txOffset == c.tx.size()) { c.tx.clear(); c.txOffset = 0; }
        c.tx += frame(type, payload);
    }

    Peer* peer(uint32_t id) {
        for (Peer& p : peers) if (p.id == id) return &p;
        return nullptr;
    }

    std::string encodePeer(const Peer& p) {
        std::string out; putU32(out, p.id); putString(out, p.name);
        putFloat(out, p.color.x); putFloat(out, p.color.y); putFloat(out, p.color.z);
        putFloat(out, p.cameraPosition.x); putFloat(out, p.cameraPosition.y); putFloat(out, p.cameraPosition.z);
        putFloat(out, p.cameraTarget.x); putFloat(out, p.cameraTarget.y); putFloat(out, p.cameraTarget.z);
        out.push_back(p.cameraValid ? 1 : 0); return out;
    }

    bool decodePeer(const std::string& in, Peer& p) {
        size_t at = 0; uint32_t id;
        if (!getU32(in, at, id) || !getString(in, at, p.name, 32)) return false;
        p.id = id;
        if (!getFloat(in, at, p.color.x) || !getFloat(in, at, p.color.y) || !getFloat(in, at, p.color.z) ||
            !getFloat(in, at, p.cameraPosition.x) || !getFloat(in, at, p.cameraPosition.y) || !getFloat(in, at, p.cameraPosition.z) ||
            !getFloat(in, at, p.cameraTarget.x) || !getFloat(in, at, p.cameraTarget.y) || !getFloat(in, at, p.cameraTarget.z) || at >= in.size()) return false;
        p.cameraValid = in[at] != 0; return true;
    }

    void broadcast(uint8_t type, const std::string& payload, uint32_t except = 0) {
        for (Connection& c : connections) if (c.joined && c.peerId != except) queue(c, type, payload);
    }

    void beginProjectTransfer(Connection& c) {
        if (!c.wantsProject || hostProjectRoot.empty()) return;
        std::string info;
        putString(info, hostProjectName); putString(info, hostCurrentLevel);
        putU32(info, (uint32_t)hostFiles.size()); putU64(info, hostProjectBytes);
        queue(c, MSG_PROJECT_INFO, info);
        c.projectSending = true;
    }

    void pumpProjectTransfer(Connection& c) {
        if (!c.projectSending || c.projectFinished || c.tx.size() - c.txOffset > 512u * 1024u) return;
        if (!c.projectFile.is_open()) {
            if (c.projectFileIndex >= hostFiles.size()) {
                queue(c, MSG_PROJECT_END, {}); c.projectFinished = true; return;
            }
            const HostFile& file = hostFiles[c.projectFileIndex];
            c.projectFile.open(file.absolute, std::ios::binary);
            if (!c.projectFile) { c.projectFileIndex++; return; }
            c.projectFileRemaining = file.size;
            std::string begin; putString(begin, file.relative); putU64(begin, file.size);
            queue(c, MSG_FILE_BEGIN, begin);
            if (file.size == 0) {
                queue(c, MSG_FILE_END, {}); c.projectFile.close(); c.projectFileIndex++;
            }
            return;
        }
        size_t want = (size_t)std::min<uint64_t>(PROJECT_CHUNK, c.projectFileRemaining);
        std::string chunk(want, '\0');
        c.projectFile.read(chunk.data(), (std::streamsize)want);
        size_t got = (size_t)c.projectFile.gcount(); chunk.resize(got);
        if (got) { queue(c, MSG_FILE_CHUNK, chunk); c.projectFileRemaining -= got; }
        if (!got || c.projectFileRemaining == 0) {
            queue(c, MSG_FILE_END, {}); c.projectFile.close(); c.projectFileIndex++;
        }
    }

    void drop(size_t index, const char* reason) {
        uint32_t id = connections[index].peerId;
        closeSocket(connections[index].socket);
        connections.erase(connections.begin() + index);
        if (id) {
            peers.erase(std::remove_if(peers.begin(), peers.end(), [=](const Peer& p) { return p.id == id; }), peers.end());
            std::string payload; putU32(payload, id); broadcast(MSG_LEAVE, payload);
            events.push_back({EventType::Left, id, reason ? reason : "Disconnected"});
        }
        if (role == Role::Guest) {
            statusText = "Host disconnected";
            events.push_back({EventType::Error, 0, statusText});
        }
    }

    void handle(Connection& c, uint8_t type, const std::string& payload) {
        if (role == Role::Host && type == MSG_JOIN) {
            size_t at = 0; std::string code, name;
            if (c.joined || !getString(payload, at, code, 12) || !getString(payload, at, name, 32) || code != sessionCode) return;
            c.wantsProject = at < payload.size() && payload[at++] != 0;
            uint32_t protocol = 0; std::string engineVersion;
            if (!getU32(payload, at, protocol) || !getString(payload, at, engineVersion, 32) || protocol != IMPULSO_LIVE_PROTOCOL) {
                std::string reason = "Incompatible engine version. Host uses " IMPULSO_ENGINE_VERSION ".";
                queue(c, MSG_REJECT, reason); c.rejectAfterSend = true; return;
            }
            c.joined = true; c.peerId = nextPeerId++;
            Peer p; p.id = c.peerId; p.name = cleanName(name); p.color = peerColor(p.id); peers.push_back(p);
            std::string accept; putU32(accept, p.id); queue(c, MSG_ACCEPT, accept);
            for (const Peer& known : peers) queue(c, MSG_PEER, encodePeer(known));
            Peer hostPeer; hostPeer.id = 0; hostPeer.name = displayName; hostPeer.color = peerColor(0);
            queue(c, MSG_PEER, encodePeer(hostPeer));
            broadcast(MSG_PEER, encodePeer(p), p.id);
            beginProjectTransfer(c);
            events.push_back({EventType::Joined, p.id, p.name});
        } else if (role == Role::Guest && type == MSG_ACCEPT) {
            size_t at = 0; if (getU32(payload, at, ownId)) { c.joined = true; statusText = "Connected to host"; }
        } else if (type == MSG_SCENE && role == Role::Guest) {
            events.push_back({EventType::SceneSnapshot, 0, payload});
        } else if (type == MSG_PROPOSAL && role == Role::Host && c.joined) {
            events.push_back({EventType::SceneProposal, c.peerId, payload});
        } else if (role == Role::Guest && type == MSG_REJECT) {
            statusText = payload.empty() ? "The host rejected this engine build." : payload;
            events.push_back({EventType::Error, 0, statusText});
        } else if (type == MSG_PEER && role == Role::Guest) {
            Peer p; if (!decodePeer(payload, p) || p.id == ownId) return;
            Peer* old = peer(p.id); if (old) *old = p; else peers.push_back(p);
        } else if (type == MSG_LEAVE && role == Role::Guest) {
            size_t at = 0; uint32_t id;
            if (getU32(payload, at, id)) {
                peers.erase(std::remove_if(peers.begin(), peers.end(), [=](const Peer& p) { return p.id == id; }), peers.end());
                events.push_back({EventType::Left, id, "Developer left"});
            }
        } else if (type == MSG_PRESENCE) {
            size_t at = 0; uint32_t id = c.peerId; Peer incoming;
            if (role == Role::Guest && !getU32(payload, at, id)) return;
            Peer* p = peer(id); if (!p) return;
            if (!getFloat(payload, at, incoming.cameraPosition.x) || !getFloat(payload, at, incoming.cameraPosition.y) || !getFloat(payload, at, incoming.cameraPosition.z) ||
                !getFloat(payload, at, incoming.cameraTarget.x) || !getFloat(payload, at, incoming.cameraTarget.y) || !getFloat(payload, at, incoming.cameraTarget.z)) return;
            p->cameraPosition = incoming.cameraPosition; p->cameraTarget = incoming.cameraTarget; p->cameraValid = true;
            if (role == Role::Host) {
                std::string out; putU32(out, id); out.append(payload.data(), payload.size()); broadcast(MSG_PRESENCE, out, id);
            }
        } else if (role == Role::Guest && type == MSG_PROJECT_INFO && !receiveRoot.empty()) {
            size_t at = 0;
            if (!getString(payload, at, receivedProjectName, 128) ||
                !getString(payload, at, receivedCurrentLevel, 1024) ||
                !getU32(payload, at, projectFilesExpected) || !getU64(payload, at, projectBytesExpected) ||
                projectFilesExpected > 100000 || projectBytesExpected > (uint64_t)1024 * 1024 * 1024 * 64) {
                events.push_back({EventType::Error, 0, "Invalid remote project manifest"}); return;
            }
            std::error_code ec; fs::create_directories(receiveRoot, ec);
            incomingProject = !ec; projectFilesReceived = 0; projectBytesReceived = 0;
            statusText = "Receiving project " + receivedProjectName + "...";
        } else if (role == Role::Guest && type == MSG_FILE_BEGIN && incomingProject) {
            size_t at = 0; std::string rel; uint64_t size = 0;
            if (!getString(payload, at, rel, 1024) || !getU64(payload, at, size) || !safeRelativePath(rel) || size > projectBytesExpected) {
                incomingProject = false; events.push_back({EventType::Error, 0, "Unsafe file in remote project"}); return;
            }
            if (incomingFile.is_open()) incomingFile.close();
            fs::path target = fs::path(receiveRoot) / fs::path(rel);
            std::error_code ec; fs::create_directories(target.parent_path(), ec);
            incomingFile.open(target, std::ios::binary); incomingRemaining = size;
            if (!incomingFile) { incomingProject = false; events.push_back({EventType::Error, 0, "Could not create remote project cache"}); }
        } else if (role == Role::Guest && type == MSG_FILE_CHUNK && incomingProject) {
            if (!incomingFile.is_open() || payload.size() > incomingRemaining || projectBytesReceived + payload.size() > projectBytesExpected) {
                incomingProject = false; events.push_back({EventType::Error, 0, "Invalid remote project file stream"}); return;
            }
            incomingFile.write(payload.data(), (std::streamsize)payload.size());
            incomingRemaining -= payload.size(); projectBytesReceived += payload.size();
        } else if (role == Role::Guest && type == MSG_FILE_END && incomingProject) {
            if (incomingFile.is_open()) incomingFile.close();
            if (incomingRemaining != 0) { incomingProject = false; events.push_back({EventType::Error, 0, "Truncated remote project file"}); return; }
            projectFilesReceived++;
            std::string progress = std::to_string(projectFilesReceived) + "/" + std::to_string(projectFilesExpected);
            events.push_back({EventType::ProjectProgress, 0, progress});
        } else if (role == Role::Guest && type == MSG_PROJECT_END && incomingProject) {
            if (incomingFile.is_open()) incomingFile.close();
            if (projectFilesReceived != projectFilesExpected || projectBytesReceived != projectBytesExpected) {
                incomingProject = false; events.push_back({EventType::Error, 0, "Remote project transfer did not complete"}); return;
            }
            incomingProject = false; statusText = "Connected to remote project";
            events.push_back({EventType::ProjectReady, 0, receiveRoot});
        }
    }

    void pumpConnection(size_t& i) {
        Connection& c = connections[i];
        std::array<char, 16384> buf;
        for (;;) {
            int n = recv(c.socket, buf.data(), (int)buf.size(), 0);
            if (n > 0) c.rx.append(buf.data(), n);
            else if (n == 0) { drop(i, "Disconnected"); return; }
            else { int e = WSAGetLastError(); if (e != WSAEWOULDBLOCK) { drop(i, "Network error"); return; } break; }
        }
        while (c.rx.size() >= 4) {
            size_t at = 0; uint32_t n;
            if (!getU32(c.rx, at, n) || n < 1 || n > MAX_FRAME) { drop(i, "Invalid live-session frame"); return; }
            if (c.rx.size() < 4u + n) break;
            uint8_t type = (uint8_t)c.rx[4];
            std::string payload(c.rx.data() + 5, n - 1);
            c.rx.erase(0, 4u + n);
            handle(c, type, payload);
        }
        while (c.txOffset < c.tx.size()) {
            int n = send(c.socket, c.tx.data() + c.txOffset, (int)std::min<size_t>(c.tx.size() - c.txOffset, 65536), 0);
            if (n > 0) c.txOffset += (size_t)n;
            else { int e = WSAGetLastError(); if (e != WSAEWOULDBLOCK) { drop(i, "Network send error"); return; } break; }
        }
        if (c.txOffset == c.tx.size()) { c.tx.clear(); c.txOffset = 0; }
        if (c.rejectAfterSend && c.tx.empty()) { drop(i, "Incompatible engine version"); return; }
        if (role == Role::Host && c.joined) pumpProjectTransfer(c);
        ++i;
    }

    void updateDiscovery() {
        if (discovery == INVALID_SOCKET) return;
        if (role == Role::Guest && connections.empty() && nowMs() - lastDiscovery > 750) {
            std::string msg = "PULSE_DISCOVER_1 " + sessionCode;
            sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(DISCOVERY_PORT); to.sin_addr.s_addr = INADDR_BROADCAST;
            sendto(discovery, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof(to));
            // Windows does not consistently loop a limited broadcast back to a
            // listener on the same PC. Probe loopback too so two local editor
            // instances behave exactly like two machines on the LAN.
            to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            sendto(discovery, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof(to));
            lastDiscovery = nowMs();
        }
        std::array<char, 128> buf; sockaddr_in from{}; int fromLen = sizeof(from);
        for (;;) {
            int n = recvfrom(discovery, buf.data(), (int)buf.size() - 1, 0, (sockaddr*)&from, &fromLen);
            if (n <= 0) break; buf[n] = 0; std::string msg(buf.data(), n);
            if (role == Role::Host && msg == "PULSE_DISCOVER_1 " + sessionCode) {
                std::string offer = "PULSE_OFFER_1 " + sessionCode + " " + std::to_string(listenPort);
                sendto(discovery, offer.data(), (int)offer.size(), 0, (sockaddr*)&from, sizeof(from));
            } else if (role == Role::Guest && connections.empty()) {
                std::string prefix = "PULSE_OFFER_1 " + sessionCode + " ";
                if (msg.rfind(prefix, 0) != 0) continue;
                int port = atoi(msg.c_str() + prefix.size()); if (port < 1 || port > 65535) continue;
                SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s == INVALID_SOCKET) continue;
                // The offer came directly from a live host on the LAN, so this
                // connect normally completes immediately. Switch to nonblocking
                // only afterwards; otherwise the first JOIN send can race an
                // in-progress connect and be reported as WSAENOTCONN.
                DWORD timeout = 1200;
                setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
                sockaddr_in target = from; target.sin_port = htons((unsigned short)port);
                if (connect(s, (sockaddr*)&target, sizeof(target)) == SOCKET_ERROR) { closesocket(s); continue; }
                nonBlocking(s);
                Connection c; c.socket = s;
                std::string joinPayload; putString(joinPayload, sessionCode); putString(joinPayload, displayName);
                joinPayload.push_back(receiveRoot.empty() ? 0 : 1); putU32(joinPayload, IMPULSO_LIVE_PROTOCOL);
                putString(joinPayload, IMPULSO_ENGINE_VERSION); queue(c, MSG_JOIN, joinPayload);
                connections.push_back(std::move(c)); statusText = "Connecting to host...";
            }
        }
    }
};

LiveSession::LiveSession() : impl_(new Impl) {}
LiveSession::~LiveSession() { stop(); if (impl_->winsockReady) WSACleanup(); }

bool LiveSession::host(const std::string& name) {
    stop(); if (!impl_->startWinsock()) { impl_->statusText = "Winsock initialization failed"; return false; }
    impl_->displayName = cleanName(name);
    std::mt19937 rng((unsigned)GetTickCount64() ^ (unsigned)(uintptr_t)this);
    impl_->sessionCode = std::to_string(std::uniform_int_distribution<int>(100000, 999999)(rng));
    impl_->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listener == INVALID_SOCKET) { impl_->statusText = "Could not create host socket"; return false; }
    sockaddr_in local{}; local.sin_family = AF_INET; local.sin_addr.s_addr = INADDR_ANY; local.sin_port = 0;
    if (bind(impl_->listener, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR || listen(impl_->listener, 8) == SOCKET_ERROR) { stop(); impl_->statusText = "Could not listen for developers"; return false; }
    int len = sizeof(local); getsockname(impl_->listener, (sockaddr*)&local, &len); impl_->listenPort = ntohs(local.sin_port); nonBlocking(impl_->listener);
    impl_->discovery = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->discovery == INVALID_SOCKET) { stop(); impl_->statusText = "Could not start discovery"; return false; }
    BOOL yes = TRUE; setsockopt(impl_->discovery, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in udp{}; udp.sin_family = AF_INET; udp.sin_addr.s_addr = INADDR_ANY; udp.sin_port = htons(DISCOVERY_PORT);
    if (bind(impl_->discovery, (sockaddr*)&udp, sizeof(udp)) == SOCKET_ERROR) { stop(); impl_->statusText = "Live discovery port is already in use"; return false; }
    nonBlocking(impl_->discovery); impl_->role = Role::Host; impl_->statusText = "Hosting live session"; return true;
}

bool LiveSession::join(const std::string& code, const std::string& name, const std::string& projectCacheRoot) {
    stop();
    if (code.size() != 6 || !std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; })) { impl_->statusText = "The session code must contain 6 digits"; return false; }
    if (!impl_->startWinsock()) { impl_->statusText = "Winsock initialization failed"; return false; }
    impl_->displayName = cleanName(name); impl_->sessionCode = code; impl_->receiveRoot = projectCacheRoot;
    impl_->discovery = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->discovery == INVALID_SOCKET) { impl_->statusText = "Could not start discovery"; return false; }
    BOOL yes = TRUE; setsockopt(impl_->discovery, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes)); nonBlocking(impl_->discovery);
    impl_->role = Role::Guest; impl_->statusText = "Searching for session on the local network..."; impl_->lastDiscovery = 0; return true;
}

bool LiveSession::shareProject(const std::string& root, const std::string& name, const std::string& currentLevel) {
    if (impl_->role != Role::Host || root.empty()) return false;
    impl_->hostProjectRoot = root; impl_->hostProjectName = cleanName(name); impl_->hostCurrentLevel = currentLevel;
    impl_->hostFiles.clear(); impl_->hostProjectBytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_directory(ec)) {
            std::string n = it->path().filename().string();
            if (n == ".git" || n == ".codex" || n == "Build" || n == "Intermediate" || n == "Saved") it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        uint64_t size = (uint64_t)it->file_size(ec); if (ec) { ec.clear(); continue; }
        fs::path relPath = fs::relative(it->path(), root, ec); if (ec) { ec.clear(); continue; }
        std::string rel = relPath.string(); if (!safeRelativePath(rel)) continue;
        impl_->hostFiles.push_back({it->path().string(), rel, size}); impl_->hostProjectBytes += size;
    }
    return true;
}

void LiveSession::stop() {
    for (auto& c : impl_->connections) closeSocket(c.socket);
    impl_->connections.clear(); closeSocket(impl_->listener); closeSocket(impl_->discovery);
    impl_->peers.clear(); impl_->events.clear(); impl_->role = Role::Offline; impl_->ownId = 0;
    impl_->sessionCode.clear(); impl_->statusText = "Offline"; impl_->listenPort = 0;
    impl_->receiveRoot.clear(); impl_->receivedProjectName.clear(); impl_->receivedCurrentLevel.clear();
    impl_->hostProjectRoot.clear(); impl_->hostProjectName.clear(); impl_->hostCurrentLevel.clear();
    impl_->hostFiles.clear(); impl_->hostProjectBytes = 0;
    impl_->projectBytesExpected = impl_->projectBytesReceived = 0;
    impl_->projectFilesExpected = impl_->projectFilesReceived = 0;
    impl_->incomingProject = false;
    if (impl_->incomingFile.is_open()) impl_->incomingFile.close();
}

void LiveSession::update() {
    if (impl_->role == Role::Offline) return;
    impl_->updateDiscovery();
    if (impl_->role == Role::Host && impl_->listener != INVALID_SOCKET) {
        for (;;) {
            SOCKET s = accept(impl_->listener, nullptr, nullptr); if (s == INVALID_SOCKET) break;
            nonBlocking(s); Impl::Connection c; c.socket = s; impl_->connections.push_back(std::move(c));
        }
    }
    for (size_t i = 0; i < impl_->connections.size();) impl_->pumpConnection(i);
}

void LiveSession::sendPresence(const Vec3& position, const Vec3& target) {
    if (nowMs() - impl_->lastPresence < 50) return; impl_->lastPresence = nowMs();
    std::string p; putFloat(p, position.x); putFloat(p, position.y); putFloat(p, position.z); putFloat(p, target.x); putFloat(p, target.y); putFloat(p, target.z);
    if (impl_->role == Role::Guest && !impl_->connections.empty()) impl_->queue(impl_->connections.front(), MSG_PRESENCE, p);
    else if (impl_->role == Role::Host) { std::string out; putU32(out, 0); out += p; impl_->broadcast(MSG_PRESENCE, out); }
}
void LiveSession::broadcastScene(const std::string& scene) { if (impl_->role == Role::Host && scene.size() <= MAX_FRAME - 1) impl_->broadcast(MSG_SCENE, scene); }
void LiveSession::sendSceneTo(uint32_t id, const std::string& scene) { for (auto& c : impl_->connections) if (c.peerId == id && scene.size() <= MAX_FRAME - 1) impl_->queue(c, MSG_SCENE, scene); }
void LiveSession::proposeScene(const std::string& scene) { if (impl_->role == Role::Guest && !impl_->connections.empty() && scene.size() <= MAX_FRAME - 1) impl_->queue(impl_->connections.front(), MSG_PROPOSAL, scene); }
bool LiveSession::pollEvent(Event& event) { if (impl_->events.empty()) return false; event = std::move(impl_->events.front()); impl_->events.pop_front(); return true; }
LiveSession::Role LiveSession::role() const { return impl_->role; }
bool LiveSession::connected() const { return impl_->role == Role::Host || (impl_->role == Role::Guest && !impl_->connections.empty() && impl_->connections.front().joined); }
const std::string& LiveSession::code() const { return impl_->sessionCode; }
const std::string& LiveSession::status() const { return impl_->statusText; }
const std::vector<LiveSession::Peer>& LiveSession::peers() const { return impl_->peers; }
uint32_t LiveSession::localId() const { return impl_->ownId; }
const std::string& LiveSession::projectRoot() const { return impl_->receiveRoot; }
const std::string& LiveSession::remoteProjectName() const { return impl_->receivedProjectName; }
const std::string& LiveSession::remoteCurrentLevel() const { return impl_->receivedCurrentLevel; }
