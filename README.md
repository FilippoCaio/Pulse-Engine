# Pulse Engine / ⚛ Impulso

Questa repository contiene due varianti/implementazioni correlate del motore di gioco:

- Pulse Engine: implementazione nativa in C++ per Windows (editor + renderer OpenGL)
- ⚛ Impulso: implementazione web/Node (editor Web, physics, renderer WebGL2)

---

# Pulse Engine

Game engine 3D con editor, scritto in C++ per Windows. Si compila con il solo
compilatore di Visual Studio e si collega alle librerie di sistema — OpenGL, Win32,
WIC per le immagini, WinMM per l'audio. **Niente package manager**: non c'e' niente
da scaricare prima di compilare.

L'unica libreria di terze parti sta in `native/third_party/sqlite/`, in sorgente,
e serve alla sola finestra **Asset Manager**: la libreria di Pulse Asset Manager e'
un database, e leggerla e' l'unico modo per cercarci dentro senza chiedere all'altro
programma di essere aperto. Il resto del motore non la tocca.

Circa 34.600 righe di C++17. Il renderer nasce dal prototipo WebGL2 che lo precede:
gli shader GLSL sono gli stessi, portati a OpenGL 3.3 core.

---

## Compilare

Serve **Visual Studio 2022** con il carico di lavoro C++ (lo script trova da solo
l'installazione tramite `vswhere`, quindi non va aperto un prompt speciale).

```bash
native\build.bat
```

Produce `native\build\impulso.exe`. Passando un nome si cambia quello dell'eseguibile:
`build.bat pulse.exe`.

---

## Com'e' fatto

```
native/
  src/
    app.cpp          editor: finestre, pannelli, browser degli asset, hub dei progetti
    blueprint.cpp    programmazione visuale a nodi: grafo, compilazione, esecuzione
    widget.cpp       sistema di UI a widget (slot, ancoraggi, layout)
    ui.cpp           controlli immediati dell'editor
    physics.cpp      corpi rigidi, collisioni, vincoli
    render.cpp       renderer OpenGL 3.3, shader GLSL
    scene.cpp        entita' e componenti, salvataggio e caricamento dei livelli
    dock.cpp         aggancio dei pannelli a quattro vie
    material.cpp     materiali
    animation.cpp    interpolazione delle clip
    curve.cpp        curve di animazione
    audio.cpp        riproduzione
    asset_library.cpp lettura del catalogo di Pulse Asset Manager (.a3dlib)
  assets/icons/      icone dei tipi di asset, lette a runtime
  third_party/sqlite/ amalgamazione sqlite3, solo per il catalogo di cui sopra
  progetto/          progetto di esempio
```

`build.bat` compila `sqlite3.c` una volta sola e riusa `build\sqlite3.obj`: sono nove
megabyte di C che non cambiano mai, e ricompilarli a ogni build costerebbe mezzo minuto
per niente. Aggiornando l'amalgamazione va cancellato quell'oggetto.

## Asset Manager: la libreria dentro l'editor

Il pannello **Asset Manager** (menu *Windows*, accanto al Content browser) mostra la
libreria di Pulse Asset Manager: si cerca per nome, categoria, autore o tag — tutte le
parole scritte devono comparire, com'e' nell'altro programma — e si filtra per tipo o
per un tag. Doppio clic su una scheda, o il pulsante *Import*, copiano i file della
variante scelta nella cartella aperta in quel momento nel Content browser.

Tre cose che vale la pena sapere:

- **La variante proposta e' Source**, cioe' gli originali del DCC: fbx, obj, png, wav
  sono i formati che questo editor sa aprire. Le varianti Unity, Unreal e Godot restano
  selezionabili, ma i loro file (`.uasset`, `.prefab`, `.tscn`) vengono saltati con un
  avviso nel Log, perche' qui non significherebbero niente.
- **Un asset di piu' file si porta dietro una cartella** col proprio nome; uno di un file
  solo entra dov'e' aperto il browser. Senza questa regola una mesh con cinque texture
  sparpaglierebbe sei file nella cartella corrente.
- **Da qui non si scrive mai** sul catalogo: tag, voti e varianti si modificano in Pulse
  Asset Manager, che e' il proprietario del database. L'elenco si aggiorna da solo quando
  quel file viene scritto, senza bisogno di premere *Refresh*.

Se la libreria non c'e' — Pulse Asset Manager non installato, o mai aperto — il pannello
lo dice e il resto dell'editor funziona come prima.

## Editor o gioco: decide un file

Lo stesso eseguibile fa due cose diverse a seconda di cosa trova accanto a se':

- da solo, parte come **editor**;
- se accanto c'e' un `impulso_build.cfg` che comincia con `IMPULSO_BUILD 1`, parte come
  **gioco impacchettato**, caricando il progetto e la scena indicati nel file e saltando
  del tutto l'interfaccia dell'editor.

E' il meccanismo con cui si esporta un gioco: si copia l'eseguibile accanto ai dati del
progetto e si scrive quel file. Vale la pena saperlo prima di spostare l'eseguibile,
perche' un `impulso_build.cfg` dimenticato nella cartella fa partire il gioco quando ci
si aspetta l'editor.

## Cosa deve stare accanto all'eseguibile

L'editor cerca le proprie risorse a partire dalla cartella che lo contiene, non da quella
di lavoro: `assets/icons/` deve stare li' accanto, altrimenti il browser degli asset
resta senza icone. Nella stessa cartella l'editor scrive il proprio stato — `editor.cfg`,
`layout.cfg`, `hub.cfg`, `session.cfg` — che infatti non sta in questo repository: sono
file di questo computer, non sorgenti.

---

## Licenza

MIT — vedi [LICENSE](LICENSE).

---

# ⚛ Impulso — Physics Game Engine (Web / Node)

Game engine 3D scritto **da zero, senza alcuna dipendenza**: fisica rigid-body propria, renderer WebGL2 con shader GLSL a mano, editor 3D e programmazione visuale a blocchi (blueprint).

## Avvio

```bash
node server.mjs
# apri http://localhost:5173
```

## Architettura

```
src/
├── core/
│   ├── math.js        Vec3, Quat, Mat4 (column-major, right-handed, Y-up)
│   ├── scene.js       Entità + componenti (mesh, corpo rigido, luce, vincolo, script)
│   └── input.js       Stato tastiera
├── physics/           ★ IL CUORE DEL MOTORE ★
│   ├── body.js        Corpo rigido: massa, tensore d'inerzia, AABB, sleeping
│   ├── collision.js   Narrowphase: sfera/sfera, sfera/box, box/box (SAT + clipping), raycast
│   ├── solver.js      Impulsi sequenziali con warm starting, attrito, restituzione
│   ├── constraints.js Vincoli di distanza (asta rigida / corda)
│   └── world.js       Broadphase sweep-and-prune, cache manifold, eventi di contatto
├── render/
│   ├── shaders.js     GLSL ES 3.00: Blinn-Phong, shadow map PCF, nebbia, ACES, griglia
│   ├── renderer.js    Pipeline: shadow pass → sky → oggetti → griglia → linee → gizmo
│   ├── primitives.js  Geometria procedurale (cubo, sfera, cilindro, cono)
│   └── camera.js      Camera orbitale con ray-casting da schermo
├── blueprint/
│   ├── nodes.js       Registro nodi: Eventi, Azioni, Valori, Matematica, Logica
│   ├── graph.js       Modello del grafo + serializzazione
│   ├── runtime.js     Interprete: eventi → catena exec, valutazione lazy dei dati
│   └── editorUI.js    Editor visuale: nodi trascinabili, fili SVG, palette, zoom/pan
└── editor/
    ├── app.js         Loop principale, play/pausa/stop, timestep fisso 60 Hz
    ├── panels.js      Gerarchia, Inspector, impostazioni Mondo
    ├── gizmo.js       Gizmo di traslazione a 3 assi
    └── demos.js       Scene demo (torre, pendoli, domino, cannone)
```

## Controlli viewport

| Input | Azione |
|---|---|
| Click sinistro | Seleziona oggetto / trascina gizmo |
| Trascina sinistro (vuoto) / destro | Orbita camera |
| Rotella | Zoom |
| Centrale / Shift+destro | Pan |
| `F` | Inquadra selezione |
| `Canc` | Elimina oggetto |
| `Ctrl+D` | Duplica |
| `Spazio` (in play, scena base) | Il cubo salta |

## Blueprint

Scheda **🧩 Blueprint** → tasto destro sul canvas per aggiungere nodi → trascina i pin per collegare. I pin bianchi (exec) definiscono il flusso, quelli colorati i dati (verde=numero, giallo=vettore, rosso=booleano, blu=oggetto). Assegna il grafo a un oggetto con il pulsante 📌 o dall'Inspector.

**Esempio**: `Evento: Tasto Premuto (Spazio)` → `Applica Impulso (0, 6, 0)` = l'oggetto salta.

## Fisica: dettagli tecnici

- Integrazione semi-implicita di Eulero, timestep fisso 1/60 s
- Solver a impulsi sequenziali, 10 iterazioni, Baumgarte 0.2, slop 5 mm
- Manifold box/box fino a 4 punti (clipping della faccia incidente)
- Warm starting con matching dei punti di contatto in spazio locale
- Sleeping automatico (0.6 s sotto soglia di energia) con risveglio al contatto
- Tensore d'inerzia mondo `R·diag(I⁻¹)·Rᵀ` aggiornato a ogni step
- I punti di contatto attivi sono visualizzati in rosso durante la simulazione

## Salvataggio

Autosalvataggio in `localStorage` a ogni modifica. Esporta/importa `.json` dalla toolbar.

