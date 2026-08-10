// ─── Procedural mesh generation: interleaved [px,py,pz,nx,ny,nz] + indices ───

export function cubeGeometry() {
  const p = [], n = [], idx = [];
  const faces = [
    { n: [1, 0, 0], u: [0, 1, 0], v: [0, 0, 1] },
    { n: [-1, 0, 0], u: [0, 0, 1], v: [0, 1, 0] },
    { n: [0, 1, 0], u: [0, 0, 1], v: [1, 0, 0] },
    { n: [0, -1, 0], u: [1, 0, 0], v: [0, 0, 1] },
    { n: [0, 0, 1], u: [1, 0, 0], v: [0, 1, 0] },
    { n: [0, 0, -1], u: [0, 1, 0], v: [1, 0, 0] },
  ];
  for (const f of faces) {
    const base = p.length / 3;
    for (const [su, sv] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
      p.push(
        (f.n[0] + f.u[0] * su + f.v[0] * sv) * 0.5,
        (f.n[1] + f.u[1] * su + f.v[1] * sv) * 0.5,
        (f.n[2] + f.u[2] * su + f.v[2] * sv) * 0.5,
      );
      n.push(...f.n);
    }
    idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  return interleave(p, n, idx);
}

export function sphereGeometry(widthSeg = 28, heightSeg = 18) {
  const p = [], n = [], idx = [];
  for (let y = 0; y <= heightSeg; y++) {
    const v = y / heightSeg;
    const phi = v * Math.PI;
    for (let x = 0; x <= widthSeg; x++) {
      const u = x / widthSeg;
      const theta = u * Math.PI * 2;
      const nx = Math.sin(phi) * Math.cos(theta);
      const ny = Math.cos(phi);
      const nz = Math.sin(phi) * Math.sin(theta);
      p.push(nx * 0.5, ny * 0.5, nz * 0.5);
      n.push(nx, ny, nz);
    }
  }
  const row = widthSeg + 1;
  for (let y = 0; y < heightSeg; y++) {
    for (let x = 0; x < widthSeg; x++) {
      const a = y * row + x, b = a + row;
      idx.push(a, b, a + 1, a + 1, b, b + 1);
    }
  }
  return interleave(p, n, idx);
}

export function cylinderGeometry(radialSeg = 24) {
  const p = [], n = [], idx = [];
  // side
  for (let i = 0; i <= radialSeg; i++) {
    const t = (i / radialSeg) * Math.PI * 2;
    const c = Math.cos(t), s = Math.sin(t);
    p.push(c * 0.5, 0.5, s * 0.5); n.push(c, 0, s);
    p.push(c * 0.5, -0.5, s * 0.5); n.push(c, 0, s);
  }
  for (let i = 0; i < radialSeg; i++) {
    const a = i * 2;
    idx.push(a, a + 1, a + 2, a + 2, a + 1, a + 3);
  }
  // caps
  for (const sign of [1, -1]) {
    const center = p.length / 3;
    p.push(0, 0.5 * sign, 0); n.push(0, sign, 0);
    for (let i = 0; i <= radialSeg; i++) {
      const t = (i / radialSeg) * Math.PI * 2;
      p.push(Math.cos(t) * 0.5, 0.5 * sign, Math.sin(t) * 0.5);
      n.push(0, sign, 0);
    }
    for (let i = 0; i < radialSeg; i++) {
      if (sign > 0) idx.push(center, center + 2 + i, center + 1 + i);
      else idx.push(center, center + 1 + i, center + 2 + i);
    }
  }
  return interleave(p, n, idx);
}

export function coneGeometry(radialSeg = 16) {
  const p = [], n = [], idx = [];
  // slanted side normals
  for (let i = 0; i <= radialSeg; i++) {
    const t = (i / radialSeg) * Math.PI * 2;
    const c = Math.cos(t), s = Math.sin(t);
    const nl = Math.sqrt(1 + 0.25);
    p.push(c * 0.5, -0.5, s * 0.5); n.push(c / nl, 0.5 / nl, s / nl);
    p.push(0, 0.5, 0); n.push(c / nl, 0.5 / nl, s / nl);
  }
  for (let i = 0; i < radialSeg; i++) {
    const a = i * 2;
    idx.push(a, a + 1, a + 2);
  }
  // base cap
  const center = p.length / 3;
  p.push(0, -0.5, 0); n.push(0, -1, 0);
  for (let i = 0; i <= radialSeg; i++) {
    const t = (i / radialSeg) * Math.PI * 2;
    p.push(Math.cos(t) * 0.5, -0.5, Math.sin(t) * 0.5);
    n.push(0, -1, 0);
  }
  for (let i = 0; i < radialSeg; i++) {
    idx.push(center, center + 1 + i, center + 2 + i);
  }
  return interleave(p, n, idx);
}

function interleave(p, n, idx) {
  const count = p.length / 3;
  const data = new Float32Array(count * 6);
  for (let i = 0; i < count; i++) {
    data[i * 6] = p[i * 3];
    data[i * 6 + 1] = p[i * 3 + 1];
    data[i * 6 + 2] = p[i * 3 + 2];
    data[i * 6 + 3] = n[i * 3];
    data[i * 6 + 4] = n[i * 3 + 1];
    data[i * 6 + 5] = n[i * 3 + 2];
  }
  return { data, indices: new (count > 65535 ? Uint32Array : Uint16Array)(idx) };
}
