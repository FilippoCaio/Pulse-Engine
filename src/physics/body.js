// ─── RigidBody: dynamic/static rigid body with sphere or box shape ───
import { Vec3, Quat } from '../core/math.js';

let NEXT_BODY_ID = 1;

export class RigidBody {
  constructor({ type = 'dynamic', shape, mass = 1, restitution = 0.3, friction = 0.5 } = {}) {
    this.id = NEXT_BODY_ID++;
    this.entity = null;          // back-reference to scene entity
    this.type = type;            // 'dynamic' | 'static'
    this.shape = shape || { kind: 'box', hx: 0.5, hy: 0.5, hz: 0.5 }; // or { kind:'sphere', radius }

    this.position = new Vec3();
    this.quat = new Quat();
    this.velocity = new Vec3();
    this.angularVelocity = new Vec3();
    this.force = new Vec3();
    this.torque = new Vec3();

    this.restitution = restitution;
    this.friction = friction;
    this.linearDamping = 0.01;
    this.angularDamping = 0.05;

    this.sleeping = false;
    this.sleepTimer = 0;
    this.canSleep = true;

    this.invInertiaWorld = new Float32Array(9); // 3x3 row-major
    this.invInertiaLocal = new Vec3();
    this.aabb = { min: new Vec3(), max: new Vec3() };

    this.setMass(type === 'static' ? 0 : mass);
  }

  setMass(mass) {
    if (this.type === 'static' || mass <= 0) {
      this.mass = 0;
      this.invMass = 0;
      this.invInertiaLocal.set(0, 0, 0);
    } else {
      this.mass = mass;
      this.invMass = 1 / mass;
      const s = this.shape;
      if (s.kind === 'sphere') {
        const i = (2 / 5) * mass * s.radius * s.radius;
        this.invInertiaLocal.set(1 / i, 1 / i, 1 / i);
      } else {
        const ex = 2 * s.hx, ey = 2 * s.hy, ez = 2 * s.hz;
        const ix = (mass / 12) * (ey * ey + ez * ez);
        const iy = (mass / 12) * (ex * ex + ez * ez);
        const iz = (mass / 12) * (ex * ex + ey * ey);
        this.invInertiaLocal.set(1 / ix, 1 / iy, 1 / iz);
      }
    }
    this.updateInertiaWorld();
  }

  updateInertiaWorld() {
    // I⁻¹_world = R · diag(I⁻¹_local) · Rᵀ
    const q = this.quat;
    const x = q.x, y = q.y, z = q.z, w = q.w;
    const r00 = 1 - 2 * (y * y + z * z), r01 = 2 * (x * y - w * z), r02 = 2 * (x * z + w * y);
    const r10 = 2 * (x * y + w * z), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z - w * x);
    const r20 = 2 * (x * z - w * y), r21 = 2 * (y * z + w * x), r22 = 1 - 2 * (x * x + y * y);
    const dx = this.invInertiaLocal.x, dy = this.invInertiaLocal.y, dz = this.invInertiaLocal.z;
    const m = this.invInertiaWorld;
    // R * D
    const a00 = r00 * dx, a01 = r01 * dy, a02 = r02 * dz;
    const a10 = r10 * dx, a11 = r11 * dy, a12 = r12 * dz;
    const a20 = r20 * dx, a21 = r21 * dy, a22 = r22 * dz;
    // (R*D) * Rᵀ
    m[0] = a00 * r00 + a01 * r01 + a02 * r02;
    m[1] = a00 * r10 + a01 * r11 + a02 * r12;
    m[2] = a00 * r20 + a01 * r21 + a02 * r22;
    m[3] = a10 * r00 + a11 * r01 + a12 * r02;
    m[4] = a10 * r10 + a11 * r11 + a12 * r12;
    m[5] = a10 * r20 + a11 * r21 + a12 * r22;
    m[6] = a20 * r00 + a21 * r01 + a22 * r02;
    m[7] = a20 * r10 + a21 * r11 + a22 * r12;
    m[8] = a20 * r20 + a21 * r21 + a22 * r22;
  }

  // out = I⁻¹_world · v
  multiplyInvInertia(out, v) {
    const m = this.invInertiaWorld;
    const { x, y, z } = v;
    out.set(m[0] * x + m[1] * y + m[2] * z,
            m[3] * x + m[4] * y + m[5] * z,
            m[6] * x + m[7] * y + m[8] * z);
    return out;
  }

  updateAABB() {
    const p = this.position, s = this.shape, { min, max } = this.aabb;
    if (s.kind === 'sphere') {
      min.set(p.x - s.radius, p.y - s.radius, p.z - s.radius);
      max.set(p.x + s.radius, p.y + s.radius, p.z + s.radius);
    } else {
      // extent of rotated box = |R| · h
      const q = this.quat;
      const x = q.x, y = q.y, z = q.z, w = q.w;
      const ex = Math.abs(1 - 2 * (y * y + z * z)) * s.hx + Math.abs(2 * (x * y - w * z)) * s.hy + Math.abs(2 * (x * z + w * y)) * s.hz;
      const ey = Math.abs(2 * (x * y + w * z)) * s.hx + Math.abs(1 - 2 * (x * x + z * z)) * s.hy + Math.abs(2 * (y * z - w * x)) * s.hz;
      const ez = Math.abs(2 * (x * z - w * y)) * s.hx + Math.abs(2 * (y * z + w * x)) * s.hy + Math.abs(1 - 2 * (x * x + y * y)) * s.hz;
      min.set(p.x - ex, p.y - ey, p.z - ez);
      max.set(p.x + ex, p.y + ey, p.z + ez);
    }
  }

  applyForce(f, worldPoint = null) {
    if (this.type !== 'dynamic') return;
    this.wake();
    this.force.add(f);
    if (worldPoint) {
      const r = new Vec3().copy(worldPoint).sub(this.position);
      this.torque.add(r.cross(f));
    }
  }

  applyTorque(t) {
    if (this.type !== 'dynamic') return;
    this.wake();
    this.torque.add(t);
  }

  applyImpulse(imp, worldPoint = null) {
    if (this.type !== 'dynamic') return;
    this.wake();
    this.velocity.addScaled(imp, this.invMass);
    if (worldPoint) {
      const r = new Vec3().copy(worldPoint).sub(this.position);
      const angImp = r.cross(new Vec3().copy(imp));
      const dw = new Vec3();
      this.multiplyInvInertia(dw, angImp);
      this.angularVelocity.add(dw);
    }
  }

  wake() {
    this.sleeping = false;
    this.sleepTimer = 0;
  }

  sleep() {
    this.sleeping = true;
    this.velocity.set(0, 0, 0);
    this.angularVelocity.set(0, 0, 0);
  }
}
