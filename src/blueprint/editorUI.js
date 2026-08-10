// ─── Blueprint node editor UI: nodes as DOM, wires as SVG, pan/zoom canvas ───
import { NODE_TYPES, PIN_COLORS, CATEGORY_COLORS, KEY_OPTIONS } from './nodes.js';
import { Graph } from './graph.js';

const SVG_OFF = 5000; // wires svg is offset by -5000px to allow negative coords

export class BlueprintEditor {
  constructor(app) {
    this.app = app;
    this.panel = document.getElementById('blueprint-panel');
    this.canvas = document.getElementById('bp-canvas');
    this.world = document.getElementById('bp-world');
    this.wiresSvg = document.getElementById('bp-wires');
    this.nodesDiv = document.getElementById('bp-nodes');
    this.graphSelect = document.getElementById('bp-graph-select');

    this.graph = null;
    this.pan = { x: 60, y: 40 };
    this.zoom = 1;
    this.selectedNode = null;
    this.selectedLink = null;
    this.drag = null; // {mode:'pan'|'node'|'wire', ...}
    this.palette = null;

    this._bindToolbar();
    this._bindCanvas();
  }

  _bindToolbar() {
    this.graphSelect.addEventListener('change', () => {
      const g = this.app.graphs.find(g => g.id === +this.graphSelect.value);
      if (g) this.setGraph(g);
    });
    document.getElementById('bp-new').onclick = () => {
      const name = prompt('Nome del nuovo grafo:', `Grafo ${this.app.graphs.length + 1}`);
      if (!name) return;
      const g = new Graph(name);
      this.app.graphs.push(g);
      this.setGraph(g);
      this.app.autosave();
    };
    document.getElementById('bp-rename').onclick = () => {
      if (!this.graph) return;
      const name = prompt('Nuovo nome:', this.graph.name);
      if (name) { this.graph.name = name; this.refreshGraphList(); this.app.autosave(); }
    };
    document.getElementById('bp-delete').onclick = () => {
      if (!this.graph) return;
      if (!confirm(`Eliminare il grafo "${this.graph.name}"?`)) return;
      const id = this.graph.id;
      this.app.graphs = this.app.graphs.filter(g => g.id !== id);
      for (const e of this.app.scene.entities) {
        if (e.script && e.script.graphId === id) e.script = null;
      }
      this.setGraph(this.app.graphs[0] || null);
      this.app.autosave();
    };
    document.getElementById('bp-assign').onclick = () => {
      if (!this.graph) return;
      const ent = this.app.selected;
      if (!ent) { this.app.log('Seleziona prima un oggetto nella scena 3D.', 'error'); return; }
      ent.script = { graphId: this.graph.id };
      this.app.log(`Blueprint "${this.graph.name}" assegnato a "${ent.name}".`, 'ok');
      this.app.inspector.render();
      this.app.autosave();
    };
  }

  refreshGraphList() {
    this.graphSelect.innerHTML = '';
    for (const g of this.app.graphs) {
      const opt = document.createElement('option');
      opt.value = g.id;
      opt.textContent = g.name;
      if (this.graph && g.id === this.graph.id) opt.selected = true;
      this.graphSelect.appendChild(opt);
    }
    if (!this.app.graphs.length) {
      const opt = document.createElement('option');
      opt.textContent = '(nessun grafo)';
      this.graphSelect.appendChild(opt);
    }
  }

  setGraph(graph) {
    this.graph = graph;
    this.selectedNode = null;
    this.selectedLink = null;
    this.refreshGraphList();
    this.render();
  }

  show() { this.panel.classList.remove('hidden'); if (!this.graph && this.app.graphs.length) this.setGraph(this.app.graphs[0]); else this.render(); }
  hide() { this.panel.classList.add('hidden'); this.closePalette(); }
  get visible() { return !this.panel.classList.contains('hidden'); }

