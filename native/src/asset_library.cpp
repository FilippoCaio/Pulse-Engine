#include "asset_library.h"
#include "../third_party/sqlite/sqlite3.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Testo e percorsi
// ---------------------------------------------------------------------------

std::wstring a3dWiden(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring wide((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], n);
    return wide;
}

static std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &utf8[0], n, nullptr, nullptr);
    return utf8;
}

static bool existsUtf8(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::exists(fs::path(a3dWiden(path)), ec);
}

// %LOCALAPPDATA%\PulseAssetManager — la cartella dati del programma, senza spazi
// nel nome (il README dell'app scrive "Pulse Asset Manager", ma il codice usa
// questa: vedi PathUtil.cpp, appDataDir).
static std::string assetManagerDataDir() {
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    if (!local || !*local) return {};
    return narrow(local) + "/PulseAssetManager";
}

std::string a3dLibraryPath() {
    const std::string dir = assetManagerDataDir();
    if (dir.empty()) return {};

    // Il programma annota le ultime dieci librerie aperte, la piu' recente in
    // cima. Dare per scontato il percorso predefinito mostrerebbe la libreria
    // sbagliata a chi tiene la propria altrove.
    if (HANDLE file = CreateFileW(a3dWiden(dir + "/librerie_recenti.txt").c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        file != INVALID_HANDLE_VALUE) {
        char buffer[8192];
        DWORD read = 0;
        ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
        CloseHandle(file);
        buffer[read] = '\0';

        std::string text(buffer, buffer + read), line;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            line = text.substr(start, end - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            if (!line.empty() && existsUtf8(line)) return line;
            start = end + 1;
        }
    }

    const std::string fallback = dir + "/Libreria.a3dlib";
    return existsUtf8(fallback) ? fallback : std::string();
}

// ---------------------------------------------------------------------------
// Etichette
// ---------------------------------------------------------------------------

const char* a3dEngineName(int engine) {
    switch (engine) {
    case A3D_UNITY:  return "Unity";
    case A3D_UNREAL: return "Unreal";
    case A3D_GODOT:  return "Godot";
    default:         return "Source";
    }
}

const char* a3dKindName(int kind) {
    switch (kind) {
    case A3D_SOUND:   return "Sound";
    case A3D_TEXTURE: return "Texture";
    default:          return "3D model";
    }
}

const A3DVariant* A3DAsset::variantFor(int engine) const {
    for (const A3DVariant& v : variants)
        if (v.engine == engine) return &v;
    return nullptr;
}

int A3DAsset::defaultVariant() const {
    // Source per prima: sono gli originali del DCC (fbx, obj, png, wav), cioe'
    // gli unici formati che il Content browser di questo editor sa gia' aprire.
    for (size_t i = 0; i < variants.size(); i++)
        if (variants[i].engine == A3D_SOURCE) return (int)i;
    return variants.empty() ? -1 : 0;
}

// ---------------------------------------------------------------------------
// Lettura del catalogo
// ---------------------------------------------------------------------------

static std::string columnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? std::string((const char*)text) : std::string();
}

static void appendLowercase(std::string& target, const std::string& source) {
    for (char c : source) target.push_back((char)tolower((unsigned char)c));
    target.push_back(' ');
}

// La radice dei file, che non e' detto stia accanto al catalogo: il programma la
// tiene in `settings`, ed e' un percorso assoluto scritto una volta sola. Dopo un
// rinomino della cartella dati punta a un posto che non c'e' piu' — Pulse Asset
// Manager lo riscrive al primo avvio, ma se leggiamo prima di quel momento le
// anteprime risulterebbero tutte mancanti.
static std::string readRoot(sqlite3* db, const std::string& dataDir) {
    std::string stored;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key='library.root';", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) stored = columnText(stmt, 0);
        sqlite3_finalize(stmt);
    }
    while (!stored.empty() && (stored.back() == ' ' || stored.back() == '/' || stored.back() == '\\'))
        stored.pop_back();

    const std::string fallback = dataDir.empty() ? std::string() : dataDir + "/Library";
    if (stored.empty()) return fallback;
    if (existsUtf8(stored)) return stored;

    const std::string relocated =
        dataDir.empty() ? std::string()
                        : dataDir + "/" + narrow(fs::path(a3dWiden(stored)).filename().wstring());
    if (!relocated.empty() && existsUtf8(relocated)) return relocated;
    return stored;
}

