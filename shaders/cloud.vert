#version 330 core

layout(location = 0) in vec2 aXZ; // local quad spanning the cloud plane

uniform mat4 uViewProj;
uniform vec3 uOrigin; // camera x/z and the fixed cloud height

out vec2 vWorldXZ;
out float vDist;

void main() {
    vec3 world = vec3(aXZ.x + uOrigin.x, uOrigin.y, aXZ.y + uOrigin.z);
    gl_Position = uViewProj * vec4(world, 1.0);
    vWorldXZ = world.xz;
    vDist = gl_Position.w;
}
