#version 450

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in float inNoise;

// Per-instance
layout(location = 4) in vec3  instPos;
layout(location = 5) in float instYaw;
layout(location = 6) in float instScale;
layout(location = 7) in float instSeed;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params; // x = windTime
} pc;

layout(location = 0) out vec3  fragPos;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec2  fragUV;
layout(location = 3) out float fragNoise;

void main() {
    // Rotate around Y by instYaw
    float cy = cos(instYaw), sy = sin(instYaw);
    mat3 rot = mat3(
         cy, 0.0,  sy,
        0.0, 1.0, 0.0,
        -sy, 0.0,  cy
    );

    vec3 localPos    = rot * (inPos * instScale);
    vec3 worldPos    = instPos + localPos;
    vec3 worldNormal = rot * inNormal;

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    fragPos     = worldPos;
    fragNormal  = worldNormal;
    fragUV      = inUV;
    fragNoise   = inNoise;
}
