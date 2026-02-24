#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

struct ChunkData {
    mat4 model;
    vec4 params; // x=sunIntensity
};
layout(set = 0, binding = 0) readonly buffer ChunkBuffer {
    ChunkData chunks[];
};

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 params;
} pc;

layout(location = 0) out vec3  fragNormal;
layout(location = 1) out float sunIntensity;

void main() {
    // firstInstance is set to the chunk's index in the draw list
    ChunkData cd = chunks[gl_InstanceIndex];
    gl_Position  = pc.viewProj * cd.model * vec4(inPos, 1.0);
    fragNormal   = normalize(mat3(cd.model) * inNormal);
    sunIntensity = cd.params.x;
}
