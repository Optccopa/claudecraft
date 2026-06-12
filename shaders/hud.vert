#version 330 core

layout(location = 0) in vec2 aPos; // pixels, origin bottom-left
layout(location = 1) in vec2 aUv;  // 0..1 within the tile when textured
layout(location = 2) in uint aData; // tile | flags << 8 (bit 0: textured, bit 1: dim)

uniform vec2 uScreenSize;

out vec2 vUv;
flat out uint vData;

void main() {
    gl_Position = vec4(aPos / uScreenSize * 2.0 - 1.0, 0.0, 1.0);
    vUv = aUv;
    vData = aData;
}
