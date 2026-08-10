// ─── Impulso Engine: main editor application ───
import { Vec3, Quat, Mat4, DEG2RAD } from '../core/math.js';
import { Input } from '../core/input.js';
import { Scene, Entity, makeGround, makeBox, makeSphere, makeCylinder, makeLight } from '../core/scene.js';
import { Renderer } from '../render/renderer.js';
import { OrbitCamera } from '../render/camera.js';
import { PhysicsWorld } from '../physics/world.js';
import { RigidBody } from '../physics/body.js';
import { DistanceConstraint } from '../physics/constraints.js';
import { Graph } from '../blueprint/graph.js';
import { ScriptInstance } from '../blueprint/runtime.js';
import { BlueprintEditor } from '../blueprint/editorUI.js';
import { Hierarchy, Inspector, WorldSettings } from './panels.js';
import { Gizmo } from './gizmo.js';
import { DEMOS, demoDefault } from './demos.js';

const FIXED_DT = 1 / 60;
const STORAGE_KEY = 'impulso-project-v1';

export class App {
  constructor() {
    this.canvas = document.getElementById('viewport');
    this.renderer = new Renderer(this.canvas);
    this.camera = new OrbitCamera();
    this.input = new Input();
    this.gizmo = new Gizmo();

    this.scene = new Scene();
    this.graphs = [];
    this.selected = null;

    this.playing = false;
    this.paused = false;
    this.world = null;
    this.instances = [];
    this.snapshot = null;
    this.playTime = 0;
    this.accumulator = 0;
    this.spawnCount = 0;

    this.hierarchy = new Hierarchy(this);
    this.inspector = new Inspector(this);
    this.worldSettings = new WorldSettings(this);
    this.blueprint = new BlueprintEditor(this);

    this.fps = 60;
    this.lastT = performance.now();
    this._saveTimer = null;
    this._mouse = { x: 0, y: 0, mode: null, sx: 0, sy: 0, moved: 0 };

    this._bindToolbar();
    this._bindViewport();
    this._bindConsole();

    if (!this.loadFromStorage()) {
      this.loadProject(demoDefault());
      this.log('Benvenuto in Impulso! Premi ▶ Gioca e poi SPAZIO per far saltare il cubo.', 'ok');
    } else {
      this.log('Progetto ripristinato dal salvataggio automatico.', 'info');
    }

    requestAnimationFrame((t) => this.frame(t));
  }

  // ═══ project management ═══
  loadProject({ scene, graphs }) {
    if (this.playing) this.stop();
    this.scene = scene;
    this.graphs = graphs;
    this.select(null);
    this.blueprint.setGraph(graphs[0] || null);
    this.refreshUI();
    this.autosave();
  }

  serializeProject() {
    return {
      scene: this.scene.serialize(),
      graphs: this.graphs.map(g => g.serialize()),
    };
  }

