#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 params; // x = sunIntensity
} pc;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out float fragSunIntensity;

void main() {
    gl_Position     = pc.mvp * vec4(inPos, 1.0);
    fragNormal      = inNormal;
    fragSunIntensity = pc.params.x;
}