void AssetLibrary::clear() {
    loaded = false;
    assets.clear();
    root.clear();
    stamp_ = 0;
}

std::string AssetLibrary::absolutePath(const std::string& rel) const {
    if (rel.empty() || root.empty()) return {};
    return root + "/" + rel;
}

static int64_t writeStamp(const std::string& path) {
    std::error_code ec;
    int64_t newest = 0;
    for (const std::string& candidate : { path, path + "-wal" }) {
        auto time = fs::last_write_time(fs::path(a3dWiden(candidate)), ec);
        if (!ec) newest = (std::max)(newest, (int64_t)time.time_since_epoch().count());
    }
    return newest;
}

bool AssetLibrary::changedOnDisk() const {
    return loaded && writeStamp(path) != stamp_;
}

bool AssetLibrary::refresh() {
    const std::string dataDir = assetManagerDataDir();
    assets.clear();
    root.clear();
    error.clear();
    loaded = false;

    path = a3dLibraryPath();
    if (path.empty()) {
        error = "No Pulse Asset Manager library found on this machine.";
        return false;
    }
    const int64_t stampBefore = writeStamp(path);

    // Non in sola lettura, anche se qui si leggerebbe soltanto: su un database in
    // modalita' WAL quel flag ne impedisce l'apertura quando nessun'altra
    // connessione lo tiene gia' aperto, perche' sqlite deve poter creare il file
    // -shm e in sola lettura non puo'. Con Pulse Asset Manager chiuso l'apertura
    // fallirebbe sempre. Il vincolo sta nelle query, non nel flag: qui sotto ci
    // sono solo SELECT.
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        error = std::string("Library not readable: ") + (db ? sqlite3_errmsg(db) : "unknown error");
        if (db) sqlite3_close(db);
        return false;
    }
    // Se il programma sta scrivendo proprio adesso, si aspetta invece di
    // presentare una libreria vuota.
    sqlite3_busy_timeout(db, 2000);

    struct Query {
        const char* sql;
        sqlite3_stmt* stmt = nullptr;
    };
    Query assetsQuery{ "SELECT id,name,category,author,rating,favorite,thumb_rel,kind"
                       "  FROM assets ORDER BY name COLLATE NOCASE, id;" };
    Query tagsQuery{ "SELECT at.asset_id,t.name FROM asset_tags at"
                     "  JOIN tags t ON t.id=at.tag_id ORDER BY t.name COLLATE NOCASE;" };
    Query variantsQuery{ "SELECT id,asset_id,engine,name FROM variants ORDER BY asset_id,engine,id;" };
    Query filesQuery{ "SELECT variant_id,role,rel_path,ext,size FROM files ORDER BY variant_id,id;" };

    Query* queries[] = { &assetsQuery, &tagsQuery, &variantsQuery, &filesQuery };
    for (Query* q : queries) {
        if (sqlite3_prepare_v2(db, q->sql, -1, &q->stmt, nullptr) != SQLITE_OK) {
            error = std::string("Library not readable: ") + sqlite3_errmsg(db);
            for (Query* other : queries) sqlite3_finalize(other->stmt);
            sqlite3_close(db);
            return false;
        }
    }

    std::vector<std::pair<int64_t, size_t>> assetIndex;      // assets.id → posizione
    while (sqlite3_step(assetsQuery.stmt) == SQLITE_ROW) {
        A3DAsset a;
        a.id = sqlite3_column_int64(assetsQuery.stmt, 0);
        a.name = columnText(assetsQuery.stmt, 1);
        a.category = columnText(assetsQuery.stmt, 2);
        a.author = columnText(assetsQuery.stmt, 3);
        a.rating = sqlite3_column_int(assetsQuery.stmt, 4);
        a.favorite = sqlite3_column_int(assetsQuery.stmt, 5) != 0;
        a.thumbRel = columnText(assetsQuery.stmt, 6);
        int kind = sqlite3_column_int(assetsQuery.stmt, 7);
        a.kind = (kind >= 0 && kind < A3D_KIND_COUNT) ? kind : A3D_MODEL;
        assetIndex.push_back({ a.id, assets.size() });
        assets.push_back(std::move(a));
    }
    std::sort(assetIndex.begin(), assetIndex.end());

    auto findAsset = [&](int64_t id) -> A3DAsset* {
        auto it = std::lower_bound(assetIndex.begin(), assetIndex.end(), std::make_pair(id, (size_t)0));
        if (it == assetIndex.end() || it->first != id) return nullptr;
        return &assets[it->second];
    };

    while (sqlite3_step(tagsQuery.stmt) == SQLITE_ROW) {
        if (A3DAsset* a = findAsset(sqlite3_column_int64(tagsQuery.stmt, 0)))
            a->tags.push_back(columnText(tagsQuery.stmt, 1));
    }

    // variants.id → posizione (asset, variante). Non puntatori: le varianti
    // vengono aggiunte in coda mentre l'indice si costruisce, e la prima
    // riallocazione del vettore renderebbe invalido tutto quello gia' segnato.
    struct VariantSlot { int64_t id; size_t asset, variant; };
    std::vector<VariantSlot> variantIndex;
    while (sqlite3_step(variantsQuery.stmt) == SQLITE_ROW) {
        const int64_t assetId = sqlite3_column_int64(variantsQuery.stmt, 1);
        auto slot = std::lower_bound(assetIndex.begin(), assetIndex.end(),
                                     std::make_pair(assetId, (size_t)0));
        if (slot == assetIndex.end() || slot->first != assetId) continue;
        A3DAsset& a = assets[slot->second];
        A3DVariant v;
        v.id = sqlite3_column_int64(variantsQuery.stmt, 0);
        int engine = sqlite3_column_int(variantsQuery.stmt, 2);
        v.engine = (engine >= 0 && engine < A3D_ENGINE_COUNT) ? engine : A3D_SOURCE;
        v.name = columnText(variantsQuery.stmt, 3);
        variantIndex.push_back({ v.id, slot->second, a.variants.size() });
        a.variants.push_back(std::move(v));
    }
    std::sort(variantIndex.begin(), variantIndex.end(),
              [](const VariantSlot& x, const VariantSlot& y) { return x.id < y.id; });

    // I file arrivano in un colpo solo invece che una query per variante: con
    // qualche migliaio di voci la differenza fra le due forme si vede.
    while (sqlite3_step(filesQuery.stmt) == SQLITE_ROW) {
        const int64_t variantId = sqlite3_column_int64(filesQuery.stmt, 0);
        auto it = std::lower_bound(variantIndex.begin(), variantIndex.end(), variantId,
                                   [](const VariantSlot& entry, int64_t id) { return entry.id < id; });
        if (it == variantIndex.end() || it->id != variantId) continue;
        A3DFile f;
        f.role = sqlite3_column_int(filesQuery.stmt, 1);
        f.relPath = columnText(filesQuery.stmt, 2);
        f.ext = columnText(filesQuery.stmt, 3);
        f.size = sqlite3_column_int64(filesQuery.stmt, 4);
        for (char& c : f.ext) c = (char)tolower((unsigned char)c);
        if (!f.ext.empty() && f.ext.front() == '.') f.ext.erase(f.ext.begin());
        assets[it->asset].variants[it->variant].files.push_back(std::move(f));
    }

    for (Query* q : queries) sqlite3_finalize(q->stmt);
    root = readRoot(db, dataDir);
    sqlite3_close(db);

    for (A3DAsset& a : assets) {
        a.haystack.reserve(a.name.size() + a.category.size() + a.author.size() + 16);
        appendLowercase(a.haystack, a.name);
        appendLowercase(a.haystack, a.category);
        appendLowercase(a.haystack, a.author);
        for (const std::string& tag : a.tags) appendLowercase(a.haystack, tag);
    }

    stamp_ = stampBefore;
    loaded = true;
    return true;
}