  autosave() {
    clearTimeout(this._saveTimer);
    this._saveTimer = setTimeout(() => {
      if (this.playing) return; // never save live simulation state
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(this.serializeProject()));
      } catch { /* storage full/unavailable */ }
    }, 400);
  }

  loadFromStorage() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return false;
      const data = JSON.parse(raw);
      this.scene = Scene.deserialize(data.scene);
      this.graphs = (data.graphs || []).map(Graph.deserialize);
      this.blueprint.setGraph(this.graphs[0] || null);
      this.refreshUI();
      return true;
    } catch (err) {
      console.error(err);
      return false;
    }
  }

  refreshUI() {
    this.hierarchy.render();
    this.inspector.render();
    this.worldSettings.render();
    this.blueprint.refreshGraphList();
  }

  select(entity) {
    this.selected = entity;
    this.hierarchy.render();
    this.inspector.render();
  }

  log(msg, cls = 'info') {
    const logEl = document.getElementById('console-log');
    const div = document.createElement('div');
    div.className = 'log-' + cls;
    const t = this.playing ? this.playTime.toFixed(2) + 's' : 'edit';
    div.textContent = `[${t}] ${msg}`;
    logEl.appendChild(div);
    while (logEl.children.length > 200) logEl.firstChild.remove();
    logEl.scrollTop = logEl.scrollHeight;
  }

  // ═══ play mode ═══
  play() {
    if (this.playing) return;
    this.snapshot = JSON.stringify(this.serializeProject().scene);
    this.world = new PhysicsWorld();
    this.world.gravity.set(0, this.scene.env.gravity, 0);
    this.instances = [];
    this.playTime = 0;
    this.accumulator = 0;
    this.spawnCount = 0;

    const bodyById = new Map();
    for (const e of this.scene.entities) {
      if (!e.body) continue;
      this.createBody(e);
      bodyById.set(e.id, e.runtimeBody);
    }
    for (const e of this.scene.entities) {
      if (!e.joint || !e.runtimeBody) continue;
      const target = this.scene.getById(e.joint.targetId);
      const tb = target && target.runtimeBody;
      if (!tb) continue;
      const len = e.joint.length || null;
      this.world.addConstraint(new DistanceConstraint(tb, e.runtimeBody, new Vec3(), new Vec3(), len, e.joint.mode));
    }
    for (const e of this.scene.entities) this.attachScript(e);

    this.world.onContact = (a, b, impulse) => {
      for (const inst of this.instances) {
        if (inst.dead) continue;
        if (a.entity && inst.entity === a.entity) inst.fireEvent('event.collision', { altro: b.entity, impulso: impulse });
        else if (b.entity && inst.entity === b.entity) inst.fireEvent('event.collision', { altro: a.entity, impulso: impulse });
      }
    };

    this.playing = true;
    this.paused = false;
    this.updatePlayUI();
    this.log(`Simulazione avviata: ${this.world.bodies.length} corpi, ${this.world.constraints.length} vincoli, ${this.instances.length} blueprint.`, 'ok');
    for (const inst of this.instances) {
      inst.ctx.time = 0;
      inst.ctx.dt = FIXED_DT;
      inst.fireEvent('event.start');
    }
  }

  createBody(e) {
    const b = new RigidBody({
      type: e.body.type,
      shape: e.colliderShape(),
      mass: e.body.mass,
      restitution: e.body.restitution,
      friction: e.body.friction,
    });
    b.linearDamping = e.body.linearDamping ?? 0.01;
    b.angularDamping = e.body.angularDamping ?? 0.05;
    b.position.copy(e.position);
    b.quat.copy(e.rotation);
    b.updateInertiaWorld();
    b.entity = e;
    e.runtimeBody = b;
    this.world.addBody(b);
    return b;
  }

  attachScript(e) {
    if (!e.script) return;
    const graph = this.graphs.find(g => g.id === e.script.graphId);
    if (!graph) return;
    const ctx = {
      entity: e, scene: this.scene, world: this.world,
      editor: this, input: this.input, dt: FIXED_DT, time: 0,
    };
    this.instances.push(new ScriptInstance(graph, e, ctx));
  }

  stop() {
    if (!this.playing) return;
    this.playing = false;
    this.paused = false;
    this.world = null;
    this.instances = [];
    const selectedId = this.selected?.id;
    const data = JSON.parse(this.snapshot);
    this.scene = Scene.deserialize(data);
    this.scene.env = { ...this.scene.env };
    this.selected = selectedId ? this.scene.getById(selectedId) : null;
    this.updatePlayUI();
    this.refreshUI();
    this.log('Simulazione fermata: scena ripristinata.', 'info');
  }

  updatePlayUI() {
    const play = document.getElementById('btn-play');
    play.textContent = this.playing ? '▶ In corso…' : '▶ Gioca';
    play.classList.toggle('playing', this.playing);
    play.disabled = this.playing;
    document.getElementById('btn-pause').disabled = !this.playing;
    document.getElementById('btn-step').disabled = !this.playing;
    document.getElementById('btn-stop').disabled = !this.playing;
    document.getElementById('play-banner').classList.toggle('hidden', !this.playing);
    document.getElementById('btn-pause').textContent = this.paused ? '⏵' : '⏸';
  }

  // called by blueprint action nodes
  spawnBall(pos, vel, radius = 0.4) {
    if (!this.playing || this.spawnCount > 150) return;
    this.spawnCount++;
    const e = makeSphere('Proiettile ' + this.spawnCount);
    const r = Math.max(0.08, Math.min(2, radius));
    e.scale.set(r * 2, r * 2, r * 2);
    e.position.copy(pos);
    e.mesh.color = [0.95, 0.55 + Math.random() * 0.3, 0.15];
    e.body.mass = Math.max(0.3, 8 * r * r * r);
    e.body.restitution = 0.4;
    this.scene.add(e);
    const b = this.createBody(e);
    b.velocity.copy(vel);
    this.hierarchy.render();
  }

  destroyEntity(e) {
    if (e.runtimeBody && this.world) this.world.removeBody(e.runtimeBody);
    for (const inst of this.instances) if (inst.entity === e) inst.dead = true;
    this.scene.remove(e);
    if (this.selected === e) this.select(null);
    else this.hierarchy.render();
    if (!this.playing) this.autosave();
  }

  // ═══ main loop ═══
  frame(t) {
    const dt = Math.min((t - this.lastT) / 1000, 0.05);
    this.lastT = t;
    this.fps = this.fps * 0.95 + (dt > 0 ? 1 / dt : 60) * 0.05;

    if (this.playing && !this.paused) {
      // key events once per frame
      if (this.input.pressed.size) {
        for (const key of this.input.pressed) {
          for (const inst of this.instances) {
            if (!inst.dead) { inst.ctx.time = this.playTime; inst.fireEvent('event.key', { key }); }
          }
        }
      }
      this.accumulator += dt;
      let steps = 0;
      while (this.accumulator >= FIXED_DT && steps < 4) {
        for (const inst of this.instances) {
          if (!inst.dead) {
            inst.ctx.time = this.playTime;
            inst.ctx.dt = FIXED_DT;
            inst.fireEvent('event.update', { dt: FIXED_DT });
          }
        }
        this.world.step(FIXED_DT);
        this.playTime += FIXED_DT;
        this.accumulator -= FIXED_DT;
        steps++;
      }
      if (steps === 4) this.accumulator = 0; // avoid spiral of death
      this.syncFromPhysics();
    }

    this.input.endFrame();

    if (this.renderer.resize()) {
      const aspect = this.canvas.width / Math.max(1, this.canvas.height);
      this.camera.update(aspect);
      this.renderer.render(this.buildFrame(), this.camera);
    }
    this.updateStats();
    requestAnimationFrame((tt) => this.frame(tt));
  }

  syncFromPhysics() {
    for (const e of this.scene.entities) {
      if (e.runtimeBody && e.body?.type === 'dynamic') {
        e.position.copy(e.runtimeBody.position);
        e.rotation.copy(e.runtimeBody.quat);
      }
    }
  }

  buildFrame() {
    const env = this.scene.env;
    const az = env.sunAzimuth * DEG2RAD, el = env.sunElevation * DEG2RAD;
    const sunDir = new Vec3(Math.cos(el) * Math.cos(az), Math.sin(el), Math.cos(el) * Math.sin(az));
    const si = env.sunIntensity;

    const items = [];
    const lights = [];
    for (const e of this.scene.entities) {
      if (e.mesh) {
        const model = Mat4.create();
        Mat4.compose(model, e.position, e.rotation, e.scale);
        items.push({
          mesh: e.mesh.shape === 'sphere' ? 'sphere' : e.mesh.shape === 'cylinder' ? 'cylinder' : 'cube',
          model,
          color: e.mesh.color,
          shininess: e.mesh.shininess,
          specular: e.mesh.specular,
          checker: e.mesh.checker || 0,
          emissive: e.mesh.emissive || 0,
        });
      }
      if (e.light) {
        lights.push({
          pos: e.position,
          color: e.light.color.map(c => c * e.light.intensity),
          range: e.light.range,
        });
      }
    }

    const lines = [];
    // selection outline
    if (this.selected) {
      lines.push({ verts: obbLines(this.selected, [1, 0.8, 0.2]), depthTest: false, alpha: 0.9 });
    }
    // joints
    const jointVerts = [];
    for (const e of this.scene.entities) {
      if (!e.joint) continue;
      const target = this.scene.getById(e.joint.targetId);
      if (!target) continue;
      pushLine(jointVerts, e.position, target.position, [0.4, 0.9, 0.95]);
    }
    if (jointVerts.length) lines.push({ verts: new Float32Array(jointVerts), depthTest: true, alpha: 0.85 });
    // contact points during play
    if (this.playing && this.world) {
      const cv = [];
      for (const m of this.world.manifolds.values()) {
        for (const cp of m.points) {
          const p = cp.rA.clone().add(m.a.position);
          const s = 0.07;
          cv.push(p.x - s, p.y, p.z, 1, 0.25, 0.25, p.x + s, p.y, p.z, 1, 0.25, 0.25);
          cv.push(p.x, p.y - s, p.z, 1, 0.25, 0.25, p.x, p.y + s, p.z, 1, 0.25, 0.25);
          cv.push(p.x, p.y, p.z - s, 1, 0.25, 0.25, p.x, p.y, p.z + s, 1, 0.25, 0.25);
        }
      }
      if (cv.length) lines.push({ verts: new Float32Array(cv), depthTest: false, alpha: 0.9 });
    }

    const overlay = this.selected ? this.gizmo.buildOverlay(this.selected.position, this.camera) : [];

    return {
      items,
      lights,
      lines,
      overlay,
      shadowCenter: this.camera.target.clone(),
      env: {
        sunDir,
        sunColor: [1 * si, 0.96 * si, 0.86 * si],
        ambientSky: [0.24, 0.29, 0.38],
        ambientGround: [0.14, 0.125, 0.11],
        fogColor: [0.45, 0.53, 0.66],
        fogDensity: env.fogDensity,
        horizon: [0.5, 0.66, 0.9],
        zenith: [0.09, 0.24, 0.55],
        shadowStrength: env.shadowStrength,
      },
    };
  }

  updateStats() {
    const el = document.getElementById('stats');
    const bodies = this.playing && this.world ? this.world.bodies.length : this.scene.entities.filter(e => e.body).length;
    const asleep = this.playing && this.world ? this.world.bodies.filter(b => b.sleeping).length : 0;
    const contacts = this.playing && this.world ? this.world.contactCount : 0;
    el.textContent =
      `${this.playing ? (this.paused ? '⏸ PAUSA' : '▶ PLAY') : '✎ EDIT'}  ${this.fps.toFixed(0)} fps\n` +
      `corpi: ${bodies}${asleep ? ` (${asleep} 💤)` : ''}  contatti: ${contacts}\n` +
      `oggetti: ${this.scene.entities.length}  draw: ${this.renderer.drawCalls}`;
  }

  // ═══ toolbar ═══
  _bindToolbar() {
    document.getElementById('btn-play').onclick = () => this.play();
    document.getElementById('btn-stop').onclick = () => this.stop();
    document.getElementById('btn-pause').onclick = () => {
      this.paused = !this.paused;
      this.updatePlayUI();
    };
    document.getElementById('btn-step').onclick = () => {
      if (!this.playing) return;
      this.paused = true;
      for (const inst of this.instances) if (!inst.dead) inst.fireEvent('event.update', { dt: FIXED_DT });
      this.world.step(FIXED_DT);
      this.playTime += FIXED_DT;
      this.syncFromPhysics();
      this.updatePlayUI();
    };

    // add menu
    const addMenu = document.getElementById('menu-add');
    const addItem = (label, fn) => {
      const b = document.createElement('button');
      b.textContent = label;
      b.onclick = () => {
        const e = fn();
        if (e) { this.select(e); this.refreshUI(); this.autosave(); }
      };
      addMenu.appendChild(b);
    };
    const dropPos = (h = 0.5) => {
      const t = this.camera.target;
      return new Vec3(t.x + (Math.random() - 0.5), Math.max(h, t.y + 2), t.z + (Math.random() - 0.5));
    };
    addItem('🧊 Cubo', () => { const e = makeBox(); e.position.copy(dropPos()); return this.scene.add(e); });
    addItem('🔵 Sfera', () => { const e = makeSphere(); e.position.copy(dropPos()); return this.scene.add(e); });
    addItem('🛢 Cilindro', () => { const e = makeCylinder(); e.position.copy(dropPos()); return this.scene.add(e); });
    addItem('💡 Luce puntuale', () => { const e = makeLight(); e.position.copy(dropPos(3)); return this.scene.add(e); });
    addMenu.appendChild(document.createElement('hr'));
    addItem('⬜ Pavimento', () => {
      if (this.scene.entities.some(e => e.name === 'Pavimento')) { this.log('C\'è già un pavimento.', 'error'); return null; }
      return this.scene.add(makeGround());
    });
    addItem('🧱 Muro statico', () => {
      const e = makeBox('Muro');
      e.position.copy(dropPos(1.5));
      e.scale.set(6, 3, 0.5);
      e.body.type = 'static';
      e.mesh.color = [0.5, 0.52, 0.58];
      return this.scene.add(e);
    });
    addItem('🗼 Pila di 5 cubi', () => {
      const t = this.camera.target;
      let last = null;
      for (let i = 0; i < 5; i++) {
        const e = makeBox(`Cubo pila ${i + 1}`);
        e.position.set(t.x, 0.5 + i * 1.01, t.z);
        e.mesh.color = [0.4 + i * 0.12, 0.55, 0.85 - i * 0.12];
        last = this.scene.add(e);
      }
      return last;
    });

    // demo menu
    const demoMenu = document.getElementById('menu-demo');
    for (const [name, fn] of DEMOS) {
      const b = document.createElement('button');
      b.textContent = name;
      b.onclick = () => {
        if (!confirm(`Caricare la demo "${name}"? La scena attuale verrà sostituita.`)) return;
        this.loadProject(fn());
        this.camera.target.set(0, 2, 0);
        this.camera.distance = 18;
        this.log(`Demo caricata: ${name}. Premi ▶ Gioca!`, 'ok');
      };
      demoMenu.appendChild(b);
    }

    // save / export / import
    document.getElementById('btn-save').onclick = () => {
      if (this.playing) { this.log('Ferma la simulazione prima di salvare.', 'error'); return; }
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.serializeProject()));
      this.log('Progetto salvato nel browser.', 'ok');
    };
    document.getElementById('btn-export').onclick = () => {
      const blob = new Blob([JSON.stringify(this.serializeProject(), null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'progetto-impulso.json';
      a.click();
      URL.revokeObjectURL(a.href);
      this.log('Progetto esportato come progetto-impulso.json.', 'ok');
    };
    document.getElementById('btn-import').onclick = () => document.getElementById('file-import').click();
    document.getElementById('file-import').onchange = (ev) => {
      const file = ev.target.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const data = JSON.parse(reader.result);
          this.loadProject({
            scene: Scene.deserialize(data.scene),
            graphs: (data.graphs || []).map(Graph.deserialize),
          });
          this.log(`Progetto importato: ${file.name}`, 'ok');
        } catch (err) {
          this.log('File non valido: ' + err.message, 'error');
        }
      };
      reader.readAsText(file);
      ev.target.value = '';
    };

    // tabs
    document.getElementById('tab-scene').onclick = () => this.setTab('scene');
    document.getElementById('tab-blueprint').onclick = () => this.setTab('blueprint');
  }

  setTab(tab) {
    document.getElementById('tab-scene').classList.toggle('active', tab === 'scene');
    document.getElementById('tab-blueprint').classList.toggle('active', tab === 'blueprint');
    if (tab === 'blueprint') this.blueprint.show();
    else this.blueprint.hide();
  }

  _bindConsole() {
    const header = document.getElementById('console-header');
    const logEl = document.getElementById('console-log');
    header.onclick = () => {
      logEl.classList.toggle('collapsed');
      document.getElementById('console-toggle').textContent = logEl.classList.contains('collapsed') ? '▸' : '▾';
    };
  }

  // ═══ viewport interaction ═══
  _bindViewport() {
    const c = this.canvas;
    c.addEventListener('contextmenu', (e) => e.preventDefault());

    c.addEventListener('mousedown', (e) => {
      c.focus?.();
      const m = this._mouse;
      m.sx = e.clientX; m.sy = e.clientY; m.moved = 0;
      if (e.button === 0) {
        const ray = this.rayFromEvent(e);
        if (this.selected) {
          const axis = this.gizmo.hitTest(ray, this.selected.position, this.camera);
          if (axis >= 0) {
            this.gizmo.beginDrag(ray, this.selected.position, axis);
            m.mode = 'gizmo';
            return;
          }
        }
        m.mode = 'maybe-select';
      } else if (e.button === 2) {
        m.mode = e.shiftKey ? 'pan' : 'orbit';
      } else if (e.button === 1) {
        m.mode = 'pan';
        e.preventDefault();
      }
    });

    window.addEventListener('mousemove', (e) => {
      const m = this._mouse;
      const dx = e.clientX - m.x, dy = e.clientY - m.y;
      m.x = e.clientX; m.y = e.clientY;
      m.moved += Math.abs(dx) + Math.abs(dy);

      if (m.mode === 'orbit') this.camera.orbit(dx, dy);
      else if (m.mode === 'pan') this.camera.pan(dx, dy);
      else if (m.mode === 'gizmo' && this.selected) {
        const ray = this.rayFromEvent(e);
        const np = this.gizmo.drag(ray);
        if (np) {
          this.selected.position.copy(np);
          this.inspector.syncBody(this.selected);
        }
      } else if (!m.mode && this.selected && e.target === c) {
        // hover highlight
        const ray = this.rayFromEvent(e);
        this.gizmo.hoverAxis = this.gizmo.hitTest(ray, this.selected.position, this.camera);
      }
    });

    window.addEventListener('mouseup', (e) => {
      const m = this._mouse;
      if (m.mode === 'gizmo') {
        this.gizmo.endDrag();
        this.inspector.render();
        this.autosave();
      } else if (m.mode === 'maybe-select' && m.moved < 6) {
        this.pick(e);
      } else if (m.mode === 'maybe-select') {
        // drag on empty space = orbit fallback (already ignored)
      }
      m.mode = null;
    });

    // orbit with left-drag on empty space too
    window.addEventListener('mousemove', (e) => {
      const m = this._mouse;
      if (m.mode === 'maybe-select' && m.moved > 6) m.mode = 'orbit';
    });

    c.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.camera.zoom(e.deltaY);
    }, { passive: false });

    window.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
      if (this.blueprint.visible) return;
      if (e.code === 'KeyF' && this.selected) {
        this.camera.focusOn(this.selected.position.clone(), Math.max(this.selected.scale.x, this.selected.scale.y, this.selected.scale.z));
      }
      if ((e.key === 'Delete') && this.selected && !this.playing) {
        this.destroyEntity(this.selected);
        this.autosave();
      }
      if (e.code === 'KeyD' && e.ctrlKey && this.selected && !this.playing) {
        e.preventDefault();
        const copy = this.selected.clone();
        copy.position.x += 1;
        this.scene.add(copy);
        this.select(copy);
        this.autosave();
      }
    });
  }

  rayFromEvent(e) {
    const r = this.canvas.getBoundingClientRect();
    const ndcX = ((e.clientX - r.left) / r.width) * 2 - 1;
    const ndcY = -(((e.clientY - r.top) / r.height) * 2 - 1);
    return this.camera.screenRay(ndcX, ndcY);
  }

  pick(e) {
    const ray = this.rayFromEvent(e);
    let best = null, bestT = Infinity;
    for (const ent of this.scene.entities) {
      if (!ent.mesh) continue;
      const proxy = { shape: ent.colliderShape(), position: ent.position, quat: ent.rotation };
      const hit = raycastProxy(proxy, ray);
      if (hit && hit.t < bestT) { bestT = hit.t; best = ent; }
    }
    this.select(best);
  }
}

// ── helpers ──
import { raycastBody } from '../physics/collision.js';
function raycastProxy(proxy, ray) {
  return raycastBody(proxy, ray.origin, ray.dir, 1000);
}

function pushLine(arr, a, b, c) {
  arr.push(a.x, a.y, a.z, c[0], c[1], c[2], b.x, b.y, b.z, c[0], c[1], c[2]);
}

function obbLines(entity, color) {
  const h = new Vec3(entity.scale.x / 2 + 0.02, entity.scale.y / 2 + 0.02, entity.scale.z / 2 + 0.02);
  const corners = [];
  for (let i = 0; i < 8; i++) {
    const p = new Vec3(
      (i & 1 ? h.x : -h.x),
      (i & 2 ? h.y : -h.y),
      (i & 4 ? h.z : -h.z),
    ).applyQuat(entity.rotation).add(entity.position);
    corners.push(p);
  }
  const edges = [[0, 1], [1, 3], [3, 2], [2, 0], [4, 5], [5, 7], [7, 6], [6, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
  const verts = [];
  for (const [a, b] of edges) pushLine(verts, corners[a], corners[b], color);
  return new Float32Array(verts);
}
