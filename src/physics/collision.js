// ─── Narrowphase: contact generation for sphere/box pairs + raycasts ───
// Contacts: { point:Vec3 (world), normal:Vec3 (from A to B), depth:number }
import { Vec3 } from '../core/math.js';

const tmp1 = new Vec3(), tmp2 = new Vec3(), tmp3 = new Vec3();

export function collide(a, b) {
  const ka = a.shape.kind, kb = b.shape.kind;
  if (ka === 'sphere' && kb === 'sphere') return sphereSphere(a, b);
  if (ka === 'sphere' && kb === 'box') return flip(boxSphere(b, a));
  if (ka === 'box' && kb === 'sphere') return boxSphere(a, b);
  return boxBox(a, b);
}

function flip(contacts) {
  if (contacts) for (const c of contacts) c.normal.negate();
  return contacts;
}

function sphereSphere(a, b) {
  const ra = a.shape.radius, rb = b.shape.radius;
  const d = Vec3.subVectors(tmp1, b.position, a.position);
  const distSq = d.lengthSq();
  const rSum = ra + rb;
  if (distSq >= rSum * rSum) return null;
  const dist = Math.sqrt(distSq);
  const normal = dist > 1e-9 ? d.clone().scale(1 / dist) : new Vec3(0, 1, 0);
  const point = a.position.clone().addScaled(normal, ra - (rSum - dist) / 2);
  return [{ point, normal, depth: rSum - dist }];
}

// rotate world→box local
function worldToLocal(out, box, worldPoint) {
  out.copy(worldPoint).sub(box.position);
  const q = box.quat;
  // apply inverse quaternion
  const inv = { x: -q.x, y: -q.y, z: -q.z, w: q.w };
  return out.applyQuat(inv);
}

function boxSphere(box, sphere) {
  // A = box, B = sphere; normal from box to sphere
  const s = box.shape, r = sphere.shape.radius;
  const local = worldToLocal(tmp1, box, sphere.position);
  const cx = Math.max(-s.hx, Math.min(s.hx, local.x));
  const cy = Math.max(-s.hy, Math.min(s.hy, local.y));
  const cz = Math.max(-s.hz, Math.min(s.hz, local.z));
  let nx, ny, nz, depth;
  const dx = local.x - cx, dy = local.y - cy, dz = local.z - cz;
  const distSq = dx * dx + dy * dy + dz * dz;
  if (distSq > 1e-12) {
    // sphere center outside box
    if (distSq >= r * r) return null;
    const dist = Math.sqrt(distSq);
    nx = dx / dist; ny = dy / dist; nz = dz / dist;
    depth = r - dist;
  } else {
    // center inside: push out along smallest penetration axis
    const px = s.hx - Math.abs(local.x), py = s.hy - Math.abs(local.y), pz = s.hz - Math.abs(local.z);
    if (px < py && px < pz) { nx = Math.sign(local.x) || 1; ny = 0; nz = 0; depth = px + r; }
    else if (py < pz) { nx = 0; ny = Math.sign(local.y) || 1; nz = 0; depth = py + r; }
    else { nx = 0; ny = 0; nz = Math.sign(local.z) || 1; depth = pz + r; }
  }
  const normal = new Vec3(nx, ny, nz).applyQuat(box.quat);
  const point = new Vec3(cx, cy, cz).applyQuat(box.quat).add(box.position);
  return [{ point, normal, depth }];
}

// ─── Box-Box: SAT with reference-face clipping (up to 4 contact points) ───
function boxAxes(box) {
  const q = box.quat;
  const x = q.x, y = q.y, z = q.z, w = q.w;
  return [
    new Vec3(1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)),
    new Vec3(2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)),
    new Vec3(2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)),
  ];
}

function projectBox(box, axes, axis) {
  const s = box.shape;
  return Math.abs(axes[0].dot(axis)) * s.hx +
         Math.abs(axes[1].dot(axis)) * s.hy +
         Math.abs(axes[2].dot(axis)) * s.hz;
}

