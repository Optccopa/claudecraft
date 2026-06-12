#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
// tile | ao << 8 | normalIndex << 10 | skyLight << 13 | blockLight << 17
layout(location = 2) in uint aData;

uniform mat4 uViewProj;
uniform vec3 uChunkOrigin;
uniform vec3 uCameraPos;
uniform float uSkyLight; // day/night scale applied to the sky channel

out vec2 vUv;
flat out uint vTile;
out float vLight;
out float vDist;

const vec3 kNormals[6] = vec3[6](
    vec3(1, 0, 0), vec3(-1, 0, 0),
    vec3(0, 1, 0), vec3(0, -1, 0),
    vec3(0, 0, 1), vec3(0, 0, -1));

// Baked AO curve: darker steps are non-linear so single-occluder corners stay subtle.
const float kAoCurve[4] = float[4](0.45, 0.65, 0.85, 1.0);

const vec3 kSunDir = normalize(vec3(0.5, 1.0, 0.3));

void main() {
    vec3 worldPos = uChunkOrigin + aPos;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vUv = aUv;
    vTile = aData & 0xFFu;
    float ao = kAoCurve[(aData >> 8u) & 0x3u];
    vec3 normal = kNormals[(aData >> 10u) & 0x7u];
    float sky = float((aData >> 13u) & 0xFu) / 15.0;
    float blockLight = float((aData >> 17u) & 0xFu) / 15.0;

    // Sun shading only applies to sky-lit surfaces: caves stay flat-dark and
    // glowstone-lit faces ignore the sun direction entirely.
    float diffuse = max(dot(normal, kSunDir), 0.0);
    float skyContrib = sky * uSkyLight * (0.45 + 0.55 * diffuse);
    float light = max(max(skyContrib, blockLight), 0.03);
    vLight = light * ao;
    vDist = distance(worldPos, uCameraPos);
}
