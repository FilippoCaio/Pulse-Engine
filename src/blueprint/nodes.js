// ─── Blueprint node registry ───
// Pure nodes implement eval(ctx, node, getIn) → { outName: value }
// Exec nodes implement exec(ctx, node, getIn) → next exec output name (or null)
import { Vec3 } from '../core/math.js';
import { KEY_OPTIONS } from '../core/input.js';

export const PIN_COLORS = {
  exec: '#e8edf5',
  number: '#5fd68b',
  vec3: '#ffd166',
  bool: '#ff6b6b',
  entity: '#4da3ff',
  any: '#b09be8',
};

export const CATEGORY_COLORS = {
  Eventi: '#a53434',
  Azioni: '#2d5f9e',
  Valori: '#2e7d4f',
  Matematica: '#6d4d9e',
  Logica: '#b0762b',
};

export function toNumber(v) {
  if (typeof v === 'number') return v;
  if (typeof v === 'boolean') return v ? 1 : 0;
  const n = parseFloat(v);
  return isNaN(n) ? 0 : n;
}
export function toVec3(v) {
  if (v instanceof Vec3) return v;
  if (v && typeof v === 'object' && 'x' in v) return new Vec3(v.x, v.y, v.z);
  const n = toNumber(v);
  return new Vec3(n, n, n);
}
export function toBool(v) {
  if (typeof v === 'boolean') return v;
  if (typeof v === 'number') return v !== 0;
  return !!v;
}

function fmt(v) {
  if (v instanceof Vec3) return `(${v.x.toFixed(2)}, ${v.y.toFixed(2)}, ${v.z.toFixed(2)})`;
  if (typeof v === 'number') return Math.abs(v) < 1e5 ? +v.toFixed(3) + '' : v.toExponential(2);
  if (v && v.name) return v.name;
  return String(v);
}

const body = (ctx, ent) => (ent && ent.runtimeBody) || null;

