// ─── WebGL2 forward renderer: shadow pass + lit pass + grid + sky + lines ───
import { Mat4, Vec3 } from '../core/math.js';
import * as SH from './shaders.js';
import { cubeGeometry, sphereGeometry, cylinderGeometry, coneGeometry } from './primitives.js';

const SHADOW_SIZE = 2048;

function compile(gl, vsSrc, fsSrc) {
  const make = (type, src) => {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      throw new Error('Shader error: ' + gl.getShaderInfoLog(s) + '\n' + src);
    }
    return s;
  };
  const prog = gl.createProgram();
  gl.attachShader(prog, make(gl.VERTEX_SHADER, vsSrc));
  gl.attachShader(prog, make(gl.FRAGMENT_SHADER, fsSrc));
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
    throw new Error('Link error: ' + gl.getProgramInfoLog(prog));
  }
  const uniforms = {};
  const count = gl.getProgramParameter(prog, gl.ACTIVE_UNIFORMS);
  for (let i = 0; i < count; i++) {
    const info = gl.getActiveUniform(prog, i);
    const name = info.name.replace(/\[0\]$/, '');
    uniforms[name] = gl.getUniformLocation(prog, info.name);
  }
  return { prog, u: uniforms };
}

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    const gl = canvas.getContext('webgl2', { antialias: true, alpha: false });
    if (!gl) throw new Error('WebGL2 non supportato da questo browser');
    this.gl = gl;

    this.litProg = compile(gl, SH.LIT_VS, SH.LIT_FS);
    this.depthProg = compile(gl, SH.DEPTH_VS, SH.DEPTH_FS);
    this.skyProg = compile(gl, SH.SKY_VS, SH.SKY_FS);
    this.gridProg = compile(gl, SH.GRID_VS, SH.GRID_FS);
    this.lineProg = compile(gl, SH.LINE_VS, SH.LINE_FS);

    this.meshes = {
      cube: this._createMesh(cubeGeometry()),
      sphere: this._createMesh(sphereGeometry()),
      cylinder: this._createMesh(cylinderGeometry()),
      cone: this._createMesh(coneGeometry()),
    };

    // fullscreen triangle (sky) & grid quad
    this.screenVao = this._createVao2D(new Float32Array([-1, -1, 3, -1, -1, 3]));
    this.gridVao = this._createVao2D(new Float32Array([-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1]));

    // dynamic line buffer
    this.lineVao = gl.createVertexArray();
    this.lineVbo = gl.createBuffer();
    gl.bindVertexArray(this.lineVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);
    gl.bindVertexArray(null);

    // shadow map
    this.shadowTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.shadowTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_FUNC, gl.LEQUAL);
    this.shadowFbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.shadowFbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, this.shadowTex, 0);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    this.lightMatrix = Mat4.create();
    this._lightView = Mat4.create();
    this._lightProj = Mat4.create();
    this._normalMat = new Float32Array(9);
    this.drawCalls = 0;

    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.CULL_FACE);
  }

  _createMesh({ data, indices }) {
    const gl = this.gl;
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);
    const ibo = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);
    gl.bindVertexArray(null);
    return { vao, count: indices.length, type: indices.BYTES_PER_ELEMENT === 2 ? gl.UNSIGNED_SHORT : gl.UNSIGNED_INT };
  }

  _createVao2D(verts) {
    const gl = this.gl;
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    return { vao, count: verts.length / 2 };
  }

  resize() {
    const c = this.canvas;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.floor(c.clientWidth * dpr), h = Math.floor(c.clientHeight * dpr);
    if (c.width !== w || c.height !== h) {
      c.width = w;
      c.height = h;
    }
    return c.clientHeight > 0;
  }

  /**
   * frame = {
   *   items: [{ mesh, model(Float32Array16), color[3], shininess, specular, checker, emissive, castShadow }],
   *   env: { sunDir:Vec3, sunColor[3], ambientSky[3], ambientGround[3], fogColor[3], fogDensity,
   *          horizon[3], zenith[3], shadowStrength },
   *   lights: [{ pos:Vec3, color[3], range }],
   *   lines: [{ verts:Float32Array (x,y,z,r,g,b)*, depthTest, alpha }],
   *   shadowCenter: Vec3
   * }
   */
  render(frame, camera) {
    const gl = this.gl;
    this.drawCalls = 0;
    const env = frame.env;

    // ── shadow pass ──
    const sc = frame.shadowCenter;
    const ext = 26;
    const eye = new Vec3().copy(env.sunDir).scale(60).add(sc);
    Mat4.lookAt(this._lightView, eye, sc, Math.abs(env.sunDir.y) > 0.98 ? new Vec3(1, 0, 0) : new Vec3(0, 1, 0));
    Mat4.ortho(this._lightProj, -ext, ext, -ext, ext, 1, 140);
    Mat4.multiply(this.lightMatrix, this._lightProj, this._lightView);

    gl.bindFramebuffer(gl.FRAMEBUFFER, this.shadowFbo);
    gl.viewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    gl.clear(gl.DEPTH_BUFFER_BIT);
    gl.useProgram(this.depthProg.prog);
    gl.uniformMatrix4fv(this.depthProg.u.uLightMatrix, false, this.lightMatrix);
    gl.cullFace(gl.FRONT); // reduce peter-panning
    for (const item of frame.items) {
      if (item.castShadow === false) continue;
      const mesh = this.meshes[item.mesh];
      if (!mesh) continue;
      gl.uniformMatrix4fv(this.depthProg.u.uModel, false, item.model);
      gl.bindVertexArray(mesh.vao);
      gl.drawElements(gl.TRIANGLES, mesh.count, mesh.type, 0);
      this.drawCalls++;
    }
    gl.cullFace(gl.BACK);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    // ── main pass ──
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    // sky
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(this.skyProg.prog);
    gl.uniformMatrix4fv(this.skyProg.u.uInvProjView, false, camera.invProjView);
    gl.uniform3fv(this.skyProg.u.uHorizon, env.horizon);
    gl.uniform3fv(this.skyProg.u.uZenith, env.zenith);
    gl.uniform3fv(this.skyProg.u.uGroundCol, env.fogColor);
    gl.uniform3f(this.skyProg.u.uSunDir, env.sunDir.x, env.sunDir.y, env.sunDir.z);
    gl.bindVertexArray(this.screenVao.vao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.enable(gl.DEPTH_TEST);
    this.drawCalls++;

    // lit objects
    const P = this.litProg;
    gl.useProgram(P.prog);
    gl.uniformMatrix4fv(P.u.uProj, false, camera.proj);
    gl.uniformMatrix4fv(P.u.uView, false, camera.view);
    gl.uniform3f(P.u.uCamPos, camera.eye.x, camera.eye.y, camera.eye.z);
    gl.uniform3f(P.u.uSunDir, env.sunDir.x, env.sunDir.y, env.sunDir.z);
    gl.uniform3fv(P.u.uSunColor, env.sunColor);
    gl.uniform3fv(P.u.uAmbientSky, env.ambientSky);
    gl.uniform3fv(P.u.uAmbientGround, env.ambientGround);
    gl.uniform3fv(P.u.uFogColor, env.fogColor);
    gl.uniform1f(P.u.uFogDensity, env.fogDensity);
    gl.uniform1f(P.u.uShadowStrength, env.shadowStrength);
    gl.uniformMatrix4fv(P.u.uShadowMatrix, false, this.lightMatrix);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.shadowTex);
    gl.uniform1i(P.u.uShadowMap, 0);

    const lightCount = Math.min(frame.lights.length, 8);
    gl.uniform1i(P.u.uLightCount, lightCount);
    if (lightCount) {
      const pos = new Float32Array(24), col = new Float32Array(24), rng = new Float32Array(8);
      for (let i = 0; i < lightCount; i++) {
        const L = frame.lights[i];
        pos[i * 3] = L.pos.x; pos[i * 3 + 1] = L.pos.y; pos[i * 3 + 2] = L.pos.z;
        col[i * 3] = L.color[0]; col[i * 3 + 1] = L.color[1]; col[i * 3 + 2] = L.color[2];
        rng[i] = L.range;
      }
      gl.uniform3fv(P.u.uLightPos, pos.subarray(0, lightCount * 3));
      gl.uniform3fv(P.u.uLightColor, col.subarray(0, lightCount * 3));
      gl.uniform1fv(P.u.uLightRange, rng.subarray(0, lightCount));
    }

    for (const item of frame.items) {
      const mesh = this.meshes[item.mesh];
      if (!mesh) continue;
      gl.uniformMatrix4fv(P.u.uModel, false, item.model);
      Mat4.normalMat3(this._normalMat, item.model);
      gl.uniformMatrix3fv(P.u.uNormalMat, false, this._normalMat);
      gl.uniform3fv(P.u.uColor, item.color);
      gl.uniform1f(P.u.uShininess, item.shininess ?? 48);
      gl.uniform1f(P.u.uSpecular, item.specular ?? 0.35);
      gl.uniform1f(P.u.uChecker, item.checker ?? 0);
      gl.uniform1f(P.u.uEmissive, item.emissive ?? 0);
      gl.bindVertexArray(mesh.vao);
      gl.drawElements(gl.TRIANGLES, mesh.count, mesh.type, 0);
      this.drawCalls++;
    }

    // grid
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(false);
    gl.useProgram(this.gridProg.prog);
    gl.uniformMatrix4fv(this.gridProg.u.uProj, false, camera.proj);
    gl.uniformMatrix4fv(this.gridProg.u.uView, false, camera.view);
    gl.uniform3f(this.gridProg.u.uCamPos, camera.eye.x, camera.eye.y, camera.eye.z);
    gl.disable(gl.CULL_FACE);
    gl.bindVertexArray(this.gridVao.vao);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
    gl.enable(gl.CULL_FACE);
    this.drawCalls++;

    // overlay lines
    if (frame.lines.length) {
      gl.useProgram(this.lineProg.prog);
      gl.uniformMatrix4fv(this.lineProg.u.uProjView, false, camera.projView);
      gl.bindVertexArray(this.lineVao);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
      for (const batch of frame.lines) {
        if (!batch.verts.length) continue;
        if (batch.depthTest === false) gl.disable(gl.DEPTH_TEST);
        gl.uniform1f(this.lineProg.u.uAlpha, batch.alpha ?? 1);
        gl.bufferData(gl.ARRAY_BUFFER, batch.verts, gl.DYNAMIC_DRAW);
        gl.drawArrays(gl.LINES, 0, batch.verts.length / 6);
        if (batch.depthTest === false) gl.enable(gl.DEPTH_TEST);
        this.drawCalls++;
      }
    }

    gl.depthMask(true);
    gl.disable(gl.BLEND);

    // overlay items (gizmo): always on top, lit program with emissive
    if (frame.overlay && frame.overlay.length) {
      gl.clear(gl.DEPTH_BUFFER_BIT);
      gl.useProgram(P.prog);
      for (const item of frame.overlay) {
        const mesh = this.meshes[item.mesh];
        if (!mesh) continue;
        gl.uniformMatrix4fv(P.u.uModel, false, item.model);
        Mat4.normalMat3(this._normalMat, item.model);
        gl.uniformMatrix3fv(P.u.uNormalMat, false, this._normalMat);
        gl.uniform3fv(P.u.uColor, item.color);
        gl.uniform1f(P.u.uShininess, 10);
        gl.uniform1f(P.u.uSpecular, 0);
        gl.uniform1f(P.u.uChecker, 0);
        gl.uniform1f(P.u.uEmissive, item.emissive ?? 2.5);
        gl.bindVertexArray(mesh.vao);
        gl.drawElements(gl.TRIANGLES, mesh.count, mesh.type, 0);
        this.drawCalls++;
      }
    }
    gl.bindVertexArray(null);
  }
}
