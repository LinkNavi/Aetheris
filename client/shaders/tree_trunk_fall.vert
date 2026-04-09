#version 450

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in float inNoise;

layout(push_constant) uniform PC {
    mat4  viewProj;
    mat4  model;
    vec4  params;
    vec4  camPos;
    vec4  sunDir;
    float yaw;      // ← add
    float pad[3];
} pc;

layout(location = 0) out vec3  fragPos;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec2  fragUV;
layout(location = 3) out float fragNoise;

void main() {
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position = pc.viewProj * worldPos;
    fragPos     = worldPos.xyz;
float cy = cos(pc.yaw), sy = sin(pc.yaw);
mat3 yawMat = mat3(cy, 0.0, -sy, 0.0, 1.0, 0.0, sy, 0.0, cy);
fragNormal = normalize(yawMat * inNormal);
    fragUV      = inUV;
    fragNoise   = inNoise;
}
