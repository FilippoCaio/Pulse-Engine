# ⚛ Impulso — Physics Game Engine

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