function boxBox(a, b) {
  const axesA = boxAxes(a), axesB = boxAxes(b);
  const d = Vec3.subVectors(new Vec3(), b.position, a.position);

  let minPen = Infinity, bestAxis = null, bestType = -1; // 0..5 face, 6+ edge
  let bestEdgeA = -1, bestEdgeB = -1;

  const testAxis = (axis, type, ea = -1, eb = -1) => {
    const lenSq = axis.lengthSq();
    if (lenSq < 1e-8) return true; // degenerate (parallel edges), skip
    const inv = 1 / Math.sqrt(lenSq);
    const ax = axis.clone().scale(inv);
    const dist = Math.abs(d.dot(ax));
    const pen = projectBox(a, axesA, ax) + projectBox(b, axesB, ax) - dist;
    if (pen < 0) return false; // separating axis found
    // bias: prefer face contacts for stability
    const weighted = type >= 6 ? pen * 0.95 - 1e-4 : pen;
    const bestWeighted = bestType >= 6 ? minPen * 0.95 - 1e-4 : minPen;
    if (weighted < bestWeighted) {
      minPen = pen;
      bestAxis = ax;
      bestType = type;
      bestEdgeA = ea; bestEdgeB = eb;
    }
    return true;
  };

  for (let i = 0; i < 3; i++) if (!testAxis(axesA[i].clone(), i)) return null;
  for (let i = 0; i < 3; i++) if (!testAxis(axesB[i].clone(), 3 + i)) return null;
  for (let i = 0; i < 3; i++)
    for (let j = 0; j < 3; j++)
      if (!testAxis(Vec3.crossVectors(new Vec3(), axesA[i], axesB[j]), 6 + i * 3 + j, i, j)) return null;

  if (!bestAxis) return null;
  // normal must point from A to B
  const normal = bestAxis.clone();
  if (normal.dot(d) < 0) normal.negate();

  if (bestType >= 6) {
    // edge-edge contact
    return edgeContact(a, b, axesA, axesB, bestEdgeA, bestEdgeB, normal, minPen);
  }
  // face contact: reference = box owning the axis
  if (bestType < 3) return faceContact(a, b, axesA, axesB, normal, minPen, false);
  return faceContact(b, a, axesB, axesA, normal.clone().negate(), minPen, true);
}

function boxVertices(box, axes) {
  const s = box.shape, p = box.position;
  const vs = [];
  for (let i = 0; i < 8; i++) {
    const sx = (i & 1) ? s.hx : -s.hx;
    const sy = (i & 2) ? s.hy : -s.hy;
    const sz = (i & 4) ? s.hz : -s.hz;
    vs.push(new Vec3(
      p.x + axes[0].x * sx + axes[1].x * sy + axes[2].x * sz,
      p.y + axes[0].y * sx + axes[1].y * sy + axes[2].y * sz,
      p.z + axes[0].z * sx + axes[1].z * sy + axes[2].z * sz,
    ));
  }
  return vs;
}

// face of a box as 4 vertices, given axis index and sign
function faceVerts(box, axes, axisIdx, sign) {
  const s = box.shape;
  const h = [s.hx, s.hy, s.hz];
  const u = (axisIdx + 1) % 3, v = (axisIdx + 2) % 3;
  const c = box.position.clone().addScaled(axes[axisIdx], h[axisIdx] * sign);
  const verts = [];
  for (const [su, sv] of [[1, 1], [-1, 1], [-1, -1], [1, -1]]) {
    verts.push(c.clone().addScaled(axes[u], h[u] * su).addScaled(axes[v], h[v] * sv));
  }
  return verts;
}

// clip polygon by plane (keep points with n·p <= dist)
function clipPoly(poly, n, dist) {
  const out = [];
  for (let i = 0; i < poly.length; i++) {
    const p0 = poly[i], p1 = poly[(i + 1) % poly.length];
    const d0 = n.dot(p0) - dist, d1 = n.dot(p1) - dist;
    if (d0 <= 0) out.push(p0);
    if ((d0 < 0 && d1 > 0) || (d0 > 0 && d1 < 0)) {
      const t = d0 / (d0 - d1);
      out.push(p0.clone().lerp(p1, t));
    }
  }
  return out;
}

// ref = reference box (normal points away from ref toward inc)
function faceContact(ref, inc, axesRef, axesInc, refNormal, pen, flipped) {
  // reference face: axis of ref most aligned with refNormal
  let refIdx = 0, refBest = -Infinity, refSign = 1;
  for (let i = 0; i < 3; i++) {
    const dp = axesRef[i].dot(refNormal);
    if (Math.abs(dp) > refBest) { refBest = Math.abs(dp); refIdx = i; refSign = Math.sign(dp) || 1; }
  }
  // incident face: axis of inc most anti-aligned with refNormal
  let incIdx = 0, incBest = Infinity, incSign = 1;
  for (let i = 0; i < 3; i++) {
    const dp = axesInc[i].dot(refNormal);
    if (dp < incBest) { incBest = dp; incIdx = i; incSign = 1; }
    if (-dp < incBest) { incBest = -dp; incIdx = i; incSign = -1; }
  }
  let poly = faceVerts(inc, axesInc, incIdx, incSign);

  // clip against 4 side planes of reference face
  const h = [ref.shape.hx, ref.shape.hy, ref.shape.hz];
  const u = (refIdx + 1) % 3, v = (refIdx + 2) % 3;
  for (const [axis, he] of [[axesRef[u], h[u]], [axesRef[v], h[v]]]) {
    const cdot = axis.dot(ref.position);
    poly = clipPoly(poly, axis, cdot + he);
    if (!poly.length) return null;
    poly = clipPoly(poly, axis.clone().negate(), -(cdot - he));
    if (!poly.length) return null;
  }

  // keep points below reference face plane
  const faceN = axesRef[refIdx].clone().scale(refSign);
  const faceDist = faceN.dot(ref.position) + h[refIdx];
  const contacts = [];
  for (const p of poly) {
    const depth = faceDist - faceN.dot(p);
    if (depth >= -1e-4) {
      // contact normal must go from A to B in caller's original order
      const n = flipped ? faceN.clone().negate() : faceN.clone();
      contacts.push({ point: p.clone().addScaled(faceN, depth / 2), normal: n, depth: Math.max(depth, 0) });
    }
  }
  if (!contacts.length) return null;
  // limit to the 4 deepest for solver stability
  contacts.sort((x, y) => y.depth - x.depth);
  return contacts.slice(0, 4);
}

