// ─── PhysicsWorld: integration, broadphase, narrowphase, solving, events ───
import { Vec3 } from '../core/math.js';
import { collide, raycastBody } from './collision.js';
import { Manifold } from './solver.js';

const VEL_ITERATIONS = 10;
const SLEEP_LIN_SQ = 0.02;
const SLEEP_ANG_SQ = 0.05;
const SLEEP_TIME = 0.6;

const tmpV = new Vec3();

export class PhysicsWorld {
  constructor() {
    this.gravity = new Vec3(0, -9.81, 0);
    this.bodies = [];
    this.constraints = [];
    this.manifolds = new Map(); // pairKey -> Manifold
    this.onContact = null;      // (bodyA, bodyB, impulse) => void
    this.contactCount = 0;
    this.time = 0;
  }

  addBody(body) { this.bodies.push(body); return body; }

  removeBody(body) {
    const i = this.bodies.indexOf(body);
    if (i >= 0) this.bodies.splice(i, 1);
    for (const key of [...this.manifolds.keys()]) {
      const m = this.manifolds.get(key);
      if (m.a === body || m.b === body) this.manifolds.delete(key);
    }
    this.constraints = this.constraints.filter(c => c.a !== body && c.b !== body);
  }

  addConstraint(c) { this.constraints.push(c); return c; }

  step(dt) {
    if (dt <= 0) return;
    this.time += dt;
    const invDt = 1 / dt;

    // 1. integrate velocities (forces + gravity + damping)
    for (const b of this.bodies) {
      if (b.type !== 'dynamic' || b.sleeping) continue;
      b.velocity.addScaled(this.gravity, dt);
      b.velocity.addScaled(b.force, b.invMass * dt);
      b.multiplyInvInertia(tmpV, b.torque);
      b.angularVelocity.addScaled(tmpV, dt);
      b.velocity.scale(Math.max(0, 1 - b.linearDamping * dt));
      b.angularVelocity.scale(Math.max(0, 1 - b.angularDamping * dt));
      b.force.set(0, 0, 0);
      b.torque.set(0, 0, 0);
    }

    // 2. broadphase: sweep and prune on X
    for (const b of this.bodies) b.updateAABB();
    const sorted = [...this.bodies].sort((a, b) => a.aabb.min.x - b.aabb.min.x);
    const pairs = [];
    for (let i = 0; i < sorted.length; i++) {
      const a = sorted[i];
      for (let j = i + 1; j < sorted.length; j++) {
        const b = sorted[j];
        if (b.aabb.min.x > a.aabb.max.x) break;
        if (a.type !== 'dynamic' && b.type !== 'dynamic') continue;
        if (a.sleeping && b.sleeping) continue;
        if (a.sleeping && b.type !== 'dynamic') continue;
        if (b.sleeping && a.type !== 'dynamic') continue;
        if (a.aabb.min.y > b.aabb.max.y || a.aabb.max.y < b.aabb.min.y) continue;
        if (a.aabb.min.z > b.aabb.max.z || a.aabb.max.z < b.aabb.min.z) continue;
        pairs.push(a.id < b.id ? [a, b] : [b, a]);
      }
    }

    // 3. narrowphase: update manifold cache
    const activeKeys = new Set();
    this.contactCount = 0;
    const newTouches = [];
    for (const [a, b] of pairs) {
      const key = a.id * 100000 + b.id;
      const contacts = collide(a, b);
      if (contacts && contacts.length) {
        activeKeys.add(key);
        let m = this.manifolds.get(key);
        if (!m) {
          m = new Manifold(a, b);
          this.manifolds.set(key, m);
          newTouches.push(m);
        }
        m.update(contacts);
        this.contactCount += contacts.length;
        // wake logic: a moving body wakes what it touches
        if (a.sleeping !== b.sleeping) {
          const awake = a.sleeping ? b : a;
          const asleep = a.sleeping ? a : b;
          if (awake.velocity.lengthSq() > SLEEP_LIN_SQ * 4 || awake.angularVelocity.lengthSq() > SLEEP_ANG_SQ * 4) {
            asleep.wake();
          }
        }
      }
    }
    for (const key of [...this.manifolds.keys()]) {
      if (!activeKeys.has(key)) this.manifolds.delete(key);
    }

    // 4. solve velocity constraints
    const solveList = [];
    for (const m of this.manifolds.values()) {
      if (m.a.sleeping && m.b.sleeping) continue;
      m.prepare(invDt);
      m.warmStart();
      solveList.push(m);
    }
    for (const c of this.constraints) {
      if (c.a.sleeping && c.b.sleeping) continue;
      if (!c.a.sleeping || !c.b.sleeping) { c.a.wake?.call(c.a); c.b.wake?.call(c.b); }
      c.impulse *= 0.9;
      c.prepare(invDt);
    }
    for (let it = 0; it < VEL_ITERATIONS; it++) {
      for (const c of this.constraints) c.solve();
      for (const m of solveList) m.solve();
    }

    // 5. integrate positions
    for (const b of this.bodies) {
      if (b.type !== 'dynamic' || b.sleeping) continue;
      b.position.addScaled(b.velocity, dt);
      b.quat.integrate(b.angularVelocity, dt);
      b.updateInertiaWorld();
    }

    // 6. sleeping
    for (const b of this.bodies) {
      if (b.type !== 'dynamic' || b.sleeping || !b.canSleep) continue;
      if (b.velocity.lengthSq() < SLEEP_LIN_SQ && b.angularVelocity.lengthSq() < SLEEP_ANG_SQ) {
        b.sleepTimer += dt;
        if (b.sleepTimer > SLEEP_TIME) b.sleep();
      } else {
        b.sleepTimer = 0;
      }
    }

    // 7. contact events (only for freshly-touching pairs)
    if (this.onContact) {
      for (const m of newTouches) {
        this.onContact(m.a, m.b, m.sumNormalImpulse());
      }
    }
  }

  raycast(origin, dir, maxDist = 1000, filter = null) {
    let best = null;
    for (const b of this.bodies) {
      if (filter && !filter(b)) continue;
      const hit = raycastBody(b, origin, dir, maxDist);
      if (hit && (!best || hit.t < best.t)) {
        best = hit;
        best.body = b;
      }
    }
    return best;
  }
}
