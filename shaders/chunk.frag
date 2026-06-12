#version 330 core

in vec2 vUv;
flat in uint vTile;
in float vLight;
in float vDist;

uniform sampler2D uAtlas;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uFogColor;
uniform float uAlpha;

out vec4 fragColor;

const float kTilesPerRow = 8.0;

void main() {
    // Greedy quads span multiple blocks, so vUv runs 0..width/0..height.
    // fract() tiles the texture per block; nearest filtering without mips
    // keeps the wrap seam and atlas borders artifact-free.
    vec2 tileOrigin = vec2(float(vTile % 8u), float(vTile / 8u)) / kTilesPerRow;
    vec2 uv = tileOrigin + fract(vUv) / kTilesPerRow;
    vec3 albedo = texture(uAtlas, uv).rgb;
    float fog = smoothstep(uFogStart, uFogEnd, vDist);
    fragColor = vec4(mix(albedo * vLight, uFogColor, fog), uAlpha);
}