function edgeContact(a, b, axesA, axesB, ea, eb, normal, pen) {
  // find supporting edge on each box along ±normal
  const suppEdge = (box, axes, axisIdx, n) => {
    const s = box.shape;
    const h = [s.hx, s.hy, s.hz];
    const u = (axisIdx + 1) % 3, v = (axisIdx + 2) % 3;
    const su = Math.sign(axes[u].dot(n)) || 1;
    const sv = Math.sign(axes[v].dot(n)) || 1;
    const mid = box.position.clone().addScaled(axes[u], h[u] * su).addScaled(axes[v], h[v] * sv);
    return [mid.clone().addScaled(axes[axisIdx], -h[axisIdx]), mid.clone().addScaled(axes[axisIdx], h[axisIdx])];
  };
  const [p1, q1] = suppEdge(a, axesA, ea, normal);
  const [p2, q2] = suppEdge(b, axesB, eb, normal.clone().negate());

  // closest points between segments
  const d1 = Vec3.subVectors(new Vec3(), q1, p1);
  const d2 = Vec3.subVectors(new Vec3(), q2, p2);
  const r = Vec3.subVectors(new Vec3(), p1, p2);
  const A = d1.lengthSq(), E = d2.lengthSq(), F = d2.dot(r);
  const B = d1.dot(d2), C = d1.dot(r);
  const den = A * E - B * B;
  let s = den > 1e-9 ? Math.max(0, Math.min(1, (B * F - C * E) / den)) : 0;
  let t = E > 1e-9 ? Math.max(0, Math.min(1, (B * s + F) / E)) : 0;
  s = A > 1e-9 ? Math.max(0, Math.min(1, (B * t - C) / A)) : 0;
  const c1 = p1.clone().addScaled(d1, s);
  const c2 = p2.clone().addScaled(d2, t);
  const point = c1.add(c2).scale(0.5);
  return [{ point, normal, depth: pen }];
}

// ─── Raycasts ───
export function raycastBody(body, origin, dir, maxDist) {
  if (body.shape.kind === 'sphere') return raySphere(body, origin, dir, maxDist);
  return rayBox(body, origin, dir, maxDist);
}

function raySphere(body, origin, dir, maxDist) {
  const r = body.shape.radius;
  const oc = Vec3.subVectors(tmp1, origin, body.position);
  const b = oc.dot(dir);
  const c = oc.lengthSq() - r * r;
  const disc = b * b - c;
  if (disc < 0) return null;
  const t = -b - Math.sqrt(disc);
  if (t < 0 || t > maxDist) return null;
  const point = origin.clone().addScaled(dir, t);
  const normal = point.clone().sub(body.position).normalize();
  return { t, point, normal };
}

function rayBox(body, origin, dir, maxDist) {
  // transform ray to box local space
  const q = body.quat;
  const inv = { x: -q.x, y: -q.y, z: -q.z, w: q.w };
  const o = tmp1.copy(origin).sub(body.position).applyQuat(inv);
  const d = tmp2.copy(dir).applyQuat(inv);
  const s = body.shape;
  const h = [s.hx, s.hy, s.hz];
  const O = [o.x, o.y, o.z], D = [d.x, d.y, d.z];
  let tmin = 0, tmax = maxDist, nAxis = 0, nSign = 1;
  for (let i = 0; i < 3; i++) {
    if (Math.abs(D[i]) < 1e-9) {
      if (O[i] < -h[i] || O[i] > h[i]) return null;
    } else {
      let t1 = (-h[i] - O[i]) / D[i];
      let t2 = (h[i] - O[i]) / D[i];
      let sign = -1;
      if (t1 > t2) { const tt = t1; t1 = t2; t2 = tt; sign = 1; }
      if (t1 > tmin) { tmin = t1; nAxis = i; nSign = sign; }
      tmax = Math.min(tmax, t2);
      if (tmin > tmax) return null;
    }
  }
  if (tmin <= 0 || tmin > maxDist) return null;
  const localN = new Vec3();
  const comps = ['x', 'y', 'z'];
  localN[comps[nAxis]] = nSign;
  const normal = localN.applyQuat(body.quat);
  const point = origin.clone().addScaled(dir, tmin);
  return { t: tmin, point, normal };
}
