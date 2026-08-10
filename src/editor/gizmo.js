// ─── Translate gizmo: 3 axis arrows, ray hit-testing, drag logic ───
import { Vec3, Quat, Mat4 } from '../core/math.js';

const AXES = [new Vec3(1, 0, 0), new Vec3(0, 1, 0), new Vec3(0, 0, 1)];
const COLORS = [[0.9, 0.25, 0.28], [0.3, 0.85, 0.4], [0.28, 0.5, 0.95]];
const HIGHLIGHT = [1, 0.85, 0.2];
// rotation of Y-aligned mesh onto each axis
const ROTS = [
  new Quat().setAxisAngle(new Vec3(0, 0, 1), -Math.PI / 2),
  new Quat(),
  new Quat().setAxisAngle(new Vec3(1, 0, 0), Math.PI / 2),
];

export class Gizmo {
  constructor() {
    this.hoverAxis = -1;
    this.activeAxis = -1;
    this.dragStart = 0;     // axis param at drag start
    this.entityStart = new Vec3();
  }

  size(pos, camera) {
    return Math.max(camera.eye.distanceTo(pos) * 0.15, 0.4);
  }

  // build overlay draw items for the selected entity
  buildOverlay(pos, camera) {
    const s = this.size(pos, camera);
    const items = [];
    const scl = new Vec3();
    for (let i = 0; i < 3; i++) {
      const active = this.activeAxis === i || (this.activeAxis === -1 && this.hoverAxis === i);
      const color = active ? HIGHLIGHT : COLORS[i];
      const axis = AXES[i];
      // shaft
      const shaft = Mat4.create();
      const shaftPos = pos.clone().addScaled(axis, s * 0.4);
      Mat4.compose(shaft, shaftPos, ROTS[i], scl.set(s * 0.035, s * 0.8, s * 0.035));
      items.push({ mesh: 'cylinder', model: shaft, color });
      // arrow head
      const head = Mat4.create();
      const headPos = pos.clone().addScaled(axis, s * 0.9);
      Mat4.compose(head, headPos, ROTS[i], scl.set(s * 0.14, s * 0.26, s * 0.14));
      items.push({ mesh: 'cone', model: head, color });
    }
    // center cube
    const c = Mat4.create();
    Mat4.compose(c, pos, new Quat(), scl.set(s * 0.07, s * 0.07, s * 0.07));
    items.push({ mesh: 'cube', model: c, color: [0.9, 0.9, 0.95] });
    return items;
  }

  // returns axis index hit by ray, or -1
  hitTest(ray, pos, camera) {
    const s = this.size(pos, camera);
    let best = -1, bestDist = Infinity;
    for (let i = 0; i < 3; i++) {
      const res = closestRayToSegment(ray, pos, AXES[i], s * 1.05);
      if (res.dist < s * 0.12 && res.rayT > 0 && res.rayT < bestDist) {
        bestDist = res.rayT;
        best = i;
      }
    }
    return best;
  }

  beginDrag(ray, entityPos, axisIdx) {
    this.activeAxis = axisIdx;
    this.entityStart.copy(entityPos);
    this.dragStart = axisParam(ray, entityPos, AXES[axisIdx]);
  }

  drag(ray) {
    if (this.activeAxis < 0) return null;
    const t = axisParam(ray, this.entityStart, AXES[this.activeAxis]);
    const delta = t - this.dragStart;
    if (!isFinite(delta)) return null;
    return this.entityStart.clone().addScaled(AXES[this.activeAxis], Math.max(-500, Math.min(500, delta)));
  }

  endDrag() { this.activeAxis = -1; }
}

// closest approach between ray and axis line through `origin`; returns param on axis line
function axisParam(ray, origin, axisDir) {
  const r = Vec3.subVectors(new Vec3(), origin, ray.origin);
  const b = axisDir.dot(ray.dir);
  const c = axisDir.dot(r);
  const e = ray.dir.dot(r);
  const denom = 1 - b * b;
  if (Math.abs(denom) < 1e-6) return 0; // axis parallel to ray
  return (b * e - c) / denom;
}

function closestRayToSegment(ray, origin, axisDir, segLen) {
  let t = axisParam(ray, origin, axisDir);
  t = Math.max(0, Math.min(segLen, t));
  const pointOnAxis = origin.clone().addScaled(axisDir, t);
  // project onto ray
  const w = Vec3.subVectors(new Vec3(), pointOnAxis, ray.origin);
  const rayT = Math.max(0, w.dot(ray.dir));
  const pointOnRay = ray.origin.clone().addScaled(ray.dir, rayT);
  return { dist: pointOnAxis.distanceTo(pointOnRay), rayT };
}
