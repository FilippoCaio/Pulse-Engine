// ─── Lettura della libreria di Pulse Asset Manager (.a3dlib) ───
//
// L'editor non incastra la finestra dell'altro programma: ne rilegge il
// catalogo e ne disegna l'elenco. Pulse Asset Manager resta il proprietario
// del file — da qui partono solo SELECT.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// variants.engine, come li numera Pulse Asset Manager
enum { A3D_SOURCE = 0, A3D_UNITY = 1, A3D_UNREAL = 2, A3D_GODOT = 3, A3D_ENGINE_COUNT = 4 };
// assets.kind
enum { A3D_MODEL = 0, A3D_SOUND = 1, A3D_TEXTURE = 2, A3D_KIND_COUNT = 3 };

const char* a3dEngineName(int engine);
const char* a3dKindName(int kind);

struct A3DFile {
    int role = 12;              // files.role: decide la sottocartella nella libreria
    std::string relPath;        // relativo alla radice della libreria, in UTF-8
    std::string ext;            // senza punto, minuscolo
    int64_t size = 0;
};

struct A3DVariant {
    int64_t id = 0;
    int engine = A3D_SOURCE;
    std::string name;
    std::vector<A3DFile> files;
};

struct A3DAsset {
    int64_t id = 0;
    std::string name, category, author;
    int kind = A3D_MODEL;
    int rating = 0;
    bool favorite = false;
    std::string thumbRel;                 // relativo alla radice, vuoto se non c'e'
    std::vector<std::string> tags;
    std::vector<A3DVariant> variants;
    // nome, categoria, autore e tag in minuscolo, tutto attaccato: il filtro
    // scorre migliaia di asset a ogni tasto premuto e non puo' ricomporlo ogni volta
    std::string haystack;

    const A3DVariant* variantFor(int engine) const;
    int defaultVariant() const;           // indice della variante da proporre, -1 se non ce ne sono
};

class AssetLibrary {
public:
    // Rilegge il catalogo da capo. false quando non c'e' niente da leggere:
    // `error` dice perche', ed e' quello che il pannello mostra.
    bool refresh();
    // Il file (o il suo -wal) e' stato scritto dopo l'ultima lettura. In WAL le
    // scritture finiscono nel -wal e la data del .a3dlib non si muove, quindi
    // guardare solo quella significherebbe non accorgersi mai di niente.
    bool changedOnDisk() const;
    void clear();

    bool loaded = false;
    std::string path;                     // il .a3dlib in uso
    std::string root;                     // cartella che contiene i file veri
    std::string error;                    // perche' non e' caricata
    std::vector<A3DAsset> assets;

    // Percorso assoluto di un rel_path o di un thumb_rel. UTF-8: va bene per
    // loadPngTexture (che riconverte lui) e per a3dWiden prima di toccare il disco.
    std::string absolutePath(const std::string& rel) const;

private:
    int64_t stamp_ = 0;                   // data dell'ultima lettura
};

// La libreria aperta per ultima da Pulse Asset Manager, "" se non risulta.
std::string a3dLibraryPath();
// UTF-8 → UTF-16. I percorsi del catalogo sono UTF-8, mentre std::filesystem
// costruito da una std::string li interpreta nella codepage ANSI: senza questa
// conversione un asset con un accento nel nome non si copierebbe.
std::wstring a3dWiden(const std::string& utf8);
