#version 330 core

in vec2 vWorldXZ;
in float vDist;

uniform float uTime;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uOpacity;

out vec4 FragColor;

// Blocky Minecraft-style cloud layer: coverage is a hash over coarse cells, two
// octaves so puffs clump. The whole field drifts on the wind (uTime), and far
// cells fade into the fog/sky so the finite quad has no visible edge. An integer
// hash is used because sin-based hashes band badly at the large world
// coordinates this plane spans.
float hash(vec2 p) {
    ivec2 i = ivec2(floor(p));
    uint h = uint(i.x) * 374761393u + uint(i.y) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return float(h & 0xFFFFu) / 65535.0;
}

void main() {
    vec2 drift = (vWorldXZ + vec2(uTime * 0.6, 0.0)) / 14.0;
    // Two octaves: a coarse field gates whole banks, a finer one breaks their
    // edges into separate puffs.
    float coverage = hash(floor(drift)) * 0.55 + hash(floor(drift * 0.5) + 17.0) * 0.45;
    if (coverage < 0.62) {
        discard;
    }
    float fog = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    vec3 col = mix(vec3(0.88), vec3(1.0), coverage);
    FragColor = vec4(mix(col, uFogColor, fog * 0.85), uOpacity * (1.0 - fog));
}
