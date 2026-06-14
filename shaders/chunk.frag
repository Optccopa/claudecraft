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
    vec4 texel = texture(uAtlas, uv);
    // Cutout for cross-plant billboards (tall grass): their tile is transparent
    // outside the blades. Opaque cubes ship alpha 1, so this never clips them.
    if (texel.a < 0.5) {
        discard;
    }
    float fog = smoothstep(uFogStart, uFogEnd, vDist);
    fragColor = vec4(mix(texel.rgb * vLight, uFogColor, fog), uAlpha);
}
