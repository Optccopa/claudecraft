#version 330 core

in vec2 vUv;
flat in uint vData;

uniform sampler2D uAtlas;

out vec4 fragColor;

const float kTilesPerRow = 8.0;

void main() {
    uint tile = vData & 0xFFu;
    uint flags = vData >> 8u;
    if ((flags & 1u) != 0u) {
        vec2 tileOrigin = vec2(float(tile % 8u), float(tile / 8u)) / kTilesPerRow;
        // Keep the texture's alpha so transparent HUD sprites (hearts, hunger,
        // cutout blocks) composite over the slot/scene instead of a black box.
        fragColor = texture(uAtlas, tileOrigin + vUv / kTilesPerRow);
    } else if ((flags & 4u) != 0u) {
        // Health/hunger/air pips, indexed by the low byte (see Hud::ColorIndex).
        vec3 c = vec3(1.0);
        if (tile == 0u) c = vec3(0.86, 0.11, 0.13);       // heart
        else if (tile == 1u) c = vec3(0.20, 0.05, 0.05);  // empty heart
        else if (tile == 2u) c = vec3(0.78, 0.45, 0.16);  // hunger
        else if (tile == 3u) c = vec3(0.18, 0.12, 0.06);  // empty hunger
        else c = vec3(0.40, 0.70, 1.0);                   // air bubble
        fragColor = vec4(c, 1.0);
    } else if ((flags & 2u) != 0u) {
        fragColor = vec4(0.0, 0.0, 0.0, 0.45);
    } else {
        fragColor = vec4(1.0, 1.0, 1.0, 0.9);
    }
}
