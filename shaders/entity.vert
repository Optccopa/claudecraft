#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uSunDir;

out vec2 vUv;
out float vLight;
out float vDist;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    gl_Position = uViewProj * world;
    // Boxes are axis-aligned; the model's rotation/scale keeps normals axis-
    // aligned, so a normalize after the linear part is enough (no inverse-T).
    vec3 n = normalize(mat3(uModel) * aNormal);
    float diffuse = max(dot(n, uSunDir), 0.0);
    // Directional shading only; day/night and cell brightness arrive via
    // uLightScale (like dropped items), so it isn't double-applied here.
    vLight = 0.55 + 0.45 * diffuse;
    vUv = aUv;
    vDist = gl_Position.w;
}
