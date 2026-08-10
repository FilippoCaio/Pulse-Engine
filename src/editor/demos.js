// ─── Demo scenes: each returns { scene, graphs } ───
import { Scene, Entity, makeGround, makeBox, makeSphere, makeCylinder, makeLight } from '../core/scene.js';
import { Graph } from '../blueprint/graph.js';

function jumpGraph() {
  const g = new Graph('Salta con Spazio');
  const ev = g.addNode('event.key', 40, 80);
  ev.props.key = 'Space';
  const imp = g.addNode('action.impulse', 320, 80);
  imp.props.in_impulso = [0, 6, 0];
  g.connect(ev.id, 'exec', imp.id, 'exec');
  return g;
}

export function demoDefault() {
  const scene = new Scene();
  scene.add(makeGround());

  const cube = makeBox('Cubo saltante');
  cube.position.set(0, 2, 0);
  scene.add(cube);

  const ball = makeSphere('Sfera');
  ball.position.set(2.2, 4, -1);
  scene.add(ball);

  const c2 = makeBox('Cubo pesante');
  c2.position.set(-2.2, 1.5, 1);
  c2.scale.set(1.4, 1.4, 1.4);
  c2.body.mass = 4;
  c2.mesh.color = [0.55, 0.4, 0.75];
  scene.add(c2);

  const light = makeLight('Luce calda');
  light.position.set(-3.5, 3.5, 2.5);
  scene.add(light);

  const g = jumpGraph();
  cube.script = { graphId: g.id };
  return { scene, graphs: [g] };
}

export function demoTower() {
  const scene = new Scene();
  scene.add(makeGround(30));

  // tower 4 wide × 7 tall
  const palette = [[0.85, 0.55, 0.25], [0.75, 0.35, 0.3], [0.8, 0.65, 0.35], [0.6, 0.55, 0.45]];
  for (let y = 0; y < 7; y++) {
    for (let x = 0; x < 4; x++) {
      const b = makeBox(`Mattone ${y * 4 + x + 1}`);
      b.position.set((x - 1.5) * 1.02 + (y % 2 ? 0.25 : 0), 0.5 + y * 1.01, 0);
      b.mesh.color = [...palette[(x + y) % palette.length]];
      b.body.friction = 0.6;
      b.body.restitution = 0.1;
      scene.add(b);
    }
  }

  const ball = makeSphere('Palla demolitrice');
  ball.position.set(0.4, 3, 13);
  ball.scale.set(1.7, 1.7, 1.7);
  ball.body.mass = 14;
  ball.mesh.color = [0.25, 0.28, 0.33];
  ball.mesh.shininess = 120;
  ball.mesh.specular = 0.9;
  scene.add(ball);

  const g = new Graph('Lancio demolitore');
  const ev = g.addNode('event.start', 40, 80);
  const vel = g.addNode('action.setvel', 320, 80);
  vel.props.in_velocita = [0, 3.5, -16];
  g.connect(ev.id, 'exec', vel.id, 'exec');
  ball.script = { graphId: g.id };

  const light = makeLight('Luce');
  light.position.set(6, 4, 6);
  scene.add(light);
  return { scene, graphs: [g] };
}

export function demoPendulums() {
  const scene = new Scene();
  scene.add(makeGround(20));
  const graphs = [];

  const n = 5, spacing = 1.02, y0 = 6.5, len = 3.4;
  for (let i = 0; i < n; i++) {
    const x = (i - (n - 1) / 2) * spacing;
    const anchor = new Entity(`Perno ${i + 1}`);
    anchor.position.set(x, y0, 0);
    anchor.scale.set(0.24, 0.24, 0.24);
    anchor.mesh = { shape: 'cube', color: [0.3, 0.32, 0.38], shininess: 60, specular: 0.5, checker: 0, emissive: 0 };
    anchor.body = { type: 'static', mass: 0, friction: 0.4, restitution: 0.5, linearDamping: 0.01, angularDamping: 0.05 };
    scene.add(anchor);

    const ball = makeSphere(`Pendolo ${i + 1}`);
    ball.mesh.color = [0.75, 0.78, 0.85];
    ball.mesh.shininess = 160;
    ball.mesh.specular = 1;
    ball.body.restitution = 0.93;
    ball.body.friction = 0.05;
    ball.body.linearDamping = 0.002;
    ball.body.mass = 2;
    if (i === 0) {
      // raised to the side, same rod length
      const off = len / Math.SQRT2;
      ball.position.set(x - off, y0 - off, 0);
    } else {
      ball.position.set(x, y0 - len, 0);
    }
    ball.joint = { targetId: anchor.id, mode: 'rod', length: len };
    scene.add(ball);
  }

  const light = makeLight('Luce');
  light.position.set(0, 5, 5);
  light.light.intensity = 2;
  scene.add(light);
  return { scene, graphs };
}

