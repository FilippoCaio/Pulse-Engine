// ─── Orbit camera for the editor viewport ───
import { Vec3, Mat4, clamp, DEG2RAD } from '../core/math.js';

export class OrbitCamera {
  constructor() {
    this.target = new Vec3(0, 1, 0);
    this.yaw = -0.7;      // radians around Y
    this.pitch = 0.42;    // radians above horizon
    this.distance = 14;
    this.fov = 55 * DEG2RAD;
    this.near = 0.1;
    this.far = 500;
    this.eye = new Vec3();
    this.view = Mat4.create();
    this.proj = Mat4.create();
    this.projView = Mat4.create();
    this.invProjView = Mat4.create();
    this._up = new Vec3(0, 1, 0);
  }

  update(aspect) {
    this.pitch = clamp(this.pitch, -1.55, 1.55);
    this.distance = clamp(this.distance, 1.2, 220);
    const cp = Math.cos(this.pitch);
    this.eye.set(
      this.target.x + Math.cos(this.yaw) * cp * this.distance,
      this.target.y + Math.sin(this.pitch) * this.distance,
      this.target.z + Math.sin(this.yaw) * cp * this.distance,
    );
    Mat4.lookAt(this.view, this.eye, this.target, this._up);
    Mat4.perspective(this.proj, this.fov, aspect, this.near, this.far);
    Mat4.multiply(this.projView, this.proj, this.view);
    Mat4.invert(this.invProjView, this.projView);
  }

  orbit(dx, dy) {
    this.yaw += dx * 0.006;
    this.pitch += dy * 0.006;
  }

  pan(dx, dy) {
    const s = this.distance * 0.0016;
    // camera right and up in world space (rows of view matrix)
    const v = this.view;
    this.target.x += (-v[0] * dx + v[1] * dy) * s;
    this.target.y += (-v[4] * dx + v[5] * dy) * s;
    this.target.z += (-v[8] * dx + v[9] * dy) * s;
  }

  zoom(delta) {
    this.distance *= Math.pow(1.0012, delta);
  }

  // NDC (-1..1) → world-space ray
  screenRay(ndcX, ndcY) {
    const near = new Vec3(ndcX, ndcY, -1).applyMat4(this.invProjView);
    const far = new Vec3(ndcX, ndcY, 1).applyMat4(this.invProjView);
    const dir = far.sub(near).normalize();
    return { origin: this.eye.clone(), dir };
  }

  focusOn(point, radius = 3) {
    this.target.copy(point);
    this.distance = Math.max(radius * 3.2, 3);
  }
}
