// ─── Joints: distance constraint (rod or rope) between two bodies ───
import { Vec3 } from '../core/math.js';

const tA = new Vec3(), tB = new Vec3(), tC = new Vec3(), tD = new Vec3();

export class DistanceConstraint {
  constructor(bodyA, bodyB, localAnchorA = new Vec3(), localAnchorB = new Vec3(), length = null, mode = 'rod') {
    this.a = bodyA;
    this.b = bodyB;
    this.localAnchorA = localAnchorA.clone();
    this.localAnchorB = localAnchorB.clone();
    this.mode = mode; // 'rod' (fixed distance) | 'rope' (max distance)
    this.worldA = new Vec3();
    this.worldB = new Vec3();
    this.rA = new Vec3();
    this.rB = new Vec3();
    this.dir = new Vec3();
    this.effMass = 0;
    this.bias = 0;
    this.impulse = 0;
    this.active = true;
    if (length === null) {
      this.updateAnchors();
      this.length = this.worldA.distanceTo(this.worldB);
    } else {
      this.length = length;
    }
  }

  updateAnchors() {
    this.worldA.copy(this.localAnchorA).applyQuat(this.a.quat).add(this.a.position);
    this.worldB.copy(this.localAnchorB).applyQuat(this.b.quat).add(this.b.position);
  }

  prepare(invDt) {
    this.updateAnchors();
    Vec3.subVectors(this.rA, this.worldA, this.a.position);
    Vec3.subVectors(this.rB, this.worldB, this.b.position);
    Vec3.subVectors(this.dir, this.worldB, this.worldA);
    const dist = this.dir.length();
    const C = dist - this.length;
    this.active = this.mode === 'rod' || C > 0;
    if (!this.active) return;
    if (dist > 1e-9) this.dir.scale(1 / dist); else this.dir.set(0, 1, 0);

    // effective mass along dir
    let k = this.a.invMass + this.b.invMass;
    Vec3.crossVectors(tA, this.rA, this.dir);
    this.a.multiplyInvInertia(tB, tA);
    Vec3.crossVectors(tC, tB, this.rA);
    k += tC.dot(this.dir);
    Vec3.crossVectors(tA, this.rB, this.dir);
    this.b.multiplyInvInertia(tB, tA);
    Vec3.crossVectors(tC, tB, this.rB);
    k += tC.dot(this.dir);
    this.effMass = k > 1e-9 ? 1 / k : 0;
    this.bias = 0.2 * invDt * C;

    // warm start
    tD.copy(this.dir).scale(this.impulse);
    this.applyImpulse(tD);
  }

  applyImpulse(imp) {
    const a = this.a, b = this.b;
    if (a.invMass > 0) {
      a.velocity.addScaled(imp, -a.invMass);
      Vec3.crossVectors(tA, this.rA, imp);
      a.multiplyInvInertia(tB, tA);
      a.angularVelocity.addScaled(tB, -1);
    }
    if (b.invMass > 0) {
      b.velocity.addScaled(imp, b.invMass);
      Vec3.crossVectors(tA, this.rB, imp);
      b.multiplyInvInertia(tB, tA);
      b.angularVelocity.add(tB);
    }
  }

  solve() {
    if (!this.active) return;
    // relative velocity along dir
    Vec3.crossVectors(tA, this.b.angularVelocity, this.rB).add(this.b.velocity);
    Vec3.crossVectors(tB, this.a.angularVelocity, this.rA);
    tA.sub(tB).sub(this.a.velocity);
    const vRel = tA.dot(this.dir);
    let dLambda = -(vRel + this.bias) * this.effMass;
    if (this.mode === 'rope') {
      // rope can only pull, never push
      const newImp = Math.min(this.impulse + dLambda, 0);
      dLambda = newImp - this.impulse;
      this.impulse = newImp;
    } else {
      this.impulse += dLambda;
    }
    tD.copy(this.dir).scale(dLambda);
    this.applyImpulse(tD);
  }
}
