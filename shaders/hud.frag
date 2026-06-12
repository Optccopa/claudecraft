#version 330 core

in vec2 vUv;
flat in uint vData;

uniform sampler2D uAtlas;

out vec4 fragColor;

const float kTilesPerRow = 4.0;

void main() {
    uint tile = vData & 0xFFu;
    uint flags = vData >> 8u;
    if ((flags & 1u) != 0u) {
        vec2 tileOrigin = vec2(float(tile % 4u), float(tile / 4u)) / kTilesPerRow;
        fragColor = vec4(texture(uAtlas, tileOrigin + vUv / kTilesPerRow).rgb, 1.0);
    } else if ((flags & 2u) != 0u) {
        fragColor = vec4(0.0, 0.0, 0.0, 0.45);
    } else {
        fragColor = vec4(1.0, 1.0, 1.0, 0.9);
    }
}