  // ── coordinate helpers ──
  screenToWorld(clientX, clientY) {
    const r = this.canvas.getBoundingClientRect();
    return {
      x: (clientX - r.left - this.pan.x) / this.zoom,
      y: (clientY - r.top - this.pan.y) / this.zoom,
    };
  }

  applyTransform() {
    this.world.style.transform = `translate(${this.pan.x}px, ${this.pan.y}px) scale(${this.zoom})`;
  }

  // ── canvas interactions ──
  _bindCanvas() {
    this.canvas.addEventListener('mousedown', (e) => {
      if (e.target !== this.canvas && e.target !== this.world && e.target !== this.nodesDiv) return;
      if (e.button === 0 || e.button === 1) {
        this.drag = { mode: 'pan', sx: e.clientX, sy: e.clientY, px: this.pan.x, py: this.pan.y };
        this.selectNode(null);
        this.selectLink(null);
        this.closePalette();
        e.preventDefault();
      }
    });

    window.addEventListener('mousemove', (e) => {
      if (!this.drag) return;
      if (this.drag.mode === 'pan') {
        this.pan.x = this.drag.px + e.clientX - this.drag.sx;
        this.pan.y = this.drag.py + e.clientY - this.drag.sy;
        this.applyTransform();
      } else if (this.drag.mode === 'node') {
        const w = this.screenToWorld(e.clientX, e.clientY);
        this.drag.node.x = w.x - this.drag.ox;
        this.drag.node.y = w.y - this.drag.oy;
        this.drag.el.style.left = this.drag.node.x + 'px';
        this.drag.el.style.top = this.drag.node.y + 'px';
        this.renderWires();
      } else if (this.drag.mode === 'wire') {
        const w = this.screenToWorld(e.clientX, e.clientY);
        this.drag.toPos = w;
        this.renderWires();
      }
    });

    window.addEventListener('mouseup', (e) => {
      if (this.drag?.mode === 'wire') {
        const pinEl = e.target.closest?.('.bp-pin');
        if (pinEl) this.tryConnect(this.drag, pinEl);
        this.drag = null;
        this.renderWires();
      } else if (this.drag) {
        if (this.drag.mode === 'node') this.app.autosave();
        this.drag = null;
      }
    });

    this.canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const r = this.canvas.getBoundingClientRect();
      const mx = e.clientX - r.left, my = e.clientY - r.top;
      const oldZoom = this.zoom;
      this.zoom = Math.max(0.35, Math.min(2, this.zoom * Math.pow(1.0015, -e.deltaY)));
      this.pan.x = mx - (mx - this.pan.x) * (this.zoom / oldZoom);
      this.pan.y = my - (my - this.pan.y) * (this.zoom / oldZoom);
      this.applyTransform();
    }, { passive: false });

    this.canvas.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      if (!this.graph) return;
      this.openPalette(e.clientX, e.clientY);
    });

    window.addEventListener('keydown', (e) => {
      if (!this.visible) return;
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
      if (e.key === 'Delete' || e.key === 'Backspace') {
        if (this.selectedNode) {
          this.graph.removeNode(this.selectedNode);
          this.selectedNode = null;
          this.render();
          this.app.autosave();
        } else if (this.selectedLink) {
          this.graph.links = this.graph.links.filter(l => l !== this.selectedLink);
          this.selectedLink = null;
          this.render();
          this.app.autosave();
        }
      }
      if (e.key === 'Escape') this.closePalette();
    });
  }

  tryConnect(drag, pinEl) {
    const toNode = +pinEl.dataset.node;
    const toPin = pinEl.dataset.pin;
    const toDir = pinEl.dataset.dir;
    const toKind = pinEl.dataset.kind;
    if (toDir === drag.dir) return;           // must connect out→in
    if (toNode === drag.node) return;
    const dataOk = (a, b) => a === b || a === 'any' || b === 'any' ||
      (a !== 'exec' && b !== 'exec' && ((a === 'number' && b === 'bool') || (a === 'bool' && b === 'number')));
    if (drag.kind === 'exec' !== (toKind === 'exec')) return;
    if (drag.kind !== 'exec' && !dataOk(drag.kind, toKind)) return;

    let from, to;
    if (drag.dir === 'out') { from = [drag.node, drag.pin]; to = [toNode, toPin]; }
    else { from = [toNode, toPin]; to = [drag.node, drag.pin]; }
    // an exec OUTPUT drives a single flow
    if (drag.kind === 'exec') {
      this.graph.links = this.graph.links.filter(l => !(l.from[0] === from[0] && l.from[1] === from[1]));
    }
    this.graph.connect(from[0], from[1], to[0], to[1]);
    this.render();
    this.app.autosave();
  }

  selectNode(node) {
    this.selectedNode = node;
    for (const el of this.nodesDiv.children) {
      el.classList.toggle('selected', node && +el.dataset.id === node.id);
    }
    if (node) this.selectLink(null);
  }

  selectLink(link) {
    this.selectedLink = link;
    this.renderWires();
    if (link) this.selectNode(null);
  }

  // ── palette ──
  openPalette(cx, cy) {
    this.closePalette();
    const worldPos = this.screenToWorld(cx, cy);
    const pal = document.createElement('div');
    pal.id = 'bp-palette';
    pal.style.left = Math.min(cx, window.innerWidth - 250) + 'px';
    pal.style.top = Math.min(cy, window.innerHeight - 360) + 'px';
    const search = document.createElement('input');
    search.placeholder = 'Cerca nodo…';
    pal.appendChild(search);
    const list = document.createElement('div');
    pal.appendChild(list);

    const build = (filter = '') => {
      list.innerHTML = '';
      const cats = {};
      for (const [type, def] of Object.entries(NODE_TYPES)) {
        if (filter && !def.title.toLowerCase().includes(filter.toLowerCase())) continue;
        (cats[def.category] ||= []).push([type, def]);
      }
      for (const [cat, items] of Object.entries(cats)) {
        const h = document.createElement('div');
        h.className = 'pal-cat';
        h.textContent = cat.toUpperCase();
        list.appendChild(h);
        for (const [type, def] of items) {
          const it = document.createElement('div');
          it.className = 'pal-item';
          const dot = document.createElement('span');
          dot.className = 'pal-dot';
          dot.style.background = CATEGORY_COLORS[def.category];
          it.appendChild(dot);
          it.appendChild(document.createTextNode(def.title));
          it.onclick = () => {
            this.graph.addNode(type, worldPos.x, worldPos.y);
            this.closePalette();
            this.render();
            this.app.autosave();
          };
          list.appendChild(it);
        }
      }
    };
    build();
    search.oninput = () => build(search.value);
    document.body.appendChild(pal);
    search.focus();
    this.palette = pal;
    setTimeout(() => {
      const close = (e) => { if (!pal.contains(e.target)) this.closePalette(); };
      pal._closer = close;
      document.addEventListener('mousedown', close);
    }, 0);
  }

  closePalette() {
    if (this.palette) {
      document.removeEventListener('mousedown', this.palette._closer);
      this.palette.remove();
      this.palette = null;
    }
  }

  // ── rendering ──
  render() {
    this.applyTransform();
    this.nodesDiv.innerHTML = '';
    if (!this.graph) { this.wiresSvg.innerHTML = ''; return; }
    for (const node of this.graph.nodes) this.nodesDiv.appendChild(this.renderNode(node));
    this.renderWires();
  }

  renderNode(node) {
    const def = NODE_TYPES[node.type];
    const el = document.createElement('div');
    el.className = 'bp-node' + (node === this.selectedNode ? ' selected' : '');
    el.dataset.id = node.id;
    el.style.left = node.x + 'px';
    el.style.top = node.y + 'px';

    const header = document.createElement('div');
    header.className = 'bp-node-header';
    header.style.background = CATEGORY_COLORS[def.category];
    header.textContent = def.title;
    el.appendChild(header);

    header.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return;
      e.stopPropagation();
      this.selectNode(node);
      const w = this.screenToWorld(e.clientX, e.clientY);
      this.drag = { mode: 'node', node, el, ox: w.x - node.x, oy: w.y - node.y };
    });
    el.addEventListener('mousedown', (e) => {
      if (e.target.classList.contains('bp-pin')) return;
      this.selectNode(node);
      e.stopPropagation();
    });

    const bodyEl = document.createElement('div');
    bodyEl.className = 'bp-node-body';
    el.appendChild(bodyEl);

    // node props (key selector, text, op, number)
    if (def.propKinds) {
      for (const [prop, kind] of Object.entries(def.propKinds)) {
        const row = document.createElement('div');
        row.className = 'bp-pin-row';
        row.style.padding = '0 10px';
        let input;
        if (kind === 'key') {
          input = document.createElement('select');
          for (const [code, label] of KEY_OPTIONS) {
            const o = document.createElement('option');
            o.value = code; o.textContent = label;
            if (node.props[prop] === code) o.selected = true;
            input.appendChild(o);
          }
        } else if (Array.isArray(kind)) {
          input = document.createElement('select');
          for (const v of kind) {
            const o = document.createElement('option');
            o.value = v; o.textContent = v;
            if (node.props[prop] === v) o.selected = true;
            input.appendChild(o);
          }
        } else {
          input = document.createElement('input');
          input.type = kind === 'number' ? 'number' : 'text';
          input.value = node.props[prop];
          input.step = 'any';
        }
        input.className = 'bp-pin-input wide';
        input.onchange = () => {
          node.props[prop] = kind === 'number' ? +input.value : input.value;
          this.app.autosave();
        };
        input.onmousedown = (e) => e.stopPropagation();
        row.appendChild(input);
        bodyEl.appendChild(row);
      }
    }

    // input pins
    for (const inp of def.inputs || []) {
      const row = document.createElement('div');
      row.className = 'bp-pin-row';
      row.appendChild(this.makePin(node, inp, 'in'));
      const lbl = document.createElement('span');
      lbl.className = 'bp-pin-label';
      lbl.textContent = inp.label || inp.name;
      row.appendChild(lbl);
      // literal editors when unconnected
      if (inp.kind !== 'exec' && !this.graph.linkInto(node.id, inp.name)) {
        const key = 'in_' + inp.name;
        if (inp.kind === 'vec3') {
          const cur = Array.isArray(node.props[key]) ? node.props[key] : [0, 0, 0];
          node.props[key] = cur;
          for (let i = 0; i < 3; i++) {
            const f = document.createElement('input');
            f.type = 'number'; f.step = 'any';
            f.className = 'bp-pin-input';
            f.value = cur[i];
            f.onchange = () => { cur[i] = +f.value || 0; this.app.autosave(); };
            f.onmousedown = (e) => e.stopPropagation();
            row.appendChild(f);
          }
        } else if (inp.kind === 'bool') {
          const f = document.createElement('input');
          f.type = 'checkbox';
          f.checked = !!node.props[key];
          f.onchange = () => { node.props[key] = f.checked; this.app.autosave(); };
          f.onmousedown = (e) => e.stopPropagation();
          row.appendChild(f);
        } else if (inp.kind === 'number' || inp.kind === 'any') {
          const f = document.createElement('input');
          f.type = inp.kind === 'number' ? 'number' : 'text';
          f.step = 'any';
          f.className = 'bp-pin-input' + (inp.kind === 'any' ? ' wide' : '');
          f.value = node.props[key] ?? '';
          f.onchange = () => {
            const num = parseFloat(f.value);
            node.props[key] = (inp.kind === 'number') ? (num || 0) : (f.value !== '' && !isNaN(num) ? num : f.value);
            this.app.autosave();
          };
          f.onmousedown = (e) => e.stopPropagation();
          row.appendChild(f);
        }
      }
      bodyEl.appendChild(row);
    }

    // output pins
    for (const out of def.outputs || []) {
      const row = document.createElement('div');
      row.className = 'bp-pin-row out';
      const lbl = document.createElement('span');
      lbl.className = 'bp-pin-label';
      lbl.textContent = out.label || out.name;
      row.appendChild(lbl);
      row.appendChild(this.makePin(node, out, 'out'));
      bodyEl.appendChild(row);
    }

    return el;
  }

  makePin(node, pinDef, dir) {
    const pin = document.createElement('span');
    pin.className = 'bp-pin' + (pinDef.kind === 'exec' ? ' exec' : '');
    pin.style.background = PIN_COLORS[pinDef.kind];
    pin.dataset.node = node.id;
    pin.dataset.pin = pinDef.name;
    pin.dataset.dir = dir;
    pin.dataset.kind = pinDef.kind;
    pin.addEventListener('mousedown', (e) => {
      e.stopPropagation();
      e.preventDefault();
      const pos = this.pinWorldPos(node.id, pinDef.name, dir);
      this.drag = { mode: 'wire', node: node.id, pin: pinDef.name, dir, kind: pinDef.kind, fromPos: pos, toPos: pos };
    });
    return pin;
  }

  pinWorldPos(nodeId, pinName, dir) {
    const pinEl = this.nodesDiv.querySelector(`.bp-pin[data-node="${nodeId}"][data-pin="${pinName}"][data-dir="${dir}"]`);
    if (!pinEl) return { x: 0, y: 0 };
    const pr = pinEl.getBoundingClientRect();
    const wr = this.world.getBoundingClientRect();
    return {
      x: (pr.left + pr.width / 2 - wr.left) / this.zoom,
      y: (pr.top + pr.height / 2 - wr.top) / this.zoom,
    };
  }

  renderWires() {
    const svg = this.wiresSvg;
    svg.innerHTML = '';
    if (!this.graph) return;
    const mk = (a, b, color, cls, link) => {
      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      const dx = Math.max(40, Math.abs(b.x - a.x) * 0.5);
      path.setAttribute('d',
        `M ${a.x + SVG_OFF} ${a.y + SVG_OFF} C ${a.x + dx + SVG_OFF} ${a.y + SVG_OFF}, ${b.x - dx + SVG_OFF} ${b.y + SVG_OFF}, ${b.x + SVG_OFF} ${b.y + SVG_OFF}`);
      path.setAttribute('stroke', color);
      path.setAttribute('stroke-width', link && link === this.selectedLink ? 4 : 2.5);
      path.setAttribute('fill', 'none');
      if (link) {
        path.style.pointerEvents = 'stroke';
        path.addEventListener('mousedown', (e) => { e.stopPropagation(); this.selectLink(link); });
      }
      svg.appendChild(path);
    };
    for (const link of this.graph.links) {
      const a = this.pinWorldPos(link.from[0], link.from[1], 'out');
      const b = this.pinWorldPos(link.to[0], link.to[1], 'in');
      const fromNode = this.graph.getNode(link.from[0]);
      const def = fromNode && NODE_TYPES[fromNode.type];
      const pin = def && (def.outputs || []).find(p => p.name === link.from[1]);
      const color = link === this.selectedLink ? '#ffd166' : (PIN_COLORS[pin?.kind] || '#888');
      mk(a, b, color, '', link);
    }
    if (this.drag?.mode === 'wire') {
      const { fromPos, toPos, dir } = this.drag;
      const a = dir === 'out' ? fromPos : toPos;
      const b = dir === 'out' ? toPos : fromPos;
      mk(a, b, '#ffffff', 'temp', null);
    }
  }
}
