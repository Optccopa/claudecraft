#version 330 core

in vec2 vUv;

uniform sampler2D uScene;
uniform float uTime;

out vec4 fragColor;

// Underwater look: the scene is sampled through an animated ripple (warp) and
// tinted toward a murky blue (cloudy). Only ever bound when the camera is
// submerged, so there is no non-underwater branch.
void main() {
    vec2 uv = vUv;
    uv.x += sin(uv.y * 18.0 + uTime * 2.1) * 0.005;
    uv.y += cos(uv.x * 16.0 + uTime * 1.7) * 0.005;

    vec3 scene = texture(uScene, clamp(uv, 0.0, 1.0)).rgb;
    const vec3 murk = vec3(0.10, 0.26, 0.48);
    vec3 tinted = mix(scene, murk, 0.4) * vec3(0.7, 0.85, 1.0);
    fragColor = vec4(tinted, 1.0);
}
