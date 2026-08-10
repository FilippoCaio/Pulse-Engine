// ─── Hierarchy + Inspector + World settings panels ───
import { Vec3 } from '../core/math.js';

const hex = (c) => '#' + c.map(v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0')).join('');
const fromHex = (h) => [parseInt(h.slice(1, 3), 16) / 255, parseInt(h.slice(3, 5), 16) / 255, parseInt(h.slice(5, 7), 16) / 255];

export class Hierarchy {
  constructor(app) {
    this.app = app;
    this.el = document.getElementById('hierarchy');
  }

  render() {
    this.el.innerHTML = '';
    for (const e of this.app.scene.entities) {
      const item = document.createElement('div');
      item.className = 'h-item' + (e === this.app.selected ? ' selected' : '');
      const icon = e.light ? '💡' : e.mesh?.shape === 'sphere' ? '🔵' : e.mesh?.shape === 'cylinder' ? '🛢' : '🧊';
      const badges =
        (e.body ? (e.body.type === 'dynamic' ? '⚛' : '⚓') : '') +
        (e.script ? '🧩' : '') +
        (e.joint ? '🔗' : '');
      item.innerHTML = `<span class="h-icon">${icon}</span><span>${e.name}</span><span class="h-badges">${badges}</span>`;
      item.onclick = () => this.app.select(e);
      item.ondblclick = () => {
        const name = prompt('Rinomina oggetto:', e.name);
        if (name) { e.name = name; this.app.refreshUI(); this.app.autosave(); }
      };
      this.el.appendChild(item);
    }
    if (!this.app.scene.entities.length) {
      this.el.innerHTML = '<div class="insp-empty">Scena vuota.<br>Usa ＋ Aggiungi in alto.</div>';
    }
  }
}

export class WorldSettings {
  constructor(app) {
    this.app = app;
    this.el = document.getElementById('world-settings');
  }

  render() {
    const env = this.app.scene.env;
    this.el.innerHTML = '';
    const add = (label, min, max, step, get, set) => {
      const row = document.createElement('div');
      row.className = 'prop-row';
      const lbl = document.createElement('label');
      lbl.textContent = label;
      const input = document.createElement('input');
      input.type = 'number';
      input.step = step;
      input.value = +get().toFixed(3);
      input.onchange = () => { set(Math.max(min, Math.min(max, +input.value || 0))); this.app.autosave(); };
      row.append(lbl, input);
      this.el.appendChild(row);
    };
    add('Gravità Y', -100, 100, 0.1, () => env.gravity, v => env.gravity = v);
    add('Sole: azimut', -360, 360, 5, () => env.sunAzimuth, v => env.sunAzimuth = v);
    add('Sole: altezza', 5, 89, 2, () => env.sunElevation, v => env.sunElevation = v);
    add('Sole: forza', 0, 4, 0.05, () => env.sunIntensity, v => env.sunIntensity = v);
    add('Nebbia', 0, 0.1, 0.001, () => env.fogDensity, v => env.fogDensity = v);
    add('Ombre', 0, 1, 0.05, () => env.shadowStrength, v => env.shadowStrength = v);
  }
}

export class Inspector {
  constructor(app) {
    this.app = app;
    this.el = document.getElementById('inspector');
  }

  render() {
    const e = this.app.selected;
    this.el.innerHTML = '';
    if (!e) {
      this.el.innerHTML = '<div class="insp-empty">Nessun oggetto selezionato.<br><br>Clicca un oggetto nella scena<br>o nella gerarchia.</div>';
      return;
    }

    // ── name ──
    const nameRow = document.createElement('div');
    nameRow.className = 'prop-row';
    nameRow.style.marginBottom = '8px';
    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.value = e.name;
    nameInput.style.fontWeight = '600';
    nameInput.onchange = () => { e.name = nameInput.value; this.app.hierarchy.render(); this.app.autosave(); };
    nameRow.appendChild(nameInput);
    this.el.appendChild(nameRow);

    // ── transform ──
    const tr = this.section('📐 Trasformazione');
    tr.body.appendChild(this.vec3Row('Posizione', () => e.position, (v) => {
      e.position.copy(v);
      this.syncBody(e);
    }));
    tr.body.appendChild(this.eulerRow(e));
    tr.body.appendChild(this.vec3Row('Scala', () => e.scale, (v) => {
      e.scale.set(Math.max(0.05, v.x), Math.max(0.05, v.y), Math.max(0.05, v.z));
    }));

    // ── mesh ──
    if (e.mesh) {
      const m = this.section('🎨 Mesh e Materiale', () => { e.mesh = null; this.refresh(); });
      m.body.appendChild(this.selectRow('Forma', ['cube', 'sphere', 'cylinder'], ['Cubo', 'Sfera', 'Cilindro'],
        () => e.mesh.shape, v => e.mesh.shape = v));
      m.body.appendChild(this.colorRow('Colore', () => e.mesh.color, v => e.mesh.color = v));
      m.body.appendChild(this.numRow('Lucentezza', 2, 256, 1, () => e.mesh.shininess, v => e.mesh.shininess = v));
      m.body.appendChild(this.numRow('Riflessi', 0, 2, 0.05, () => e.mesh.specular, v => e.mesh.specular = v));
      m.body.appendChild(this.numRow('Scacchiera', 0, 10, 0.5, () => e.mesh.checker || 0, v => e.mesh.checker = v));
      m.body.appendChild(this.numRow('Emissivo', 0, 5, 0.1, () => e.mesh.emissive || 0, v => e.mesh.emissive = v));
    }

    // ── physics body ──
    if (e.body) {
      const b = this.section('⚛ Fisica (Corpo Rigido)', () => { e.body = null; this.refresh(); });
      b.body.appendChild(this.selectRow('Tipo', ['dynamic', 'static'], ['Dinamico', 'Statico'],
        () => e.body.type, v => e.body.type = v));
      b.body.appendChild(this.numRow('Massa (kg)', 0.01, 10000, 0.1, () => e.body.mass, v => e.body.mass = v));
      b.body.appendChild(this.numRow('Attrito', 0, 2, 0.05, () => e.body.friction, v => e.body.friction = v));
      b.body.appendChild(this.numRow('Rimbalzo', 0, 1, 0.05, () => e.body.restitution, v => e.body.restitution = v));
      b.body.appendChild(this.numRow('Freno lin.', 0, 5, 0.01, () => e.body.linearDamping, v => e.body.linearDamping = v));
      b.body.appendChild(this.numRow('Freno ang.', 0, 5, 0.01, () => e.body.angularDamping, v => e.body.angularDamping = v));
      const note = document.createElement('div');
      note.style.cssText = 'font-size:10.5px;color:var(--text-dim);';
      note.textContent = 'Il collisore segue forma e scala della mesh (sfera o box).';
      b.body.appendChild(note);
    }

    // ── light ──
    if (e.light) {
      const l = this.section('💡 Luce Puntuale', () => { e.light = null; this.refresh(); });
      l.body.appendChild(this.colorRow('Colore', () => e.light.color, v => e.light.color = v));
      l.body.appendChild(this.numRow('Intensità', 0, 20, 0.1, () => e.light.intensity, v => e.light.intensity = v));
      l.body.appendChild(this.numRow('Raggio', 0.5, 60, 0.5, () => e.light.range, v => e.light.range = v));
    }

    // ── joint ──
    if (e.joint) {
      const j = this.section('🔗 Vincolo di Distanza', () => { e.joint = null; this.refresh(); });
      const others = this.app.scene.entities.filter(o => o !== e);
      j.body.appendChild(this.selectRow('Collega a',
        others.map(o => String(o.id)), others.map(o => o.name),
        () => String(e.joint.targetId ?? ''), v => e.joint.targetId = +v));
      j.body.appendChild(this.selectRow('Modo', ['rod', 'rope'], ['Asta rigida', 'Corda'],
        () => e.joint.mode, v => e.joint.mode = v));
      j.body.appendChild(this.numRow('Lunghezza (0=auto)', 0, 100, 0.1, () => e.joint.length || 0, v => e.joint.length = v || null));
    }

    // ── script ──
    if (e.script) {
      const s = this.section('🧩 Blueprint', () => { e.script = null; this.refresh(); });
      const graphs = this.app.graphs;
      s.body.appendChild(this.selectRow('Grafo',
        graphs.map(g => String(g.id)), graphs.map(g => g.name),
        () => String(e.script.graphId ?? ''), v => e.script.graphId = +v));
      const open = document.createElement('button');
      open.textContent = '✏ Apri nel Blueprint editor';
      open.onclick = () => {
        const g = graphs.find(g => g.id === e.script.graphId);
        if (g) { this.app.blueprint.setGraph(g); this.app.setTab('blueprint'); }
      };
      s.body.appendChild(open);
    }

    // ── add-component buttons ──
    const missing = [];
    if (!e.mesh) missing.push(['🎨 Mesh', () => e.mesh = { shape: 'cube', color: [0.8, 0.5, 0.3], shininess: 48, specular: 0.35, checker: 0, emissive: 0 }]);
    if (!e.body) missing.push(['⚛ Fisica', () => e.body = { type: 'dynamic', mass: 1, friction: 0.5, restitution: 0.3, linearDamping: 0.01, angularDamping: 0.05 }]);
    if (!e.light) missing.push(['💡 Luce', () => e.light = { color: [1, 0.85, 0.55], intensity: 3, range: 12 }]);
    if (!e.joint) missing.push(['🔗 Vincolo', () => e.joint = { targetId: this.app.scene.entities.find(o => o !== e)?.id ?? null, mode: 'rod', length: null }]);
    if (!e.script) missing.push(['🧩 Blueprint', () => {
      if (!this.app.graphs.length) { this.app.log('Crea prima un grafo nella scheda Blueprint.', 'error'); return; }
      e.script = { graphId: this.app.graphs[0].id };
    }]);
    for (const [label, fn] of missing) {
      const btn = document.createElement('button');
      btn.className = 'insp-add-comp';
      btn.textContent = '＋ ' + label;
      btn.onclick = () => { fn(); this.refresh(); };
      this.el.appendChild(btn);
    }
  }

  refresh() {
    this.render();
    this.app.hierarchy.render();
    this.app.autosave();
  }

  syncBody(e) {
    if (e.runtimeBody) {
      e.runtimeBody.position.copy(e.position);
      e.runtimeBody.quat.copy(e.rotation);
      e.runtimeBody.wake();
    }
  }

  section(title, onRemove = null) {
    const sec = document.createElement('div');
    sec.className = 'insp-section';
    const header = document.createElement('div');
    header.className = 'insp-header';
    header.textContent = title;
    if (onRemove) {
      const rm = document.createElement('button');
      rm.className = 'insp-remove';
      rm.textContent = '✕';
      rm.title = 'Rimuovi componente';
      rm.onclick = onRemove;
      header.appendChild(rm);
    }
    const body = document.createElement('div');
    body.className = 'insp-body';
    sec.append(header, body);
    this.el.appendChild(sec);
    return { sec, body };
  }

  vec3Row(label, get, set) {
    const row = document.createElement('div');
    row.className = 'prop-row';
    const lbl = document.createElement('label');
    lbl.textContent = label;
    row.appendChild(lbl);
    const wrap = document.createElement('div');
    wrap.className = 'vec3-row';
    const axes = ['x', 'y', 'z'];
    for (let i = 0; i < 3; i++) {
      const input = document.createElement('input');
      input.type = 'number';
      input.step = '0.1';
      input.className = 'axis-' + axes[i];
      input.value = +get()[axes[i]].toFixed(3);
      input.onchange = () => {
        const v = get().clone();
        v[axes[i]] = +input.value || 0;
        set(v);
        this.app.autosave();
      };
      wrap.appendChild(input);
    }
    row.appendChild(wrap);
    return row;
  }

  eulerRow(e) {
    const row = document.createElement('div');
    row.className = 'prop-row';
    const lbl = document.createElement('label');
    lbl.textContent = 'Rotazione °';
    row.appendChild(lbl);
    const wrap = document.createElement('div');
    wrap.className = 'vec3-row';
    const euler = e.rotation.toEulerDeg();
    const vals = [...euler];
    const axes = ['x', 'y', 'z'];
    for (let i = 0; i < 3; i++) {
      const input = document.createElement('input');
      input.type = 'number';
      input.step = '5';
      input.className = 'axis-' + axes[i];
      input.value = +vals[i].toFixed(1);
      input.onchange = () => {
        vals[i] = +input.value || 0;
        e.rotation.fromEulerDeg(vals[0], vals[1], vals[2]);
        this.syncBody(e);
        this.app.autosave();
      };
      wrap.appendChild(input);
    }
    row.appendChild(wrap);
    return row;
  }

  numRow(label, min, max, step, get, set) {
    const row = document.createElement('div');
    row.className = 'prop-row';
    const lbl = document.createElement('label');
    lbl.textContent = label;
    const input = document.createElement('input');
    input.type = 'number';
    input.min = min; input.max = max; input.step = step;
    input.value = +(+get()).toFixed(3);
    input.onchange = () => { set(Math.max(min, Math.min(max, +input.value || 0))); this.app.autosave(); };
    row.append(lbl, input);
    return row;
  }

  selectRow(label, values, labels, get, set) {
    const row = document.createElement('div');
    row.className = 'prop-row';
    const lbl = document.createElement('label');
    lbl.textContent = label;
    const sel = document.createElement('select');
    values.forEach((v, i) => {
      const o = document.createElement('option');
      o.value = v;
      o.textContent = labels[i];
      if (get() === v) o.selected = true;
      sel.appendChild(o);
    });
    sel.onchange = () => { set(sel.value); this.app.autosave(); };
    row.append(lbl, sel);
    return row;
  }

  colorRow(label, get, set) {
    const row = document.createElement('div');
    row.className = 'prop-row';
    const lbl = document.createElement('label');
    lbl.textContent = label;
    const input = document.createElement('input');
    input.type = 'color';
    input.value = hex(get());
    input.oninput = () => { set(fromHex(input.value)); this.app.autosave(); };
    row.append(lbl, input);
    return row;
  }
}
