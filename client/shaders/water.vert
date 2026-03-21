#version 450

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec2  inUV;
layout(location = 2) in float inDepth;
layout(location = 3) in float inFlow;

layout(push_constant) uniform PC {
    mat4  viewProj;
    vec4  params;   // x=waterTime, y=sunIntensity, z=camY, w=0
    vec4  camPos;
} pc;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out float fragDepth;
layout(location = 2) out float fragFlow;
layout(location = 3) out float fragDist;
layout(location = 4) out vec3  fragWorldPos;

void main() {
    float t = pc.params.x;

    // Gentle wave displacement — two overlapping sine waves
    float wave1 = sin(inPos.x * 0.8 + t * 1.2) * cos(inPos.z * 0.6 + t * 0.9) * 0.12;
    float wave2 = sin(inPos.x * 1.3 + t * 0.7 + 1.5) * cos(inPos.z * 1.1 + t * 1.4) * 0.07;
    float waveY = wave1 + wave2;

    vec3 worldPos = inPos + vec3(0.0, waveY, 0.0);

    gl_Position  = pc.viewProj * vec4(worldPos, 1.0);
    fragUV       = inUV;
    fragDepth    = inDepth;
    fragFlow     = inFlow;
    fragWorldPos = worldPos;
    fragDist     = length(worldPos.xz - pc.camPos.xz);
}
