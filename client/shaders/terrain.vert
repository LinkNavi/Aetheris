#version 450

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in uint  inTint;   // was 'material', now carries packed tint

struct ChunkData {
    mat4 model;
    vec4 params;
};
layout(set = 0, binding = 0) readonly buffer ChunkBuffer {
    ChunkData chunks[];
};

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 params;   // x=sunIntensity, y=fogStart, z=fogEnd
    vec4 camPos;
} pc;

layout(location = 0) out vec3  fragNormal;
layout(location = 1) out float sunIntensity;
layout(location = 2) out vec2  fragUV;
layout(location = 3) out float fragDist;
layout(location = 4) out flat uint fragTint;

void main() {
    ChunkData cd  = chunks[gl_InstanceIndex];
    vec4 worldPos = cd.model * vec4(inPos, 1.0);
    gl_Position   = pc.viewProj * worldPos;
    fragNormal    = normalize(mat3(cd.model) * inNormal);
    sunIntensity  = cd.params.x;
    fragUV        = inUV;
    fragDist      = length(worldPos.xyz - pc.camPos.xyz);
    fragTint      = inTint;
}
