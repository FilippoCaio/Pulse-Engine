// ─── GLSL ES 3.00 shader sources ───

export const LIT_VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
uniform mat4 uProj, uView, uModel;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
void main() {
  vec4 wp = uModel * vec4(aPosition, 1.0);
  vWorldPos = wp.xyz;
  vNormal = uNormalMat * aNormal;
  gl_Position = uProj * uView * wp;
}`;

export const LIT_FS = `#version 300 es
precision highp float;
precision highp sampler2DShadow;
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 fragColor;

uniform vec3 uColor;
uniform float uShininess;
uniform float uSpecular;
uniform float uChecker;       // 0 = off, else checker cell size
uniform vec3 uCamPos;
uniform vec3 uSunDir;         // direction TOWARD the sun
uniform vec3 uSunColor;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uEmissive;

uniform int uLightCount;
uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform float uLightRange[8];

uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowMatrix;
uniform float uShadowStrength;

float shadowFactor(vec3 N) {
  vec4 sc = uShadowMatrix * vec4(vWorldPos, 1.0);
  vec3 p = sc.xyz / sc.w * 0.5 + 0.5;
  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
  float bias = max(0.0022 * (1.0 - dot(N, uSunDir)), 0.0006);
  float ref = p.z - bias;
  float texel = 1.0 / 2048.0;
  float sum = 0.0;
  for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
      sum += texture(uShadowMap, vec3(p.xy + vec2(x, y) * texel, ref));
  float lit = sum / 9.0;
  return mix(1.0, lit, uShadowStrength);
}

vec3 aces(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
  vec3 N = normalize(vNormal);
  vec3 V = normalize(uCamPos - vWorldPos);
  vec3 base = uColor;

  if (uChecker > 0.0) {
    vec2 cell = floor(vWorldPos.xz / uChecker);
    float chk = mod(cell.x + cell.y, 2.0);
    base *= mix(0.82, 1.0, chk);
  }

  // hemispheric ambient
  float hemi = N.y * 0.5 + 0.5;
  vec3 color = base * mix(uAmbientGround, uAmbientSky, hemi);

  // sun with shadows
  float sh = shadowFactor(N);
  float ndl = max(dot(N, uSunDir), 0.0);
  vec3 H = normalize(uSunDir + V);
  float spec = pow(max(dot(N, H), 0.0), uShininess) * uSpecular;
  color += (base * ndl + vec3(spec)) * uSunColor * sh;

  // point lights
  for (int i = 0; i < 8; i++) {
    if (i >= uLightCount) break;
    vec3 L = uLightPos[i] - vWorldPos;
    float dist = length(L);
    L /= max(dist, 1e-4);
    float x = clamp(1.0 - pow(dist / uLightRange[i], 2.0), 0.0, 1.0);
    float att = x * x;
    float nl = max(dot(N, L), 0.0);
    vec3 Hp = normalize(L + V);
    float sp = pow(max(dot(N, Hp), 0.0), uShininess) * uSpecular;
    color += (base * nl + vec3(sp)) * uLightColor[i] * att;
  }

  color += base * uEmissive;

  // fog
  float fd = length(uCamPos - vWorldPos);
  float fog = 1.0 - exp(-uFogDensity * uFogDensity * fd * fd);
  color = mix(color, uFogColor, clamp(fog, 0.0, 1.0));

  color = aces(color);
  color = pow(color, vec3(1.0 / 2.2));
  fragColor = vec4(color, 1.0);
}`;

export const DEPTH_VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
uniform mat4 uLightMatrix, uModel;
void main() { gl_Position = uLightMatrix * uModel * vec4(aPosition, 1.0); }`;

export const DEPTH_FS = `#version 300 es
precision highp float;
void main() {}`;

export const SKY_VS = `#version 300 es
precision highp float;
layout(location=0) in vec2 aPosition;
uniform mat4 uInvProjView;
out vec3 vDir;
void main() {
  gl_Position = vec4(aPosition, 0.9999, 1.0);
  vec4 near = uInvProjView * vec4(aPosition, -1.0, 1.0);
  vec4 far = uInvProjView * vec4(aPosition, 1.0, 1.0);
  vDir = far.xyz / far.w - near.xyz / near.w;
}`;

export const SKY_FS = `#version 300 es
precision highp float;
in vec3 vDir;
out vec4 fragColor;
uniform vec3 uHorizon, uZenith, uGroundCol;
uniform vec3 uSunDir;
void main() {
  vec3 d = normalize(vDir);
  vec3 sky;
  if (d.y >= 0.0) {
    sky = mix(uHorizon, uZenith, pow(d.y, 0.5));
    float sun = pow(max(dot(d, uSunDir), 0.0), 800.0);
    sky += vec3(1.0, 0.93, 0.8) * sun * 1.2;
    float glow = pow(max(dot(d, uSunDir), 0.0), 8.0);
    sky += vec3(0.35, 0.28, 0.18) * glow * 0.25;
  } else {
    sky = mix(uHorizon, uGroundCol, min(1.0, -d.y * 3.0));
  }
  sky = pow(sky, vec3(1.0 / 2.2));
  fragColor = vec4(sky, 1.0);
}`;

export const GRID_VS = `#version 300 es
precision highp float;
layout(location=0) in vec2 aPosition;
uniform mat4 uProj, uView;
uniform vec3 uCamPos;
out vec2 vXZ;
void main() {
  vec3 wp = vec3(aPosition.x * 300.0 + uCamPos.x, 0.0, aPosition.y * 300.0 + uCamPos.z);
  vXZ = wp.xz;
  gl_Position = uProj * uView * vec4(wp, 1.0);
}`;

export const GRID_FS = `#version 300 es
precision highp float;
in vec2 vXZ;
out vec4 fragColor;
uniform vec3 uCamPos;

float gridLine(vec2 p, float scale) {
  vec2 g = abs(fract(p / scale - 0.5) - 0.5) / fwidth(p / scale);
  return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
  float g1 = gridLine(vXZ, 1.0) * 0.35;
  float g10 = gridLine(vXZ, 10.0) * 0.6;
  float axisX = (1.0 - min(abs(vXZ.y) / fwidth(vXZ.y), 1.0));
  float axisZ = (1.0 - min(abs(vXZ.x) / fwidth(vXZ.x), 1.0));
  float dist = length(vXZ - uCamPos.xz);
  float fade = 1.0 - smoothstep(20.0, 120.0, dist);
  vec3 col = vec3(0.42, 0.45, 0.5);
  float a = max(g1, g10);
  if (axisX > 0.0) { col = vec3(0.9, 0.3, 0.32); a = max(a, axisX * 0.9); }
  if (axisZ > 0.0) { col = vec3(0.3, 0.5, 0.95); a = max(a, axisZ * 0.9); }
  fragColor = vec4(col, a * fade * 0.85);
  if (fragColor.a < 0.003) discard;
}`;

export const LINE_VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aColor;
uniform mat4 uProjView;
out vec3 vColor;
void main() {
  vColor = aColor;
  gl_Position = uProjView * vec4(aPosition, 1.0);
}`;

export const LINE_FS = `#version 300 es
precision highp float;
in vec3 vColor;
out vec4 fragColor;
uniform float uAlpha;
void main() { fragColor = vec4(vColor, uAlpha); }`;