export function demoDomino() {
  const scene = new Scene();
  scene.add(makeGround(34));

  const count = 16;
  for (let i = 0; i < count; i++) {
    const d = makeBox(`Domino ${i + 1}`);
    d.scale.set(0.28, 1.7, 0.9);
    d.position.set(-7 + i * 1.05, 0.85, 0);
    d.mesh.color = i % 2 ? [0.9, 0.88, 0.82] : [0.85, 0.3, 0.3];
    d.body.friction = 0.45;
    d.body.restitution = 0.05;
    d.body.mass = 0.8;
    if (i === 0) d.rotation.fromEulerDeg(0, 0, -28); // push the first one over
    scene.add(d);
  }

  // curve at the end: a ball on a pedestal gets knocked off
  const pedestal = makeBox('Piedistallo');
  pedestal.position.set(10.2, 1, 0);
  pedestal.scale.set(0.8, 2, 0.8);
  pedestal.body.type = 'static';
  pedestal.mesh.color = [0.4, 0.42, 0.5];
  scene.add(pedestal);

  const ball = makeSphere('Gran finale');
  ball.position.set(10.2, 2.5, 0);
  ball.mesh.color = [0.95, 0.65, 0.2];
  ball.body.restitution = 0.7;
  scene.add(ball);

  const light = makeLight('Luce');
  light.position.set(0, 4, 4);
  scene.add(light);
  return { scene, graphs: [] };
}

export function demoCannon() {
  const scene = new Scene();
  scene.add(makeGround(30));
  const graphs = [];

  // cannon
  const base = makeBox('Base cannone');
  base.position.set(0, 0.5, -8);
  base.scale.set(1.6, 1, 1.6);
  base.body.type = 'static';
  base.mesh.color = [0.3, 0.32, 0.38];
  scene.add(base);

  const barrel = makeCylinder('Cannone');
  barrel.position.set(0, 1.6, -7.6);
  barrel.scale.set(0.7, 1.8, 0.7);
  barrel.rotation.fromEulerDeg(65, 0, 0);
  barrel.body.type = 'static';
  barrel.mesh.color = [0.2, 0.22, 0.26];
  barrel.mesh.shininess = 100;
  barrel.mesh.specular = 0.8;
  scene.add(barrel);

  const fire = new Graph('Cannone: Spara con Spazio');
  const ev = fire.addNode('event.key', 40, 60);
  ev.props.key = 'Space';
  const spawn = fire.addNode('action.spawnball', 320, 60);
  spawn.props.in_posizione = [0, 2.6, -6.8];
  spawn.props.in_velocita = [0, 4.5, 14];
  spawn.props.in_raggio = 0.38;
  fire.connect(ev.id, 'exec', spawn.id, 'exec');
  const prnt = fire.addNode('action.print', 620, 60);
  prnt.props.in_valore = '💥 Fuoco!';
  fire.connect(spawn.id, 'exec', prnt.id, 'exec');
  graphs.push(fire);
  barrel.script = { graphId: fire.id };

  // target wall
  const hit = new Graph('Bersaglio: colpito');
  const evc = hit.addNode('event.collision', 40, 60);
  const branch = hit.addNode('logic.branch', 300, 60);
  const cmp = hit.addNode('logic.compare', 40, 220);
  cmp.props.op = '>';
  cmp.props.in_b = 1.2;
  const col = hit.addNode('action.setcolor', 560, 60);
  col.props.in_rgb = [1, 0.15, 0.15];
  hit.connect(evc.id, 'exec', branch.id, 'exec');
  hit.connect(evc.id, 'impulso', cmp.id, 'a');
  hit.connect(cmp.id, 'r', branch.id, 'cond');
  hit.connect(branch.id, 'vero', col.id, 'exec');
  graphs.push(hit);

  for (let y = 0; y < 4; y++) {
    for (let x = 0; x < 5; x++) {
      const b = makeBox(`Bersaglio ${y * 5 + x + 1}`);
      b.position.set((x - 2) * 1.05, 0.5 + y * 1.02, 8);
      b.mesh.color = [0.35, 0.6, 0.85];
      b.body.friction = 0.5;
      b.script = { graphId: hit.id };
      scene.add(b);
    }
  }

  const light = makeLight('Luce');
  light.position.set(-5, 4, 0);
  scene.add(light);
  return { scene, graphs };
}

export const DEMOS = [
  ['Scena base (salto)', demoDefault],
  ['🏰 Torre da abbattere', demoTower],
  ['🕰 Pendoli vincolati', demoPendulums],
  ['🁢 Effetto domino', demoDomino],
  ['💣 Cannone (Blueprint)', demoCannon],
];