export const NODE_TYPES = {
  // ═══ EVENTI ═══
  'event.start': {
    title: '▶ Evento: Inizio', category: 'Eventi',
    inputs: [], outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    isEvent: true,
  },
  'event.update': {
    title: '🔁 Evento: Ogni Frame', category: 'Eventi',
    inputs: [], outputs: [{ name: 'exec', label: '', kind: 'exec' }, { name: 'dt', label: 'delta t', kind: 'number' }],
    isEvent: true,
  },
  'event.collision': {
    title: '💥 Evento: Collisione', category: 'Eventi',
    inputs: [],
    outputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'altro', label: 'altro oggetto', kind: 'entity' },
      { name: 'impulso', label: 'impulso', kind: 'number' },
    ],
    isEvent: true,
  },
  'event.key': {
    title: '⌨ Evento: Tasto Premuto', category: 'Eventi',
    inputs: [], outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    props: { key: 'Space' }, propKinds: { key: 'key' },
    isEvent: true,
  },

  // ═══ AZIONI ═══
  'action.force': {
    title: 'Applica Forza', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'oggetto', label: 'oggetto', kind: 'entity' },
      { name: 'forza', label: 'forza (N)', kind: 'vec3', default: [0, 0, 0] },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const b = body(ctx, ent);
      if (b) b.applyForce(toVec3(getIn('forza')));
      return 'exec';
    },
  },
  'action.impulse': {
    title: 'Applica Impulso', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'oggetto', label: 'oggetto', kind: 'entity' },
      { name: 'impulso', label: 'impulso', kind: 'vec3', default: [0, 5, 0] },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const b = body(ctx, ent);
      if (b) b.applyImpulse(toVec3(getIn('impulso')));
      return 'exec';
    },
  },
  'action.setvel': {
    title: 'Imposta Velocità', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'oggetto', label: 'oggetto', kind: 'entity' },
      { name: 'velocita', label: 'velocità', kind: 'vec3', default: [0, 0, 0] },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const b = body(ctx, ent);
      if (b) { b.wake(); b.velocity.copy(toVec3(getIn('velocita'))); }
      return 'exec';
    },
  },
  'action.torque': {
    title: 'Applica Torsione', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'oggetto', label: 'oggetto', kind: 'entity' },
      { name: 'coppia', label: 'coppia', kind: 'vec3', default: [0, 10, 0] },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const b = body(ctx, ent);
      if (b) b.applyTorque(toVec3(getIn('coppia')));
      return 'exec';
    },
  },
  'action.teleport': {
    title: 'Imposta Posizione', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'oggetto', label: 'oggetto', kind: 'entity' },
      { name: 'posizione', label: 'posizione', kind: 'vec3', default: [0, 3, 0] },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const b = body(ctx, ent);
      const p = toVec3(getIn('posizione'));
      if (b) { b.wake(); b.position.copy(p); b.velocity.set(0, 0, 0); }
      else if (ent) ent.position.copy(p);
      return 'exec';
    },
  },
  'action.spawnball': {
    title: 'Spara Sfera', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'posizione', label: 'da posizione', kind: 'vec3', default: [0, 2, 0] },
      { name: 'velocita', label: 'velocità', kind: 'vec3', default: [0, 6, 10] },
      { name: 'raggio', label: 'raggio', kind: 'number', default: 0.4 },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      ctx.editor.spawnBall(toVec3(getIn('posizione')), toVec3(getIn('velocita')), toNumber(getIn('raggio')));
      return 'exec';
    },
  },
  'action.destroy': {
    title: 'Distruggi Oggetto', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'oggetto', label: 'oggetto', kind: 'entity' },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      if (ent) ctx.editor.destroyEntity(ent);
      return 'exec';
    },
  },
  'action.setcolor': {
    title: 'Imposta Colore', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'oggetto', label: 'oggetto', kind: 'entity' },
      { name: 'rgb', label: 'colore (0-1)', kind: 'vec3', default: [1, 0.3, 0.3] },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const c = toVec3(getIn('rgb'));
      if (ent && ent.mesh) ent.mesh.color = [c.x, c.y, c.z];
      return 'exec';
    },
  },
  'action.print': {
    title: 'Stampa in Console', category: 'Azioni',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'valore', label: 'valore', kind: 'any', default: 'ciao!' },
    ],
    outputs: [{ name: 'exec', label: '', kind: 'exec' }],
    exec(ctx, node, getIn) {
      ctx.editor.log(`[${ctx.entity.name}] ${fmt(getIn('valore'))}`, 'script');
      return 'exec';
    },
  },

  // ═══ VALORI ═══
  'value.self': {
    title: 'Me Stesso', category: 'Valori',
    inputs: [], outputs: [{ name: 'io', label: 'io', kind: 'entity' }],
    eval(ctx) { return { io: ctx.entity }; },
  },
  'value.find': {
    title: 'Trova Oggetto', category: 'Valori',
    inputs: [], outputs: [{ name: 'oggetto', label: 'oggetto', kind: 'entity' }],
    props: { nome: 'Cubo' }, propKinds: { nome: 'text' },
    eval(ctx, node) { return { oggetto: ctx.scene.getByName(node.props.nome) }; },
  },
  'value.position': {
    title: 'Posizione Di', category: 'Valori',
    inputs: [{ name: 'oggetto', label: 'oggetto', kind: 'entity' }],
    outputs: [{ name: 'pos', label: 'posizione', kind: 'vec3' }],
    eval(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const b = body(ctx, ent);
      return { pos: b ? b.position.clone() : (ent ? ent.position.clone() : new Vec3()) };
    },
  },
  'value.velocity': {
    title: 'Velocità Di', category: 'Valori',
    inputs: [{ name: 'oggetto', label: 'oggetto', kind: 'entity' }],
    outputs: [{ name: 'vel', label: 'velocità', kind: 'vec3' }],
    eval(ctx, node, getIn) {
      const ent = getIn('oggetto') || ctx.entity;
      const b = body(ctx, ent);
      return { vel: b ? b.velocity.clone() : new Vec3() };
    },
  },
  'value.number': {
    title: 'Numero', category: 'Valori',
    inputs: [], outputs: [{ name: 'n', label: '', kind: 'number' }],
    props: { valore: 1 }, propKinds: { valore: 'number' },
    eval(ctx, node) { return { n: toNumber(node.props.valore) }; },
  },
  'value.vec3': {
    title: 'Vettore 3D', category: 'Valori',
    inputs: [
      { name: 'x', label: 'x', kind: 'number', default: 0 },
      { name: 'y', label: 'y', kind: 'number', default: 0 },
      { name: 'z', label: 'z', kind: 'number', default: 0 },
    ],
    outputs: [{ name: 'v', label: '', kind: 'vec3' }],
    eval(ctx, node, getIn) {
      return { v: new Vec3(toNumber(getIn('x')), toNumber(getIn('y')), toNumber(getIn('z'))) };
    },
  },
  'value.time': {
    title: 'Tempo Trascorso', category: 'Valori',
    inputs: [], outputs: [{ name: 't', label: 'secondi', kind: 'number' }],
    eval(ctx) { return { t: ctx.time }; },
  },
  'value.random': {
    title: 'Numero Casuale', category: 'Valori',
    inputs: [
      { name: 'min', label: 'min', kind: 'number', default: 0 },
      { name: 'max', label: 'max', kind: 'number', default: 1 },
    ],
    outputs: [{ name: 'n', label: '', kind: 'number' }],
    eval(ctx, node, getIn) {
      const a = toNumber(getIn('min')), b = toNumber(getIn('max'));
      return { n: a + Math.random() * (b - a) };
    },
  },
  'value.keydown': {
    title: 'Tasto Giù?', category: 'Valori',
    inputs: [], outputs: [{ name: 'giu', label: 'è premuto', kind: 'bool' }],
    props: { key: 'Space' }, propKinds: { key: 'key' },
    eval(ctx, node) { return { giu: ctx.input.isDown(node.props.key) }; },
  },

  // ═══ MATEMATICA ═══
  'math.add': {
    title: 'Somma (A+B)', category: 'Matematica',
    inputs: [
      { name: 'a', label: 'A', kind: 'number', default: 0 },
      { name: 'b', label: 'B', kind: 'number', default: 0 },
    ],
    outputs: [{ name: 'r', label: '', kind: 'number' }],
    eval(ctx, node, getIn) { return { r: toNumber(getIn('a')) + toNumber(getIn('b')) }; },
  },
  'math.sub': {
    title: 'Sottrai (A−B)', category: 'Matematica',
    inputs: [
      { name: 'a', label: 'A', kind: 'number', default: 0 },
      { name: 'b', label: 'B', kind: 'number', default: 0 },
    ],
    outputs: [{ name: 'r', label: '', kind: 'number' }],
    eval(ctx, node, getIn) { return { r: toNumber(getIn('a')) - toNumber(getIn('b')) }; },
  },
  'math.mul': {
    title: 'Moltiplica (A×B)', category: 'Matematica',
    inputs: [
      { name: 'a', label: 'A', kind: 'number', default: 1 },
      { name: 'b', label: 'B', kind: 'number', default: 1 },
    ],
    outputs: [{ name: 'r', label: '', kind: 'number' }],
    eval(ctx, node, getIn) { return { r: toNumber(getIn('a')) * toNumber(getIn('b')) }; },
  },
  'math.sin': {
    title: 'Seno', category: 'Matematica',
    inputs: [{ name: 'x', label: 'x (rad)', kind: 'number', default: 0 }],
    outputs: [{ name: 'r', label: '', kind: 'number' }],
    eval(ctx, node, getIn) { return { r: Math.sin(toNumber(getIn('x'))) }; },
  },
  'math.scalevec': {
    title: 'Scala Vettore (V×s)', category: 'Matematica',
    inputs: [
      { name: 'v', label: 'V', kind: 'vec3', default: [0, 0, 0] },
      { name: 's', label: 'scala', kind: 'number', default: 1 },
    ],
    outputs: [{ name: 'r', label: '', kind: 'vec3' }],
    eval(ctx, node, getIn) { return { r: toVec3(getIn('v')).scale(toNumber(getIn('s'))) }; },
  },
  'math.addvec': {
    title: 'Somma Vettori', category: 'Matematica',
    inputs: [
      { name: 'a', label: 'A', kind: 'vec3', default: [0, 0, 0] },
      { name: 'b', label: 'B', kind: 'vec3', default: [0, 0, 0] },
    ],
    outputs: [{ name: 'r', label: '', kind: 'vec3' }],
    eval(ctx, node, getIn) { return { r: toVec3(getIn('a')).add(toVec3(getIn('b'))) }; },
  },
  'math.length': {
    title: 'Lunghezza Vettore', category: 'Matematica',
    inputs: [{ name: 'v', label: 'V', kind: 'vec3', default: [0, 0, 0] }],
    outputs: [{ name: 'r', label: '', kind: 'number' }],
    eval(ctx, node, getIn) { return { r: toVec3(getIn('v')).length() }; },
  },
  'math.normalize': {
    title: 'Normalizza Vettore', category: 'Matematica',
    inputs: [{ name: 'v', label: 'V', kind: 'vec3', default: [0, 0, 1] }],
    outputs: [{ name: 'r', label: '', kind: 'vec3' }],
    eval(ctx, node, getIn) { return { r: toVec3(getIn('v')).normalize() }; },
  },
  'math.negate': {
    title: 'Inverti Vettore (−V)', category: 'Matematica',
    inputs: [{ name: 'v', label: 'V', kind: 'vec3', default: [0, 0, 0] }],
    outputs: [{ name: 'r', label: '', kind: 'vec3' }],
    eval(ctx, node, getIn) { return { r: toVec3(getIn('v')).negate() }; },
  },

  // ═══ LOGICA ═══
  'logic.branch': {
    title: 'Se / Altrimenti', category: 'Logica',
    inputs: [
      { name: 'exec', label: '', kind: 'exec' },
      { name: 'cond', label: 'condizione', kind: 'bool', default: true },
    ],
    outputs: [
      { name: 'vero', label: 'vero', kind: 'exec' },
      { name: 'falso', label: 'falso', kind: 'exec' },
    ],
    exec(ctx, node, getIn) { return toBool(getIn('cond')) ? 'vero' : 'falso'; },
  },
  'logic.compare': {
    title: 'Confronta', category: 'Logica',
    inputs: [
      { name: 'a', label: 'A', kind: 'number', default: 0 },
      { name: 'b', label: 'B', kind: 'number', default: 0 },
    ],
    outputs: [{ name: 'r', label: '', kind: 'bool' }],
    props: { op: '>' }, propKinds: { op: ['>', '<', '≥', '≤', '='] },
    eval(ctx, node, getIn) {
      const a = toNumber(getIn('a')), b = toNumber(getIn('b'));
      const op = node.props.op;
      const r = op === '>' ? a > b : op === '<' ? a < b : op === '≥' ? a >= b : op === '≤' ? a <= b : Math.abs(a - b) < 1e-9;
      return { r };
    },
  },
  'logic.and': {
    title: 'E (AND)', category: 'Logica',
    inputs: [
      { name: 'a', label: 'A', kind: 'bool', default: true },
      { name: 'b', label: 'B', kind: 'bool', default: true },
    ],
    outputs: [{ name: 'r', label: '', kind: 'bool' }],
    eval(ctx, node, getIn) { return { r: toBool(getIn('a')) && toBool(getIn('b')) }; },
  },
  'logic.not': {
    title: 'Non (NOT)', category: 'Logica',
    inputs: [{ name: 'a', label: 'A', kind: 'bool', default: true }],
    outputs: [{ name: 'r', label: '', kind: 'bool' }],
    eval(ctx, node, getIn) { return { r: !toBool(getIn('a')) }; },
  },
};

export { KEY_OPTIONS };
