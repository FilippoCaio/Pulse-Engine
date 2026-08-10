# Pulse Engine

Game engine 3D con editor, scritto in C++ per Windows. **Nessuna dipendenza esterna**:
si compila con il solo compilatore di Visual Studio e si collega unicamente alle
librerie di sistema — OpenGL, Win32, WIC per le immagini, WinMM per l'audio. Non c'e'
nessun package manager da configurare, niente da scaricare prima di compilare.

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
  assets/icons/      icone dei tipi di asset, lette a runtime
  progetto/          progetto di esempio
```

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
