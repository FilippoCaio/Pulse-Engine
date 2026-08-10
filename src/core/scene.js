// ─── Scene model: entities with components + JSON serialization ───
import { Vec3, Quat } from './math.js';

let NEXT_ENTITY_ID = 1;

export class Entity {
  constructor(name = 'Oggetto') {
    this.id = NEXT_ENTITY_ID++;
    this.name = name;
    this.position = new Vec3();
    this.rotation = new Quat();
    this.scale = new Vec3(1, 1, 1);
    this.mesh = null;       // { shape:'cube'|'sphere'|'cylinder', color:[r,g,b], shininess, specular, checker, emissive }
    this.body = null;       // { type:'dynamic'|'static', mass, friction, restitution, linearDamping, angularDamping }
    this.light = null;      // { color:[r,g,b], intensity, range }
    this.script = null;     // { graphId }
    this.joint = null;      // { targetId, mode:'rod'|'rope', length:number|null }
    this.runtimeBody = null; // RigidBody during play
  }

  // collider shape derived from mesh shape + scale
  colliderShape() {
    const meshShape = this.mesh?.shape || 'cube';
    if (meshShape === 'sphere') {
      return { kind: 'sphere', radius: Math.max(0.01, Math.abs(this.scale.x) * 0.5) };
    }
    return {
      kind: 'box',
      hx: Math.max(0.01, Math.abs(this.scale.x) * 0.5),
      hy: Math.max(0.01, Math.abs(this.scale.y) * 0.5),
      hz: Math.max(0.01, Math.abs(this.scale.z) * 0.5),
    };
  }

  serialize() {
    return {
      id: this.id,
      name: this.name,
      position: this.position.toArray(),
      rotation: this.rotation.toArray(),
      scale: this.scale.toArray(),
      mesh: this.mesh ? { ...this.mesh, color: [...this.mesh.color] } : null,
      body: this.body ? { ...this.body } : null,
      light: this.light ? { ...this.light, color: [...this.light.color] } : null,
      script: this.script ? { ...this.script } : null,
      joint: this.joint ? { ...this.joint } : null,
    };
  }

  static deserialize(data) {
    const e = new Entity(data.name);
    e.id = data.id;
    NEXT_ENTITY_ID = Math.max(NEXT_ENTITY_ID, data.id + 1);
    e.position.fromArray(data.position);
    e.rotation.fromArray(data.rotation);
    e.scale.fromArray(data.scale);
    e.mesh = data.mesh ? { ...data.mesh, color: [...data.mesh.color] } : null;
    e.body = data.body ? { ...data.body } : null;
    e.light = data.light ? { ...data.light, color: [...data.light.color] } : null;
    e.script = data.script ? { ...data.script } : null;
    e.joint = data.joint ? { ...data.joint } : null;
    return e;
  }

  clone() {
    const d = this.serialize();
    d.id = NEXT_ENTITY_ID++;
    d.name = this.name + ' copia';
    return Entity.deserialize(d);
  }
}

export function defaultEnv() {
  return {
    gravity: -9.81,
    sunAzimuth: 40,      // degrees
    sunElevation: 42,
    sunIntensity: 1.15,
    fogDensity: 0.006,
    shadowStrength: 0.85,
  };
}

export class Scene {
  constructor() {
    this.entities = [];
    this.env = defaultEnv();
  }

  add(entity) { this.entities.push(entity); return entity; }

  remove(entity) {
    const i = this.entities.indexOf(entity);
    if (i >= 0) this.entities.splice(i, 1);
  }

  getById(id) { return this.entities.find(e => e.id === id) || null; }
  getByName(name) { return this.entities.find(e => e.name === name) || null; }

  serialize() {
    return {
      env: { ...this.env },
      entities: this.entities.map(e => e.serialize()),
    };
  }

  static deserialize(data) {
    const s = new Scene();
    s.env = { ...defaultEnv(), ...(data.env || {}) };
    s.entities = (data.entities || []).map(Entity.deserialize);
    return s;
  }
}

// ─── entity factories ───
export function makeGround(size = 24) {
  const e = new Entity('Pavimento');
  e.position.set(0, -0.5, 0);
  e.scale.set(size, 1, size);
  e.mesh = { shape: 'cube', color: [0.42, 0.45, 0.5], shininess: 24, specular: 0.12, checker: 2, emissive: 0 };
  e.body = { type: 'static', mass: 0, friction: 0.7, restitution: 0.25, linearDamping: 0.01, angularDamping: 0.05 };
  return e;
}

export function makeBox(name = 'Cubo') {
  const e = new Entity(name);
  e.mesh = { shape: 'cube', color: [0.85, 0.55, 0.25], shininess: 48, specular: 0.35, checker: 0, emissive: 0 };
  e.body = { type: 'dynamic', mass: 1, friction: 0.5, restitution: 0.25, linearDamping: 0.01, angularDamping: 0.05 };
  return e;
}

export function makeSphere(name = 'Sfera') {
  const e = new Entity(name);
  e.mesh = { shape: 'sphere', color: [0.35, 0.65, 0.95], shininess: 90, specular: 0.6, checker: 0, emissive: 0 };
  e.body = { type: 'dynamic', mass: 1, friction: 0.4, restitution: 0.55, linearDamping: 0.01, angularDamping: 0.05 };
  return e;
}

export function makeCylinder(name = 'Cilindro') {
  const e = new Entity(name);
  e.mesh = { shape: 'cylinder', color: [0.6, 0.85, 0.45], shininess: 48, specular: 0.3, checker: 0, emissive: 0 };
  e.body = { type: 'dynamic', mass: 1, friction: 0.5, restitution: 0.25, linearDamping: 0.01, angularDamping: 0.05 };
  return e;
}

export function makeLight(name = 'Luce') {
  const e = new Entity(name);
  e.position.set(0, 3, 0);
  e.scale.set(0.25, 0.25, 0.25);
  e.mesh = { shape: 'sphere', color: [1, 0.9, 0.6], shininess: 10, specular: 0, checker: 0, emissive: 3 };
  e.light = { color: [1, 0.85, 0.55], intensity: 3, range: 12 };
  return e;
}
