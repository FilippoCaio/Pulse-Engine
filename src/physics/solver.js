// ─── Sequential-impulse contact solver with warm starting ───
import { Vec3 } from '../core/math.js';

const BAUMGARTE = 0.2;
const SLOP = 0.005;
const RESTITUTION_THRESHOLD = 1.0; // m/s

const tA = new Vec3(), tB = new Vec3(), tC = new Vec3(), tD = new Vec3();

class ContactPoint {
  constructor() {
    this.rA = new Vec3();
    this.rB = new Vec3();
    this.normal = new Vec3();
    this.tangent1 = new Vec3();
    this.tangent2 = new Vec3();
    this.normalMass = 0;
    this.tangentMass1 = 0;
    this.tangentMass2 = 0;
    this.bias = 0;
    this.impulseN = 0;   // accumulated
    this.impulseT1 = 0;
    this.impulseT2 = 0;
    this.localA = new Vec3(); // anchor in A local space, for warm-start matching
  }
}

export class Manifold {
  constructor(a, b) {
    this.a = a;
    this.b = b;
    this.points = [];
    this.friction = Math.sqrt(a.friction * b.friction);
    this.restitution = Math.max(a.restitution, b.restitution);
    this.totalImpulse = 0; // for collision events
    this.fresh = true;
  }

  update(contacts) {
    const a = this.a;
    const oldPoints = this.points;
    this.points = [];
    this.friction = Math.sqrt(a.friction * this.b.friction);
    this.restitution = Math.max(a.restitution, this.b.restitution);
    const invQ = { x: -a.quat.x, y: -a.quat.y, z: -a.quat.z, w: a.quat.w };
    for (const c of contacts) {
      const cp = new ContactPoint();
      cp.rA.copy(c.point).sub(a.position);
      cp.rB.copy(c.point).sub(this.b.position);
      cp.normal.copy(c.normal);
      cp.depth = c.depth;
      cp.localA.copy(cp.rA).applyQuat(invQ);
      // warm start: reuse impulses from nearest old point
      let best = null, bestD = 0.02 * 0.02;
      for (const op of oldPoints) {
        const d = Vec3.subVectors(tA, op.localA, cp.localA).lengthSq();
        if (d < bestD) { bestD = d; best = op; }
      }
      if (best) {
        cp.impulseN = best.impulseN;
        cp.impulseT1 = best.impulseT1;
        cp.impulseT2 = best.impulseT2;
      }
      this.points.push(cp);
    }
  }

  prepare(invDt) {
    const a = this.a, b = this.b;
    for (const cp of this.points) {
      const n = cp.normal;
      // tangent basis
      if (Math.abs(n.x) > 0.7) cp.tangent1.set(-n.y, n.x, 0).normalize();
      else cp.tangent1.set(0, -n.z, n.y).normalize();
      Vec3.crossVectors(cp.tangent2, n, cp.tangent1);

      cp.normalMass = 1 / this.effectiveMass(cp, n);
      cp.tangentMass1 = 1 / this.effectiveMass(cp, cp.tangent1);
      cp.tangentMass2 = 1 / this.effectiveMass(cp, cp.tangent2);

      // relative velocity at contact (B relative to A)
      const vRel = this.relVelocity(cp, tC);
      const vn = vRel.dot(n);
      // restitution bounce + positional Baumgarte bias, take the stronger
      const bounce = vn < -RESTITUTION_THRESHOLD ? -this.restitution * vn : 0;
      const baum = BAUMGARTE * invDt * Math.max(0, cp.depth - SLOP);
      cp.bias = Math.max(bounce, baum);
    }
  }

  effectiveMass(cp, dir) {
    const a = this.a, b = this.b;
    let k = a.invMass + b.invMass;
    Vec3.crossVectors(tA, cp.rA, dir);
    a.multiplyInvInertia(tB, tA);
    Vec3.crossVectors(tD, tB, cp.rA);
    k += tD.dot(dir);
    Vec3.crossVectors(tA, cp.rB, dir);
    b.multiplyInvInertia(tB, tA);
    Vec3.crossVectors(tD, tB, cp.rB);
    k += tD.dot(dir);
    return Math.max(k, 1e-9);
  }

  relVelocity(cp, out) {
    const a = this.a, b = this.b;
    // v = vB + ωB×rB − vA − ωA×rA
    Vec3.crossVectors(out, b.angularVelocity, cp.rB).add(b.velocity);
    Vec3.crossVectors(tA, a.angularVelocity, cp.rA);
    out.sub(tA).sub(a.velocity);
    return out;
  }

  applyImpulseAt(cp, imp) {
    const a = this.a, b = this.b;
    if (a.invMass > 0) {
      a.velocity.addScaled(imp, -a.invMass);
      Vec3.crossVectors(tA, cp.rA, imp);
      a.multiplyInvInertia(tB, tA);
      a.angularVelocity.addScaled(tB, -1);
    }
    if (b.invMass > 0) {
      b.velocity.addScaled(imp, b.invMass);
      Vec3.crossVectors(tA, cp.rB, imp);
      b.multiplyInvInertia(tB, tA);
      b.angularVelocity.add(tB);
    }
  }

  warmStart() {
    for (const cp of this.points) {
      tD.set(0, 0, 0)
        .addScaled(cp.normal, cp.impulseN)
        .addScaled(cp.tangent1, cp.impulseT1)
        .addScaled(cp.tangent2, cp.impulseT2);
      this.applyImpulseAt(cp, tD);
    }
  }

  solve() {
    for (const cp of this.points) {
      // normal impulse
      const vRel = this.relVelocity(cp, tC);
      const vn = vRel.dot(cp.normal);
      let dLambda = -(vn - cp.bias) * cp.normalMass;
      const newImpulse = Math.max(cp.impulseN + dLambda, 0);
      dLambda = newImpulse - cp.impulseN;
      cp.impulseN = newImpulse;
      tD.copy(cp.normal).scale(dLambda);
      this.applyImpulseAt(cp, tD);

      // friction impulses (clamped to μ·Pn)
      const maxF = this.friction * cp.impulseN;
      const vRel2 = this.relVelocity(cp, tC);

      let vt1 = vRel2.dot(cp.tangent1);
      let dT1 = -vt1 * cp.tangentMass1;
      const newT1 = Math.max(-maxF, Math.min(maxF, cp.impulseT1 + dT1));
      dT1 = newT1 - cp.impulseT1;
      cp.impulseT1 = newT1;

      let vt2 = vRel2.dot(cp.tangent2);
      let dT2 = -vt2 * cp.tangentMass2;
      const newT2 = Math.max(-maxF, Math.min(maxF, cp.impulseT2 + dT2));
      dT2 = newT2 - cp.impulseT2;
      cp.impulseT2 = newT2;

      tD.set(0, 0, 0).addScaled(cp.tangent1, dT1).addScaled(cp.tangent2, dT2);
      this.applyImpulseAt(cp, tD);
    }
  }

  sumNormalImpulse() {
    let s = 0;
    for (const cp of this.points) s += cp.impulseN;
    return s;
  }
}
