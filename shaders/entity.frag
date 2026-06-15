#version 330 core

in vec2 vUv;
in float vLight;
in float vDist;

uniform int uTextured;     // 1: sample uTex; 0: use uColor (flat fallback)
uniform sampler2D uTex;
uniform vec3 uColor;
uniform float uLightScale; // mob's sampled cell light, 0..1
uniform float uHurt;       // red damage-flash, 0..1
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

out vec4 FragColor;

void main() {
    vec3 albedo;
    if (uTextured == 1) {
        vec4 t = texture(uTex, vUv);
        if (t.a < 0.5) {
            discard; // transparent skin/overlay texels (e.g. sheep wool gaps)
        }
        albedo = t.rgb;
    } else {
        albedo = uColor;
    }
    albedo = mix(albedo, vec3(1.0, 0.2, 0.2), 0.6 * uHurt);
    vec3 c = albedo * vLight * uLightScale;
    float fog = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    FragColor = vec4(mix(c, uFogColor, fog), 1.0);
}
